#include "vocal_separation_controller.hpp"

#include "audio_preview_controller.hpp"
#include "import_controller.hpp"
#include "library_model.hpp"
#include "playlist_model.hpp"
#include "vocal_separation_installer.hpp"
#include "vocal_separation_path_safety.hpp"
#include "waveform_provider.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

QString stemName(VocalSeparationController::StemKind kind)
{
    using StemKind = VocalSeparationController::StemKind;
    switch (kind) {
    case StemKind::Original: return QStringLiteral("original");
    case StemKind::Vocals: return QStringLiteral("vocals");
    case StemKind::Accompaniment: return QStringLiteral("instrumental");
    case StemKind::Drums: return QStringLiteral("drums");
    case StemKind::Bass: return QStringLiteral("bass");
    case StemKind::Other: return QStringLiteral("other");
    }
    return {};
}

QString localizedStemLabel(VocalSeparationController::StemKind kind)
{
    switch (kind) {
    case VocalSeparationController::StemKind::Vocals:
        return QCoreApplication::translate("VocalSeparationController", "Vocals");
    case VocalSeparationController::StemKind::Accompaniment:
        return QCoreApplication::translate("VocalSeparationController", "Instrumental");
    case VocalSeparationController::StemKind::Drums:
        return QCoreApplication::translate("VocalSeparationController", "Drums");
    case VocalSeparationController::StemKind::Bass:
        return QCoreApplication::translate("VocalSeparationController", "Bass");
    case VocalSeparationController::StemKind::Other:
        return QCoreApplication::translate("VocalSeparationController", "Other");
    case VocalSeparationController::StemKind::Original:
        break;
    }
    return {};
}

VocalSeparationController::StemKind stemKind(const QString& name)
{
    using StemKind = VocalSeparationController::StemKind;
    if (name == QStringLiteral("vocals")) return StemKind::Vocals;
    if (name == QStringLiteral("instrumental")) return StemKind::Accompaniment;
    if (name == QStringLiteral("drums")) return StemKind::Drums;
    if (name == QStringLiteral("bass")) return StemKind::Bass;
    return StemKind::Other;
}

QString deviceName(VocalSeparationController::DeviceMode mode)
{
    using DeviceMode = VocalSeparationController::DeviceMode;
    switch (mode) {
    case DeviceMode::Auto: return QStringLiteral("auto");
    case DeviceMode::CPU: return QStringLiteral("cpu");
    case DeviceMode::GPU: return QStringLiteral("gpu");
    }
    return QStringLiteral("auto");
}

using vocal_separation_paths::SafePathKind;
using vocal_separation_paths::openRegularFileForReadWithin;
using vocal_separation_paths::safeExistingFile;
using vocal_separation_paths::safeExistingFileWithin;
using vocal_separation_paths::safeExistingPathWithin;
using vocal_separation_paths::safePathKind;

VocalSeparationControllerOptions normalizeOptions(
    VocalSeparationControllerOptions options)
{
    if (options.catalog.isEmpty()) options.catalog = VocalSeparationCatalog::models();
    if (options.dataRoot.isEmpty()) {
        options.dataRoot = QDir(QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation)).filePath(QStringLiteral("separation"));
    }
    if (options.outputDirectory.isEmpty()) {
        options.outputDirectory = QDir(QStandardPaths::writableLocation(
            QStandardPaths::MusicLocation)).filePath(QStringLiteral("AgPlayer Separation"));
    }
    if (options.workerProgram.isEmpty()) {
        options.workerProgram = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("AgSeparationWorker.exe"));
    }
    if (options.runtimeLibraryPath.isEmpty()) {
        options.runtimeLibraryPath = VocalSeparationInstaller::runtimeLibraryPath(
            QDir(options.dataRoot).filePath(QStringLiteral("runtime")));
    }
    return options;
}

QString defaultModelStorageDirectory(const QString& dataRoot)
{
    return QDir(dataRoot).filePath(QStringLiteral("models"));
}

QString modelDirectorySettingsPath(const QString& dataRoot)
{
    return QDir(dataRoot).filePath(QStringLiteral("model-directory.json"));
}

QString loadModelStorageDirectory(const QString& dataRoot)
{
    QFile file(modelDirectorySettingsPath(dataRoot));
    if (!file.open(QIODevice::ReadOnly))
        return defaultModelStorageDirectory(dataRoot);
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QString stored = document.object()
        .value(QStringLiteral("directory")).toString();
    if (stored.isEmpty() || !QDir::isAbsolutePath(stored))
        return defaultModelStorageDirectory(dataRoot);
    return QFileInfo(stored).absoluteFilePath();
}

bool saveModelStorageDirectory(const QString& dataRoot,
                               const QString& directory)
{
    if (!QDir().mkpath(dataRoot)) return false;
    QSaveFile file(modelDirectorySettingsPath(dataRoot));
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QJsonObject object{
        {QStringLiteral("version"), 1},
        {QStringLiteral("directory"), directory},
    };
    if (file.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) < 0)
        return false;
    return file.commit();
}

bool modelFilesMatchSizes(const VocalModelCard& model,
                          const QStringList& paths)
{
    if (model.files.isEmpty() || paths.size() != model.files.size())
        return false;
    for (qsizetype index = 0; index < model.files.size(); ++index) {
        const QFileInfo candidate(paths.at(index));
        if (!candidate.isFile()
            || candidate.size() != model.files.at(index).bytes) {
            return false;
        }
    }
    return true;
}

using ModelFileIndex = QHash<QString, QStringList>;

QStringList resolveModelFilePaths(const VocalModelCard& model,
                                  const QString& storageDirectory,
                                  const ModelFileIndex& files)
{
    QStringList nested;
    QStringList flat;
    nested.reserve(model.files.size());
    flat.reserve(model.files.size());
    const QDir root(storageDirectory);
    for (const VocalDownloadFile& file : model.files) {
        nested.push_back(QDir(root.filePath(model.id)).filePath(file.fileName));
        flat.push_back(root.filePath(file.fileName));
    }
    if (modelFilesMatchSizes(model, nested)) return nested;
    if (modelFilesMatchSizes(model, flat)) return flat;

    QStringList discovered;
    discovered.reserve(model.files.size());
    for (const VocalDownloadFile& expected : model.files) {
        QString matchedPath;
        const QStringList candidates = files.value(expected.fileName.toLower());
        for (const QString& candidate : candidates) {
            const QFileInfo info(candidate);
            if (info.size() == expected.bytes
                && safeExistingFileWithin(info.absoluteFilePath(), storageDirectory)) {
                matchedPath = info.absoluteFilePath();
                break;
            }
        }
        if (matchedPath.isEmpty()) return nested;
        discovered.push_back(matchedPath);
    }
    return modelFilesMatchSizes(model, discovered) ? discovered : nested;
}

void addFingerprintValue(QCryptographicHash& hash, const QByteArray& value)
{
    hash.addData(QByteArray::number(value.size()));
    hash.addData(QByteArrayView(":", 1));
    hash.addData(value);
    hash.addData(QByteArrayView("\n", 1));
}

void addFileIdentity(QCryptographicHash& hash, const QFileInfo& info)
{
    addFingerprintValue(hash, QDir::cleanPath(info.absoluteFilePath()).toUtf8());
    addFingerprintValue(hash, info.canonicalFilePath().toUtf8());
    addFingerprintValue(hash, QByteArray::number(info.isFile()));
    addFingerprintValue(hash, QByteArray::number(info.isDir()));
    addFingerprintValue(hash, QByteArray::number(info.isSymLink()));
    addFingerprintValue(hash, QByteArray::number(
        int(safePathKind(info.absoluteFilePath()))));
    addFingerprintValue(hash, QByteArray::number(info.size()));
    addFingerprintValue(hash, QByteArray::number(
        info.lastModified().toMSecsSinceEpoch()));
    addFingerprintValue(hash, QByteArray::number(
        info.metadataChangeTime().toMSecsSinceEpoch()));
}

QString modelVerificationFingerprint(const VocalModelCard& model,
                                     const QString& storageDirectory,
                                     const ModelFileIndex& files)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    addFingerprintValue(hash, model.id.toUtf8());
    const QStringList paths = resolveModelFilePaths(
        model, storageDirectory, files);
    for (qsizetype index = 0; index < model.files.size(); ++index) {
        const VocalDownloadFile& expected = model.files.at(index);
        addFingerprintValue(hash, expected.fileName.toUtf8());
        addFingerprintValue(hash, QByteArray::number(expected.bytes));
        addFingerprintValue(hash, expected.sha256.toLower().toUtf8());
        const QString path = index < paths.size()
            ? paths.at(index) : QString();
        addFileIdentity(hash, QFileInfo(path));
        addFingerprintValue(hash, QByteArray::number(
            !path.isEmpty()
            && safeExistingFileWithin(path, storageDirectory)));
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString runtimeVerificationFingerprint(const QString& runtimePath,
                                        const QString& expectedArchiveSha256,
                                        const bool verifyIntegrity)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    addFingerprintValue(hash, expectedArchiveSha256.toLower().toUtf8());
    addFingerprintValue(hash, QByteArray::number(verifyIntegrity));
    const QFileInfo library(runtimePath);
    addFileIdentity(hash, library);
    if (!library.isFile())
        return QString::fromLatin1(hash.result().toHex());

    const QDir directory(library.absolutePath());
    const QFileInfoList entries = directory.entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System
            | QDir::NoDotAndDotDot,
        QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& entry : entries)
        addFileIdentity(hash, entry);
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

VocalSeparationController::ModelDirectoryIndex
VocalSeparationController::buildModelDirectoryIndex(const QString& root)
{
    ModelDirectoryIndex result;
    if (QFileInfo(root).isDir()) result.directories.push_back(root);
    QDirIterator iterator(root, QDir::Files | QDir::Readable | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QFileInfo info(iterator.next());
        result.filesByName[info.fileName().toLower()].push_back(
            info.absoluteFilePath());
        if (info.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0)
            result.manifests.push_back(info.absoluteFilePath());
    }
    QDirIterator directoryIterator(root, QDir::Dirs | QDir::NoDotAndDotDot,
                                   QDirIterator::Subdirectories);
    while (directoryIterator.hasNext())
        result.directories.push_back(directoryIterator.next());
    result.directories.removeDuplicates();
    return result;
}

VocalSeparationController::VocalSeparationController(
    AudioPreviewController* preview, WaveformProvider* waveformProvider,
    LibraryModel* library, ImportController* importer, PlaylistModel* playlists,
    VocalSeparationControllerOptions options, QObject* parent)
    : QObject(parent),
      preview_(preview),
      waveformProvider_(waveformProvider),
      library_(library),
      importer_(importer),
      playlists_(playlists),
      options_(normalizeOptions(std::move(options))),
      historyStore_(QDir(options_.dataRoot).filePath(QStringLiteral("history.json"))),
      process_(options_.workerProgram, options_.workerArguments,
               options_.deadlines, this),
      downloader_(std::make_unique<VocalSeparationDownloader>(&network_, this)),
      externalRuntime_(std::make_unique<ExternalSeparationRuntime>(
          QDir(options_.dataRoot).filePath("runtime/python-vr-1"), &network_, this)),
      cudaRuntime_(std::make_unique<CudaSeparationRuntime>(
          QDir(options_.dataRoot).filePath("runtime/cuda-1"), &network_, this)),
      outputDirectory_(options_.outputDirectory)
{
    connect(externalRuntime_.get(), &ExternalSeparationRuntime::progress, this,
            [this](double value, const QString& detail) {
        downloadProgress_ = value;
        downloadSource_ = detail;
        runtimeProgress_["python"] = value; runtimeDetails_["python"] = detail;
        refreshModels();
        emit downloadProgressChanged();
        emit downloadStateChanged();
    });
    connect(cudaRuntime_.get(), &CudaSeparationRuntime::progress, this,
        [this](double value, const QString& detail) {
            downloadProgress_ = value; downloadSource_ = detail;
            runtimeProgress_["cuda"] = value; runtimeDetails_["cuda"] = detail;
            emit downloadProgressChanged(); emit downloadStateChanged();
        });
    connect(cudaRuntime_.get(), &CudaSeparationRuntime::changed, this, [this] {
        validatedGpuProviders_.clear();
        refreshModels(); emit downloadStateChanged();
        if (cudaRuntime_->ready() && !cudaRuntime_->busy()) {
            deviceProbePending_ = true;
            QTimer::singleShot(0, this, [this] { if (!requestInFlight()) probeDevices(); });
        }
    });
    connect(cudaRuntime_.get(), &CudaSeparationRuntime::finished, this,
        [this](bool success, const QString& error) {
            runtimeErrors_["cuda"] = success ? QString() : error;
            if (!success) setError(error);
            configurationChanged();
            if (success) QTimer::singleShot(0, this, [this] { probeDevices(); });
        });
    connect(externalRuntime_.get(), &ExternalSeparationRuntime::changed,
            this, &VocalSeparationController::refreshModels);
    connect(externalRuntime_.get(), &ExternalSeparationRuntime::finished, this,
            [this](bool success, const QString& diagnostic) {
        runtimeErrors_["python"] = success ? QString() : diagnostic;
        if (!success) setError(diagnostic);
        emit downloadStateChanged();
        refreshModels();
    });
    modelStorageDirectory_ = loadModelStorageDirectory(options_.dataRoot);
    QDir().mkpath(modelStorageDirectory_);
    modelDirectoryScanTimer_.setSingleShot(true);
    modelDirectoryScanTimer_.setInterval(700);
    connect(&modelDirectoryScanTimer_, &QTimer::timeout,
            this, &VocalSeparationController::scanModelDirectory);
    connect(&modelDirectoryWatcher_, &QFileSystemWatcher::directoryChanged,
            this, [this] {
        rebuildModelDirectoryWatcher();
        scheduleModelDirectoryScan();
    });
    rebuildModelDirectoryWatcher();
    baseCatalog_ = options_.catalog;
    history_ = historyStore_.load();
    availableDevices_ = {
        QVariantMap{{QStringLiteral("mode"), int(DeviceMode::Auto)},
                    {QStringLiteral("name"), QStringLiteral("Auto")},
                    {QStringLiteral("available"), true},
                    {QStringLiteral("reason"), tr("自动选择已验证的可用设备")}},
        QVariantMap{{QStringLiteral("mode"), int(DeviceMode::CPU)},
                    {QStringLiteral("name"), QStringLiteral("CPU")},
                    {QStringLiteral("available"), true},
                    {QStringLiteral("reason"), QString()}},
        QVariantMap{{QStringLiteral("mode"), int(DeviceMode::GPU)},
                    {QStringLiteral("name"), QStringLiteral("DirectML")},
                    {QStringLiteral("available"), false},
                    {QStringLiteral("reason"), tr("尚未探测")}},
    };
    refreshModels();
    scheduleModelDirectoryScan();
    if (!options_.catalog.isEmpty()) selectedModelId_ = options_.catalog.first().id;
    rebuildStems();

    if (preview_ != nullptr) {
        connect(preview_, &AudioPreviewController::errorOccurred, this,
                [this](const QString& message) {
            setError(message);
            resetResultPreviewState();
        });
    }

    connect(&process_, &SeparationProcessClient::progressReceived, this,
            [this](double fraction, const QString& stage) {
        progress_ = std::max(progress_, fraction);
        stage_ = stage;
        emit progressChanged();
        emit jobStateChanged();
    });
    connect(&process_, &SeparationProcessClient::probeReceived,
            this, &VocalSeparationController::handleProbe);
    connect(&process_, &SeparationProcessClient::resultReceived,
            this, &VocalSeparationController::handleResult);
    connect(&process_, &SeparationProcessClient::requestAvailabilityChanged,
            this, &VocalSeparationController::startEligibilityChanged);
    connect(&process_, &SeparationProcessClient::requestAvailabilityChanged,
            this, [this] {
        if (deviceProbePending_ && !requestInFlight() && process_.canAcceptRequest())
            QTimer::singleShot(0, this, [this] { if (deviceProbePending_) probeDevices(); });
    });
    connect(&process_, &SeparationProcessClient::failed, this,
            [this](const QString& message, bool) {
        if (activeRequest_.has_value()) failedRequest_ = activeRequest_;
        activeRequest_.reset();
        setError(message);
        setJobState(JobState::JobFailed, QStringLiteral("error"));
    });
    connect(&process_, &SeparationProcessClient::cancelled, this, [this] {
        activeRequest_.reset();
        failedRequest_.reset();
        setJobState(JobState::Cancelled, QStringLiteral("cancelled"));
    });
    connect(downloader_.get(), &VocalSeparationDownloader::stateChanged,
            this, [this](VocalDownloadState) {
        refreshModels();
        emit downloadStateChanged();
    });
    connect(downloader_.get(), &VocalSeparationDownloader::progressChanged,
            this, [this](qint64 received, qint64 total) {
        const qint64 expected = downloadQueue_.isEmpty()
            ? total : downloadQueue_.constFirst().file.bytes;
        const qint64 current = qBound<qint64>(0, received,
                                              qMax<qint64>(0, expected));
        downloadProgress_ = totalDownloadBytes_ > 0
            ? static_cast<double>(completedDownloadBytes_ + current)
                  / static_cast<double>(totalDownloadBytes_)
            : 0.0;
        if (runtimeOnlyDownload_) runtimeProgress_["directml"] = downloadProgress_;
        emit downloadProgressChanged();
        emit downloadStateChanged();
    });
    connect(downloader_.get(), &VocalSeparationDownloader::finished,
            this, [this](const VocalInstallResult& result) {
        if (!result.ok) {
            handleDownloadFailure(result, true);
            return;
        }
        const DownloadItem completed = downloadQueue_.takeFirst();
        completedDownloadBytes_ += completed.file.bytes;
        if (completed.runtimeArchive) {
            auto* const watcher = new QFutureWatcher<VocalInstallResult>(this);
            const auto cancellation = std::make_shared<std::atomic_bool>(false);
            runtimeInstallerWatcher_ = watcher;
            runtimeInstallCancellation_ = cancellation;
            connect(watcher,
                    &QFutureWatcher<VocalInstallResult>::finished, this,
                    [this, watcher, cancellation] {
                const VocalInstallResult installed = watcher->result();
                watcher->deleteLater();
                if (runtimeInstallerWatcher_ != watcher) return;
                runtimeInstallerWatcher_ = nullptr;
                if (runtimeInstallCancellation_ == cancellation)
                    runtimeInstallCancellation_.reset();
                if (cancellation->load(std::memory_order_acquire)) {
                    configurationChanged();
                    if (modelDirectoryRescanPending_) {
                        modelDirectoryRescanPending_ = false;
                        scheduleModelDirectoryScan();
                    }
                    return;
                }
                if (!installed.ok) {
                    finishExhaustedDownload(QStringLiteral("runtime"),
                                            installed.error);
                    return;
                }
                runtimeVerified_ = true;
                runtimeVerificationKnown_ = true;
                runtimeVerificationFingerprint_ =
                    runtimeVerificationFingerprint(
                        options_.runtimeLibraryPath,
                        VocalSeparationCatalog::nativeRuntime().sha256,
                        options_.verifyRuntimeIntegrity);
                startNextDownload();
            });
            const QString archive = completed.destination;
            const QString root = runtimeDirectory();
            watcher->setFuture(QtConcurrent::run(
                [archive, root, cancellation] {
                    return VocalSeparationInstaller::installDirectMlRuntime(
                        archive, root, cancellation);
                }));
            return;
        }
        startNextDownload();
    });
    if (waveformProvider_ != nullptr) {
        connect(waveformProvider_, &WaveformProvider::waveformReady,
                this, &VocalSeparationController::handleWaveform);
        connect(waveformProvider_, &WaveformProvider::waveformFailed,
                this, &VocalSeparationController::handleWaveformFailure);
    }
}

VocalSeparationController::~VocalSeparationController()
{
    for (const auto& task : modelConfigurations_) {
        task->cancellation->store(true);
        task->downloader->disconnect(this);
        task->downloader->cancel();
        if (task->verification) {
            task->verification->disconnect(this);
            task->verification->future().waitForFinished();
        }
    }
    modelConfigurations_.clear();
    if (downloader_) downloader_->cancel();
    ++verificationGeneration_;
    if (verificationCancellation_)
        verificationCancellation_->store(true, std::memory_order_release);
    if (verificationWatcher_ != nullptr) {
        QFutureWatcher<VerificationResult>* const watcher = verificationWatcher_;
        disconnect(watcher, nullptr, this, nullptr);
        watcher->future().waitForFinished();
        verificationWatcher_ = nullptr;
    }
    verificationCancellation_.reset();
    if (modelDirectoryIndexWatcher_ != nullptr) {
        QFutureWatcher<ModelDirectoryIndex>* const watcher =
            modelDirectoryIndexWatcher_;
        disconnect(watcher, nullptr, this, nullptr);
        watcher->future().waitForFinished();
        modelDirectoryIndexWatcher_ = nullptr;
    }
    if (runtimeInstallCancellation_)
        runtimeInstallCancellation_->store(true, std::memory_order_release);
    if (runtimeInstallerWatcher_ != nullptr) {
        QFutureWatcher<VocalInstallResult>* const watcher = runtimeInstallerWatcher_;
        disconnect(watcher, nullptr, this, nullptr);
        watcher->future().waitForFinished();
        runtimeInstallerWatcher_ = nullptr;
    }
    runtimeInstallCancellation_.reset();
}

QVariantMap VocalSeparationController::inputInfo() const { return inputInfo_; }
QVariantList VocalSeparationController::models() const { return models_; }
QString VocalSeparationController::selectedModelId() const { return selectedModelId_; }
VocalSeparationController::DeviceMode VocalSeparationController::deviceMode() const noexcept { return deviceMode_; }
QVariantList VocalSeparationController::availableDevices() const { return availableDevices_; }
VocalSeparationController::JobState VocalSeparationController::jobState() const noexcept { return jobState_; }
QString VocalSeparationController::stage() const { return stage_; }
double VocalSeparationController::progress() const noexcept { return progress_; }
double VocalSeparationController::downloadProgress() const noexcept {
    if (modelConfigurations_.isEmpty()) return downloadProgress_;
    double total = 0;
    for (const auto& task : modelConfigurations_) total += qMax(0.0, task->progress);
    return total / double(modelConfigurations_.size());
}
QString VocalSeparationController::downloadingModelId() const {
    if (!downloadingModelId_.isEmpty()) return downloadingModelId_;
    for (const auto& task : modelConfigurations_)
        if (modelConfigurationBusy(task->modelId)) return task->modelId;
    return {};
}
bool VocalSeparationController::downloadBusy() const noexcept
{
    if (!downloadingModelId_.isEmpty() || externalRuntime_->busy() || cudaRuntime_->busy()) return true;
    for (const auto& task : modelConfigurations_)
        if (task->state != "idle" && modelConfigurationBusy(task->modelId)) return true;
    return false;
}
QString VocalSeparationController::downloadSource() const
{
    return downloadSource_;
}
bool VocalSeparationController::canRetry() const noexcept
{
    return process_.canAcceptRequest() && jobState_ == JobState::JobFailed
        && failedRequest_.has_value();
}
QString VocalSeparationController::error() const { return error_; }
QString VocalSeparationController::outputFormat() const { return outputFormat_; }
QString VocalSeparationController::outputDirectory() const { return outputDirectory_; }
QString VocalSeparationController::modelStorageDirectory() const
{
    return modelStorageDirectory_;
}
bool VocalSeparationController::canStart() const
{
    return startDisabledReason().isEmpty();
}

QString VocalSeparationController::startDisabledReason() const
{
    if (downloadConflictsWithModel(selectedModelId_)) return tr("请先完成或取消当前模型/运行时配置");
    if (requestInFlight()) return tr("当前任务尚未结束");
    if (!process_.canAcceptRequest()) return tr("当前任务尚未结束");
    if (!safeExistingFile(inputInfo_.value(QStringLiteral("path")).toString()))
        return tr("请选择有效输入音频");
    const VocalModelCard* model = selectedModel();
    if (model == nullptr) return tr("请选择模型");
    if (model->id == QStringLiteral("python-vr-5hp")) {
        if (!modelFilesPresent(*model)) return tr("本地 VR 模型文件缺失，请重新检测目录");
        if (!externalRuntime_->ready()) return tr("请点击模型卡片的一键配置，下载独立 Python / PyTorch 环境");
        if (!verifiedModelIds_.contains(model->id)) return tr("本地 VR 模型未通过完整性校验，请重新检测目录");
        if (selectedStemNames().isEmpty()) return tr("至少选择一个输出音轨");
        return {};
    }
    if (!modelInstalled(*model)
        && (verifiedOrRejectedModelIds_.contains(model->id)
            || !modelFilesPresent(*model))) {
        return tr("所选模型尚未安装或未通过校验");
    }
    if (selectedStemNames().isEmpty()) return tr("至少选择一个输出音轨");
#ifndef Q_OS_MACOS
    if (model->family == VocalModelFamily::Demucs && deviceChosenByUser_
        && deviceMode_ == DeviceMode::GPU && !cudaRuntime_->ready()) {
        if (!cudaRuntime_->nvidiaAvailable())
            return tr("标准五轨 GPU 分离需要 NVIDIA CUDA；%1").arg(cudaRuntime_->hardwareSummary());
        return cudaRuntime_->hardwareSummary()
            + tr("；标准五轨 GPU 需要应用专用 CUDA 组件，请点击模型卡片“CUDA · 1.51 GB”，无需重装显卡驱动");
    }
#endif
    if (!QFileInfo(options_.runtimeLibraryPath).isFile())
        return tr("ONNX Runtime 尚未安装");
    if (!deviceAvailable(deviceMode_)) {
        for (const QVariant& value : availableDevices_) {
            const QVariantMap device = value.toMap();
            if (device.value(QStringLiteral("mode")).toInt() == int(deviceMode_))
                return device.value(QStringLiteral("reason")).toString();
        }
        return tr("所选设备不可用");
    }
    return {};
}
QVariantList VocalSeparationController::stems() const { return stems_; }
QVariantList VocalSeparationController::history() const { return history_; }
VocalSeparationController::ResultPreviewMode
VocalSeparationController::resultPreviewMode() const noexcept
{
    return resultPreviewMode_;
}

VocalSeparationController::StemKind
VocalSeparationController::resultPreviewSoloKind() const noexcept
{
    return resultPreviewSoloKind_;
}

bool VocalSeparationController::selectInput(const QUrl& url)
{
    if (requestInFlight()) return false;
    const QString path = QFileInfo(url.toLocalFile()).absoluteFilePath();
    const QFileInfo file(path);
    if (!file.isFile()) {
        setError(tr("输入音频不存在"));
        return false;
    }
    resetInputSession();
    ++inputWaveformGeneration_;
    inputWaveformTrackId_ = QStringLiteral("separation-input-%1")
                                .arg(inputWaveformGeneration_);
    QUrl coverUrl;
    if (library_ != nullptr) {
        const int row = library_->indexForLocalFile(path);
        if (row >= 0)
            coverUrl = library_->data(library_->index(row),
                                      LibraryModel::CoverUrlRole).toUrl();
    }
    inputInfo_ = {{QStringLiteral("path"), path},
                  {QStringLiteral("name"), file.fileName()},
                  {QStringLiteral("bytes"), file.size()},
                  {QStringLiteral("coverUrl"), coverUrl},
                  {QStringLiteral("coverFallbackUrl"),
                   QUrl(QStringLiteral(
                       "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"))},
                  {QStringLiteral("waveform"), QVariantList{}},
                  {QStringLiteral("durationMs"), qlonglong(0)}};
    invalidateRetry();
    setError({});
    emit inputInfoChanged();
    emit startEligibilityChanged();
    if (waveformProvider_ != nullptr) {
        const QString trackId = inputWaveformTrackId_;
        const quint64 generation = inputWaveformGeneration_;
        QTimer::singleShot(0, this, [this, trackId, path, generation] {
            if (waveformProvider_ != nullptr
                && inputWaveformGeneration_ == generation
                && inputWaveformTrackId_ == trackId
                && inputInfo_.value(QStringLiteral("path")).toString() == path) {
                waveformProvider_->loadForTrack(trackId, path);
            }
        });
    }
    return true;
}

bool VocalSeparationController::dropInput(const QList<QUrl>& urls)
{
    return urls.size() == 1 && selectInput(urls.first());
}

bool VocalSeparationController::clearInput()
{
    if (requestInFlight() || inputInfo_.isEmpty()) return false;
    const QString path = inputInfo_.value(QStringLiteral("path")).toString();
    resetInputSession();
    ++inputWaveformGeneration_;
    inputWaveformTrackId_.clear();
    inputInfo_.clear();
    invalidateRetry();
    setError({});
    if (waveformProvider_ != nullptr && !path.isEmpty())
        waveformProvider_->cancelForTrack(path);
    emit inputInfoChanged();
    emit startEligibilityChanged();
    return true;
}

bool VocalSeparationController::downloadModel(const QString& modelId)
{
    return beginModelDownload(modelId, false);
}

bool VocalSeparationController::downloadModelFromMirror(const QString& modelId)
{
    return beginModelDownload(modelId, true);
}

bool VocalSeparationController::configureGpuRuntime(const QString& modelId)
{
    const auto* model = modelForId(modelId);
    if (!model || modelId == QStringLiteral("python-vr-5hp")) return false;
    if (cudaRuntime_->ready() || cudaRuntime_->busy()) return true;
    if (activeRequest_ && activeRequest_->kind == RequestKind::Separation
        && activeRequest_->modelId != QStringLiteral("python-vr-5hp")) return false;
    if (cudaRuntime_->checking()) { setError(tr("正在异步校验 CUDA 组件和 NVIDIA 驱动，请稍候")); return false; }
    if (!cudaRuntime_->nvidiaAvailable()) {
        setError(tr("未检测到可用 NVIDIA 驱动；CUDA 环境仅适用于 NVIDIA 显卡，不会补齐未知模型适配器")); return false;
    }
    runtimeErrors_.remove("cuda"); setError({});
    if (!cudaRuntime_->start()) {
        setError(tr("CUDA 配置无法开始：请等待环境检查完成，并保留至少 5 GB 可用磁盘空间"));
        return false;
    }
    refreshModels(); emit downloadStateChanged(); return true;
}

bool VocalSeparationController::configureRuntime(const QString& modelIdForUi)
{
    if (modelForId(modelIdForUi) != nullptr
        && modelIdForUi != QStringLiteral("python-vr-5hp"))
        return beginModelConfiguration(modelIdForUi, false);
    if (modelIdForUi == QStringLiteral("python-vr-5hp")
        && (externalRuntime_->ready() || externalRuntime_->busy()))
        return true;
    if (verificationPurpose_ == VerificationPurpose::Download
        && downloadingModelId_ == modelIdForUi) return true;
    if (!canConfigureModel(modelIdForUi)) return false;
    if (verificationWatcher_ != nullptr) {
        if (verificationPurpose_ == VerificationPurpose::Download
            && downloadingModelId_ == modelIdForUi) {
            return true;
        }
        if (verificationPurpose_ == VerificationPurpose::Download) {
            setError(tr("配置正在下载或校验，请等待当前进度完成"));
            return false;
        }
        deferredRuntimeConfigurationModelId_ = modelIdForUi;
        ++verificationGeneration_;
        if (verificationCancellation_)
            verificationCancellation_->store(true, std::memory_order_release);
        verificationPurpose_ = VerificationPurpose::None;
        verifyingModelId_.clear();
        // A background hash check may overlap a separation worker. Taking
        // over that check must not discard the worker's immutable context.
        if (!activeRequest_ || activeRequest_->kind == RequestKind::Probe) {
            activeRequest_.reset();
            failedRequest_.reset();
            deviceProbePending_ = false;
            if (jobState_ == JobState::Probing)
                setJobState(JobState::Idle, {});
        }
        setError({});
        refreshModels();
        if (modelIdForUi == QStringLiteral("python-vr-5hp")) {
            deferredRuntimeConfigurationModelId_.reset();
            failedDownloadModelId_.clear();
            if (!externalRuntime_->start()) {
                setError(tr("无法启动外置环境配置，请检查模型缓存目录是否可写"));
                return false;
            }
            emit downloadStateChanged();
        }
        return true;
    }
    if (modelIdForUi == QStringLiteral("python-vr-5hp")) {
        if (externalRuntime_->busy()) return true;
        failedDownloadModelId_.clear();
        setError({});
        if (!externalRuntime_->start()) {
            setError(tr("无法启动外置环境配置，请检查模型缓存目录是否可写"));
            return false;
        }
        emit downloadStateChanged();
        return true;
    }
    if (!downloadQueue_.isEmpty()
        || runtimeInstallerWatcher_ != nullptr
        || downloader_->state() == VocalDownloadState::Downloading
        || downloader_->state() == VocalDownloadState::Paused
        || downloader_->state() == VocalDownloadState::Verifying) {
        setError(tr("配置正在下载或校验，请等待当前进度完成"));
        return false;
    }
    for (const QVariant& value : rejectedCustomModels_) {
        const QVariantMap model = value.toMap();
        if (model.value(QStringLiteral("id")).toString() != modelIdForUi) continue;
        if (model.value(QStringLiteral("backend")).toString()
            == QStringLiteral("external-python")) {
            setError(tr("此模型需要专用 Python/PyTorch 推理适配器；当前版本未提供该适配器，安装 ONNX Runtime 无法运行 .pth/.th 模型。可选择已支持的 ONNX 模型。"));
            return false;
        }
        if (runtimeReady()) {
            setError(tr("ONNX Runtime 已就绪；此模型尚无匹配的张量与频谱配置，无法通过下载运行时自动适配。%1")
                         .arg(model.value(QStringLiteral("failureReason")).toString()));
            return false;
        }
    }
    if (const VocalModelCard* model = modelForId(modelIdForUi);
        model != nullptr && !modelInstalled(*model)) {
        const bool builtIn = std::any_of(
            baseCatalog_.cbegin(), baseCatalog_.cend(),
            [&modelIdForUi](const VocalModelCard& candidate) {
                return candidate.id == modelIdForUi;
            });
        if (builtIn) return beginModelDownload(modelIdForUi, false);
        if (modelFilesPresent(*model)) {
            setError({});
            return beginVerification(VerificationPurpose::Refresh, model);
        }
        setError(tr("模型已变更，请重新检测模型目录"));
        return false;
    }
    if (runtimeReady()) {
        if (const VocalModelCard* model = modelForId(modelIdForUi)) {
            setError({});
            return beginVerification(VerificationPurpose::Refresh, model);
        }
        setError(modelIdForUi.isEmpty() ? tr("ONNX Runtime 已配置完成")
                                      : tr("模型已变更，请重新检测模型目录"));
        return modelIdForUi.isEmpty();
    }

    const VocalRuntimePackage package =
        VocalSeparationCatalog::nativeRuntime();
    const VocalDownloadFile archive{
        QStringLiteral("runtime.nupkg"), package.url,
        package.bytes, package.sha256};
    downloadQueue_.push_back({
        archive,
        QDir(options_.dataRoot).filePath(
            QStringLiteral("downloads/runtime.nupkg")),
        true, {}, false});
    runtimeOnlyDownload_ = true;
    preferDomesticMirror_ = false;
    downloadSource_ = tr("官方线路");
    downloadingModelId_ = modelIdForUi.isEmpty()
        ? QStringLiteral("runtime") : modelIdForUi;
    failedDownloadModelId_.clear();
    downloadProgress_ = 0.0;
    completedDownloadBytes_ = 0;
    totalDownloadBytes_ = package.bytes;
    emit downloadProgressChanged();
    emit downloadStateChanged();
    setError({});
    startNextDownload();
    return true;
}

bool VocalSeparationController::beginModelDownload(
    const QString& modelId, const bool preferDomesticMirror)
{
    if (modelId == QStringLiteral("python-vr-5hp")) return configureRuntime(modelId);
    return beginModelConfiguration(modelId, preferDomesticMirror);
}

bool VocalSeparationController::modelConfigurationBusy(const QString& modelId) const
{
    const auto task = modelConfigurations_.value(modelId);
    return task && (task->state == "checking" || task->state == "downloading"
        || task->state == "paused" || task->state == "waiting-runtime"
        || task->verification != nullptr);
}

void VocalSeparationController::configurationChanged()
{
    for (const auto& task : modelConfigurations_) {
        if (task->state != "waiting-runtime") continue;
        if (runtimeReady()) task->state = "complete";
        else if (!runtimeErrors_.value("directml").isEmpty()) {
            task->state = "failed"; task->error = runtimeErrors_.value("directml");
        }
    }
    emit downloadProgressChanged();
    emit downloadStateChanged();
    refreshModels();
    if (!downloadBusy() && modelDirectoryRescanPending_) scheduleModelDirectoryScan();
}

bool VocalSeparationController::ensureSharedRuntime()
{
    if (runtimeReady()) return true;
    if (runtimeOnlyDownload_ || runtimeInstallerWatcher_ != nullptr) return true;
    if (!downloadQueue_.isEmpty()) return false;
    if (activeRequest_ && activeRequest_->kind == RequestKind::Separation
        && activeRequest_->modelId != QStringLiteral("python-vr-5hp")) return false;
    const auto package = VocalSeparationCatalog::nativeRuntime();
    downloadQueue_.push_back({{QStringLiteral("runtime.nupkg"), package.url,
        package.bytes, package.sha256}, QDir(options_.dataRoot).filePath(
            QStringLiteral("downloads/runtime.nupkg")), true, {}, false});
    runtimeOnlyDownload_ = true;
    downloadingModelId_ = QStringLiteral("runtime");
    downloadSource_ = tr("官方线路");
    runtimeErrors_.remove(QStringLiteral("directml"));
    runtimeDetails_["directml"] = downloadSource_;
    completedDownloadBytes_ = 0;
    totalDownloadBytes_ = package.bytes;
    downloadProgress_ = 0;
    runtimeProgress_["directml"] = 0;
    startNextDownload();
    return true;
}

bool VocalSeparationController::beginModelConfiguration(const QString& modelId, bool mirror)
{
    const VocalModelCard* model = modelForId(modelId);
    if (!model) return false;
    const auto existing = modelConfigurations_.value(modelId);
    if (existing && existing->state == "idle" && existing->verification != nullptr) return false;
    if (existing && modelConfigurationBusy(modelId)) return true;
    if (modelInstalled(*model) && runtimeReady()) return true;
    if (!canConfigureModel(modelId)) return false;
    if (mirror && std::none_of(model->files.cbegin(), model->files.cend(),
        [](const VocalDownloadFile& file) { return vocalDomesticMirrorUrl(file.url).isValid(); }))
        return false;
    auto task = std::make_shared<ModelConfiguration>();
    task->modelId = modelId;
    task->mirror = mirror;
    task->cancellation = std::make_shared<std::atomic_bool>(false);
    task->downloader = std::make_unique<VocalSeparationDownloader>(&network_);
    modelConfigurations_.insert(modelId, task);
    const std::weak_ptr<ModelConfiguration> weak = task;
    connect(task->downloader.get(), &VocalSeparationDownloader::progressChanged, this,
        [this, weak](qint64 received, qint64) {
            const auto current = weak.lock(); if (!current || current->queue.isEmpty()) return;
            current->progress = current->totalBytes > 0
                ? double(current->completedBytes + qBound<qint64>(0, received, current->queue.first().file.bytes)) / double(current->totalBytes) : 0;
            configurationChanged();
        });
    connect(task->downloader.get(), &VocalSeparationDownloader::stateChanged, this,
        [this, weak](VocalDownloadState state) {
            const auto current = weak.lock(); if (!current) return;
            if (state == VocalDownloadState::Downloading) current->state = "downloading";
            else if (state == VocalDownloadState::Paused) current->state = "paused";
            else if (state == VocalDownloadState::Verifying) current->state = "checking";
            configurationChanged();
        });
    connect(task->downloader.get(), &VocalSeparationDownloader::finished, this,
        [this, weak](const VocalInstallResult& result) {
            const auto current = weak.lock();
            if (!current || current->state == "idle" || current->queue.isEmpty()) return;
            if (!result.ok) {
                auto& file = current->queue.first();
                if (!file.mirrorAttempted && file.mirrorUrl.isValid()) {
                    file.file.url = file.mirrorUrl; file.mirrorAttempted = true;
                    advanceModelConfiguration(current); return;
                }
                current->state = "failed"; current->error = result.error;
                configurationChanged();
                emit downloadSourcesExhausted({{"modelId", current->modelId},
                    {"source", current->detail}, {"diagnostic", current->error}});
                return;
            }
            current->completedBytes += current->queue.takeFirst().file.bytes;
            advanceModelConfiguration(current);
        });
    const VocalModelCard snapshot = *model;
    const QString root = modelStorageDirectory_;
    const QString runtime = options_.runtimeLibraryPath;
    const bool verifyRuntime = options_.verifyRuntimeIntegrity;
    const auto cancellation = task->cancellation;
    auto* watcher = new QFutureWatcher<VerificationResult>(this);
    task->verification = watcher;
    connect(watcher, &QFutureWatcher<VerificationResult>::finished, this,
        [this, task, watcher, snapshot, root] {
            const auto result = watcher->result(); watcher->deleteLater(); task->verification = nullptr;
            if (task->cancellation->load() || task->state == "idle"
                || root != modelStorageDirectory_ || modelConfigurations_.value(task->modelId) != task) {
                configurationChanged(); return;
            }
            if (result.runtimeChecked && result.runtimeFingerprint == runtimeVerificationFingerprint(
                    options_.runtimeLibraryPath, VocalSeparationCatalog::nativeRuntime().sha256,
                    options_.verifyRuntimeIntegrity)) {
                runtimeVerified_ = result.runtimeVerified; runtimeVerificationKnown_ = true;
                runtimeVerificationFingerprint_ = result.runtimeFingerprint;
            }
            for (const auto& file : snapshot.files) {
                const QString destination = QDir(modelDirectory(snapshot.id)).filePath(file.fileName);
                if (result.verifiedModels.contains(snapshot.id)
                    || result.verifiedFiles.contains(QFileInfo(destination).absoluteFilePath())) continue;
                const QUrl backup = vocalDomesticMirrorUrl(file.url);
                auto selected = file; if (task->mirror && backup.isValid()) selected.url = backup;
                task->queue.push_back({selected, destination, false, backup, task->mirror});
                task->totalBytes += file.bytes;
            }
            if (!ensureSharedRuntime()) {
                task->state = "failed"; task->error = tr("运行时正在使用，无法配置");
                configurationChanged(); return;
            }
            advanceModelConfiguration(task);
        });
    watcher->setFuture(QtConcurrent::run([snapshot, root, runtime, verifyRuntime, cancellation] {
        VerificationResult result;
        const auto index = buildModelDirectoryIndex(root).filesByName;
        const QString before = modelVerificationFingerprint(snapshot, root, index);
        const auto paths = resolveModelFilePaths(snapshot, root, index);
        bool valid = paths.size() == snapshot.files.size() && !paths.isEmpty();
        for (qsizetype i = 0; i < snapshot.files.size(); ++i) {
            if (cancellation->load()) return result;
            if (i < paths.size() && VocalSeparationInstaller::isVerifiedFile(snapshot.files[i], paths[i], cancellation))
                result.verifiedFiles.insert(QFileInfo(paths[i]).absoluteFilePath());
            else valid = false;
        }
        if (before != modelVerificationFingerprint(snapshot, root, index)) {
            valid = false; result.verifiedFiles.clear();
        }
        if (valid) result.verifiedModels.insert(snapshot.id);
        const auto hash = VocalSeparationCatalog::nativeRuntime().sha256;
        const auto fingerprint = runtimeVerificationFingerprint(runtime, hash, verifyRuntime);
        result.runtimeVerified = QFileInfo(runtime).isFile() && (!verifyRuntime
            || VocalSeparationInstaller::runtimeDirectoryIsVerified(QFileInfo(runtime).absolutePath(), hash, cancellation));
        result.runtimeChecked = !cancellation->load()
            && fingerprint == runtimeVerificationFingerprint(runtime, hash, verifyRuntime);
        result.runtimeFingerprint = fingerprint;
        return result;
    }));
    configurationChanged();
    return true;
}

void VocalSeparationController::advanceModelConfiguration(const std::shared_ptr<ModelConfiguration>& task)
{
    if (task->state == "idle" || task->cancellation->load()) return;
    if (task->queue.isEmpty()) {
        verifiedModelIds_.insert(task->modelId);
        verifiedOrRejectedModelIds_.insert(task->modelId);
        if (const auto* model = modelForId(task->modelId))
            modelVerificationFingerprints_.insert(task->modelId, modelVerificationFingerprint(
                *model, modelStorageDirectory_, buildModelDirectoryIndex(modelStorageDirectory_).filesByName));
        task->progress = 1;
        task->state = runtimeReady() ? "complete" : "waiting-runtime";
        configurationChanged(); return;
    }
    const auto& item = task->queue.first();
    task->detail = item.mirrorAttempted ? tr("国内镜像") : tr("官方线路");
    if (!QDir().mkpath(QFileInfo(item.destination).absolutePath())) {
        task->state = "failed"; task->error = tr("无法创建模型下载目录");
        configurationChanged(); return;
    }
    task->state = "downloading";
    task->downloader->start(item.file, item.destination);
    configurationChanged();
}

bool VocalSeparationController::verifyInstalledModels()
{
    verifyAllModelsOnNextScan_ = true;
    modelDirectoryScanTimer_.stop();
    scanModelDirectory();
    return modelDirectoryIndexWatcher_ != nullptr
        || verificationWatcher_ != nullptr;
}

void VocalSeparationController::pauseDownload() {
    if (downloadingModelId_.isEmpty()) {
        const auto id = downloadingModelId();
        if (!id.isEmpty()) { pauseConfiguration("model:" + id); return; }
    }
    if (cudaRuntime_->busy()) cudaRuntime_->pause(); else if (externalRuntime_->busy()) externalRuntime_->pause(); else downloader_->pause();
}
void VocalSeparationController::resumeDownload() {
    if (downloadingModelId_.isEmpty()) {
        const auto id = downloadingModelId();
        if (!id.isEmpty()) { resumeConfiguration("model:" + id); return; }
    }
    if (cudaRuntime_->busy()) cudaRuntime_->resume(); else if (externalRuntime_->busy()) externalRuntime_->resume(); else downloader_->resume();
}

void VocalSeparationController::cancelDownload()
{
    if (downloadingModelId_.isEmpty()) {
        const auto id = downloadingModelId();
        if (!id.isEmpty()) { cancelConfiguration("model:" + id); return; }
    }
    if (cudaRuntime_->busy()) {
        cudaRuntime_->cancel(); downloadingModelId_.clear(); emit downloadStateChanged(); refreshModels(); return;
    }
    if (externalRuntime_->busy()) {
        externalRuntime_->cancel();
        downloadingModelId_.clear();
        emit downloadStateChanged();
        refreshModels();
        return;
    }
    if (downloader_) downloader_->cancel();
    if (verificationPurpose_ == VerificationPurpose::Download) {
        ++verificationGeneration_;
        if (verificationCancellation_)
            verificationCancellation_->store(true, std::memory_order_release);
        verificationPurpose_ = VerificationPurpose::None;
        verifyingModelId_.clear();
    }
    if (runtimeInstallCancellation_)
        runtimeInstallCancellation_->store(true, std::memory_order_release);
    downloadQueue_.clear();
    runtimeOnlyDownload_ = false;
    downloadingModelId_.clear();
    failedDownloadModelId_.clear();
    downloadSource_.clear();
    preferDomesticMirror_ = false;
    downloadProgress_ = 0.0;
    completedDownloadBytes_ = 0;
    totalDownloadBytes_ = 0;
    emit downloadProgressChanged();
    emit downloadStateChanged();
    setError({});
    refreshModels();
}

bool VocalSeparationController::deleteModel(const QString& modelId)
{
    const VocalModelCard* model = modelForId(modelId);
    const VocalDownloadState downloadState = downloader_->state();
    const bool downloadActive = downloadingModelId_ == modelId
        && (downloadState == VocalDownloadState::Downloading
            || downloadState == VocalDownloadState::Paused
            || downloadState == VocalDownloadState::Verifying);
    if (model == nullptr) {
        const auto diagnostic = std::find_if(
            rejectedCustomModels_.cbegin(), rejectedCustomModels_.cend(),
            [&modelId](const QVariant& value) {
                return value.toMap().value(QStringLiteral("id")).toString()
                    == modelId;
            });
        if (diagnostic == rejectedCustomModels_.cend() || requestInFlight()
            || modelDirectoryIndexWatcher_ != nullptr
            || verificationWatcher_ != nullptr) {
            return false;
        }
        const QStringList paths = diagnostic->toMap()
                                      .value(QStringLiteral("paths"))
                                      .toStringList();
        if (paths.isEmpty()) {
            setError(tr("此诊断项没有可安全删除的本地模型文件"));
            return false;
        }
        for (const QString& path : paths) {
            if (!safeExistingFileWithin(path, modelStorageDirectory_)
                || !QFile::remove(path)) {
                setError(tr("无法删除本地模型文件：%1").arg(path));
                return false;
            }
        }
        scheduleModelDirectoryScan();
        return true;
    }
    if (requestInFlight() || downloadActive || modelConfigurationBusy(modelId)
        || runtimeInstallerWatcher_ != nullptr
        || modelDirectoryIndexWatcher_ != nullptr
        || verificationWatcher_ != nullptr)
        return false;
    const QStringList resolvedPaths = modelFilePaths(*model);
    const bool manuallyManagedFlatModel = !resolvedPaths.isEmpty()
        && QFileInfo(resolvedPaths.constFirst()).absolutePath()
            == QFileInfo(modelStorageDirectory_).absoluteFilePath();
    if (manuallyManagedFlatModel) {
        setError(tr("根目录中的手动模型请在模型目录中删除"));
        return false;
    }
    const VocalInstallResult removed =
        VocalSeparationInstaller::deleteModelFiles(*model,
                                                   modelStorageDirectory_);
    if (!removed.ok) {
        setError(removed.error);
        return false;
    }
    if (downloadingModelId_ == modelId) {
        downloadingModelId_.clear();
        downloadProgress_ = 0.0;
        completedDownloadBytes_ = 0;
        totalDownloadBytes_ = 0;
        emit downloadProgressChanged();
        emit downloadStateChanged();
    }
    if (failedDownloadModelId_ == modelId) failedDownloadModelId_.clear();
    modelConfigurations_.remove(modelId);
    verifiedModelIds_.remove(modelId);
    verifiedOrRejectedModelIds_.remove(modelId);
    modelVerificationFingerprints_.remove(modelId);
    refreshModels();
    return true;
}

bool VocalSeparationController::selectModel(const QString& modelId)
{
    if (modelForId(modelId) == nullptr || jobState_ == JobState::Running
        || jobState_ == JobState::Cancelling) return false;
    if (selectedModelId_ == modelId) return true;
    selectedModelId_ = modelId;
    invalidateRetry();
    emit selectedModelIdChanged();
    rebuildStems();
    deviceProbePending_ = true;
    QTimer::singleShot(0, this, [this, modelId] {
        if (deviceProbePending_ && selectedModelId_ == modelId && runtimeReady()) probeDevices();
    });
    return true;
}

bool VocalSeparationController::setStemSelected(StemKind kind, bool selected)
{
    if (requestInFlight()) return false;
    for (QVariant& value : stems_) {
        QVariantMap stem = value.toMap();
        if (stem.value(QStringLiteral("kind")).toInt() != int(kind)) continue;
        if (!stem.value(QStringLiteral("supported")).toBool()
            && !stem.value(QStringLiteral("available")).toBool()) return false;
        stem.insert(QStringLiteral("selected"), selected);
        value = stem;
        invalidateRetry();
        emit stemsChanged();
        emit startEligibilityChanged();
        return true;
    }
    return false;
}

bool VocalSeparationController::selectDevice(DeviceMode mode)
{
    if (requestInFlight()) return false;
    if (!deviceAvailable(mode)) return false;
    deviceChosenByUser_ = true;
    if (deviceMode_ == mode) return true;
    deviceMode_ = mode;
    invalidateRetry();
    emit deviceModeChanged();
    emit startEligibilityChanged();
    return true;
}

bool VocalSeparationController::selectOutputFormat(const QString& format)
{
    if (requestInFlight()) return false;
    const QString normalized = format.trimmed().toLower();
    if (!QStringList{QStringLiteral("wav"), QStringLiteral("flac"),
                     QStringLiteral("mp3")}
             .contains(normalized)) return false;
    outputFormat_ = normalized;
    invalidateRetry();
    emit outputFormatChanged();
    return true;
}

bool VocalSeparationController::selectOutputDirectory(const QUrl& directory)
{
    if (requestInFlight()) return false;
    const QString path = QFileInfo(directory.toLocalFile()).absoluteFilePath();
    if (path.isEmpty() || (!QDir(path).exists() && !QDir().mkpath(path))) return false;
    outputDirectory_ = path;
    invalidateRetry();
    emit outputDirectoryChanged();
    return true;
}

bool VocalSeparationController::probeDevices(bool force)
{
    // A queued page/hardware callback must not enqueue a probe behind an
    // active separation, including its verification phase.
    if (jobState_ == JobState::Running || jobState_ == JobState::Cancelling)
        return false;
    if (force) {
        deviceProbeCache_.remove(selectedModelId_);
        deviceProbeFingerprints_.remove(selectedModelId_);
    }
    if (!cudaRuntime_->nvidiaAvailable() && !cudaRuntime_->ready()) cudaRuntime_->refreshHardware();
    if (verificationWatcher_ != nullptr || modelDirectoryIndexWatcher_ != nullptr) {
        deviceProbePending_ = true;
        return true;
    }
    if (requestInFlight() || !process_.canAcceptRequest()
        || verificationWatcher_ != nullptr) return false;
    deviceProbePending_ = false;
    activeRequest_ = ActiveRequestContext{RequestKind::Probe};
    activeRequest_->modelId = selectedModelId_;
    const QString fingerprint = probeFingerprint(selectedModelId_);
    if (deviceProbeCache_.contains(selectedModelId_)
        && deviceProbeFingerprints_.value(selectedModelId_) == fingerprint) {
        activeProbeFingerprint_ = fingerprint;
        handleProbe(deviceProbeCache_.value(selectedModelId_));
        return true;
    }
    setError({});
#ifdef Q_OS_MACOS
    if (selectedModelId_ == QStringLiteral("python-vr-5hp") && externalRuntime_->ready()
        && selectedModel() && modelInstalled(*selectedModel())) {
        if (launchProbe()) return true;
        activeRequest_.reset(); return false;
    }
#endif
    setJobState(JobState::Probing, QStringLiteral("runtime_verification"));
    if (beginVerification(VerificationPurpose::Probe, selectedModel()))
        return true;
    activeRequest_.reset();
    setJobState(JobState::Idle, {});
    return false;
}

bool VocalSeparationController::start()
{
    if (downloadConflictsWithModel(selectedModelId_)) {
        reportStartDisabledReason();
        return false;
    }
    if (requestInFlight() || !process_.canAcceptRequest()) return false;
    // Starting the selected model supersedes its queued background probe.
    // Do not let that probe replace the completed separation's UI state.
    deviceProbePending_ = false;
    const VocalModelCard* model = selectedModel();
    const QString inputPath = inputInfo_.value(QStringLiteral("path")).toString();
    const QStringList stemNames = selectedStemNames();
    if (model == nullptr) {
        ++resultGeneration_;
        clearPublishedResult();
        setError(tr("请选择有效输入音频和至少一个输出音轨"));
        failedRequest_.reset();
        setJobState(JobState::JobFailed, QStringLiteral("validation"));
        return false;
    }
#ifndef Q_OS_MACOS
    if (model->family == VocalModelFamily::Demucs && deviceChosenByUser_
        && deviceMode_ == DeviceMode::GPU && !cudaRuntime_->ready()) {
        reportStartDisabledReason();
        return false;
    }
    if (((model->family == VocalModelFamily::Demucs && !cudaRuntime_->ready()) || model->id == QStringLiteral("python-vr-5hp"))
        && !deviceChosenByUser_
        && deviceMode_ == DeviceMode::GPU) {
        deviceMode_ = DeviceMode::Auto;
        emit deviceModeChanged();
    }
#endif
    ActiveRequestContext context;
    context.kind = RequestKind::Separation;
    context.inputPath = inputPath;
    context.modelId = model->id;
    context.outputRoot = outputDirectory_;
    context.outputFormat = outputFormat_;
    context.device = deviceMode_;
    context.stemKinds = selectedStemKinds();
    context.stemNames = stemNames;
    for (const QString& name : stemNames) {
        context.stemLabels.push_back(localizedStemLabel(stemKind(name)));
    }
    return beginSeparationRequest(std::move(context));
}

void VocalSeparationController::reportStartDisabledReason()
{
    QString reason = startDisabledReason();
    if (reason == tr("ONNX Runtime 尚未安装")) {
        reason += tr("，请点击所选模型的“一键配置”并等待下载完成");
    } else if (reason == tr("所选模型尚未安装或未通过校验")) {
        reason += tr("，请先下载或配置所选模型");
    }
    setError(reason);
}

bool VocalSeparationController::beginSeparationRequest(
    ActiveRequestContext context)
{
    context.resultGeneration = ++resultGeneration_;
    stopPreviewForCurrentInputOrResult();
    clearPublishedResult();
    if (!safeExistingFile(context.inputPath) || context.stemNames.isEmpty()) {
        failRequest(context, tr("请选择有效输入音频和至少一个输出音轨"),
                    QStringLiteral("validation"));
        return false;
    }
    if (!QDir().mkpath(context.outputRoot)
        || safePathKind(context.outputRoot) != SafePathKind::Directory) {
        failRequest(context, tr("无法创建安全的分离输出目录"),
                    QStringLiteral("validation"));
        return false;
    }
    const QString canonicalRoot = QDir(context.outputRoot).canonicalPath();
    if (canonicalRoot.isEmpty()) {
        failRequest(context, tr("无法规范化分离输出目录"),
                    QStringLiteral("validation"));
        return false;
    }
    context.outputRoot = canonicalRoot;
    activeRequest_ = context;
    failedRequest_.reset();
    progress_ = 0.0;
    emit progressChanged();
    setError({});
    setJobState(JobState::Running, QStringLiteral("model_verification"));
    if (context.modelId == QStringLiteral("python-vr-5hp")) {
        if (externalRuntime_->ready() && launchSeparation(context)) return true;
        failRequest(context, tr("Python VR 环境缺失，请使用模型卡片的一键配置"), "runtime_verification");
        return false;
    }
    if (verificationWatcher_ != nullptr) {
        // Hashing is bounded and shared; keep the start request alive while
        // an unrelated background check finishes, without stopping downloads.
        pendingStartVerification_ = true;
        return true;
    }
    if (beginVerification(VerificationPurpose::Start,
                          modelForId(context.modelId))) return true;
    failRequest(context, tr("无法开始模型校验"),
                QStringLiteral("model_verification"));
    return false;
}

void VocalSeparationController::failRequest(
    const ActiveRequestContext& context, const QString& error,
    const QString& stage)
{
    activeRequest_.reset();
    failedRequest_ = context;
    setError(error);
    setJobState(JobState::JobFailed, stage);
}

void VocalSeparationController::invalidateRetry()
{
    if (!requestInFlight() && failedRequest_.has_value()) {
        failedRequest_.reset();
        emit jobStateChanged();
        emit startEligibilityChanged();
    }
}

void VocalSeparationController::cancel()
{
    if (jobState_ != JobState::Running && jobState_ != JobState::Probing) return;
    if (pendingStartVerification_) {
        pendingStartVerification_ = false;
        activeRequest_.reset();
        failedRequest_.reset();
        setJobState(JobState::Cancelled, QStringLiteral("cancelled"));
        return;
    }
    if (verificationWatcher_ != nullptr
        && (verificationPurpose_ == VerificationPurpose::Start
            || verificationPurpose_ == VerificationPurpose::Probe)) {
        if (verificationCancellation_)
            verificationCancellation_->store(true, std::memory_order_release);
        if (!verifyingModelId_.isEmpty()) {
            verifiedModelIds_.remove(verifyingModelId_);
            verifiedOrRejectedModelIds_.remove(verifyingModelId_);
            modelVerificationFingerprints_.remove(verifyingModelId_);
        }
        ++verificationGeneration_;
        verificationPurpose_ = VerificationPurpose::None;
        verifyingModelId_.clear();
        activeRequest_.reset();
        failedRequest_.reset();
        setJobState(JobState::Cancelled, QStringLiteral("cancelled"));
        refreshModels();
        return;
    }
    setJobState(JobState::Cancelling, QStringLiteral("cancelling"));
    process_.cancel();
}

bool VocalSeparationController::retry()
{
    if (!process_.canAcceptRequest() || jobState_ != JobState::JobFailed
        || !failedRequest_.has_value()) return false;
    const ActiveRequestContext context = *failedRequest_;
    if (context.kind == RequestKind::Separation)
        return beginSeparationRequest(context);
    activeRequest_ = context;
    failedRequest_.reset();
    setError({});
    setJobState(JobState::Probing, QStringLiteral("runtime_verification"));
    if (beginVerification(VerificationPurpose::Probe)) return true;
    failRequest(context, tr("无法开始运行时校验"),
                QStringLiteral("runtime_verification"));
    return false;
}

bool VocalSeparationController::previewInput()
{
    const QString path = inputInfo_.value(QStringLiteral("path")).toString();
    if (preview_ != nullptr) preview_->setVolume(1.0);
    resetResultPreviewState();
    return togglePreviewPath(path);
}

bool VocalSeparationController::previewStem(StemKind kind)
{
    const QString path = pathForStem(kind);
    if (preview_ != nullptr) {
        preview_->setVolume(stemPreviewVolumes_.value(int(kind), 0.8));
    }
    const bool started = togglePreviewPath(path, publishedOutputRoot_);
    if (started) {
        const bool changed = resultPreviewMode_ != ResultPreviewMode::Solo
            || resultPreviewSoloKind_ != kind
            || resultPreviewMixKinds_ != QList<StemKind>{kind};
        resultPreviewMode_ = ResultPreviewMode::Solo;
        resultPreviewSoloKind_ = kind;
        resultPreviewMixKinds_ = {kind};
        if (changed) emit resultPreviewChanged();
    }
    return started;
}

bool VocalSeparationController::toggleResultMix(const qint64 positionMs)
{
    if (preview_ == nullptr) return false;
    if (resultPreviewMode_ == ResultPreviewMode::Mix
        && preview_->mixActive()) {
        if (preview_->playing()) preview_->pause();
        else preview_->resume();
        return true;
    }
    const QList<StemKind> kinds = resultMixKinds();
    return startResultPreview(kinds, positionMs, ResultPreviewMode::Mix,
                              StemKind::Original);
}

bool VocalSeparationController::previewStemAt(const StemKind kind,
                                              const qint64 positionMs)
{
    return startResultPreview({kind}, positionMs, ResultPreviewMode::Solo,
                              kind);
}

bool VocalSeparationController::setStemPreviewVolume(StemKind kind,
                                                      double volume)
{
    const double bounded = qBound(0.0, volume, 1.0);
    for (QVariant& value : stems_) {
        QVariantMap stem = value.toMap();
        if (stem.value(QStringLiteral("kind")).toInt() != int(kind)) continue;
        if (!stem.value(QStringLiteral("supported")).toBool()
            && !stem.value(QStringLiteral("available")).toBool()) return false;
        stemPreviewVolumes_.insert(int(kind), bounded);
        stem.insert(QStringLiteral("previewVolume"), bounded);
        value = stem;
        const QString path = stem.value(QStringLiteral("path")).toString();
        if (preview_ != nullptr && !path.isEmpty()) {
            if (preview_->mixSourceIds().contains(path)) {
                (void)preview_->setMixSourceGain(path, bounded);
            } else if (preview_->isCurrentSource(QUrl::fromLocalFile(path))) {
                preview_->setVolume(bounded);
            }
        }
        emit stemsChanged();
        return true;
    }
    return false;
}

bool VocalSeparationController::exportStem(StemKind kind,
                                           const QUrl& destination)
{
    const QString source = pathForStem(kind);
    if (!safeExistingFileWithin(source, publishedOutputRoot_)) return false;
    QString target = destination.toLocalFile();
    if (QFileInfo(target).isDir()) target = QDir(target).filePath(QFileInfo(source).fileName());
    const bool copied = atomicCopyNoOverwrite(source, publishedOutputRoot_, target);
    if (!copied) setError(tr("导出失败：源文件不安全、目标已存在或写入失败"));
    return copied;
}

bool VocalSeparationController::exportStemToOutputDirectory(StemKind kind)
{
    return exportStem(kind, QUrl::fromLocalFile(outputDirectory_));
}

bool VocalSeparationController::exportSelected(const QUrl& destinationDirectory)
{
    QList<StemKind> kinds;
    for (const auto& value : stems_) {
        const auto stem = value.toMap();
        if (stem.value("available").toBool() && stem.value("selected").toBool())
            kinds.push_back(static_cast<StemKind>(stem.value("kind").toInt()));
    }
    return exportKinds(kinds, destinationDirectory);
}

bool VocalSeparationController::exportAll(const QUrl& destinationDirectory)
{
    QList<StemKind> kinds;
    for (const QVariant& value : stems_) {
        const QVariantMap stem = value.toMap();
        if (stem.value(QStringLiteral("available")).toBool()) {
            kinds.push_back(static_cast<StemKind>(
                stem.value(QStringLiteral("kind")).toInt()));
        }
    }
    return exportKinds(kinds, destinationDirectory);
}

bool VocalSeparationController::exportKinds(
    const QList<StemKind>& kinds, const QUrl& destinationDirectory)
{
    const QString requestedDestination = destinationDirectory.toLocalFile();
    if (safePathKind(requestedDestination) != SafePathKind::Directory) {
        setError(tr("批量导出目录不存在或不安全"));
        return false;
    }
    const QString destination = QDir(requestedDestination).canonicalPath();
    if (destination.isEmpty()) return false;
    QList<QPair<QString, QString>> files;
    for (const StemKind kind : kinds) {
        const QString source = pathForStem(kind);
        if (!safeExistingFileWithin(source, publishedOutputRoot_)) {
            setError(tr("批量导出失败：至少一个已选音轨不存在或不安全"));
            return false;
        }
        files.push_back({source, QFileInfo(source).fileName()});
    }
    if (files.isEmpty()) return false;
    const QString token = QUuid::createUuid().toString(QUuid::Id128).toLower();
    const QString stagingName = QStringLiteral(".agplayer-export-%1.staging").arg(token);
    const QString finalName = QStringLiteral("AgPlayer-separation-export-%1").arg(token);
    const QString staging = QDir(destination).filePath(stagingName);
    const QString final = QDir(destination).filePath(finalName);
    if (!QDir(destination).mkdir(stagingName)
        || safePathKind(staging) != SafePathKind::Directory) {
        setError(tr("无法创建安全的批量导出暂存目录"));
        return false;
    }
    QStringList stagedFiles;
    const auto cleanup = [&] {
        for (const QString& path : stagedFiles) QFile::remove(path);
        QDir().rmdir(staging);
    };
    for (const auto& file : files) {
        const QString target = QDir(staging).filePath(file.second);
        if (!atomicCopyNoOverwrite(file.first, publishedOutputRoot_, target)) {
            cleanup();
            setError(tr("批量导出失败，未发布任何音轨"));
            return false;
        }
        stagedFiles.push_back(target);
    }
    if (QFileInfo::exists(final) || !QDir().rename(staging, final)) {
        cleanup();
        setError(tr("批量导出提交失败，未发布不完整结果"));
        return false;
    }
    setError({});
    return true;
}

bool VocalSeparationController::addStemToPlaylist(
    StemKind kind, const QString& playlistId)
{
    const QString path = pathForStem(kind);
    return safeExistingFileWithin(path, publishedOutputRoot_)
        && addPathsToPlaylist({path}, playlistId);
}

bool VocalSeparationController::addSelectedToPlaylist(const QString& playlistId)
{
    QStringList paths;
    for (const auto& value : stems_) {
        const auto stem = value.toMap();
        if (!stem.value("selected").toBool() || !stem.value("available").toBool()) continue;
        const QString path = stem.value("path").toString();
        if (!safeExistingFileWithin(path, publishedOutputRoot_)) {
            setError(tr("无法加入播放列表：至少一个已选音轨不存在或不安全"));
            return false;
        }
        paths.push_back(path);
    }
    return !paths.isEmpty() && addPathsToPlaylist(paths, playlistId);
}

bool VocalSeparationController::openOutputDirectory()
{
    return QDir(outputDirectory_).exists()
        && QDesktopServices::openUrl(QUrl::fromLocalFile(outputDirectory_));
}

bool VocalSeparationController::selectHistoryInput(const QString& localPath)
{
    if (localPath.trimmed().isEmpty()) return false;
    return selectInput(QUrl::fromLocalFile(localPath));
}

bool VocalSeparationController::openHistoryOutputDirectory(
    const QString& localPath)
{
    if (localPath.trimmed().isEmpty()) return false;
    const QString absolutePath = QFileInfo(localPath).absoluteFilePath();
    return QDir(absolutePath).exists()
        && QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath));
}

bool VocalSeparationController::openModelDirectory()
{
    return QDir().mkpath(modelStorageDirectory_)
        && QDesktopServices::openUrl(
            QUrl::fromLocalFile(modelStorageDirectory_));
}

bool VocalSeparationController::selectModelDirectory(const QUrl& directory)
{
    const VocalDownloadState downloadState = downloader_
        ? downloader_->state() : VocalDownloadState::Idle;
    const bool downloadActive = downloadState == VocalDownloadState::Downloading
        || downloadState == VocalDownloadState::Paused
        || downloadState == VocalDownloadState::Verifying;
    if (requestInFlight() || downloadBusy() || verificationWatcher_ != nullptr
        || modelDirectoryIndexWatcher_ != nullptr
        || runtimeInstallerWatcher_ != nullptr || downloadActive
        || !downloadQueue_.isEmpty()) {
        return false;
    }
    const QString localPath = directory.toLocalFile();
    const QFileInfo info(localPath);
    if (!directory.isLocalFile() || !info.isDir()) {
        setError(tr("请选择可访问的本地模型目录"));
        return false;
    }
    const QString selected = info.canonicalFilePath().isEmpty()
        ? info.absoluteFilePath() : info.canonicalFilePath();
    if (!saveModelStorageDirectory(options_.dataRoot, selected)) {
        setError(tr("无法保存模型目录设置"));
        return false;
    }
    modelDirectoryScanTimer_.stop();
    modelStorageDirectory_ = selected;
    indexedModelFiles_.clear();
    modelDirectoryRescanPending_ = false;
    verifiedModelIds_.clear();
    verifiedOrRejectedModelIds_.clear();
    modelVerificationFingerprints_.clear();
    setError({});
    rebuildModelDirectoryWatcher();
    refreshModels();
    emit modelStorageDirectoryChanged();
    scheduleModelDirectoryScan();
    return true;
}

const VocalModelCard* VocalSeparationController::selectedModel() const
{
    return modelForId(selectedModelId_);
}

const VocalModelCard* VocalSeparationController::modelForId(
    const QString& modelId) const
{
    const auto it = std::find_if(options_.catalog.cbegin(), options_.catalog.cend(),
                                 [&modelId](const VocalModelCard& model) {
        return model.id == modelId;
    });
    return it == options_.catalog.cend() ? nullptr : &*it;
}

QString VocalSeparationController::modelDirectory(const QString& modelId) const
{
    return QDir(modelStorageDirectory_).filePath(modelId);
}

QStringList VocalSeparationController::modelFilePaths(
    const VocalModelCard& model) const
{
    return resolveModelFilePaths(model, modelStorageDirectory_,
                                 indexedModelFiles_);
}

QString VocalSeparationController::runtimeDirectory() const
{
    return QDir(options_.dataRoot).filePath(QStringLiteral("runtime"));
}

bool VocalSeparationController::modelInstalled(const VocalModelCard& model) const
{
    if (model.id == QStringLiteral("python-vr-5hp"))
        return externalRuntime_->ready() && verifiedModelIds_.contains(model.id) && modelFilesPresent(model);
    return verifiedModelIds_.contains(model.id);
}

bool VocalSeparationController::modelFilesPresent(
    const VocalModelCard& model) const
{
    return modelFilesMatchSizes(model, modelFilePaths(model));
}

bool VocalSeparationController::runtimeReady() const
{
    if (!QFileInfo(options_.runtimeLibraryPath).isFile()) return false;
    return !options_.verifyRuntimeIntegrity || runtimeVerified_;
}

bool VocalSeparationController::deviceAvailable(DeviceMode mode) const
{
    for (const QVariant& value : availableDevices_) {
        const QVariantMap device = value.toMap();
        if (device.value(QStringLiteral("mode")).toInt() == int(mode))
            return device.value(QStringLiteral("available")).toBool();
    }
    return false;
}

QVariantMap VocalSeparationController::configurationFields(const QString& modelId) const
{
    const auto task = modelConfigurations_.value(modelId);
    const auto* model = modelForId(modelId);
    const bool ready = model && modelInstalled(*model)
        && (modelId == "python-vr-5hp" ? externalRuntime_->ready() : runtimeReady());
    QString state = task ? task->state : ready ? QStringLiteral("complete") : QStringLiteral("idle");
    QString taskId = "model:" + modelId;
    if (!task && modelId == downloadingModelId_) {
        if (downloader_->state() == VocalDownloadState::Downloading) state = "downloading";
        if (downloader_->state() == VocalDownloadState::Paused) state = "paused";
        if (downloader_->state() == VocalDownloadState::Verifying) state = "checking";
    }
    if (modelId == "python-vr-5hp") {
        taskId = "runtime:python";
        if (externalRuntime_->busy()) state = externalRuntime_->paused() ? "paused" : "downloading";
        else if (!runtimeErrors_.value("python").isEmpty()) state = "failed";
    }
    const auto taskError = task ? task->error : modelId == "python-vr-5hp" ? runtimeErrors_.value("python") : QString();
    return {{"configurationTaskId", taskId}, {"configurationState", state},
        {"configurationProgress", modelId == "python-vr-5hp"
            ? (ready && !externalRuntime_->busy() ? 1.0 : runtimeProgress_.value("python", 0))
            : task ? task->progress : modelId == downloadingModelId_ ? downloadProgress_ : ready ? 1.0 : 0.0},
        {"configurationDetail", modelId == "python-vr-5hp" ? runtimeDetails_.value("python")
            : task ? task->detail : QString()},
        {"configurationError", taskError},
        {"configurationCanPause", state == "downloading"},
        {"configurationCanResume", state == "paused" || state == "failed"},
        {"configurationCanCancel", (task && modelConfigurationBusy(modelId))
            || state == "downloading" || state == "paused" || state == "checking"}};
}

QVariantList VocalSeparationController::runtimeConfigurations() const
{
    QVariantList rows;
    for (const QString& id : {QStringLiteral("directml"), QStringLiteral("python"), QStringLiteral("cuda")}) {
#ifdef Q_OS_MACOS
        if (id == QStringLiteral("cuda")) continue;
#endif
        const bool python = id == "python", cuda = id == "cuda";
        const bool busy = python ? externalRuntime_->busy() : cuda ? cudaRuntime_->busy()
            : runtimeOnlyDownload_ || runtimeInstallerWatcher_ != nullptr;
        const bool ready = python ? externalRuntime_->ready() : cuda ? cudaRuntime_->ready() : runtimeReady();
        const bool paused = python ? externalRuntime_->paused() : cuda ? cudaRuntime_->paused()
            : downloader_->state() == VocalDownloadState::Paused;
        const auto error = runtimeErrors_.value(id);
        const QString state = busy ? paused ? "paused" : "downloading"
            : ready ? "complete" : !error.isEmpty() ? "failed" : "idle";
        const QString taskId = "runtime:" + id;
        rows.push_back(QVariantMap{{"id", id}, {"taskId", taskId}, {"configurationTaskId", taskId},
            {"name", python ? "Python / PyTorch" : cuda ? "CUDA / cuDNN" :
#ifdef Q_OS_MACOS
                "ONNX Runtime / CoreML"},
#else
                "ONNX Runtime / DirectML"},
#endif
            {"configurationState", state}, {"configurationProgress", ready && !busy ? 1.0 : runtimeProgress_.value(id, 0)},
            {"configurationDetail", runtimeDetails_.value(id)},
            {"configurationError", error}, {"configurationCanPause", busy && !paused
                && (id != "directml" || downloader_->state() == VocalDownloadState::Downloading)},
            {"configurationCanResume", paused || state == "failed"},
            {"configurationCanCancel", busy}});
    }
    return rows;
}

void VocalSeparationController::pauseConfiguration(const QString& taskId)
{
    if (taskId == "runtime:python") externalRuntime_->pause();
    else if (taskId == "runtime:cuda") cudaRuntime_->pause();
    else if (taskId == "runtime:directml") downloader_->pause();
    else if (taskId.startsWith("model:")) {
        const auto task = modelConfigurations_.value(taskId.mid(6));
        if (task) task->downloader->pause();
        else if (taskId.mid(6) == downloadingModelId_) downloader_->pause();
    }
    configurationChanged();
}

void VocalSeparationController::resumeConfiguration(const QString& taskId)
{
    if (taskId == "runtime:python") {
        if (activeRequest_ && activeRequest_->kind == RequestKind::Separation
            && activeRequest_->modelId == "python-vr-5hp") return;
        if (externalRuntime_->busy()) externalRuntime_->resume();
        else { runtimeErrors_.remove("python"); if (!externalRuntime_->start()) runtimeErrors_["python"] = tr("无法启动配置"); }
    } else if (taskId == "runtime:cuda") {
        if (activeRequest_ && activeRequest_->kind == RequestKind::Separation
            && activeRequest_->modelId != "python-vr-5hp") return;
        if (cudaRuntime_->busy()) cudaRuntime_->resume();
        else { runtimeErrors_.remove("cuda"); if (!cudaRuntime_->start()) runtimeErrors_["cuda"] = tr("无法启动配置"); }
    } else if (taskId == "runtime:directml") {
        if (downloader_->state() == VocalDownloadState::Paused) downloader_->resume();
        else if (!ensureSharedRuntime()) runtimeErrors_["directml"] = tr("无法启动配置");
    } else if (taskId.startsWith("model:")) {
        const auto task = modelConfigurations_.value(taskId.mid(6));
        if (task && task->state == "paused") task->downloader->resume();
        else if (task && task->state == "failed") (void)beginModelConfiguration(task->modelId, task->mirror);
        else if (!task && taskId.mid(6) == downloadingModelId_) downloader_->resume();
    }
    configurationChanged();
}

void VocalSeparationController::cancelConfiguration(const QString& taskId)
{
    if (taskId == "runtime:python") externalRuntime_->cancel();
    else if (taskId == "runtime:cuda") cudaRuntime_->cancel();
    else if (taskId == "runtime:directml") {
        downloader_->cancel();
        if (runtimeInstallCancellation_) runtimeInstallCancellation_->store(true);
        downloadQueue_.clear(); runtimeOnlyDownload_ = false;
        if (downloadingModelId_ == "runtime") downloadingModelId_.clear();
        runtimeErrors_["directml"] = tr("配置已取消");
    } else if (taskId.startsWith("model:")) {
        const auto task = modelConfigurations_.value(taskId.mid(6));
        if (task) {
            task->cancellation->store(true); ++task->generation;
            task->state = "idle"; task->downloader->cancel(); task->queue.clear();
        } else if (taskId.mid(6) == downloadingModelId_) {
            downloader_->cancel(); downloadQueue_.clear(); downloadingModelId_.clear();
            downloadProgress_ = 0;
        }
    }
    configurationChanged();
}

bool VocalSeparationController::downloadConflictsWithModel(const QString& modelId) const
{
    if (modelConfigurationBusy(modelId)) return true;
    // Separate models can be downloaded while a worker reads an installed model.
    // Never write the selected model or its shared native runtime underneath it.
    return downloadBusy() && (downloadingModelId_ == modelId
        || (modelId != QStringLiteral("python-vr-5hp")
            && (runtimeOnlyDownload_ || runtimeInstallerWatcher_ != nullptr
                || std::any_of(downloadQueue_.cbegin(), downloadQueue_.cend(),
                    [](const DownloadItem& item) { return item.runtimeArchive; }))));
}

bool VocalSeparationController::canConfigureModel(const QString& modelId) const
{
    if (!modelId.isEmpty() && modelId == downloadingModelId_) return false;
    if (modelConfigurationBusy(modelId)) return false;
    if (modelId == "python-vr-5hp" && (externalRuntime_->busy() || externalRuntime_->ready())) return false;
    if (const auto* model = modelForId(modelId);
        model && modelInstalled(*model) && runtimeReady()) return false;
    if (jobState_ == JobState::Cancelling) return false;
    if (jobState_ != JobState::Running) return true;
    if (!activeRequest_ || activeRequest_->kind != RequestKind::Separation
        || activeRequest_->modelId == modelId
        || pendingStartVerification_
        || verificationPurpose_ == VerificationPurpose::Start) return false;
    return modelId == QStringLiteral("python-vr-5hp")
        || activeRequest_->modelId == QStringLiteral("python-vr-5hp")
        || runtimeReady();
}

QString VocalSeparationController::probeFingerprint(const QString& modelId) const
{
    const auto* model = modelForId(modelId);
    const QString runtimePath = cudaRuntime_->ready()
        ? cudaRuntime_->libraryPath() : options_.runtimeLibraryPath;
    return runtimeVerificationFingerprint(runtimePath,
        VocalSeparationCatalog::nativeRuntime().sha256,
        options_.verifyRuntimeIntegrity)
        + QLatin1Char('|') + cudaRuntime_->hardwareName()
        + QLatin1Char('|') + cudaRuntime_->driverVersion()
        + QLatin1Char('|') + (model ? modelVerificationFingerprint(
            *model, modelStorageDirectory_, indexedModelFiles_) : QString());
}

bool VocalSeparationController::beginVerification(
    VerificationPurpose purpose, const VocalModelCard* model)
{
    if (verificationWatcher_ != nullptr || purpose == VerificationPurpose::None)
        return false;
    const QList<VocalModelCard> catalog = model == nullptr
        ? options_.catalog : QList<VocalModelCard>{*model};
    const QString modelStorageDirectory = modelStorageDirectory_;
    const QString runtimePath = options_.runtimeLibraryPath;
    const bool verifyRuntime = options_.verifyRuntimeIntegrity;
    const QString runtimeHash = VocalSeparationCatalog::nativeRuntime().sha256;
    const QHash<QString, QString> cachedModelFingerprints =
        modelVerificationFingerprints_;
    const QSet<QString> cachedVerifiedModels = verifiedModelIds_;
    const QSet<QString> cachedCheckedModels = verifiedOrRejectedModelIds_;
    const bool cachedRuntimeKnown = runtimeVerificationKnown_;
    const bool cachedRuntimeVerified = runtimeVerified_;
    const QString cachedRuntimeFingerprint = runtimeVerificationFingerprint_;
    VerificationResult cachedResult;
    bool fullyCached = !modelDirectoryRescanPending_;
    for (const VocalModelCard& candidate : catalog) {
        const QString fingerprint = modelVerificationFingerprint(
            candidate, modelStorageDirectory_, indexedModelFiles_);
        if (!cachedCheckedModels.contains(candidate.id)
            || cachedModelFingerprints.value(candidate.id) != fingerprint) {
            fullyCached = false;
            break;
        }
        cachedResult.checkedModels.insert(candidate.id);
        cachedResult.modelFingerprints.insert(candidate.id, fingerprint);
        if (cachedVerifiedModels.contains(candidate.id))
            cachedResult.verifiedModels.insert(candidate.id);
    }
    const QString currentRuntimeFingerprint = runtimeVerificationFingerprint(
        runtimePath, runtimeHash, verifyRuntime);
    if (!cachedRuntimeKnown
        || cachedRuntimeFingerprint != currentRuntimeFingerprint) {
        fullyCached = false;
    } else {
        cachedResult.runtimeChecked = true;
        cachedResult.runtimeVerified = cachedRuntimeVerified;
        cachedResult.runtimeFingerprint = currentRuntimeFingerprint;
    }
    if (fullyCached) {
        verificationPurpose_ = purpose;
        verifyingModelId_ = model == nullptr ? QString() : model->id;
        finishVerification(verificationGeneration_, cachedResult);
        return true;
    }

    verificationPurpose_ = purpose;
    verifyingModelId_ = model == nullptr ? QString() : model->id;
    const quint64 generation = ++verificationGeneration_;
    verificationCancellation_ = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = verificationCancellation_;
    verificationWatcher_ = new QFutureWatcher<VerificationResult>(this);
    QFutureWatcher<VerificationResult>* const watcher = verificationWatcher_;
    connect(watcher, &QFutureWatcher<VerificationResult>::finished,
            this, [this, watcher, generation, cancellation] {
        const VerificationResult result = watcher->result();
        watcher->deleteLater();
        if (verificationWatcher_ == watcher) {
            verificationWatcher_ = nullptr;
            if (verificationCancellation_ == cancellation)
                verificationCancellation_.reset();
        }
        if (generation != verificationGeneration_) {
            refreshModels();
            if (deferredRuntimeConfigurationModelId_.has_value()) {
                const QString modelId =
                    *std::exchange(deferredRuntimeConfigurationModelId_,
                                   std::nullopt);
                QTimer::singleShot(0, this, [this, modelId] {
                    configureRuntime(modelId);
                });
                return;
            }
            if (modelDirectoryRescanPending_) {
                modelDirectoryRescanPending_ = false;
                scheduleModelDirectoryScan();
            }
            return;
        }
        finishVerification(generation, result);
    });
    refreshModels();
    verificationWatcher_->setFuture(QtConcurrent::run(
        [catalog, modelStorageDirectory, runtimePath, verifyRuntime, runtimeHash,
         cachedModelFingerprints, cachedVerifiedModels, cachedCheckedModels,
         cachedRuntimeKnown, cachedRuntimeVerified, cachedRuntimeFingerprint,
         cancellation] {
            VerificationResult result;
            const ModelFileIndex fileIndex = buildModelDirectoryIndex(
                modelStorageDirectory).filesByName;
            const auto cancelled = [&cancellation] {
                return cancellation->load(std::memory_order_acquire);
            };
            for (const VocalModelCard& candidate : catalog) {
                if (cancelled()) return result;
                const QString fingerprint = modelVerificationFingerprint(
                    candidate, modelStorageDirectory, fileIndex);
                if (cachedCheckedModels.contains(candidate.id)
                    && cachedModelFingerprints.value(candidate.id)
                        == fingerprint) {
                    result.checkedModels.insert(candidate.id);
                    result.modelFingerprints.insert(candidate.id,
                                                    fingerprint);
                    if (cachedVerifiedModels.contains(candidate.id))
                        result.verifiedModels.insert(candidate.id);
                    continue;
                }
                bool verified = !candidate.files.isEmpty();
                QSet<QString> verifiedFiles;
                const QStringList paths = resolveModelFilePaths(
                    candidate, modelStorageDirectory, fileIndex);
                if (paths.size() != candidate.files.size()) verified = false;
                for (qsizetype index = 0;
                     index < candidate.files.size(); ++index) {
                    if (cancelled()) return result;
                    if (index >= paths.size()) break;
                    const VocalDownloadFile& file = candidate.files.at(index);
                    const QString& path = paths.at(index);
                    if (VocalSeparationInstaller::isVerifiedFile(
                            file, path, cancellation)) {
                        verifiedFiles.insert(QFileInfo(path).absoluteFilePath());
                    } else {
                        if (cancelled()) return result;
                        verified = false;
                    }
                }
                const QString verifiedFingerprint = modelVerificationFingerprint(
                    candidate, modelStorageDirectory, fileIndex);
                if (fingerprint != verifiedFingerprint) continue;
                result.checkedModels.insert(candidate.id);
                result.modelFingerprints.insert(candidate.id,
                                                verifiedFingerprint);
                result.verifiedFiles.unite(verifiedFiles);
                if (verified) result.verifiedModels.insert(candidate.id);
            }
            if (cancelled()) return result;
            const QString runtimeFingerprint = runtimeVerificationFingerprint(
                runtimePath, runtimeHash, verifyRuntime);
            if (cachedRuntimeKnown
                && cachedRuntimeFingerprint == runtimeFingerprint) {
                result.runtimeChecked = true;
                result.runtimeVerified = cachedRuntimeVerified;
                result.runtimeFingerprint = runtimeFingerprint;
            } else {
                result.runtimeVerified = QFileInfo(runtimePath).isFile()
                    && (!verifyRuntime
                        || VocalSeparationInstaller::runtimeDirectoryIsVerified(
                            QFileInfo(runtimePath).absolutePath(), runtimeHash,
                            cancellation));
                const QString verifiedRuntimeFingerprint =
                    runtimeVerificationFingerprint(
                        runtimePath, runtimeHash, verifyRuntime);
                if (!cancelled()
                    && runtimeFingerprint == verifiedRuntimeFingerprint) {
                    result.runtimeChecked = true;
                    result.runtimeFingerprint = verifiedRuntimeFingerprint;
                }
            }
            return result;
        }));
    return true;
}

void VocalSeparationController::finishVerification(
    quint64 generation, const VerificationResult& result)
{
    if (generation != verificationGeneration_) return;
    const auto pendingStart = qScopeGuard([this] {
        if (!pendingStartVerification_) return;
        QTimer::singleShot(0, this, [this] {
            if (!pendingStartVerification_ || verificationWatcher_ != nullptr) return;
            pendingStartVerification_ = false;
            if (activeRequest_ && activeRequest_->kind == RequestKind::Separation
                && jobState_ == JobState::Running) {
                const ActiveRequestContext context = *activeRequest_;
                if (!beginVerification(VerificationPurpose::Start,
                                       modelForId(context.modelId)))
                    failRequest(context, tr("无法开始模型校验"), QStringLiteral("model_verification"));
            }
        });
    });
    const auto pendingRescan = qScopeGuard([this] {
        if (!modelDirectoryRescanPending_) return;
        modelDirectoryRescanPending_ = false;
        scheduleModelDirectoryScan();
    });
    const VerificationPurpose purpose = verificationPurpose_;
    const QString verifiedModelId = verifyingModelId_;
    verificationPurpose_ = VerificationPurpose::None;
    verifyingModelId_.clear();
    const QString currentRuntimeFingerprint = runtimeVerificationFingerprint(
        options_.runtimeLibraryPath, VocalSeparationCatalog::nativeRuntime().sha256,
        options_.verifyRuntimeIntegrity);
    if (result.runtimeChecked && result.runtimeFingerprint == currentRuntimeFingerprint) {
        runtimeVerified_ = result.runtimeVerified;
        runtimeVerificationKnown_ = true;
        runtimeVerificationFingerprint_ = result.runtimeFingerprint;
    } else if (runtimeVerificationFingerprint_ != currentRuntimeFingerprint) {
        runtimeVerified_ = false;
        runtimeVerificationKnown_ = false;
        runtimeVerificationFingerprint_.clear();
        modelDirectoryRescanPending_ = true;
    }

    // An independent download may finish while this snapshot is hashing.
    // Only publish matching snapshots; retain newer matching cache entries.
    for (const auto& model : options_.catalog) {
        if (!verifiedModelId.isEmpty() && model.id != verifiedModelId) continue;
        const QString fingerprint = modelVerificationFingerprint(model, modelStorageDirectory_, indexedModelFiles_);
        if (result.checkedModels.contains(model.id)
            && result.modelFingerprints.value(model.id) == fingerprint) {
            if (result.verifiedModels.contains(model.id)) verifiedModelIds_.insert(model.id);
            else verifiedModelIds_.remove(model.id);
            verifiedOrRejectedModelIds_.insert(model.id);
            modelVerificationFingerprints_.insert(model.id, fingerprint);
        } else if (modelVerificationFingerprints_.value(model.id) != fingerprint) {
            verifiedModelIds_.remove(model.id);
            verifiedOrRejectedModelIds_.remove(model.id);
            modelVerificationFingerprints_.remove(model.id);
            modelDirectoryRescanPending_ = true;
        }
    }

    if (purpose == VerificationPurpose::Refresh) {
        refreshModels();
        if (deviceProbePending_) QTimer::singleShot(0, this, [this] { probeDevices(); });
        return;
    }
    if (purpose == VerificationPurpose::Download) {
        const VocalModelCard* model = modelForId(verifiedModelId);
        if (model == nullptr) {
            failedDownloadModelId_ = downloadingModelId_;
            downloadingModelId_.clear();
            downloadProgress_ = 0.0;
            completedDownloadBytes_ = 0;
            totalDownloadBytes_ = 0;
            emit downloadProgressChanged();
            emit downloadStateChanged();
            refreshModels();
            return;
        }
        const bool flatModelVerified =
            result.verifiedModels.contains(model->id);
        for (const VocalDownloadFile& file : model->files) {
            const QString destination = QDir(modelDirectory(model->id))
                .filePath(file.fileName);
            if (!flatModelVerified && !result.verifiedFiles.contains(
                    QFileInfo(destination).absoluteFilePath())) {
                VocalDownloadFile selectedFile = file;
                const QUrl mirror = vocalDomesticMirrorUrl(file.url);
                if (preferDomesticMirror_ && mirror.isValid())
                    selectedFile.url = mirror;
                downloadQueue_.push_back({selectedFile, destination, false,
                                          mirror, preferDomesticMirror_});
            }
        }
        if (!runtimeReady()) {
            const VocalRuntimePackage package =
                VocalSeparationCatalog::nativeRuntime();
            const VocalDownloadFile archive{
                QStringLiteral("runtime.nupkg"), package.url,
                package.bytes, package.sha256};
            downloadQueue_.push_back({
                archive,
                QDir(options_.dataRoot).filePath(
                    QStringLiteral("downloads/runtime.nupkg")),
                true, {}, false});
        }
        completedDownloadBytes_ = 0;
        totalDownloadBytes_ = 0;
        for (const DownloadItem& item : std::as_const(downloadQueue_))
            totalDownloadBytes_ += item.file.bytes;
        startNextDownload();
        return;
    }
    if (purpose == VerificationPurpose::Probe) {
        if (!runtimeReady()) {
            activeRequest_.reset();
            failedRequest_.reset();
            setError(tr("ONNX Runtime 尚未配置，请点击模型卡片的一键配置"));
            setJobState(JobState::Idle, QStringLiteral("runtime_missing"));
            return;
        }
        if (!runtimeReady() || !launchProbe()) {
            const ActiveRequestContext context = activeRequest_.value_or(
                ActiveRequestContext{RequestKind::Probe});
            failRequest(context, tr("ONNX Runtime 尚未安装或校验失败"),
                        QStringLiteral("runtime_verification"));
        }
        return;
    }
    if (purpose == VerificationPurpose::Start) {
        const auto* requestedModel = activeRequest_ ? modelForId(activeRequest_->modelId) : nullptr;
        if (!activeRequest_.has_value()
            || requestedModel == nullptr || !modelInstalled(*requestedModel)
            || !runtimeReady()
            || !launchSeparation(*activeRequest_)) {
            const ActiveRequestContext context = activeRequest_.value_or(
                ActiveRequestContext{RequestKind::Separation});
            failRequest(context,
                        tr("所选模型或 ONNX Runtime 尚未完成校验安装"),
                        QStringLiteral("model_verification"));
        }
    }
}

bool VocalSeparationController::launchProbe()
{
    const auto* model = modelForId(activeRequest_.has_value()
        ? activeRequest_->modelId : selectedModelId_);
#ifdef Q_OS_MACOS
    const bool external = model && model->id == QStringLiteral("python-vr-5hp") && externalRuntime_->ready();
    if (!process_.setWorker(external ? externalRuntime_->python() : options_.workerProgram,
            external ? QStringList{externalRuntime_->workerScript()} : options_.workerArguments)) return false;
#else
    if (!process_.setWorker(options_.workerProgram, options_.workerArguments)) return false;
#endif
    activeProbeFingerprint_ = probeFingerprint(model ? model->id : QString());
    QJsonObject payload{{QStringLiteral("runtimePath"), cudaRuntime_->ready() ? cudaRuntime_->libraryPath() : options_.runtimeLibraryPath}};
    if (model) {
        payload.insert("modelId", model->id);
        payload.insert("family", model->id == QStringLiteral("python-vr-5hp") ? "vr" : model->family == VocalModelFamily::Demucs ? "demucs" : "mdx");
        if (modelInstalled(*model)) payload.insert("modelFiles", QJsonArray::fromStringList(modelFilePaths(*model)));
    }
    if (!process_.startProbe(payload))
        return false;
    setJobState(JobState::Probing, QStringLiteral("provider_probe"));
    return true;
}

bool VocalSeparationController::launchSeparation(
    const ActiveRequestContext& context)
{
    const VocalModelCard* model = modelForId(context.modelId);
    if (model == nullptr) return false;
    const bool external = context.modelId == QStringLiteral("python-vr-5hp");
    if (!process_.setWorker(external ? externalRuntime_->python() : options_.workerProgram,
            external ? QStringList{externalRuntime_->workerScript()} : options_.workerArguments)) return false;
    QJsonArray modelFiles;
    for (const QString& path : modelFilePaths(*model))
        modelFiles.push_back(path);
    QJsonArray requestedStems;
    for (const QString& name : context.stemNames) requestedStems.push_back(name);
    QJsonArray stemLabels;
    for (const QString& label : context.stemLabels) stemLabels.push_back(label);
    QJsonObject payload{
        {QStringLiteral("runtimePath"), context.device != DeviceMode::CPU && cudaRuntime_->ready()
            ? cudaRuntime_->libraryPath() : options_.runtimeLibraryPath},
        {QStringLiteral("inputPath"), context.inputPath},
        {QStringLiteral("modelFiles"), modelFiles},
        {QStringLiteral("outputDirectory"), context.outputRoot},
        {QStringLiteral("baseName"),
         QFileInfo(context.inputPath).completeBaseName()},
        {QStringLiteral("directoryName"),
         QFileInfo(context.inputPath).completeBaseName()
             + QLatin1Char('-') + context.modelId},
        {QStringLiteral("modelName"), model->displayName},
        {QStringLiteral("extension"), context.outputFormat},
        {QStringLiteral("stems"), requestedStems},
        {QStringLiteral("stemLabels"), stemLabels},
        // Automatically highlighted GPU still uses the safe CPU fallback path.
        {QStringLiteral("device"), deviceName(!deviceChosenByUser_
             && context.device == DeviceMode::GPU ? DeviceMode::Auto : context.device)},
    };
    const auto custom = customModelBindings_.constFind(context.modelId);
    if (custom != customModelBindings_.cend()) {
        QJsonArray hashes;
        for (const QString& hash : custom->sha256) hashes.push_back(hash);
        QJsonArray bytes;
        for (const qint64 size : custom->bytes) bytes.push_back(size);
        QJsonArray roles;
        for (const QString& role : custom->roles) roles.push_back(role);
        payload.insert(QStringLiteral("modelProfile"), custom->profileId);
        payload.insert(QStringLiteral("modelSha256"), hashes);
        payload.insert(QStringLiteral("modelBytes"), bytes);
        payload.insert(QStringLiteral("modelRoles"), roles);
    }
    if (!process_.startJob(payload)) return false;
    setJobState(JobState::Running, QStringLiteral("starting"));
    return true;
}

void VocalSeparationController::refreshModels()
{
    models_.clear();
    for (const VocalModelCard& model : options_.catalog) {
        const bool customModel = std::none_of(
            baseCatalog_.cbegin(), baseCatalog_.cend(),
            [&model](const VocalModelCard& builtIn) {
                return builtIn.id == model.id;
            });
        ModelState state = modelInstalled(model) ? ModelState::Installed
            : modelFilesPresent(model)
                && !verifiedOrRejectedModelIds_.contains(model.id)
                ? ModelState::PendingVerification : ModelState::NotInstalled;
        if (verificationWatcher_ != nullptr
            && verificationPurpose_ != VerificationPurpose::None
            && (verifyingModelId_.isEmpty()
                || verifyingModelId_ == model.id)) {
            state = ModelState::Verifying;
        }
        if (model.id == downloadingModelId_) {
            switch (downloader_ ? downloader_->state() : VocalDownloadState::Idle) {
            case VocalDownloadState::Downloading: state = ModelState::Downloading; break;
            case VocalDownloadState::Paused: state = ModelState::Paused; break;
            case VocalDownloadState::Verifying: state = ModelState::Verifying; break;
            case VocalDownloadState::Failed: state = ModelState::ModelFailed; break;
            default: break;
            }
        }
        if (model.id == failedDownloadModelId_)
            state = ModelState::ModelFailed;
        const auto configuration = modelConfigurations_.value(model.id);
        if (configuration) {
            if (configuration->state == "checking") state = ModelState::Verifying;
            else if (configuration->state == "downloading") state = ModelState::Downloading;
            else if (configuration->state == "paused") state = ModelState::Paused;
            else if (configuration->state == "failed") state = ModelState::ModelFailed;
        }
        if (model.id == QStringLiteral("python-vr-5hp")) {
            state = modelInstalled(model) ? ModelState::Installed : ModelState::ModelFailed;
            if (externalRuntime_->busy()) state = externalRuntime_->paused() ? ModelState::Paused : ModelState::Downloading;
        }
        QVariantList kinds;
        if (model.id == downloadingModelId_ && cudaRuntime_->busy())
            state = cudaRuntime_->paused() ? ModelState::Paused : ModelState::Downloading;
        for (const QString& name : model.stems) kinds.push_back(int(stemKind(name)));
        qint64 totalBytes = 0;
        const QStringList modelPaths = modelFilePaths(model);
        QStringList modelHashes;
        for (const VocalDownloadFile& file : model.files) {
            totalBytes += file.bytes;
            modelHashes.push_back(file.sha256.toLower());
        }
        bool mirrorAvailable = false;
        for (const VocalDownloadFile& file : model.files) {
            if (vocalDomesticMirrorUrl(file.url).isValid()) {
                mirrorAvailable = true;
                break;
            }
        }
        models_.push_back(QVariantMap{
#ifdef Q_OS_MACOS
            {QStringLiteral("gpuRuntimeReady"), model.id == QStringLiteral("python-vr-5hp") ? externalRuntime_->ready() : runtimeReady()},
            {QStringLiteral("gpuRuntimeConfigurable"), false},
            {QStringLiteral("gpuConfigurationEnabled"), false},
            {QStringLiteral("gpuProvider"), validatedGpuProviders_.value(model.id,
                 model.id == QStringLiteral("python-vr-5hp") ? QStringLiteral("mps") : QStringLiteral("coreml"))},
            {QStringLiteral("gpuCompatibility"), validatedGpuProviders_.contains(model.id) ? QStringLiteral("validated") : QStringLiteral("unverified")},
            {QStringLiteral("gpuReason"), deviceProbeCache_.value(model.id).value(QStringLiteral("gpuReason")).toString().isEmpty()
                ? tr("Apple Silicon 将使用当前模型验证 CoreML / MPS；Intel Mac 使用 CPU。尚未验证不表示模型不兼容")
                : deviceProbeCache_.value(model.id).value(QStringLiteral("gpuReason")).toString()},
#else
            {QStringLiteral("gpuRuntimeReady"), cudaRuntime_->ready()},
            {QStringLiteral("gpuHardwareName"), cudaRuntime_->hardwareName()},
            {QStringLiteral("gpuDriverVersion"), cudaRuntime_->driverVersion()},
            {QStringLiteral("gpuRuntimeConfigurable"), cudaRuntime_->nvidiaAvailable() && model.id != QStringLiteral("python-vr-5hp")},
            {QStringLiteral("gpuConfigurationEnabled"), cudaRuntime_->nvidiaAvailable()
                && !cudaRuntime_->ready() && !cudaRuntime_->busy()
                && model.id != QStringLiteral("python-vr-5hp")
                && (!activeRequest_ || activeRequest_->kind != RequestKind::Separation
                    || activeRequest_->modelId == QStringLiteral("python-vr-5hp"))},
            {QStringLiteral("gpuProvider"), validatedGpuProviders_.value(model.id,
                cudaRuntime_->ready() ? QStringLiteral("cuda") : model.family == VocalModelFamily::Mdx ? QStringLiteral("directml") : QString())},
            {QStringLiteral("gpuCompatibility"), model.id == QStringLiteral("python-vr-5hp") ? QStringLiteral("unsupported")
                : validatedGpuProviders_.contains(model.id) ? QStringLiteral("validated")
                : !cudaRuntime_->ready() && model.family == VocalModelFamily::Demucs ? QStringLiteral("missing-runtime") : QStringLiteral("candidate")},
            {QStringLiteral("gpuReason"), model.id == QStringLiteral("python-vr-5hp") ? tr("此 VR 适配器仅支持 CPU")
                : cudaRuntime_->checking() || !cudaRuntime_->nvidiaAvailable() ? cudaRuntime_->hardwareSummary()
                : !cudaRuntime_->ready() ? cudaRuntime_->hardwareSummary()
                    + (model.family == VocalModelFamily::Mdx
                        ? tr("；此模型可检测 DirectML，无需 CUDA。可选 CUDA 配置约 1.51 GB 下载、5 GB 可用磁盘")
                        : tr("；标准五轨缺少应用专用 CUDA 组件（不是缺显卡驱动）。一键配置约 1.51 GB 下载、5 GB 可用磁盘，安装后验证当前模型"))
                : validatedGpuProviders_.contains(model.id) ? tr("当前模型已通过真实 GPU 推理验证") : tr("CUDA 组件已校验，等待当前模型 GPU 推理验证")},
#endif
            {QStringLiteral("id"), model.id},
            {QStringLiteral("configurationEnabled"), canConfigureModel(model.id)},
            {QStringLiteral("family"), model.family == VocalModelFamily::Mdx
                 ? QStringLiteral("mdx") : QStringLiteral("demucs")},
            {QStringLiteral("stems"), kinds},
            {QStringLiteral("state"), int(state)},
            {QStringLiteral("bytes"), totalBytes},
            {QStringLiteral("provenance"), model.provenance},
            {QStringLiteral("resourceGuidance"), model.resourceGuidance},
            {QStringLiteral("name"), model.displayName.isEmpty() ? model.id : model.displayName},
            {QStringLiteral("useCase"), model.useCase},
            {QStringLiteral("description"), model.useCase},
            {QStringLiteral("tierLabel"), model.tierLabel},
            {QStringLiteral("badgeLabel"), model.badgeLabel},
            {QStringLiteral("provider"), model.provider},
            {QStringLiteral("repositoryUrl"), model.repositoryUrl},
            {QStringLiteral("domesticMirrorAvailable"), mirrorAvailable},
            {QStringLiteral("origin"), customModel
                 ? QStringLiteral("custom") : QStringLiteral("built-in")},
            {QStringLiteral("compatibility"),
             QStringLiteral("trusted-worker-profile")},
            {QStringLiteral("profile"), customModelBindings_.contains(model.id)
                 ? customModelBindings_.value(model.id).profileId
                 : (model.family == VocalModelFamily::Mdx
                        ? QStringLiteral("mdx:") : QStringLiteral("demucs:"))
                       + model.stems.join(QLatin1Char(','))},
            {QStringLiteral("paths"), modelPaths},
            {QStringLiteral("modelPath"), modelPaths.isEmpty()
                 ? QString() : modelPaths.constFirst()},
            {QStringLiteral("stemCount"), model.stems.size()},
            {QStringLiteral("backend"), model.id == QStringLiteral("python-vr-5hp")
                 ? QStringLiteral("external-python") : QStringLiteral("onnxruntime-native")},
            {QStringLiteral("available"),
             state == ModelState::Installed && (model.id == QStringLiteral("python-vr-5hp")
                ? externalRuntime_->ready() : runtimeReady())},
            {QStringLiteral("failureReason"),
             model.id == QStringLiteral("python-vr-5hp") && !externalRuntime_->ready()
                 ? tr("本地模型已找到，请一键配置外置 Python VR 环境")
                 : model.id != QStringLiteral("python-vr-5hp") && state == ModelState::Installed && !runtimeReady()
                 ? tr("ONNX Runtime 尚未安装或未通过校验")
                 : state == ModelState::ModelFailed
                     ? tr("模型校验失败") : QString()},
            {QStringLiteral("hashes"), modelHashes},
            {QStringLiteral("rejectionReason"), QString()},
        });
    }
    for (const QVariant& rejected : std::as_const(rejectedCustomModels_)) {
        QVariantMap diagnostic = rejected.toMap();
        diagnostic.insert(QStringLiteral("configurationEnabled"),
            canConfigureModel(diagnostic.value(QStringLiteral("id")).toString()));
        models_.push_back(diagnostic);
    }
    for (QVariant& value : models_) {
        auto card = value.toMap();
#ifdef Q_OS_MACOS
        const auto probe = deviceProbeCache_.value(card.value(QStringLiteral("id")).toString());
        if (probe.value(QStringLiteral("cpuValidated")).toBool()
            && !probe.value(QStringLiteral("cpu")).toBool()
            && !probe.value(QStringLiteral("gpu")).toBool()) {
            card.insert(QStringLiteral("executionError"), probe.value(QStringLiteral("cpuReason")).toString());
            card.insert(QStringLiteral("available"), false);
        }
#endif
        const auto fields = configurationFields(card.value("id").toString());
        for (auto it = fields.cbegin(); it != fields.cend(); ++it) card.insert(it.key(), it.value());
        value = card;
    }
    emit modelsChanged();
    emit startEligibilityChanged();
}

void VocalSeparationController::rebuildStems()
{
    const QVariantList previous = stems_;
    stems_.clear();
    const VocalModelCard* model = selectedModel();
    const QList<StemKind> fixedKinds{
        StemKind::Vocals, StemKind::Accompaniment, StemKind::Drums,
        StemKind::Bass, StemKind::Other};
    for (const StemKind kind : fixedKinds) {
        const bool supported = model != nullptr
            && model->stems.contains(stemName(kind));
        stems_.push_back(QVariantMap{
            {QStringLiteral("kind"), int(kind)},
            {QStringLiteral("name"), stemName(kind)},
            {QStringLiteral("supported"), supported},
            {QStringLiteral("selected"), supported
                 && (kind == StemKind::Vocals || kind == StemKind::Accompaniment)},
            {QStringLiteral("derived"), supported
                 && kind == StemKind::Accompaniment
                 && model->family == VocalModelFamily::Demucs},
            {QStringLiteral("available"), false},
            {QStringLiteral("path"), QString()},
            {QStringLiteral("waveform"), QVariantList{}},
            {QStringLiteral("previewVolume"),
             stemPreviewVolumes_.value(int(kind), 0.8)},
        });
        for (const QVariant& oldValue : previous) {
            const QVariantMap old = oldValue.toMap();
            if (old.value(QStringLiteral("kind")).toInt() != int(kind)
                || !old.value(QStringLiteral("available")).toBool()) continue;
            QVariantMap current = stems_.last().toMap();
            for (const auto* key : {"available", "path", "waveform", "selected", "previewVolume", "derived"})
                current.insert(QString::fromLatin1(key), old.value(QString::fromLatin1(key)));
            stems_.last() = current;
        }
    }
    emit stemsChanged();
    emit startEligibilityChanged();
}

void VocalSeparationController::setJobState(JobState state, const QString& stage)
{
    const bool changed = jobState_ != state || stage_ != stage;
    jobState_ = state;
    stage_ = stage;
    if (changed) {
        emit jobStateChanged();
        refreshModels();
    }
    emit startEligibilityChanged();
    if (deviceProbePending_ && state != JobState::Running
        && state != JobState::Probing && state != JobState::Cancelling) {
        QTimer::singleShot(0, this, [this] {
            if (deviceProbePending_ && !requestInFlight()) probeDevices();
        });
    }
}

void VocalSeparationController::setError(const QString& error)
{
    if (error_ == error) return;
    error_ = error;
    emit errorChanged();
}

void VocalSeparationController::handleDownloadFailure(
    const VocalInstallResult& result, const bool startRetry)
{
    if (!downloadQueue_.isEmpty()
        && !downloadQueue_.first().mirrorAttempted
        && downloadQueue_.first().mirrorUrl.isValid()) {
        DownloadItem& retry = downloadQueue_.first();
        retry.file.url = retry.mirrorUrl;
        retry.mirrorAttempted = true;
        downloadSource_ = tr("国内镜像");
        if (runtimeOnlyDownload_) runtimeDetails_["directml"] = downloadSource_;
        emit downloadStateChanged();
        setError(tr("官方线路失败，已自动切换国内镜像"));
        if (startRetry) startNextDownload();
        return;
    }
    finishExhaustedDownload(downloadSource_, result.error);
}

void VocalSeparationController::finishExhaustedDownload(
    const QString& source, const QString& diagnostic)
{
    QString visibleDiagnostic = diagnostic;
    if (diagnostic == QStringLiteral("Unsafe partial download path")) {
        visibleDiagnostic = tr("临时下载路径不安全，已停止下载。请更换模型目录后重试");
    } else if (diagnostic == QStringLiteral("Download exceeded expected size")) {
        visibleDiagnostic = tr("下载内容超过清单声明大小，已停止下载");
    }
    if (runtimeOnlyDownload_ || source == "runtime")
        runtimeErrors_["directml"] = visibleDiagnostic;
    const QString modelId = downloadingModelId_;
    downloadQueue_.clear();
    runtimeOnlyDownload_ = false;
    failedDownloadModelId_ = modelId;
    downloadingModelId_.clear();
    downloadProgress_ = 0.0;
    completedDownloadBytes_ = 0;
    totalDownloadBytes_ = 0;
    emit downloadProgressChanged();
    emit downloadStateChanged();
    setError(visibleDiagnostic);
    configurationChanged();
    emit downloadSourcesExhausted(QVariantMap{
        {QStringLiteral("modelId"), modelId},
        {QStringLiteral("source"), source},
        {QStringLiteral("diagnostic"), visibleDiagnostic},
    });
}

void VocalSeparationController::startNextDownload()
{
    if (downloadQueue_.isEmpty()) {
        if (!runtimeOnlyDownload_ && !downloadingModelId_.isEmpty()) {
            verifiedModelIds_.insert(downloadingModelId_);
            verifiedOrRejectedModelIds_.insert(downloadingModelId_);
            if (const VocalModelCard* model = modelForId(downloadingModelId_)) {
                modelVerificationFingerprints_.insert(
                    downloadingModelId_, modelVerificationFingerprint(
                        *model, modelStorageDirectory_, indexedModelFiles_));
            }
        }
        runtimeOnlyDownload_ = false;
        downloadingModelId_.clear();
        downloadProgress_ = 1.0;
        runtimeProgress_["directml"] = 1.0;
        completedDownloadBytes_ = totalDownloadBytes_;
        emit downloadProgressChanged();
        emit downloadStateChanged();
        rebuildModelDirectoryWatcher();
        configurationChanged();
        if (modelDirectoryRescanPending_) {
            modelDirectoryRescanPending_ = false;
            scheduleModelDirectoryScan();
        }
        return;
    }
    const DownloadItem& item = downloadQueue_.first();
    const QString route = item.mirrorAttempted ? tr("国内镜像")
                                                : tr("官方线路");
    if (runtimeOnlyDownload_) runtimeDetails_["directml"] = route;
    if (downloadSource_ != route) {
        downloadSource_ = route;
        emit downloadStateChanged();
    }
    if (!QDir().mkpath(QFileInfo(item.destination).absolutePath())) {
        downloadQueue_.clear();
        failedDownloadModelId_ = downloadingModelId_;
        downloadingModelId_.clear();
        downloadProgress_ = 0.0;
        completedDownloadBytes_ = 0;
        totalDownloadBytes_ = 0;
        emit downloadProgressChanged();
        emit downloadStateChanged();
        setError(tr("无法创建模型下载目录"));
        refreshModels();
        return;
    }
    rebuildModelDirectoryWatcher();
    downloader_->start(item.file, item.destination);
}

void VocalSeparationController::rebuildModelDirectoryWatcher()
{
    const QStringList watched = modelDirectoryWatcher_.directories();
    if (!watched.isEmpty()) modelDirectoryWatcher_.removePaths(watched);
    QStringList directories;
    if (QFileInfo(modelStorageDirectory_).isDir())
        directories.push_back(modelStorageDirectory_);
    for (const VocalModelCard& model : std::as_const(options_.catalog)) {
        const QString directory = modelDirectory(model.id);
        if (QFileInfo(directory).isDir()) directories.push_back(directory);
    }
    directories.removeDuplicates();
    if (!directories.isEmpty()) modelDirectoryWatcher_.addPaths(directories);
}

void VocalSeparationController::scheduleModelDirectoryScan()
{
    modelDirectoryScanTimer_.start();
}

void VocalSeparationController::discoverCustomModels(
    const QStringList& manifestPaths)
{
    options_.catalog = baseCatalog_;
    rejectedCustomModels_.clear();
    customModelBindings_.clear();

    const QDir root(modelStorageDirectory_);
    if (!root.exists()) return;
    QSet<QString> knownIds;
    for (const VocalModelCard& model : std::as_const(baseCatalog_))
        knownIds.insert(model.id);
    const auto hashesFor = [](const QList<VocalDownloadFile>& files) {
        QStringList hashes;
        for (const VocalDownloadFile& file : files)
            hashes.push_back(file.sha256.toLower());
        hashes.sort();
        return hashes;
    };
    const auto reject = [this](const QString& id, const QString& reason) {
        rejectedCustomModels_.push_back(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("family"), QStringLiteral("custom")},
            {QStringLiteral("stems"), QVariantList{}},
            {QStringLiteral("state"), int(ModelState::ModelFailed)},
            {QStringLiteral("bytes"), 0},
            {QStringLiteral("provenance"), tr("本地 sidecar 清单")},
            {QStringLiteral("resourceGuidance"), reason},
            {QStringLiteral("name"), id},
            {QStringLiteral("useCase"), reason},
            {QStringLiteral("description"), reason},
            {QStringLiteral("tierLabel"), tr("自定义模型")},
            {QStringLiteral("badgeLabel"), tr("未通过")},
            {QStringLiteral("provider"), tr("本地文件")},
            {QStringLiteral("repositoryUrl"), QString()},
            {QStringLiteral("domesticMirrorAvailable"), false},
            {QStringLiteral("origin"), QStringLiteral("custom")},
            {QStringLiteral("compatibility"), QStringLiteral("rejected")},
            {QStringLiteral("profile"), QString()},
            {QStringLiteral("paths"), QStringList{}},
            {QStringLiteral("modelPath"), QString()},
            {QStringLiteral("stemCount"), 0},
            {QStringLiteral("backend"), QStringLiteral("unknown")},
            {QStringLiteral("available"), false},
            {QStringLiteral("failureReason"), reason},
            {QStringLiteral("hashes"), QStringList{}},
            {QStringLiteral("rejectionReason"), reason},
        });
    };
    QFileInfoList manifests;
    manifests.reserve(manifestPaths.size());
    for (const QString& path : manifestPaths) manifests.push_back(QFileInfo(path));
    std::sort(manifests.begin(), manifests.end(),
              [](const QFileInfo& left, const QFileInfo& right) {
                  return left.absoluteFilePath().compare(
                             right.absoluteFilePath(), Qt::CaseInsensitive) < 0;
              });
    for (const QFileInfo& info : manifests) {
        QFile file(info.absoluteFilePath());
        if (!safeExistingFileWithin(info.absoluteFilePath(), modelStorageDirectory_)
            || info.size() <= 0 || info.size() > 256 * 1024
            || !file.open(QIODevice::ReadOnly)) {
            reject(info.completeBaseName(), tr("模型清单不是安全的普通文件"));
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(
            file.readAll(), &parseError);
        const QJsonObject manifest = document.object();
        const QString id = manifest.value(QStringLiteral("id"))
                               .toString(info.completeBaseName());
        const CustomManifestValidationResult valid =
            validateCustomModelManifest(manifest);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()
            || !valid.accepted) {
            reject(id, valid.error.isEmpty() ? tr("模型清单 JSON 无效")
                                             : valid.error);
            continue;
        }
        if (knownIds.contains(id)) {
            reject(id, tr("模型标识与现有模型重复"));
            continue;
        }
        const VocalModelFamily family =
            manifest.value(QStringLiteral("family")).toString()
                    == QStringLiteral("Demucs")
            ? VocalModelFamily::Demucs : VocalModelFamily::Mdx;
        QStringList stems;
        for (const QJsonValue& stem :
             manifest.value(QStringLiteral("stems")).toArray())
            stems.push_back(stem.toString());
        QList<VocalDownloadFile> modelFiles;
        QStringList roles;
        bool safeFiles = true;
        for (const QJsonValue& value :
             manifest.value(QStringLiteral("files")).toArray()) {
            const QJsonObject entry = value.toObject();
            const QString name = entry.value(QStringLiteral("name")).toString();
            const QString manifestFilePath = info.dir().filePath(name);
            if (!safeExistingFileWithin(manifestFilePath,
                                        modelStorageDirectory_)) {
                safeFiles = false;
                break;
            }
            modelFiles.push_back({name, {},
                entry.value(QStringLiteral("bytes")).toInteger(),
                entry.value(QStringLiteral("sha256")).toString().toLower()});
            roles.push_back(entry.value(QStringLiteral("role")).toString());
        }
        const QString declaredProfile = manifest.value(QStringLiteral("profile"))
                                            .toString();
        const QHash<QString, QPair<VocalModelFamily, qsizetype>> knownProfiles{
            {QStringLiteral("uvr-mdxnet-kara"),
             {VocalModelFamily::Mdx, 1}},
            {QStringLiteral("uvr-mdx-net-inst-hq3"),
             {VocalModelFamily::Mdx, 1}},
            {QStringLiteral("htdemucs-ft-fp16"),
             {VocalModelFamily::Demucs, 4}},
        };
        const auto exactBase = std::find_if(
            baseCatalog_.cbegin(), baseCatalog_.cend(),
            [&](const VocalModelCard& candidate) {
                return candidate.family == family
                    && hashesFor(candidate.files) == hashesFor(modelFiles);
            });
        QString profileId = declaredProfile;
        qsizetype profileFileCount = 0;
        if (declaredProfile.isEmpty() && exactBase != baseCatalog_.cend()) {
            profileId = exactBase->id;
            profileFileCount = exactBase->files.size();
        } else if (knownProfiles.contains(declaredProfile)
                   && knownProfiles.value(declaredProfile).first == family) {
            profileFileCount = knownProfiles.value(declaredProfile).second;
        }
        const QStringList expectedRoles{
            QStringLiteral("drums"), QStringLiteral("bass"),
            QStringLiteral("other"), QStringLiteral("vocals")};
        QStringList sortedRoles = roles;
        sortedRoles.removeAll(QString());
        sortedRoles.sort();
        QStringList sortedExpectedRoles = expectedRoles;
        sortedExpectedRoles.sort();
        const bool validRoles = family == VocalModelFamily::Mdx
            ? sortedRoles.isEmpty() : sortedRoles == sortedExpectedRoles;
        if (!safeFiles || profileFileCount <= 0
            || modelFiles.size() != profileFileCount || !validRoles) {
            reject(id, safeFiles
                ? tr("模型执行配置或音轨角色不受支持，已拒绝执行")
                : tr("模型文件缺失、越界或是链接文件"));
            continue;
        }
        if (knownProfiles.contains(profileId)) {
            CustomModelBinding binding;
            binding.profileId = profileId;
            binding.roles = family == VocalModelFamily::Demucs ? roles : QStringList{};
            for (const VocalDownloadFile& modelFile : modelFiles) {
                binding.sha256.push_back(modelFile.sha256.toLower());
                binding.bytes.push_back(modelFile.bytes);
            }
            customModelBindings_.insert(id, binding);
        }
        options_.catalog.push_back(VocalModelCard{
            id, family, modelFiles, stems,
            tr("本地 sidecar 清单（受信指纹）"),
            tr("启动前仍会由 Worker 校验张量与 opset"),
            id, tr("用户提供的兼容 ONNX 模型"), tr("自定义模型"),
            tr("受信指纹"), tr("本地文件"), QString()});
        knownIds.insert(id);
    }

    // Raw model files are intentionally discoverable at any nesting depth so
    // users can diagnose what AgPlayer found without a WebEngine or bundled
    // Python runtime.  They remain non-executable until a trusted worker
    // profile (sidecar manifest) and the corresponding backend are verified.
    QSet<QString> catalogFileNames;
    for (const VocalModelCard& model : std::as_const(options_.catalog)) {
        for (const VocalDownloadFile& file : model.files)
            catalogFileNames.insert(file.fileName.toLower());
    }
    QSet<QString> rawPaths;
    for (auto it = indexedModelFiles_.cbegin();
         it != indexedModelFiles_.cend(); ++it) {
        if (catalogFileNames.contains(it.key()))
            continue;
        for (const QString& path : it.value()) {
            const QFileInfo raw(path);
            const QString suffix = raw.suffix().toLower();
            if (!QStringList{QStringLiteral("onnx"), QStringLiteral("pth"),
                             QStringLiteral("th")}.contains(suffix)
                || rawPaths.contains(raw.absoluteFilePath())
                || !safeExistingFileWithin(raw.absoluteFilePath(),
                                           modelStorageDirectory_)) {
                continue;
            }
            rawPaths.insert(raw.absoluteFilePath());
            const QString lowered = raw.fileName().toLower();
            if (lowered == QStringLiteral("5_hp-karaoke-uvr.pth") && raw.size() == 126782699) {
                if (!knownIds.contains(QStringLiteral("python-vr-5hp"))) {
                    options_.catalog.push_back(VocalModelCard{
                        "python-vr-5hp", VocalModelFamily::Mdx,
                        {{raw.fileName(), {}, 126782699, "fe00891defbb61f4261500af22f7624f1a3df8dc75fa3998d1aece02e6be4537"}},
                        {"vocals", "instrumental"}, tr("UVR VR 架构 / 完整 SHA-256 校验"),
                        tr("一键下载独立 Python 3.11、CPU PyTorch、audio-separator 和 FFmpeg。约 450 MB 下载 / 1.5 GB 磁盘；不修改系统 Python。当前适配 5_HP-Karaoke-UVR.pth，其他 .pth/.th/.ckpt 架构不会冒充兼容。"),
                        raw.completeBaseName(), tr("人声 / 伴奏；外置 Python VR 推理"), tr("自定义模型"),
                        tr("Python VR"), "UVR / audio-separator", "https://github.com/nomadkaraoke/python-audio-separator"});
                    knownIds.insert(QStringLiteral("python-vr-5hp"));
                }
                continue;
            }
            struct LocalMdxContract {
                const char* fileName;
                const char* id;
                qint64 bytes;
                const char* sha256;
            };
            // Additional local UVR profiles: detected cheaply by file name and
            // size, then SHA-256 verified asynchronously before execution.
            static const LocalMdxContract localContracts[]{
                {"kim_vocal_2.onnx", "kim-vocal-2", 66'759'214,
                 "ce74ef3b6a6024ce44211a07be9cf8bc6d87728cc852a68ab34eb8e58cde9c8b"},
                {"uvr-mdx-net-inst_hq_1.onnx", "uvr-mdx-net-inst-hq1", 66'759'214,
                 "38a045c4ded87e3bf97b609ec5be7910e8a7cecec455f507227ab12b5e29f7f9"},
                {"uvr-mdx-net-voc_ft.onnx", "uvr-mdx-net-voc-ft", 66'762'490,
                 "534b2070fcc7df514b13ef660dc8cbb328679c2374d04354a5c42bb14ecce111"},
            };
            const auto contract = std::find_if(std::begin(localContracts),
                std::end(localContracts), [&](const LocalMdxContract& entry) {
                    return lowered == QLatin1String(entry.fileName)
                        && raw.size() == entry.bytes;
                });
            if (contract != std::end(localContracts)) {
                const QString localId = QString::fromLatin1(contract->id);
                if (!knownIds.contains(localId)) {
                    options_.catalog.push_back(VocalModelCard{
                        localId, VocalModelFamily::Mdx,
                        {{raw.fileName(), {}, contract->bytes,
                          QString::fromLatin1(contract->sha256)}},
                        {QStringLiteral("vocals"), QStringLiteral("instrumental")},
                        tr("UVR 官方频谱参数与完整 SHA-256 指纹"),
                        tr("已内置适配参数；校验本地模型后使用 ONNX Runtime 执行，无需重新下载模型"),
                        raw.completeBaseName(), tr("本地人声 / 伴奏分离"),
                        tr("自定义模型"), tr("自动适配"), QStringLiteral("UVR / TRvlvr"),
                        QStringLiteral("https://github.com/TRvlvr/application_data")});
                    knownIds.insert(localId);
                }
                continue;
            }
            const bool demucs = suffix == QStringLiteral("th")
                || lowered.contains(QStringLiteral("demucs"));
            const bool vr = suffix == QStringLiteral("pth");
            const QString family = demucs ? QStringLiteral("demucs")
                : vr ? QStringLiteral("vr") : QStringLiteral("mdx");
            const QString backend = suffix == QStringLiteral("onnx")
                ? QStringLiteral("onnxruntime-native")
                : QStringLiteral("external-python");
            const QString reason = suffix == QStringLiteral("onnx")
                ? tr("已识别 ONNX 文件；需要兼容的 sidecar 配置并通过张量与运行时探测后才能执行")
                : tr("已识别模型文件，但当前没有适配此架构的推理模块；仅安装 Python 环境不能使其运行。当前 Python 模式支持 5_HP-Karaoke-UVR.pth");
            const QString id = QStringLiteral("local-%1").arg(
                QString::fromLatin1(QCryptographicHash::hash(
                    raw.absoluteFilePath().toUtf8(),
                    QCryptographicHash::Sha256).toHex().left(12)));
            rejectedCustomModels_.push_back(QVariantMap{
                {QStringLiteral("id"), id},
                {QStringLiteral("family"), family},
                {QStringLiteral("stems"), QVariantList{}},
                {QStringLiteral("state"), int(ModelState::ModelFailed)},
                {QStringLiteral("bytes"), raw.size()},
                {QStringLiteral("provenance"), tr("用户模型目录递归扫描")},
                {QStringLiteral("resourceGuidance"), reason},
                {QStringLiteral("name"), raw.completeBaseName()},
                {QStringLiteral("useCase"), reason},
                {QStringLiteral("description"), reason},
                {QStringLiteral("tierLabel"), tr("自定义模型")},
                {QStringLiteral("badgeLabel"), tr("待配置")},
                {QStringLiteral("provider"), vr ? QStringLiteral("UVR")
                    : demucs ? QStringLiteral("Demucs")
                             : QStringLiteral("本地 ONNX")},
                {QStringLiteral("repositoryUrl"), QString()},
                {QStringLiteral("domesticMirrorAvailable"), false},
                {QStringLiteral("origin"), QStringLiteral("custom")},
                {QStringLiteral("compatibility"), QStringLiteral("diagnostic")},
                {QStringLiteral("profile"), QString()},
                {QStringLiteral("paths"), QStringList{raw.absoluteFilePath()}},
                {QStringLiteral("modelPath"), raw.absoluteFilePath()},
                {QStringLiteral("stemCount"), 0},
                {QStringLiteral("backend"), backend},
                {QStringLiteral("available"), false},
                {QStringLiteral("failureReason"), reason},
                {QStringLiteral("hashes"), QStringList{}},
                {QStringLiteral("rejectionReason"), reason},
            });
        }
    }
}

void VocalSeparationController::scanModelDirectory()
{
    const VocalDownloadState downloadState = downloader_
        ? downloader_->state() : VocalDownloadState::Idle;
    const bool busy = requestInFlight() || downloadBusy() || verificationWatcher_ != nullptr
        || modelDirectoryIndexWatcher_ != nullptr
        || runtimeInstallerWatcher_ != nullptr || !downloadQueue_.isEmpty()
        || downloadState == VocalDownloadState::Downloading
        || downloadState == VocalDownloadState::Paused
        || downloadState == VocalDownloadState::Verifying;
    if (busy) {
        modelDirectoryRescanPending_ = true;
        return;
    }
    const bool verifyAllModels = std::exchange(
        verifyAllModelsOnNextScan_, false);
    const QString scannedRoot = modelStorageDirectory_;
    auto* const watcher = new QFutureWatcher<ModelDirectoryIndex>(this);
    modelDirectoryIndexWatcher_ = watcher;
    connect(watcher, &QFutureWatcher<ModelDirectoryIndex>::finished, this,
            [this, watcher, scannedRoot, verifyAllModels] {
        const ModelDirectoryIndex result = watcher->result();
        watcher->deleteLater();
        if (modelDirectoryIndexWatcher_ != watcher) return;
        modelDirectoryIndexWatcher_ = nullptr;
        if (scannedRoot != modelStorageDirectory_) {
            if (verifyAllModels) verifyAllModelsOnNextScan_ = true;
            scheduleModelDirectoryScan();
            return;
        }
        indexedModelFiles_ = result.filesByName;
        const QStringList watched = modelDirectoryWatcher_.directories();
        if (!watched.isEmpty()) modelDirectoryWatcher_.removePaths(watched);
        if (!result.directories.isEmpty())
            modelDirectoryWatcher_.addPaths(result.directories);
        discoverCustomModels(result.manifests);
        QSet<QString> catalogIds;
        QSet<QString> changedModelIds;
        for (const VocalModelCard& model : std::as_const(options_.catalog)) {
            catalogIds.insert(model.id);
            const QString fingerprint = modelVerificationFingerprint(
                model, modelStorageDirectory_, indexedModelFiles_);
            if (modelVerificationFingerprints_.value(model.id)
                == fingerprint
                && verifiedOrRejectedModelIds_.contains(model.id)) {
                continue;
            }
            modelVerificationFingerprints_.remove(model.id);
            verifiedModelIds_.remove(model.id);
            verifiedOrRejectedModelIds_.remove(model.id);
            validatedGpuProviders_.remove(model.id);
            if (modelFilesPresent(model))
                changedModelIds.insert(model.id);
        }
        for (auto it = modelVerificationFingerprints_.begin();
             it != modelVerificationFingerprints_.end();) {
            if (!catalogIds.contains(it.key())) {
                verifiedModelIds_.remove(it.key());
                verifiedOrRejectedModelIds_.remove(it.key());
                validatedGpuProviders_.remove(it.key());
                it = modelVerificationFingerprints_.erase(it);
            } else {
                ++it;
            }
        }
        refreshModels();
        const bool rescanRequested = std::exchange(
            modelDirectoryRescanPending_, false);
        if (verifyAllModels || !changedModelIds.isEmpty())
            beginVerification(VerificationPurpose::Refresh);
        else if (deviceProbePending_)
            QTimer::singleShot(0, this, [this] { probeDevices(); });
        if (rescanRequested) scheduleModelDirectoryScan();
    });
    watcher->setFuture(QtConcurrent::run(
        [scannedRoot] { return buildModelDirectoryIndex(scannedRoot); }));
}

void VocalSeparationController::handleProbe(const QJsonObject& payload)
{
    const QString probedModelId = activeRequest_ && activeRequest_->kind == RequestKind::Probe
        ? activeRequest_->modelId : selectedModelId_;
    if (!activeProbeFingerprint_.isEmpty()
        && activeProbeFingerprint_ == probeFingerprint(probedModelId)
        && (payload.value(QStringLiteral("cpuValidated")).toBool()
            || payload.value(QStringLiteral("cpu")).toBool()
            || payload.value(QStringLiteral("gpu")).toBool())) {
        deviceProbeCache_.insert(probedModelId, payload);
        deviceProbeFingerprints_.insert(probedModelId, activeProbeFingerprint_);
    }
    const QString provider = payload.value(QStringLiteral("provider")).toString();
    validatedGpuProviders_.remove(probedModelId);
    if (payload.value(QStringLiteral("modelValidated")).toBool()
        && payload.value(QStringLiteral("gpu")).toBool())
        validatedGpuProviders_.insert(probedModelId, provider);
    if (probedModelId != selectedModelId_) {
        activeRequest_.reset();
        failedRequest_.reset();
        deviceProbePending_ = true;
        setJobState(JobState::Idle, {});
        refreshModels();
        QTimer::singleShot(0, this, [this] { if (deviceProbePending_) probeDevices(); });
        return;
    }
    const QString workerReason =
        payload.value(QStringLiteral("gpuReason")).toString();
    const bool cpuAvailable = payload.value(QStringLiteral("cpu")).toBool();
    const bool gpuAvailable = payload.value(QStringLiteral("gpu")).toBool();
    const QString gpuReason = !workerReason.isEmpty() ? workerReason : gpuAvailable
        ? tr("已发现 DirectML 硬件候选；开始分离时将用所选模型验证")
        : workerReason;
    availableDevices_[0] = QVariantMap{
        {QStringLiteral("mode"), int(DeviceMode::Auto)},
        {QStringLiteral("name"), QStringLiteral("Auto")},
        {QStringLiteral("available"), cpuAvailable || gpuAvailable},
        {QStringLiteral("reason"), gpuAvailable
             ? tr("自动优先使用 %1 GPU，失败时安全回退 CPU").arg(provider.isEmpty() ? "DirectML" : provider.toUpper())
             : cpuAvailable
                 ? tr("自动使用 CPU")
                 : tr("CPU 和 GPU 均未通过设备探测")},
    };
    availableDevices_[1] = QVariantMap{
        {QStringLiteral("mode"), int(DeviceMode::CPU)},
        {QStringLiteral("name"), QStringLiteral("CPU")},
        {QStringLiteral("available"), cpuAvailable},
        {QStringLiteral("reason"), QString()},
    };
    availableDevices_[2] = QVariantMap{
        {QStringLiteral("mode"), int(DeviceMode::GPU)},
        {QStringLiteral("name"), provider == QStringLiteral("mps") ? QStringLiteral("MPS")
            : provider == QStringLiteral("coreml") ? QStringLiteral("CoreML")
            : provider == QStringLiteral("cuda") ? QStringLiteral("CUDA") : QStringLiteral("DirectML")},
        {QStringLiteral("provider"), provider},
        {QStringLiteral("available"), gpuAvailable},
        {QStringLiteral("reason"), gpuReason},
    };
    emit availableDevicesChanged();
    if (!deviceChosenByUser_) {
        deviceMode_ = gpuAvailable ? DeviceMode::GPU : DeviceMode::Auto;
        emit deviceModeChanged();
    }
    emit startEligibilityChanged();
    activeRequest_.reset();
    failedRequest_.reset();
    setJobState(JobState::Idle, QStringLiteral("ready"));
    refreshModels();
    if (deviceProbePending_) QTimer::singleShot(0, this, [this] { probeDevices(); });
    if (std::exchange(modelDirectoryRescanPending_, false))
        scheduleModelDirectoryScan();
}

void VocalSeparationController::handleResult(const QJsonObject& payload)
{
    if (!activeRequest_.has_value()
        || activeRequest_->kind != RequestKind::Separation) {
        setError(tr("Worker 结果没有匹配的活动请求"));
        setJobState(JobState::JobFailed, QStringLiteral("verification"));
        return;
    }
    const ActiveRequestContext context = *activeRequest_;
    if (context.resultGeneration != resultGeneration_
        || selectedModelId_ != context.modelId
        || selectedStemKinds() != context.stemKinds) {
        failRequest(context, tr("当前界面选择与 Worker 请求不匹配"),
                    QStringLiteral("verification"));
        return;
    }
    const QJsonArray outputs = payload.value(QStringLiteral("outputs")).toArray();
    if (outputs.size() != context.stemKinds.size()) {
        failRequest(context, tr("Worker 返回的输出数量无效"),
                    QStringLiteral("verification"));
        return;
    }
    QStringList verifiedPaths;
    verifiedPaths.reserve(outputs.size());
    for (qsizetype index = 0; index < outputs.size(); ++index) {
        const QString path = QFileInfo(outputs.at(index).toString()).absoluteFilePath();
        const QFileInfo file(path);
        if (!file.isFile() || file.size() <= 0
            || !safeExistingFileWithin(path, context.outputRoot)) {
            failRequest(context, tr("Worker 输出不存在、为空或超出输出目录"),
                        QStringLiteral("verification"));
            return;
        }
        verifiedPaths.push_back(path);
    }
    QVariantList historyStems;
    for (qsizetype index = 0; index < verifiedPaths.size(); ++index) {
        historyStems.push_back(QVariantMap{
            {QStringLiteral("kind"), int(context.stemKinds.at(index))},
            {QStringLiteral("path"), verifiedPaths.at(index)},
            {QStringLiteral("available"), true}});
    }
    const QString fallbackReason = payload.value(QStringLiteral("fallbackReason")).toString();
    const QString cleanupWarning = payload.value(QStringLiteral("cleanupWarning")).toString();
    const QVariantMap record{
        {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("inputPath"), context.inputPath},
        {QStringLiteral("inputName"), QFileInfo(context.inputPath).fileName()},
        {QStringLiteral("modelId"), context.modelId},
        {QStringLiteral("status"), QStringLiteral("completed")},
        {QStringLiteral("outputPath"), context.outputRoot},
        {QStringLiteral("provider"), payload.value(QStringLiteral("provider"))},
        {QStringLiteral("fallbackReason"), fallbackReason},
        {QStringLiteral("cleanupWarning"), cleanupWarning},
        {QStringLiteral("outputGain"), payload.value(QStringLiteral("outputGain")).toDouble(1.0)},
        {QStringLiteral("rawPeaks"), payload.value(QStringLiteral("rawPeaks"))},
        {QStringLiteral("stems"), historyStems},
    };
    if (!historyStore_.append(record)) {
        failRequest(context, tr("无法保存分离历史记录"),
                    QStringLiteral("history"));
        return;
    }
    history_ = historyStore_.load();
    emit historyChanged();
    actualProvider_ = payload.value(QStringLiteral("provider")).toString();
    actualDevice_ = payload.value(QStringLiteral("device")).toString(actualProvider_ == "cpu" ? "CPU" : "GPU");
    fallbackReason_ = payload.value(QStringLiteral("fallbackReason")).toString();
    outputGain_ = payload.value(QStringLiteral("outputGain")).toDouble(1.0);
    emit actualExecutionChanged();
    if (!cleanupWarning.isEmpty()) {
        setError(tr("输出已生成，但临时预约标记清理失败；后续将自动重试清理。"));
    }
    waveformQueue_.clear();
    for (qsizetype index = 0; index < verifiedPaths.size(); ++index) {
        const QString& path = verifiedPaths.at(index);
        const StemKind kind = context.stemKinds.at(index);
        for (QVariant& value : stems_) {
            QVariantMap stem = value.toMap();
            if (stem.value(QStringLiteral("kind")).toInt() != int(kind)) continue;
            stem.insert(QStringLiteral("path"), path);
            stem.insert(QStringLiteral("available"), true);
            stem.insert(QStringLiteral("waveform"), QVariantList{});
            value = stem;
            break;
        }
        waveformQueue_.push_back({
            path,
            QStringLiteral("separation-result-%1-%2")
                .arg(context.resultGeneration).arg(index),
            kind,
            context.resultGeneration});
    }
    emit stemsChanged();
    publishedOutputRoot_ = context.outputRoot;
    if (!fallbackReason.isEmpty()) {
        const VocalModelCard* resultModel = modelForId(context.modelId);
        if (resultModel == nullptr || (resultModel->family != VocalModelFamily::Demucs
            && resultModel->id != QStringLiteral("python-vr-5hp"))) {
            QVariantMap gpu = availableDevices_.at(2).toMap();
            gpu.insert(QStringLiteral("available"), false);
            gpu.insert(QStringLiteral("reason"), fallbackReason);
            availableDevices_[2] = gpu;
        }
        if (!deviceChosenByUser_ && deviceMode_ == DeviceMode::GPU) {
            deviceMode_ = DeviceMode::Auto;
            emit deviceModeChanged();
        }
        emit availableDevicesChanged();
        emit startEligibilityChanged();
    }
    progress_ = 1.0;
    emit progressChanged();
    setJobState(JobState::Completed, QStringLiteral("completed"));
    activeRequest_.reset();
    failedRequest_.reset();
    analyzeNextWaveform();
}

void VocalSeparationController::analyzeNextWaveform()
{
    if (waveformProvider_ != nullptr && !waveformQueue_.isEmpty()) {
        const WaveformWork& work = waveformQueue_.first();
        waveformProvider_->loadForTrack(work.trackId, work.path);
    }
}

void VocalSeparationController::handleWaveform(
    const QString& path, const QVariantMap& layers)
{
    if (!inputWaveformTrackId_.isEmpty()
        && layers.value(QStringLiteral("_trackId")).toString()
               == inputWaveformTrackId_
        && inputInfo_.value(QStringLiteral("path")).toString() == path) {
        inputInfo_.insert(QStringLiteral("waveform"),
                          boundedPeaks(layers.value(QStringLiteral("mix")).toList()));
        inputInfo_.insert(QStringLiteral("durationMs"),
                          layers.value(QStringLiteral("_durationMs")).toLongLong());
        emit inputInfoChanged();
        return;
    }
    if (waveformQueue_.isEmpty()) return;
    const WaveformWork work = waveformQueue_.first();
    if (work.path != path || work.resultGeneration != resultGeneration_
        || layers.value(QStringLiteral("_trackId")).toString() != work.trackId)
        return;
    for (QVariant& value : stems_) {
        QVariantMap stem = value.toMap();
        if (stem.value(QStringLiteral("kind")).toInt() != int(work.kind)
            || stem.value(QStringLiteral("path")).toString() != work.path) continue;
        stem.insert(QStringLiteral("waveform"),
                    boundedPeaks(layers.value(QStringLiteral("mix")).toList()));
        value = stem;
        break;
    }
    emit stemsChanged();
    // The provider also emits progressive snapshots. Starting another track
    // here cancels the active decoder and loses the remaining waveform.
    if (layers.value(QStringLiteral("_complete"), true).toBool()) {
        waveformQueue_.removeFirst();
        QTimer::singleShot(0, this, &VocalSeparationController::analyzeNextWaveform);
    }
}

void VocalSeparationController::handleWaveformFailure(
    const QString& path, const QString& trackId, qulonglong, int)
{
    if (trackId == inputWaveformTrackId_
        && inputInfo_.value(QStringLiteral("path")).toString() == path) {
        inputInfo_.insert(QStringLiteral("waveform"), QVariantList{});
        inputInfo_.insert(QStringLiteral("durationMs"), qlonglong(0));
        emit inputInfoChanged();
        return;
    }
    if (waveformQueue_.isEmpty()) return;
    const WaveformWork work = waveformQueue_.first();
    if (work.path != path || work.trackId != trackId
        || work.resultGeneration != resultGeneration_) return;
    waveformQueue_.removeFirst();
    analyzeNextWaveform();
}

void VocalSeparationController::clearPublishedResult()
{
    actualProvider_.clear(); actualDevice_.clear(); fallbackReason_.clear(); outputGain_ = 1.0; emit actualExecutionChanged();
    if (waveformProvider_ != nullptr) {
        for (const WaveformWork& work : waveformQueue_)
            waveformProvider_->cancelForTrack(work.path);
    }
    waveformQueue_.clear();
    publishedOutputRoot_.clear();
    for (QVariant& value : stems_) {
        QVariantMap stem = value.toMap();
        stem.insert(QStringLiteral("available"), false);
        stem.insert(QStringLiteral("path"), QString());
        stem.insert(QStringLiteral("waveform"), QVariantList{});
        value = stem;
    }
    emit stemsChanged();
}

void VocalSeparationController::resetInputSession()
{
    stopPreviewForCurrentInputOrResult();
    const QString inputPath = inputInfo_.value(QStringLiteral("path")).toString();
    if (waveformProvider_ != nullptr && !inputPath.isEmpty())
        waveformProvider_->cancelForTrack(inputPath);
    ++resultGeneration_;
    clearPublishedResult();
    failedRequest_.reset();
    if (progress_ != 0.0) {
        progress_ = 0.0;
        emit progressChanged();
    }
    setJobState(JobState::Idle, {});
}

QList<VocalSeparationController::StemKind>
VocalSeparationController::resultMixKinds() const
{
    const auto available = [this](const StemKind kind) {
        for (const QVariant& value : stems_) {
            const QVariantMap stem = value.toMap();
            if (stem.value(QStringLiteral("kind")).toInt() == int(kind)) {
                return stem.value(QStringLiteral("available")).toBool()
                    && !stem.value(QStringLiteral("path")).toString().isEmpty();
            }
        }
        return false;
    };

    QList<StemKind> result;
    if (available(StemKind::Vocals)) result.push_back(StemKind::Vocals);

    const bool completeComponents = available(StemKind::Drums)
        && available(StemKind::Bass) && available(StemKind::Other);
    if (completeComponents) {
        result.push_back(StemKind::Drums);
        result.push_back(StemKind::Bass);
        result.push_back(StemKind::Other);
    } else if (available(StemKind::Accompaniment)) {
        result.push_back(StemKind::Accompaniment);
    } else {
        for (const StemKind kind : {StemKind::Drums, StemKind::Bass,
                                    StemKind::Other}) {
            if (available(kind)) result.push_back(kind);
        }
    }
    return result;
}

bool VocalSeparationController::startResultPreview(
    const QList<StemKind>& kinds, const qint64 positionMs,
    const ResultPreviewMode mode, const StemKind soloKind)
{
    if (preview_ == nullptr || kinds.isEmpty()) return false;

    QList<AudioPreviewController::MixSource> sources;
    sources.reserve(kinds.size());
    for (const StemKind kind : kinds) {
        const QString path = pathForStem(kind);
        const bool safe = publishedOutputRoot_.isEmpty()
            ? safeExistingFile(path)
            : safeExistingFileWithin(path, publishedOutputRoot_);
        if (!safe) return false;
        sources.push_back({path, path,
                           stemPreviewVolumes_.value(int(kind), 0.8)});
    }

    const qint64 duration = inputInfo_
        .value(QStringLiteral("durationMs")).toLongLong();
    const qint64 target = qBound<qint64>(
        0, positionMs, duration > 0 ? duration : std::max<qint64>(0, positionMs));
    if (!preview_->playMix(sources, target)) {
        resetResultPreviewState();
        return false;
    }

    const bool changed = resultPreviewMode_ != mode
        || resultPreviewSoloKind_ != soloKind
        || resultPreviewMixKinds_ != kinds;
    resultPreviewMode_ = mode;
    resultPreviewSoloKind_ = soloKind;
    resultPreviewMixKinds_ = kinds;
    if (changed) emit resultPreviewChanged();
    return true;
}

void VocalSeparationController::resetResultPreviewState()
{
    const bool changed = resultPreviewMode_ != ResultPreviewMode::None
        || resultPreviewSoloKind_ != StemKind::Original
        || !resultPreviewMixKinds_.isEmpty();
    resultPreviewMode_ = ResultPreviewMode::None;
    resultPreviewSoloKind_ = StemKind::Original;
    resultPreviewMixKinds_.clear();
    if (changed) emit resultPreviewChanged();
}

void VocalSeparationController::stopPreviewForCurrentInputOrResult()
{
    if (preview_ == nullptr) {
        resetResultPreviewState();
        return;
    }
    if (resultPreviewMode_ != ResultPreviewMode::None
        || preview_->mixActive()) {
        if (preview_->hasSource()) preview_->stop();
        resetResultPreviewState();
        return;
    }
    if (!preview_->hasSource()) return;
    const QString inputPath = inputInfo_.value(QStringLiteral("path")).toString();
    if (!inputPath.isEmpty()
        && preview_->isCurrentSource(QUrl::fromLocalFile(inputPath))) {
        preview_->stop();
        return;
    }
    for (const QVariant& value : stems_) {
        const QString path = value.toMap().value(QStringLiteral("path")).toString();
        if (!path.isEmpty()
            && preview_->isCurrentSource(QUrl::fromLocalFile(path))) {
            preview_->stop();
            return;
        }
    }
}

bool VocalSeparationController::togglePreviewPath(const QString& path,
                                                  const QString& root)
{
    if (preview_ == nullptr
        || (root.isEmpty() ? !safeExistingFile(path)
                           : !safeExistingFileWithin(path, root))) {
        return false;
    }
    const QUrl source = QUrl::fromLocalFile(path);
    preview_->toggle(source);
    return preview_->isCurrentSource(source);
}

bool VocalSeparationController::requestInFlight() const noexcept
{
    return jobState_ == JobState::Probing || jobState_ == JobState::Running
        || jobState_ == JobState::Cancelling;
}

QString VocalSeparationController::pathForStem(StemKind kind) const
{
    for (const QVariant& value : stems_) {
        const QVariantMap stem = value.toMap();
        if (stem.value(QStringLiteral("kind")).toInt() == int(kind))
            return stem.value(QStringLiteral("path")).toString();
    }
    return {};
}

QStringList VocalSeparationController::selectedStemNames() const
{
    QStringList result;
    for (const QVariant& value : stems_) {
        const QVariantMap stem = value.toMap();
        if (stem.value(QStringLiteral("supported")).toBool()
            && stem.value(QStringLiteral("selected")).toBool())
            result.push_back(stem.value(QStringLiteral("name")).toString());
    }
    return result;
}

QList<VocalSeparationController::StemKind>
VocalSeparationController::selectedStemKinds() const
{
    QList<StemKind> result;
    for (const QVariant& value : stems_) {
        const QVariantMap stem = value.toMap();
        if (stem.value(QStringLiteral("supported")).toBool()
            && stem.value(QStringLiteral("selected")).toBool())
            result.push_back(static_cast<StemKind>(
                stem.value(QStringLiteral("kind")).toInt()));
    }
    return result;
}

bool VocalSeparationController::addPathsToPlaylist(
    const QStringList& paths, const QString& playlistId)
{
    if (library_ == nullptr || importer_ == nullptr || playlists_ == nullptr
        || paths.isEmpty() || importer_->busy() || playlistOperation_.has_value()
        || !playlistExists(playlistId)) return false;
    for (const QString& path : paths) {
        if (!safeExistingFileWithin(path, publishedOutputRoot_)) return false;
    }
    playlistOperation_ = PlaylistOperation{playlistId, paths, {}};
    const auto commitOperation = [this] {
        if (!playlistOperation_.has_value() || library_ == nullptr
            || playlists_ == nullptr
            || !playlistExists(playlistOperation_->playlistId)) {
            finishPlaylistOperation(false,
                tr("播放列表在导入完成前已不可用"));
            return false;
        }
        const QString targetPlaylistId = playlistOperation_->playlistId;
        QStringList trackIdsToAdd;
        for (const QString& path : playlistOperation_->paths) {
            if (!safeExistingFileWithin(path, publishedOutputRoot_)) {
                finishPlaylistOperation(false,
                    tr("导入期间分离结果已变得不可用"));
                return false;
            }
            const int row = library_->indexForLocalFile(path);
            if (row < 0) {
                finishPlaylistOperation(false,
                    tr("音轨导入失败，播放列表没有保留部分结果"));
                return false;
            }
            const QString trackId = library_->data(
                library_->index(row), LibraryModel::TrackIdRole).toString();
            if (trackId.isEmpty()) {
                finishPlaylistOperation(false,
                    tr("导入的音轨缺少有效标识"));
                return false;
            }
            if (!playlists_->containsTrack(targetPlaylistId, trackId)
                && !trackIdsToAdd.contains(trackId)) {
                trackIdsToAdd.push_back(trackId);
            }
        }
        for (const QString& trackId : trackIdsToAdd) {
            if (!playlists_->addTrack(targetPlaylistId, trackId)) {
                finishPlaylistOperation(false,
                    tr("无法将全部分离结果加入播放列表"));
                return false;
            }
            playlistOperation_->addedTrackIds.push_back(trackId);
        }
        finishPlaylistOperation(true, {});
        return true;
    };
    QStringList unresolved;
    for (const QString& path : paths) {
        if (library_->indexForLocalFile(path) < 0) unresolved.push_back(path);
    }
    if (unresolved.isEmpty()) return commitOperation();
    connect(importer_, &ImportController::finished, this,
            [commitOperation] { commitOperation(); },
            Qt::SingleShotConnection);
    importer_->importPaths(unresolved);
    return true;
}

bool VocalSeparationController::playlistExists(const QString& playlistId) const
{
    if (playlists_ == nullptr || playlistId.isEmpty()) return false;
    for (int row = 0; row < playlists_->rowCount(); ++row) {
        if (playlists_->idAt(row) == playlistId) return true;
    }
    return false;
}

void VocalSeparationController::finishPlaylistOperation(
    bool success, const QString& diagnostic)
{
    if (!playlistOperation_.has_value()) return;
    const PlaylistOperation operation = *playlistOperation_;
    playlistOperation_.reset();
    if (!success && playlists_ != nullptr) {
        for (const QString& trackId : operation.addedTrackIds)
            playlists_->removeTrack(operation.playlistId, trackId);
    }
    setError(success ? QString{} : diagnostic);
    emit playlistOperationFinished(success, diagnostic);
}

QVariantList VocalSeparationController::boundedPeaks(const QVariantList& peaks)
{
    constexpr qsizetype maximum = 2048;
    if (peaks.size() <= maximum) return peaks;
    QVariantList result;
    result.reserve(maximum);
    for (qsizetype bucket = 0; bucket < maximum; ++bucket) {
        const qsizetype begin = bucket * peaks.size() / maximum;
        const qsizetype end = (bucket + 1) * peaks.size() / maximum;
        double peak = 0.0;
        for (qsizetype index = begin; index < end; ++index)
            peak = std::max(peak, std::abs(peaks.at(index).toDouble()));
        result.push_back(peak);
    }
    return result;
}

bool VocalSeparationController::atomicCopyNoOverwrite(
    const QString& source, const QString& sourceRoot,
    const QString& destination)
{
    if (!safeExistingFileWithin(source, sourceRoot)
        || QFileInfo::exists(destination)) return false;
    const QFileInfo target(destination);
    if (!QDir().mkpath(target.absolutePath())) return false;
    QFile input(source);
    QTemporaryFile temporary(QDir(target.absolutePath()).filePath(
        QStringLiteral(".agplayer-export-XXXXXX")));
    temporary.setAutoRemove(false);
    if (!openRegularFileForReadWithin(&input, sourceRoot)
        || !temporary.open()) return false;
    while (!input.atEnd()) {
        const QByteArray block = input.read(1024 * 1024);
        if (block.isEmpty() && input.error() != QFileDevice::NoError) {
            temporary.remove();
            return false;
        }
        if (temporary.write(block) != block.size()) {
            temporary.remove();
            return false;
        }
    }
    const QString temporaryPath = temporary.fileName();
    if (!temporary.flush()) {
        temporary.remove();
        return false;
    }
    temporary.close();
    if (!temporary.rename(destination)) {
        QFile::remove(temporaryPath);
        return false;
    }
    return true;
}

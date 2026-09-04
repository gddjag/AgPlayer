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
      outputDirectory_(options_.outputDirectory)
{
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
        emit downloadProgressChanged();
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
                    refreshModels();
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
double VocalSeparationController::downloadProgress() const noexcept { return downloadProgress_; }
QString VocalSeparationController::downloadingModelId() const { return downloadingModelId_; }
bool VocalSeparationController::downloadBusy() const noexcept
{
    return !downloadingModelId_.isEmpty();
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
    if (requestInFlight()) return tr("当前任务尚未结束");
    if (!process_.canAcceptRequest()) return tr("当前任务尚未结束");
    if (!safeExistingFile(inputInfo_.value(QStringLiteral("path")).toString()))
        return tr("请选择有效输入音频");
    const VocalModelCard* model = selectedModel();
    if (model == nullptr) return tr("请选择模型");
    if (!modelInstalled(*model)
        && (verifiedOrRejectedModelIds_.contains(model->id)
            || !modelFilesPresent(*model))) {
        return tr("所选模型尚未安装或未通过校验");
    }
    if (selectedStemNames().isEmpty()) return tr("至少选择一个输出音轨");
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

bool VocalSeparationController::configureRuntime(const QString& modelIdForUi)
{
    if (!downloadQueue_.isEmpty() || verificationWatcher_ != nullptr
        || runtimeInstallerWatcher_ != nullptr
        || downloader_->state() == VocalDownloadState::Downloading
        || downloader_->state() == VocalDownloadState::Paused
        || downloader_->state() == VocalDownloadState::Verifying) {
        return false;
    }
    if (runtimeReady()) return true;

    const VocalRuntimePackage package =
        VocalSeparationCatalog::directMlRuntime();
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
    const VocalModelCard* model = modelForId(modelId);
    if (model == nullptr || !downloadQueue_.isEmpty()
        || verificationWatcher_ != nullptr || runtimeInstallerWatcher_ != nullptr
        || downloader_->state() == VocalDownloadState::Downloading
        || downloader_->state() == VocalDownloadState::Paused
        || downloader_->state() == VocalDownloadState::Verifying) {
        return false;
    }
    if (preferDomesticMirror) {
        bool mirrorAvailable = false;
        for (const VocalDownloadFile& file : model->files) {
            if (vocalDomesticMirrorUrl(file.url).isValid()) {
                mirrorAvailable = true;
                break;
            }
        }
        if (!mirrorAvailable) {
            setError(tr("当前模型没有可自动下载的国内镜像，请使用备用公益地址"));
            return false;
        }
    }
    preferDomesticMirror_ = preferDomesticMirror;
    downloadSource_ = preferDomesticMirror ? tr("国内镜像") : tr("官方线路");
    downloadingModelId_ = modelId;
    failedDownloadModelId_.clear();
    downloadProgress_ = 0.0;
    completedDownloadBytes_ = 0;
    totalDownloadBytes_ = 0;
    emit downloadProgressChanged();
    emit downloadStateChanged();
    setError({});
    return beginVerification(VerificationPurpose::Download, model);
}

bool VocalSeparationController::verifyInstalledModels()
{
    modelDirectoryScanTimer_.stop();
    scanModelDirectory();
    return modelDirectoryIndexWatcher_ != nullptr
        || verificationWatcher_ != nullptr;
}

void VocalSeparationController::pauseDownload() { downloader_->pause(); }
void VocalSeparationController::resumeDownload() { downloader_->resume(); }

void VocalSeparationController::cancelDownload()
{
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
    if (requestInFlight() || downloadActive
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
    verifiedModelIds_.remove(modelId);
    verifiedOrRejectedModelIds_.remove(modelId);
    refreshModels();
    return true;
}

bool VocalSeparationController::selectModel(const QString& modelId)
{
    if (modelForId(modelId) == nullptr || requestInFlight()) return false;
    if (selectedModelId_ == modelId) return true;
    if (resultPreviewMode_ != ResultPreviewMode::None
        || (preview_ != nullptr && preview_->mixActive())) {
        if (preview_ != nullptr) preview_->stop();
        resetResultPreviewState();
    }
    ++resultGeneration_;
    clearPublishedResult();
    selectedModelId_ = modelId;
    invalidateRetry();
    emit selectedModelIdChanged();
    rebuildStems();
    return true;
}

bool VocalSeparationController::setStemSelected(StemKind kind, bool selected)
{
    if (requestInFlight()) return false;
    for (QVariant& value : stems_) {
        QVariantMap stem = value.toMap();
        if (stem.value(QStringLiteral("kind")).toInt() != int(kind)) continue;
        if (!stem.value(QStringLiteral("supported")).toBool()) return false;
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

bool VocalSeparationController::probeDevices()
{
    if (requestInFlight() || !process_.canAcceptRequest()
        || verificationWatcher_ != nullptr) return false;
    activeRequest_ = ActiveRequestContext{RequestKind::Probe};
    setError({});
    setJobState(JobState::Probing, QStringLiteral("runtime_verification"));
    if (beginVerification(VerificationPurpose::Probe)) return true;
    activeRequest_.reset();
    setJobState(JobState::Idle, {});
    return false;
}

bool VocalSeparationController::start()
{
    if (requestInFlight() || !process_.canAcceptRequest()) return false;
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
    if (verificationWatcher_ != nullptr
        && (verificationPurpose_ == VerificationPurpose::Start
            || verificationPurpose_ == VerificationPurpose::Probe)) {
        if (verificationCancellation_)
            verificationCancellation_->store(true, std::memory_order_release);
        if (!verifyingModelId_.isEmpty()) {
            verifiedModelIds_.remove(verifyingModelId_);
            verifiedOrRejectedModelIds_.insert(verifyingModelId_);
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
        if (!stem.value(QStringLiteral("supported")).toBool()) return false;
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
    return exportKinds(selectedStemKinds(), destinationDirectory);
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
    for (const StemKind kind : selectedStemKinds()) {
        const QString path = pathForStem(kind);
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
    if (requestInFlight() || verificationWatcher_ != nullptr
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
    const QString runtimeHash = VocalSeparationCatalog::directMlRuntime().sha256;
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
         cancellation] {
            VerificationResult result;
            const ModelFileIndex fileIndex = buildModelDirectoryIndex(
                modelStorageDirectory).filesByName;
            const auto cancelled = [&cancellation] {
                return cancellation->load(std::memory_order_acquire);
            };
            for (const VocalModelCard& candidate : catalog) {
                if (cancelled()) return result;
                bool verified = !candidate.files.isEmpty();
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
                        result.verifiedFiles.insert(QFileInfo(path).absoluteFilePath());
                    } else {
                        if (cancelled()) return result;
                        verified = false;
                    }
                }
                if (verified) result.verifiedModels.insert(candidate.id);
            }
            if (cancelled()) return result;
            result.runtimeVerified = QFileInfo(runtimePath).isFile()
                && (!verifyRuntime
                    || VocalSeparationInstaller::runtimeDirectoryIsVerified(
                        QFileInfo(runtimePath).absolutePath(), runtimeHash,
                        cancellation));
            return result;
        }));
    return true;
}

void VocalSeparationController::finishVerification(
    quint64 generation, const VerificationResult& result)
{
    if (generation != verificationGeneration_) return;
    const auto pendingRescan = qScopeGuard([this] {
        if (!modelDirectoryRescanPending_) return;
        modelDirectoryRescanPending_ = false;
        scheduleModelDirectoryScan();
    });
    const VerificationPurpose purpose = verificationPurpose_;
    const QString verifiedModelId = verifyingModelId_;
    verificationPurpose_ = VerificationPurpose::None;
    verifyingModelId_.clear();
    runtimeVerified_ = result.runtimeVerified;

    if (verifiedModelId.isEmpty()) {
        verifiedModelIds_ = result.verifiedModels;
        for (const VocalModelCard& model : options_.catalog)
            verifiedOrRejectedModelIds_.insert(model.id);
    } else if (result.verifiedModels.contains(verifiedModelId)) {
        verifiedModelIds_.insert(verifiedModelId);
        verifiedOrRejectedModelIds_.insert(verifiedModelId);
    } else {
        verifiedModelIds_.remove(verifiedModelId);
        verifiedOrRejectedModelIds_.insert(verifiedModelId);
    }

    if (purpose == VerificationPurpose::Refresh) {
        refreshModels();
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
        if (!result.runtimeVerified) {
            const VocalRuntimePackage package =
                VocalSeparationCatalog::directMlRuntime();
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
        if (!result.runtimeVerified || !launchProbe()) {
            const ActiveRequestContext context = activeRequest_.value_or(
                ActiveRequestContext{RequestKind::Probe});
            failRequest(context, tr("ONNX Runtime 尚未安装或校验失败"),
                        QStringLiteral("runtime_verification"));
        }
        return;
    }
    if (purpose == VerificationPurpose::Start) {
        if (!activeRequest_.has_value()
            || !result.verifiedModels.contains(activeRequest_->modelId)
            || !result.runtimeVerified
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
    if (!process_.startProbe(
            {{QStringLiteral("runtimePath"), options_.runtimeLibraryPath}}))
        return false;
    setJobState(JobState::Probing, QStringLiteral("provider_probe"));
    return true;
}

bool VocalSeparationController::launchSeparation(
    const ActiveRequestContext& context)
{
    const VocalModelCard* model = modelForId(context.modelId);
    if (model == nullptr) return false;
    QJsonArray modelFiles;
    for (const QString& path : modelFilePaths(*model))
        modelFiles.push_back(path);
    QJsonArray requestedStems;
    for (const QString& name : context.stemNames) requestedStems.push_back(name);
    QJsonArray stemLabels;
    for (const QString& label : context.stemLabels) stemLabels.push_back(label);
    QJsonObject payload{
        {QStringLiteral("runtimePath"), options_.runtimeLibraryPath},
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
        {QStringLiteral("device"), deviceName(context.device)},
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
        QVariantList kinds;
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
            {QStringLiteral("id"), model.id},
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
            {QStringLiteral("backend"), QStringLiteral("onnxruntime-native")},
            {QStringLiteral("available"),
             state == ModelState::Installed && runtimeReady()},
            {QStringLiteral("failureReason"),
             state == ModelState::Installed && !runtimeReady()
                 ? tr("ONNX Runtime 尚未安装或未通过校验")
                 : state == ModelState::ModelFailed
                     ? tr("模型校验失败") : QString()},
            {QStringLiteral("hashes"), modelHashes},
            {QStringLiteral("rejectionReason"), QString()},
        });
    }
    for (const QVariant& rejected : std::as_const(rejectedCustomModels_))
        models_.push_back(rejected);
    emit modelsChanged();
    emit startEligibilityChanged();
}

void VocalSeparationController::rebuildStems()
{
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
    }
    emit stemsChanged();
    emit startEligibilityChanged();
}

void VocalSeparationController::setJobState(JobState state, const QString& stage)
{
    const bool changed = jobState_ != state || stage_ != stage;
    jobState_ = state;
    stage_ = stage;
    if (changed) emit jobStateChanged();
    emit startEligibilityChanged();
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
    setError(diagnostic);
    refreshModels();
    emit downloadSourcesExhausted(QVariantMap{
        {QStringLiteral("modelId"), modelId},
        {QStringLiteral("source"), source},
        {QStringLiteral("diagnostic"), diagnostic},
    });
}

void VocalSeparationController::startNextDownload()
{
    if (downloadQueue_.isEmpty()) {
        if (!runtimeOnlyDownload_ && !downloadingModelId_.isEmpty())
            verifiedModelIds_.insert(downloadingModelId_);
        if (!runtimeOnlyDownload_ && !downloadingModelId_.isEmpty())
            verifiedOrRejectedModelIds_.insert(downloadingModelId_);
        runtimeOnlyDownload_ = false;
        downloadingModelId_.clear();
        downloadProgress_ = 1.0;
        completedDownloadBytes_ = totalDownloadBytes_;
        emit downloadProgressChanged();
        emit downloadStateChanged();
        rebuildModelDirectoryWatcher();
        refreshModels();
        if (modelDirectoryRescanPending_) {
            modelDirectoryRescanPending_ = false;
            scheduleModelDirectoryScan();
        }
        return;
    }
    const DownloadItem& item = downloadQueue_.first();
    const QString route = item.mirrorAttempted ? tr("国内镜像")
                                                : tr("官方线路");
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
                : tr("已识别模型文件；需要安装可选外置 Python 运行时后才能执行");
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
    const bool busy = requestInFlight() || verificationWatcher_ != nullptr
        || modelDirectoryIndexWatcher_ != nullptr
        || runtimeInstallerWatcher_ != nullptr || !downloadQueue_.isEmpty()
        || downloadState == VocalDownloadState::Downloading
        || downloadState == VocalDownloadState::Paused
        || downloadState == VocalDownloadState::Verifying;
    if (busy) {
        modelDirectoryRescanPending_ = true;
        return;
    }
    const QString scannedRoot = modelStorageDirectory_;
    auto* const watcher = new QFutureWatcher<ModelDirectoryIndex>(this);
    modelDirectoryIndexWatcher_ = watcher;
    connect(watcher, &QFutureWatcher<ModelDirectoryIndex>::finished, this,
            [this, watcher, scannedRoot] {
        const ModelDirectoryIndex result = watcher->result();
        watcher->deleteLater();
        if (modelDirectoryIndexWatcher_ != watcher) return;
        modelDirectoryIndexWatcher_ = nullptr;
        if (scannedRoot != modelStorageDirectory_) {
            scheduleModelDirectoryScan();
            return;
        }
        indexedModelFiles_ = result.filesByName;
        const QStringList watched = modelDirectoryWatcher_.directories();
        if (!watched.isEmpty()) modelDirectoryWatcher_.removePaths(watched);
        if (!result.directories.isEmpty())
            modelDirectoryWatcher_.addPaths(result.directories);
        discoverCustomModels(result.manifests);
        verifiedModelIds_.clear();
        verifiedOrRejectedModelIds_.clear();
        refreshModels();
        bool completeKnownModel = false;
        for (const VocalModelCard& model : std::as_const(options_.catalog)) {
            if (modelFilesPresent(model)) {
                completeKnownModel = true;
                break;
            }
        }
        const bool rescanRequested = std::exchange(
            modelDirectoryRescanPending_, false);
        if (completeKnownModel)
            beginVerification(VerificationPurpose::Refresh);
        if (rescanRequested) scheduleModelDirectoryScan();
    });
    watcher->setFuture(QtConcurrent::run(
        [scannedRoot] { return buildModelDirectoryIndex(scannedRoot); }));
}

void VocalSeparationController::handleProbe(const QJsonObject& payload)
{
    const QString workerReason =
        payload.value(QStringLiteral("gpuReason")).toString();
    const bool cpuAvailable = payload.value(QStringLiteral("cpu")).toBool();
    const bool gpuAvailable = payload.value(QStringLiteral("gpu")).toBool();
    const QString gpuReason = gpuAvailable
        ? tr("已发现 DirectML 硬件候选；开始分离时将用所选模型验证")
        : workerReason;
    availableDevices_[0] = QVariantMap{
        {QStringLiteral("mode"), int(DeviceMode::Auto)},
        {QStringLiteral("name"), QStringLiteral("Auto")},
        {QStringLiteral("available"), cpuAvailable || gpuAvailable},
        {QStringLiteral("reason"), gpuAvailable
             ? tr("自动优先尝试 DirectML 候选，失败时安全回退 CPU")
             : cpuAvailable
                 ? tr("自动使用已验证的 CPU")
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
        {QStringLiteral("name"), QStringLiteral("DirectML")},
        {QStringLiteral("available"), gpuAvailable},
        {QStringLiteral("reason"), gpuReason},
    };
    emit availableDevicesChanged();
    emit startEligibilityChanged();
    activeRequest_.reset();
    failedRequest_.reset();
    setJobState(JobState::Idle, QStringLiteral("ready"));
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
        {QStringLiteral("stems"), historyStems},
    };
    if (!historyStore_.append(record)) {
        failRequest(context, tr("无法保存分离历史记录"),
                    QStringLiteral("history"));
        return;
    }
    history_ = historyStore_.load();
    emit historyChanged();
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
        QVariantMap gpu = availableDevices_.at(2).toMap();
        gpu.insert(QStringLiteral("available"), false);
        gpu.insert(QStringLiteral("reason"), fallbackReason);
        availableDevices_[2] = gpu;
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
    waveformQueue_.removeFirst();
    emit stemsChanged();
    analyzeNextWaveform();
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

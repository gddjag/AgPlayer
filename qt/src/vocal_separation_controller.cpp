#include "vocal_separation_controller.hpp"

#include "audio_preview_controller.hpp"
#include "import_controller.hpp"
#include "library_model.hpp"
#include "playlist_model.hpp"
#include "vocal_separation_installer.hpp"
#include "vocal_separation_path_safety.hpp"
#include "waveform_provider.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QStandardPaths>
#include <QTemporaryFile>
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

} // namespace

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
    history_ = historyStore_.load();
    availableDevices_ = {
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
    if (!options_.catalog.isEmpty()) selectedModelId_ = options_.catalog.first().id;
    rebuildStems();

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
    connect(&process_, &SeparationProcessClient::failed, this,
            [this](const QString& message, bool) {
        if (activeRequest_.has_value()) failedRequest_ = activeRequest_;
        activeRequest_.reset();
        setError(message);
        setJobState(JobState::Failed, QStringLiteral("error"));
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
        progress_ = total > 0 ? static_cast<double>(received) / total : 0.0;
        emit progressChanged();
    });
    connect(downloader_.get(), &VocalSeparationDownloader::finished,
            this, [this](const VocalInstallResult& result) {
        if (!result.ok) {
            downloadQueue_.clear();
            setError(result.error);
            refreshModels();
            return;
        }
        const DownloadItem completed = downloadQueue_.takeFirst();
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
                if (!installed.ok) {
                    downloadQueue_.clear();
                    setError(installed.error);
                    refreshModels();
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
    if (verificationWatcher_ != nullptr) {
        QFutureWatcher<VerificationResult>* const watcher = verificationWatcher_;
        disconnect(watcher, nullptr, this, nullptr);
        watcher->future().waitForFinished();
        verificationWatcher_ = nullptr;
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
QString VocalSeparationController::error() const { return error_; }
QVariantList VocalSeparationController::stems() const { return stems_; }
QVariantList VocalSeparationController::history() const { return history_; }

bool VocalSeparationController::selectInput(const QUrl& url)
{
    if (requestInFlight()) return false;
    const QString path = QFileInfo(url.toLocalFile()).absoluteFilePath();
    const QFileInfo file(path);
    if (!file.isFile()) {
        setError(tr("输入音频不存在"));
        return false;
    }
    inputInfo_ = {{QStringLiteral("path"), path},
                  {QStringLiteral("name"), file.fileName()},
                  {QStringLiteral("bytes"), file.size()}};
    invalidateRetry();
    setError({});
    emit inputInfoChanged();
    return true;
}

bool VocalSeparationController::dropInput(const QList<QUrl>& urls)
{
    return urls.size() == 1 && selectInput(urls.first());
}

bool VocalSeparationController::downloadModel(const QString& modelId)
{
    const VocalModelCard* model = modelForId(modelId);
    if (model == nullptr || !downloadQueue_.isEmpty()
        || verificationWatcher_ != nullptr || runtimeInstallerWatcher_ != nullptr
        || downloader_->state() == VocalDownloadState::Downloading
        || downloader_->state() == VocalDownloadState::Paused
        || downloader_->state() == VocalDownloadState::Verifying) {
        return false;
    }
    downloadingModelId_ = modelId;
    setError({});
    return beginVerification(VerificationPurpose::Download, model);
}

bool VocalSeparationController::verifyInstalledModels()
{
    return beginVerification(VerificationPurpose::Refresh);
}

void VocalSeparationController::pauseDownload() { downloader_->pause(); }
void VocalSeparationController::resumeDownload() { downloader_->resume(); }

bool VocalSeparationController::deleteModel(const QString& modelId)
{
    const VocalModelCard* model = modelForId(modelId);
    const VocalDownloadState downloadState = downloader_->state();
    const bool downloadActive = downloadingModelId_ == modelId
        && (downloadState == VocalDownloadState::Downloading
            || downloadState == VocalDownloadState::Paused
            || downloadState == VocalDownloadState::Verifying);
    if (model == nullptr || requestInFlight() || downloadActive
        || runtimeInstallerWatcher_ != nullptr
        || verificationWatcher_ != nullptr)
        return false;
    const QString modelsRoot = QDir(options_.dataRoot).filePath(
        QStringLiteral("models"));
    const VocalInstallResult removed =
        VocalSeparationInstaller::deleteModelFiles(*model, modelsRoot);
    if (!removed.ok) {
        setError(removed.error);
        return false;
    }
    if (downloadingModelId_ == modelId) downloadingModelId_.clear();
    verifiedModelIds_.remove(modelId);
    verifiedOrRejectedModelIds_.remove(modelId);
    refreshModels();
    return true;
}

bool VocalSeparationController::selectModel(const QString& modelId)
{
    if (modelForId(modelId) == nullptr || requestInFlight()) return false;
    if (selectedModelId_ == modelId) return true;
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
        stem.insert(QStringLiteral("selected"), selected);
        value = stem;
        invalidateRetry();
        emit stemsChanged();
        return true;
    }
    return false;
}

bool VocalSeparationController::selectDevice(DeviceMode mode)
{
    if (requestInFlight()) return false;
    if (deviceMode_ == mode) return true;
    deviceMode_ = mode;
    invalidateRetry();
    emit deviceModeChanged();
    return true;
}

bool VocalSeparationController::selectOutputFormat(const QString& format)
{
    if (requestInFlight()) return false;
    const QString normalized = format.trimmed().toLower();
    if (!QStringList{QStringLiteral("wav"), QStringLiteral("flac"),
                     QStringLiteral("mp3")}.contains(normalized)) return false;
    outputFormat_ = normalized;
    invalidateRetry();
    return true;
}

bool VocalSeparationController::selectOutputDirectory(const QUrl& directory)
{
    if (requestInFlight()) return false;
    const QString path = QFileInfo(directory.toLocalFile()).absoluteFilePath();
    if (path.isEmpty() || (!QDir(path).exists() && !QDir().mkpath(path))) return false;
    outputDirectory_ = path;
    invalidateRetry();
    return true;
}

bool VocalSeparationController::probeDevices()
{
    if (requestInFlight() || verificationWatcher_ != nullptr) return false;
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
    if (requestInFlight()) return false;
    const VocalModelCard* model = selectedModel();
    const QString inputPath = inputInfo_.value(QStringLiteral("path")).toString();
    const QStringList stemNames = selectedStemNames();
    if (model == nullptr) {
        ++resultGeneration_;
        clearPublishedResult();
        setError(tr("请选择有效输入音频和至少一个输出音轨"));
        failedRequest_.reset();
        setJobState(JobState::Failed, QStringLiteral("validation"));
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
    return beginSeparationRequest(std::move(context));
}

bool VocalSeparationController::beginSeparationRequest(
    ActiveRequestContext context)
{
    context.resultGeneration = ++resultGeneration_;
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
    setJobState(JobState::Failed, stage);
}

void VocalSeparationController::invalidateRetry()
{
    if (!requestInFlight()) failedRequest_.reset();
}

void VocalSeparationController::cancel()
{
    if (jobState_ != JobState::Running && jobState_ != JobState::Probing) return;
    if (verificationWatcher_ != nullptr
        && (verificationPurpose_ == VerificationPurpose::Start
            || verificationPurpose_ == VerificationPurpose::Probe)) {
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
    if (jobState_ != JobState::Failed || !failedRequest_.has_value()) return false;
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
    return preview_ != nullptr && safeExistingFile(path)
        && preview_->switchSourcePreservingPosition(QUrl::fromLocalFile(path));
}

bool VocalSeparationController::previewStem(StemKind kind)
{
    const QString path = pathForStem(kind);
    return preview_ != nullptr
        && safeExistingFileWithin(path, publishedOutputRoot_)
        && preview_->switchSourcePreservingPosition(QUrl::fromLocalFile(path));
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

bool VocalSeparationController::exportSelected(const QUrl& destinationDirectory)
{
    const QString requestedDestination = destinationDirectory.toLocalFile();
    if (safePathKind(requestedDestination) != SafePathKind::Directory) {
        setError(tr("批量导出目录不存在或不安全"));
        return false;
    }
    const QString destination = QDir(requestedDestination).canonicalPath();
    if (destination.isEmpty()) return false;
    QList<QPair<QString, QString>> files;
    for (const StemKind kind : selectedStemKinds()) {
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

bool VocalSeparationController::openModelDirectory()
{
    const QString path = modelDirectory(selectedModelId_);
    return QDir(path).exists()
        && QDesktopServices::openUrl(QUrl::fromLocalFile(path));
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
    return QDir(options_.dataRoot).filePath(QStringLiteral("models/%1").arg(modelId));
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
    if (model.files.isEmpty()) return false;
    for (const VocalDownloadFile& file : model.files) {
        const QFileInfo candidate(
            QDir(modelDirectory(model.id)).filePath(file.fileName));
        if (!candidate.isFile() || candidate.size() != file.bytes) return false;
    }
    return true;
}

bool VocalSeparationController::runtimeReady() const
{
    if (!QFileInfo(options_.runtimeLibraryPath).isFile()) return false;
    return !options_.verifyRuntimeIntegrity || runtimeVerified_;
}

bool VocalSeparationController::beginVerification(
    VerificationPurpose purpose, const VocalModelCard* model)
{
    if (verificationWatcher_ != nullptr || purpose == VerificationPurpose::None)
        return false;
    const QList<VocalModelCard> catalog = model == nullptr
        ? options_.catalog : QList<VocalModelCard>{*model};
    const QString dataRoot = options_.dataRoot;
    const QString runtimePath = options_.runtimeLibraryPath;
    const bool verifyRuntime = options_.verifyRuntimeIntegrity;
    const QString runtimeHash = VocalSeparationCatalog::directMlRuntime().sha256;
    verificationPurpose_ = purpose;
    verifyingModelId_ = model == nullptr ? QString() : model->id;
    const quint64 generation = ++verificationGeneration_;
    verificationWatcher_ = new QFutureWatcher<VerificationResult>(this);
    QFutureWatcher<VerificationResult>* const watcher = verificationWatcher_;
    connect(watcher, &QFutureWatcher<VerificationResult>::finished,
            this, [this, watcher, generation] {
        const VerificationResult result = watcher->result();
        watcher->deleteLater();
        if (verificationWatcher_ == watcher) verificationWatcher_ = nullptr;
        if (generation != verificationGeneration_) {
            refreshModels();
            return;
        }
        finishVerification(generation, result);
    });
    refreshModels();
    verificationWatcher_->setFuture(QtConcurrent::run(
        [catalog, dataRoot, runtimePath, verifyRuntime, runtimeHash] {
            VerificationResult result;
            for (const VocalModelCard& candidate : catalog) {
                bool verified = !candidate.files.isEmpty();
                for (const VocalDownloadFile& file : candidate.files) {
                    const QString path = QDir(dataRoot).filePath(
                        QStringLiteral("models/%1/%2")
                            .arg(candidate.id, file.fileName));
                    if (VocalSeparationInstaller::isVerifiedFile(file, path)) {
                        result.verifiedFiles.insert(QFileInfo(path).absoluteFilePath());
                    } else {
                        verified = false;
                    }
                }
                if (verified) result.verifiedModels.insert(candidate.id);
            }
            result.runtimeVerified = QFileInfo(runtimePath).isFile()
                && (!verifyRuntime
                    || VocalSeparationInstaller::runtimeDirectoryIsVerified(
                        QFileInfo(runtimePath).absolutePath(), runtimeHash));
            return result;
        }));
    return true;
}

void VocalSeparationController::finishVerification(
    quint64 generation, const VerificationResult& result)
{
    if (generation != verificationGeneration_) return;
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
            downloadingModelId_.clear();
            refreshModels();
            return;
        }
        for (const VocalDownloadFile& file : model->files) {
            const QString destination = QDir(modelDirectory(model->id))
                .filePath(file.fileName);
            if (!result.verifiedFiles.contains(
                    QFileInfo(destination).absoluteFilePath())) {
                downloadQueue_.push_back({file, destination, false});
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
                true});
        }
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
    for (const VocalDownloadFile& file : model->files) {
        modelFiles.push_back(
            QDir(modelDirectory(model->id)).filePath(file.fileName));
    }
    QJsonArray requestedStems;
    for (const QString& name : context.stemNames) requestedStems.push_back(name);
    const QJsonObject payload{
        {QStringLiteral("runtimePath"), options_.runtimeLibraryPath},
        {QStringLiteral("inputPath"), context.inputPath},
        {QStringLiteral("modelFiles"), modelFiles},
        {QStringLiteral("outputDirectory"), context.outputRoot},
        {QStringLiteral("baseName"),
         QFileInfo(context.inputPath).completeBaseName()
             + QLatin1Char('-') + context.modelId},
        {QStringLiteral("extension"), context.outputFormat},
        {QStringLiteral("stems"), requestedStems},
        {QStringLiteral("device"), deviceName(context.device)},
    };
    if (!process_.startJob(payload)) return false;
    setJobState(JobState::Running, QStringLiteral("starting"));
    return true;
}

void VocalSeparationController::refreshModels()
{
    models_.clear();
    for (const VocalModelCard& model : options_.catalog) {
        ModelState state = modelInstalled(model) ? ModelState::Installed
            : modelFilesPresent(model)
                && !verifiedOrRejectedModelIds_.contains(model.id)
                ? ModelState::Verifying : ModelState::NotInstalled;
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
            case VocalDownloadState::Failed: state = ModelState::Failed; break;
            default: break;
            }
        }
        QVariantList kinds;
        for (const QString& name : model.stems) kinds.push_back(int(stemKind(name)));
        qint64 totalBytes = 0;
        for (const VocalDownloadFile& file : model.files) totalBytes += file.bytes;
        models_.push_back(QVariantMap{
            {QStringLiteral("id"), model.id},
            {QStringLiteral("family"), model.family == VocalModelFamily::Mdx
                 ? QStringLiteral("mdx") : QStringLiteral("demucs")},
            {QStringLiteral("stems"), kinds},
            {QStringLiteral("state"), int(state)},
            {QStringLiteral("bytes"), totalBytes},
            {QStringLiteral("provenance"), model.provenance},
            {QStringLiteral("resourceGuidance"), model.resourceGuidance},
        });
    }
    emit modelsChanged();
}

void VocalSeparationController::rebuildStems()
{
    stems_.clear();
    const VocalModelCard* model = selectedModel();
    if (model != nullptr) {
        for (const QString& name : model->stems) {
            const StemKind kind = stemKind(name);
            stems_.push_back(QVariantMap{
                {QStringLiteral("kind"), int(kind)},
                {QStringLiteral("name"), name},
                {QStringLiteral("supported"), true},
                {QStringLiteral("selected"), kind == StemKind::Vocals
                     || kind == StemKind::Accompaniment},
                {QStringLiteral("derived"), kind == StemKind::Accompaniment
                     && model->family == VocalModelFamily::Demucs},
                {QStringLiteral("available"), false},
                {QStringLiteral("path"), QString()},
                {QStringLiteral("waveform"), QVariantList{}},
            });
        }
    }
    emit stemsChanged();
}

void VocalSeparationController::setJobState(JobState state, const QString& stage)
{
    const bool changed = jobState_ != state || stage_ != stage;
    jobState_ = state;
    stage_ = stage;
    if (changed) emit jobStateChanged();
}

void VocalSeparationController::setError(const QString& error)
{
    if (error_ == error) return;
    error_ = error;
    emit errorChanged();
}

void VocalSeparationController::startNextDownload()
{
    if (downloadQueue_.isEmpty()) {
        if (!downloadingModelId_.isEmpty())
            verifiedModelIds_.insert(downloadingModelId_);
        if (!downloadingModelId_.isEmpty())
            verifiedOrRejectedModelIds_.insert(downloadingModelId_);
        downloadingModelId_.clear();
        progress_ = 1.0;
        emit progressChanged();
        refreshModels();
        return;
    }
    const DownloadItem& item = downloadQueue_.first();
    progress_ = 0.0;
    emit progressChanged();
    if (!QDir().mkpath(QFileInfo(item.destination).absolutePath())) {
        downloadQueue_.clear();
        setError(tr("无法创建模型下载目录"));
        refreshModels();
        return;
    }
    downloader_->start(item.file, item.destination);
}

void VocalSeparationController::handleProbe(const QJsonObject& payload)
{
    const QString reason = payload.value(QStringLiteral("gpuReason")).toString();
    availableDevices_[0] = QVariantMap{
        {QStringLiteral("mode"), int(DeviceMode::CPU)},
        {QStringLiteral("name"), QStringLiteral("CPU")},
        {QStringLiteral("available"), payload.value(QStringLiteral("cpu")).toBool()},
        {QStringLiteral("reason"), QString()},
    };
    availableDevices_[1] = QVariantMap{
        {QStringLiteral("mode"), int(DeviceMode::GPU)},
        {QStringLiteral("name"), QStringLiteral("DirectML")},
        {QStringLiteral("available"), payload.value(QStringLiteral("gpu")).toBool()},
        {QStringLiteral("reason"), reason},
    };
    emit availableDevicesChanged();
    activeRequest_.reset();
    failedRequest_.reset();
    setJobState(JobState::Idle, QStringLiteral("ready"));
}

void VocalSeparationController::handleResult(const QJsonObject& payload)
{
    if (!activeRequest_.has_value()
        || activeRequest_->kind != RequestKind::Separation) {
        setError(tr("Worker 结果没有匹配的活动请求"));
        setJobState(JobState::Failed, QStringLiteral("verification"));
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
        {QStringLiteral("modelId"), context.modelId},
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
        QVariantMap gpu = availableDevices_.at(1).toMap();
        gpu.insert(QStringLiteral("available"), false);
        gpu.insert(QStringLiteral("reason"), fallbackReason);
        availableDevices_[1] = gpu;
        emit availableDevicesChanged();
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
    if (waveformQueue_.isEmpty()) return;
    const WaveformWork work = waveformQueue_.first();
    if (work.path != path || work.trackId != trackId
        || work.resultGeneration != resultGeneration_) return;
    waveformQueue_.removeFirst();
    analyzeNextWaveform();
}

void VocalSeparationController::clearPublishedResult()
{
    if (waveformProvider_ != nullptr && !waveformQueue_.isEmpty())
        waveformProvider_->cancelForTrack(waveformQueue_.first().path);
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
        if (stem.value(QStringLiteral("selected")).toBool())
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
        if (stem.value(QStringLiteral("selected")).toBool())
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
    QStringList unresolved;
    for (const QString& path : paths) {
        const int row = library_->indexForLocalFile(path);
        if (row >= 0) {
            const QString trackId = library_->data(
                library_->index(row), LibraryModel::TrackIdRole).toString();
            if (!playlists_->containsTrack(playlistId, trackId)) {
                if (!playlists_->addTrack(playlistId, trackId)) {
                    finishPlaylistOperation(false,
                        tr("无法将分离结果加入播放列表"));
                    return false;
                }
                playlistOperation_->addedTrackIds.push_back(trackId);
            }
        } else {
            unresolved.push_back(path);
        }
    }
    if (unresolved.isEmpty()) {
        finishPlaylistOperation(true, {});
        return true;
    }
    connect(importer_, &ImportController::finished, this,
            [this, unresolved] {
        if (!playlistOperation_.has_value() || library_ == nullptr
            || playlists_ == nullptr) return;
        const QString playlistId = playlistOperation_->playlistId;
        for (const QString& path : unresolved) {
            if (!safeExistingFileWithin(path, publishedOutputRoot_)) {
                finishPlaylistOperation(false,
                    tr("导入期间分离结果已变得不可用"));
                return;
            }
            const int row = library_->indexForLocalFile(path);
            if (row < 0) {
                finishPlaylistOperation(false,
                    tr("音轨导入失败，播放列表没有保留部分结果"));
                return;
            }
            const QString trackId = library_->data(
                library_->index(row), LibraryModel::TrackIdRole).toString();
            if (!playlists_->containsTrack(playlistId, trackId)) {
                if (!playlists_->addTrack(playlistId, trackId)) {
                    finishPlaylistOperation(false,
                        tr("无法将全部分离结果加入播放列表"));
                    return;
                }
                playlistOperation_->addedTrackIds.push_back(trackId);
            }
        }
        finishPlaylistOperation(true, {});
    }, Qt::SingleShotConnection);
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

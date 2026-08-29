#include "vocal_separation_controller.hpp"

#include "audio_preview_controller.hpp"
#include "import_controller.hpp"
#include "library_model.hpp"
#include "playlist_model.hpp"
#include "vocal_separation_installer.hpp"
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

enum class SafePathKind { Missing, RegularFile, Directory, Unsafe };

QString absoluteCleanPath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

#ifdef Q_OS_WIN
QString extendedNativePath(const QString& path)
{
    QString native = QDir::toNativeSeparators(absoluteCleanPath(path));
    if (native.startsWith(QStringLiteral("\\\\?\\"))) return native;
    if (native.startsWith(QStringLiteral("\\\\")))
        return QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
    return QStringLiteral("\\\\?\\") + native;
}

SafePathKind safePathKind(const QString& path)
{
    const QString native = extendedNativePath(path);
    const DWORD attributes = GetFileAttributesW(
        reinterpret_cast<LPCWSTR>(native.utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
            ? SafePathKind::Missing : SafePathKind::Unsafe;
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return SafePathKind::Unsafe;
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0
        ? SafePathKind::Directory : SafePathKind::RegularFile;
}
#else
SafePathKind safePathKind(const QString& path)
{
    const QFileInfo info(path);
    if (info.isSymbolicLink()) return SafePathKind::Unsafe;
    if (!info.exists()) return SafePathKind::Missing;
    if (info.isFile()) return SafePathKind::RegularFile;
    if (info.isDir()) return SafePathKind::Directory;
    return SafePathKind::Unsafe;
}
#endif

bool samePath(const QString& left, const QString& right)
{
#ifdef Q_OS_WIN
    return absoluteCleanPath(left).compare(absoluteCleanPath(right),
                                           Qt::CaseInsensitive) == 0;
#else
    return absoluteCleanPath(left) == absoluteCleanPath(right);
#endif
}

bool lexicallyWithin(const QString& path, const QString& directory)
{
    QString root = QDir::fromNativeSeparators(absoluteCleanPath(directory));
    const QString candidate = QDir::fromNativeSeparators(absoluteCleanPath(path));
    if (!root.endsWith(QLatin1Char('/'))) root += QLatin1Char('/');
#ifdef Q_OS_WIN
    return candidate.startsWith(root, Qt::CaseInsensitive);
#else
    return candidate.startsWith(root, Qt::CaseSensitive);
#endif
}

bool safeExistingPathWithin(const QString& path, const QString& directory,
                            SafePathKind finalKind)
{
    if (safePathKind(directory) != SafePathKind::Directory
        || !lexicallyWithin(path, directory)) return false;
    const QString relative = QDir(directory).relativeFilePath(path);
    const QStringList parts = QDir::fromNativeSeparators(relative).split(
        QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty() || parts.contains(QStringLiteral(".."))) return false;
    QString current = absoluteCleanPath(directory);
    for (qsizetype index = 0; index < parts.size(); ++index) {
        current = QDir(current).filePath(parts.at(index));
        const SafePathKind expected = index + 1 == parts.size()
            ? finalKind : SafePathKind::Directory;
        if (safePathKind(current) != expected) return false;
    }
    QString root = QDir(directory).canonicalPath();
    QString candidate = QFileInfo(path).canonicalFilePath();
    if (root.isEmpty() || candidate.isEmpty()) return false;
    root = QDir::fromNativeSeparators(root);
    candidate = QDir::fromNativeSeparators(candidate);
    if (!root.endsWith(QLatin1Char('/'))) root += QLatin1Char('/');
#ifdef Q_OS_WIN
    return candidate.startsWith(root, Qt::CaseInsensitive);
#else
    return candidate.startsWith(root, Qt::CaseSensitive);
#endif
}

bool safeExistingFileWithin(const QString& path, const QString& directory)
{
    return safeExistingPathWithin(path, directory, SafePathKind::RegularFile);
}

bool safeExistingFile(const QString& path)
{
    return safePathKind(path) == SafePathKind::RegularFile;
}

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
        setError(message);
        setJobState(JobState::Failed, QStringLiteral("error"));
    });
    connect(&process_, &SeparationProcessClient::cancelled, this, [this] {
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
            runtimeInstallerWatcher_ = new QFutureWatcher<VocalInstallResult>(this);
            connect(runtimeInstallerWatcher_,
                    &QFutureWatcher<VocalInstallResult>::finished, this, [this] {
                const VocalInstallResult installed = runtimeInstallerWatcher_->result();
                runtimeInstallerWatcher_->deleteLater();
                runtimeInstallerWatcher_ = nullptr;
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
            runtimeInstallerWatcher_->setFuture(QtConcurrent::run(
                [archive, root] {
                    return VocalSeparationInstaller::installDirectMlRuntime(
                        archive, root);
                }));
            return;
        }
        startNextDownload();
    });
    if (waveformProvider_ != nullptr) {
        connect(waveformProvider_, &WaveformProvider::waveformReady,
                this, &VocalSeparationController::handleWaveform);
    }
}

VocalSeparationController::~VocalSeparationController() = default;

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
        || (verificationWatcher_ != nullptr && verifyingModelId_ == modelId))
        return false;
    const QString directory = modelDirectory(modelId);
    const QString modelsRoot = QDir(options_.dataRoot).filePath(
        QStringLiteral("models"));
    if (QFileInfo::exists(directory)
        && !safeExistingPathWithin(directory, modelsRoot,
                                   SafePathKind::Directory)) return false;
    for (const VocalDownloadFile& file : model->files) {
        const QString path = QDir(directory).filePath(file.fileName);
        if (QFileInfo::exists(path) && !QFile::remove(path)) return false;
        QFile::remove(VocalSeparationInstaller::partPath(path));
    }
    QDir().rmdir(directory);
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
    return true;
}

bool VocalSeparationController::selectOutputDirectory(const QUrl& directory)
{
    if (requestInFlight()) return false;
    const QString path = QFileInfo(directory.toLocalFile()).absoluteFilePath();
    if (path.isEmpty() || (!QDir(path).exists() && !QDir().mkpath(path))) return false;
    outputDirectory_ = path;
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
    if (model == nullptr || !QFileInfo(inputPath).isFile() || stemNames.isEmpty()) {
        setError(tr("请选择有效输入音频和至少一个输出音轨"));
        return false;
    }
    if (!QDir().mkpath(outputDirectory_)) {
        setError(tr("无法创建分离输出目录"));
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
    activeRequest_ = std::move(context);
    progress_ = 0.0;
    emit progressChanged();
    setError({});
    setJobState(JobState::Running, QStringLiteral("model_verification"));
    if (beginVerification(VerificationPurpose::Start, model)) return true;
    activeRequest_.reset();
    setJobState(JobState::Idle, {});
    return false;
}

void VocalSeparationController::cancel()
{
    if (jobState_ != JobState::Running && jobState_ != JobState::Probing) return;
    if (verificationWatcher_ != nullptr
        && (verificationPurpose_ == VerificationPurpose::Start
            || verificationPurpose_ == VerificationPurpose::Probe)) {
        ++verificationGeneration_;
        verificationPurpose_ = VerificationPurpose::None;
        verifyingModelId_.clear();
        setJobState(JobState::Cancelled, QStringLiteral("cancelled"));
        refreshModels();
        return;
    }
    setJobState(JobState::Cancelling, QStringLiteral("cancelling"));
    process_.cancel();
}

bool VocalSeparationController::retry()
{
    if (jobState_ != JobState::Failed || !process_.retryLast()) return false;
    progress_ = 0.0;
    emit progressChanged();
    setError({});
    const bool probe = activeRequest_.has_value()
        && activeRequest_->kind == RequestKind::Probe;
    setJobState(probe ? JobState::Probing : JobState::Running,
                probe ? QStringLiteral("provider_probe")
                      : QStringLiteral("starting"));
    return true;
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
    return atomicCopyNoOverwrite(source, target);
}

bool VocalSeparationController::exportSelected(const QUrl& destinationDirectory)
{
    const QString destination = destinationDirectory.toLocalFile();
    if (!QFileInfo(destination).isDir()) return false;
    bool exported = false;
    for (const StemKind kind : selectedStemKinds()) {
        const QString source = pathForStem(kind);
        if (!safeExistingFileWithin(source, publishedOutputRoot_)
            || !atomicCopyNoOverwrite(source,
                QDir(destination).filePath(QFileInfo(source).fileName()))) return false;
        exported = true;
    }
    return exported;
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
        if (safeExistingFileWithin(path, publishedOutputRoot_)) paths.push_back(path);
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
    connect(verificationWatcher_, &QFutureWatcher<VerificationResult>::finished,
            this, [this, generation] {
        const VerificationResult result = verificationWatcher_->result();
        verificationWatcher_->deleteLater();
        verificationWatcher_ = nullptr;
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
            setError(tr("ONNX Runtime 尚未安装或校验失败"));
            setJobState(JobState::Failed, QStringLiteral("runtime_verification"));
        }
        return;
    }
    if (purpose == VerificationPurpose::Start) {
        if (!activeRequest_.has_value()
            || !result.verifiedModels.contains(activeRequest_->modelId)
            || !result.runtimeVerified
            || !launchSeparation(*activeRequest_)) {
            setError(tr("所选模型或 ONNX Runtime 尚未完成校验安装"));
            setJobState(JobState::Failed, QStringLiteral("model_verification"));
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
    const QJsonArray outputs = payload.value(QStringLiteral("outputs")).toArray();
    if (outputs.size() != context.stemKinds.size()) {
        setError(tr("Worker 返回的输出数量无效"));
        setJobState(JobState::Failed, QStringLiteral("verification"));
        return;
    }
    QStringList verifiedPaths;
    verifiedPaths.reserve(outputs.size());
    for (qsizetype index = 0; index < outputs.size(); ++index) {
        const QString path = QFileInfo(outputs.at(index).toString()).absoluteFilePath();
        const QFileInfo file(path);
        if (!file.isFile() || file.size() <= 0
            || !safeExistingFileWithin(path, context.outputRoot)) {
            setError(tr("Worker 输出不存在、为空或超出输出目录"));
            setJobState(JobState::Failed, QStringLiteral("verification"));
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
        setError(tr("无法保存分离历史记录"));
        setJobState(JobState::Failed, QStringLiteral("history"));
        return;
    }
    history_ = historyStore_.load();
    emit historyChanged();
    waveformQueue_.clear();
    waveformKinds_.clear();
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
        waveformQueue_.push_back(path);
        waveformKinds_.insert(path, kind);
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
    analyzeNextWaveform();
}

void VocalSeparationController::analyzeNextWaveform()
{
    if (waveformProvider_ != nullptr && !waveformQueue_.isEmpty()) {
        waveformProvider_->loadForTrack(waveformQueue_.first());
    }
}

void VocalSeparationController::handleWaveform(
    const QString& path, const QVariantMap& layers)
{
    if (!waveformKinds_.contains(path)) return;
    const StemKind kind = waveformKinds_.take(path);
    for (QVariant& value : stems_) {
        QVariantMap stem = value.toMap();
        if (stem.value(QStringLiteral("kind")).toInt() != int(kind)) continue;
        stem.insert(QStringLiteral("waveform"),
                    boundedPeaks(layers.value(QStringLiteral("mix")).toList()));
        value = stem;
        break;
    }
    waveformQueue_.removeAll(path);
    emit stemsChanged();
    analyzeNextWaveform();
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
        || playlistId.isEmpty() || importer_->busy()) return false;
    QStringList unresolved;
    bool added = false;
    for (const QString& path : paths) {
        const int row = library_->indexForLocalFile(path);
        if (row >= 0) {
            if (!playlists_->addTrack(
                playlistId,
                library_->data(library_->index(row), LibraryModel::TrackIdRole).toString())) {
                return false;
            }
            added = true;
        } else {
            unresolved.push_back(path);
        }
    }
    if (unresolved.isEmpty()) return added;
    connect(importer_, &ImportController::finished, this,
            [this, unresolved, playlistId] {
        if (library_ == nullptr || playlists_ == nullptr) return;
        for (const QString& path : unresolved) {
            const int row = library_->indexForLocalFile(path);
            if (row < 0 || !playlists_->addTrack(
                    playlistId,
                    library_->data(library_->index(row), LibraryModel::TrackIdRole)
                        .toString())) {
                setError(tr("无法将分离结果加入播放列表"));
            }
        }
    }, Qt::SingleShotConnection);
    importer_->importPaths(unresolved);
    return true;
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
    const QString& source, const QString& destination)
{
    if (!QFileInfo(source).isFile() || QFileInfo::exists(destination)) return false;
    const QFileInfo target(destination);
    if (!QDir().mkpath(target.absolutePath())) return false;
    QFile input(source);
    QTemporaryFile temporary(QDir(target.absolutePath()).filePath(
        QStringLiteral(".agplayer-export-XXXXXX")));
    temporary.setAutoRemove(false);
    if (!input.open(QIODevice::ReadOnly) || !temporary.open()) return false;
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

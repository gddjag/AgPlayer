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

#include <algorithm>
#include <cmath>
#include <utility>

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

bool isWithinDirectory(const QString& path, const QString& directory)
{
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
        options.runtimeLibraryPath = QDir(options.dataRoot).filePath(
            QStringLiteral("runtime/%1/onnxruntime.dll")
                .arg(VocalSeparationCatalog::directMlRuntime().id));
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
            const VocalInstallResult installed =
                VocalSeparationInstaller::installDirectMlRuntime(
                    completed.destination, runtimeDirectory());
            if (!installed.ok) {
                downloadQueue_.clear();
                setError(installed.error);
                refreshModels();
                return;
            }
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
    if (jobState_ == JobState::Running || jobState_ == JobState::Cancelling) return false;
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
        || downloader_->state() == VocalDownloadState::Downloading
        || downloader_->state() == VocalDownloadState::Paused) {
        return false;
    }
    downloadingModelId_ = modelId;
    for (const VocalDownloadFile& file : model->files) {
        const QString destination = QDir(modelDirectory(modelId)).filePath(file.fileName);
        if (!VocalSeparationInstaller::isVerifiedFile(file, destination)) {
            downloadQueue_.push_back({file, destination, false});
        }
    }
    if (!runtimeReady()) {
        const VocalRuntimePackage package = VocalSeparationCatalog::directMlRuntime();
        const VocalDownloadFile archive{QStringLiteral("runtime.nupkg"), package.url,
                                        package.bytes, package.sha256};
        downloadQueue_.push_back(
            {archive, QDir(options_.dataRoot).filePath(QStringLiteral("downloads/runtime.nupkg")), true});
    }
    if (downloadQueue_.isEmpty()) {
        refreshModels();
        return true;
    }
    setError({});
    startNextDownload();
    return true;
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
    if (model == nullptr || jobState_ == JobState::Running || downloadActive)
        return false;
    const QString directory = modelDirectory(modelId);
    for (const VocalDownloadFile& file : model->files) {
        const QString path = QDir(directory).filePath(file.fileName);
        if (QFileInfo::exists(path) && !QFile::remove(path)) return false;
        QFile::remove(VocalSeparationInstaller::partPath(path));
    }
    QDir().rmdir(directory);
    if (downloadingModelId_ == modelId) downloadingModelId_.clear();
    refreshModels();
    return true;
}

bool VocalSeparationController::selectModel(const QString& modelId)
{
    if (modelForId(modelId) == nullptr || jobState_ == JobState::Running) return false;
    if (selectedModelId_ == modelId) return true;
    selectedModelId_ = modelId;
    emit selectedModelIdChanged();
    rebuildStems();
    return true;
}

bool VocalSeparationController::setStemSelected(StemKind kind, bool selected)
{
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
    if (jobState_ == JobState::Running || jobState_ == JobState::Cancelling) return false;
    if (deviceMode_ == mode) return true;
    deviceMode_ = mode;
    emit deviceModeChanged();
    return true;
}

bool VocalSeparationController::selectOutputFormat(const QString& format)
{
    const QString normalized = format.trimmed().toLower();
    if (!QStringList{QStringLiteral("wav"), QStringLiteral("flac"),
                     QStringLiteral("mp3")}.contains(normalized)) return false;
    outputFormat_ = normalized;
    return true;
}

bool VocalSeparationController::selectOutputDirectory(const QUrl& directory)
{
    const QString path = QFileInfo(directory.toLocalFile()).absoluteFilePath();
    if (path.isEmpty() || (!QDir(path).exists() && !QDir().mkpath(path))) return false;
    outputDirectory_ = path;
    return true;
}

bool VocalSeparationController::probeDevices()
{
    if (!runtimeReady()) {
        setError(tr("ONNX Runtime 尚未安装"));
        return false;
    }
    if (!process_.startProbe(
            {{QStringLiteral("runtimePath"), options_.runtimeLibraryPath}})) return false;
    lastRequestWasProbe_ = true;
    setError({});
    setJobState(JobState::Probing, QStringLiteral("provider_probe"));
    return true;
}

bool VocalSeparationController::start()
{
    const VocalModelCard* model = selectedModel();
    const QString inputPath = inputInfo_.value(QStringLiteral("path")).toString();
    const QStringList stemNames = selectedStemNames();
    if (model == nullptr || !modelInstalled(*model)) {
        setError(tr("所选分离模型尚未完成校验安装"));
        return false;
    }
    if (!runtimeReady()) {
        setError(tr("ONNX Runtime 尚未安装"));
        return false;
    }
    if (!QFileInfo(inputPath).isFile() || stemNames.isEmpty()) {
        setError(tr("请选择有效输入音频和至少一个输出音轨"));
        return false;
    }
    if (!QDir().mkpath(outputDirectory_)) {
        setError(tr("无法创建分离输出目录"));
        return false;
    }
    QJsonArray modelFiles;
    for (const VocalDownloadFile& file : model->files) {
        modelFiles.push_back(QDir(modelDirectory(model->id)).filePath(file.fileName));
    }
    QJsonArray requestedStems;
    for (const QString& name : stemNames) requestedStems.push_back(name);
    const QJsonObject payload{
        {QStringLiteral("runtimePath"), options_.runtimeLibraryPath},
        {QStringLiteral("inputPath"), inputPath},
        {QStringLiteral("modelFiles"), modelFiles},
        {QStringLiteral("outputDirectory"), outputDirectory_},
        {QStringLiteral("baseName"), QFileInfo(inputPath).completeBaseName()
                                         + QLatin1Char('-') + model->id},
        {QStringLiteral("extension"), outputFormat_},
        {QStringLiteral("stems"), requestedStems},
        {QStringLiteral("device"), deviceName(deviceMode_)},
    };
    if (!process_.startJob(payload)) return false;
    lastRequestWasProbe_ = false;
    activeStemKinds_ = selectedStemKinds();
    progress_ = 0.0;
    emit progressChanged();
    setError({});
    setJobState(JobState::Running, QStringLiteral("starting"));
    return true;
}

void VocalSeparationController::cancel()
{
    if (jobState_ != JobState::Running) return;
    setJobState(JobState::Cancelling, QStringLiteral("cancelling"));
    process_.cancel();
}

bool VocalSeparationController::retry()
{
    if (jobState_ != JobState::Failed || !process_.retryLast()) return false;
    progress_ = 0.0;
    emit progressChanged();
    setError({});
    setJobState(lastRequestWasProbe_ ? JobState::Probing : JobState::Running,
                lastRequestWasProbe_ ? QStringLiteral("provider_probe")
                                     : QStringLiteral("starting"));
    return true;
}

bool VocalSeparationController::previewInput()
{
    return preview_ != nullptr
        && preview_->switchSourcePreservingPosition(QUrl::fromLocalFile(
            inputInfo_.value(QStringLiteral("path")).toString()));
}

bool VocalSeparationController::previewStem(StemKind kind)
{
    const QString path = pathForStem(kind);
    return preview_ != nullptr && QFileInfo(path).isFile()
        && preview_->switchSourcePreservingPosition(QUrl::fromLocalFile(path));
}

bool VocalSeparationController::exportStem(StemKind kind,
                                           const QUrl& destination)
{
    const QString source = pathForStem(kind);
    if (!QFileInfo(source).isFile()) return false;
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
        if (!QFileInfo(source).isFile()
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
    return QFileInfo(path).isFile() && addPathsToPlaylist({path}, playlistId);
}

bool VocalSeparationController::addSelectedToPlaylist(const QString& playlistId)
{
    QStringList paths;
    for (const StemKind kind : selectedStemKinds()) {
        const QString path = pathForStem(kind);
        if (QFileInfo(path).isFile()) paths.push_back(path);
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
    for (const VocalDownloadFile& file : model.files) {
        if (!VocalSeparationInstaller::isVerifiedFile(
                file, QDir(modelDirectory(model.id)).filePath(file.fileName))) return false;
    }
    return !model.files.isEmpty();
}

bool VocalSeparationController::runtimeReady() const
{
    if (!QFileInfo(options_.runtimeLibraryPath).isFile()) return false;
    return !options_.verifyRuntimeIntegrity
        || VocalSeparationInstaller::runtimeDirectoryIsVerified(
            QFileInfo(options_.runtimeLibraryPath).absolutePath(),
            VocalSeparationCatalog::directMlRuntime().sha256);
}

void VocalSeparationController::refreshModels()
{
    models_.clear();
    for (const VocalModelCard& model : options_.catalog) {
        ModelState state = modelInstalled(model) ? ModelState::Installed
                                                 : ModelState::NotInstalled;
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
        downloadingModelId_.clear();
        progress_ = 1.0;
        emit progressChanged();
        refreshModels();
        return;
    }
    const DownloadItem& item = downloadQueue_.first();
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
    const QJsonArray outputs = payload.value(QStringLiteral("outputs")).toArray();
    if (outputs.size() != activeStemKinds_.size()) {
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
            || !isWithinDirectory(path, outputDirectory_)) {
            setError(tr("Worker 输出不存在、为空或超出输出目录"));
            setJobState(JobState::Failed, QStringLiteral("verification"));
            return;
        }
        verifiedPaths.push_back(path);
    }
    QVariantList historyStems;
    waveformQueue_.clear();
    waveformKinds_.clear();
    for (qsizetype index = 0; index < verifiedPaths.size(); ++index) {
        const QString& path = verifiedPaths.at(index);
        const StemKind kind = activeStemKinds_.at(index);
        historyStems.push_back(QVariantMap{{QStringLiteral("kind"), int(kind)},
                                           {QStringLiteral("path"), path},
                                           {QStringLiteral("available"), true}});
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
    const QString fallbackReason = payload.value(QStringLiteral("fallbackReason")).toString();
    if (!fallbackReason.isEmpty()) {
        QVariantMap gpu = availableDevices_.at(1).toMap();
        gpu.insert(QStringLiteral("available"), false);
        gpu.insert(QStringLiteral("reason"), fallbackReason);
        availableDevices_[1] = gpu;
        emit availableDevicesChanged();
    }
    const QVariantMap record{
        {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("inputPath"), inputInfo_.value(QStringLiteral("path"))},
        {QStringLiteral("modelId"), selectedModelId_},
        {QStringLiteral("provider"), payload.value(QStringLiteral("provider"))},
        {QStringLiteral("fallbackReason"), fallbackReason},
        {QStringLiteral("stems"), historyStems},
    };
    if (historyStore_.append(record)) {
        history_ = historyStore_.load();
        emit historyChanged();
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
    for (const QString& path : paths) {
        const int row = library_->indexForLocalFile(path);
        if (row >= 0) {
            playlists_->addTrack(
                playlistId,
                library_->data(library_->index(row), LibraryModel::TrackIdRole).toString());
        } else {
            unresolved.push_back(path);
        }
    }
    if (unresolved.isEmpty()) return true;
    connect(importer_, &ImportController::finished, this,
            [this, unresolved, playlistId] {
        if (library_ == nullptr || playlists_ == nullptr) return;
        for (const QString& path : unresolved) {
            const int row = library_->indexForLocalFile(path);
            if (row >= 0) playlists_->addTrack(
                playlistId,
                library_->data(library_->index(row), LibraryModel::TrackIdRole).toString());
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

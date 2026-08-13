#include "voice_clone_controller.hpp"

#include "voice_clone_capability_schema.hpp"
#include "voice_clone_package_manager.hpp"
#include "voice_clone_registry.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace agplayer::voice_clone {
namespace {

bool isReparsePoint(const QFileInfo& info)
{
#ifdef Q_OS_WIN
    const std::wstring path = QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString();
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return info.isSymLink();
#endif
}

bool removeTreeNoLinks(const QString& path)
{
    const QFileInfo root(path);
    if (!root.exists() && !root.isSymLink()) return true;
    if (isReparsePoint(root)) return root.isDir() ? QDir().rmdir(path) : QFile::remove(path);
    if (!root.isDir()) return QFile::remove(path);
    const QDir directory(path);
    const auto entries = directory.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System
                                                  | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        if (!removeTreeNoLinks(entry.absoluteFilePath())) return false;
    }
    return QDir().rmdir(path);
}

bool hasReparseAncestor(const QString& path)
{
    QString current = QFileInfo(path).absoluteFilePath();
    while (!current.isEmpty()) {
        const QFileInfo info(current);
        if ((info.exists() || info.isSymLink()) && isReparsePoint(info)) return true;
        const QString parent = info.dir().absolutePath();
        if (parent == current) break;
        current = parent;
    }
    return false;
}

bool isSafeContainedDirectory(const QString& root, const QString& candidate)
{
    const QString canonicalRoot = QDir::fromNativeSeparators(QFileInfo(root).canonicalFilePath());
    const QString canonicalCandidate = QDir::fromNativeSeparators(QFileInfo(candidate).canonicalFilePath());
    if (canonicalRoot.isEmpty() || canonicalCandidate.isEmpty()) return false;
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    if (canonicalCandidate != canonicalRoot
        && !canonicalCandidate.startsWith(canonicalRoot + QLatin1Char('/'), sensitivity)) return false;
    QString current = canonicalRoot;
    const QString relative = QDir(canonicalRoot).relativeFilePath(canonicalCandidate);
    for (const QString& component : relative.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        current = QDir(current).filePath(component);
        const QFileInfo info(current);
        if (!info.exists() || !info.isDir() || isReparsePoint(info)) return false;
    }
    return !isReparsePoint(QFileInfo(canonicalRoot));
}

QVariantMap parameterMap(const QJsonObject& object)
{
    return object.toVariantMap();
}

bool copyVerifiedPartToNewFile(const QString& partPath,
                               const QString& finalPath,
                               QString* error)
{
    QFile destination(finalPath);
#ifdef Q_OS_WIN
    const std::wstring nativePart = QDir::toNativeSeparators(partPath).toStdWString();
    const HANDLE source = CreateFileW(nativePart.c_str(), GENERIC_READ | DELETE,
                                      FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
                                      nullptr);
    if (source == INVALID_HANDLE_VALUE) {
        *error = QStringLiteral("Could not open the generated temporary WAV handle");
        return false;
    }
    FILE_ATTRIBUTE_TAG_INFO tagInfo{};
    LARGE_INTEGER before{};
    if (!GetFileInformationByHandleEx(source, FileAttributeTagInfo, &tagInfo, sizeof(tagInfo))
        || (tagInfo.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0
        || !GetFileSizeEx(source, &before) || before.QuadPart <= 0) {
        CloseHandle(source);
        *error = QStringLiteral("Generated temporary WAV is not a regular non-reparse file");
        return false;
    }
    if (!destination.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        CloseHandle(source);
        *error = QStringLiteral("Could not create the generated WAV exclusively");
        return false;
    }
    qint64 copied = 0;
    QByteArray chunk(64 * 1024, Qt::Uninitialized);
    bool ok = true;
    while (true) {
        DWORD read = 0;
        if (!ReadFile(source, chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr)) {
            ok = false;
            break;
        }
        if (read == 0) break;
        if (destination.write(chunk.constData(), static_cast<qint64>(read))
            != static_cast<qint64>(read)) {
            ok = false;
            break;
        }
        copied += read;
    }
    LARGE_INTEGER after{};
    ok = ok && GetFileSizeEx(source, &after) && before.QuadPart == after.QuadPart
         && copied == before.QuadPart && destination.flush();
    if (ok) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        ok = SetFileInformationByHandle(source, FileDispositionInfo,
                                        &disposition, sizeof(disposition)) != 0;
    }
    destination.close();
    CloseHandle(source);
#else
    const QByteArray nativePart = QFile::encodeName(partPath);
    const int fd = ::open(nativePart.constData(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        *error = QStringLiteral("Could not open the generated temporary WAV handle");
        return false;
    }
    struct stat before{};
    if (::fstat(fd, &before) != 0 || !S_ISREG(before.st_mode) || before.st_size <= 0) {
        ::close(fd);
        *error = QStringLiteral("Generated temporary WAV is not a regular non-link file");
        return false;
    }
    QFile source;
    if (!source.open(fd, QIODevice::ReadOnly, QFileDevice::DontCloseHandle)
        || !destination.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        source.close();
        ::close(fd);
        *error = QStringLiteral("Could not open generated WAV files safely");
        return false;
    }
    qint64 copied = 0;
    bool ok = true;
    while (!source.atEnd()) {
        const QByteArray chunk = source.read(64 * 1024);
        if (chunk.isEmpty() && source.error() != QFile::NoError) {
            ok = false;
            break;
        }
        if (destination.write(chunk) != chunk.size()) {
            ok = false;
            break;
        }
        copied += chunk.size();
    }
    struct stat after{};
    ok = ok && ::fstat(fd, &after) == 0 && before.st_dev == after.st_dev
         && before.st_ino == after.st_ino && before.st_size == after.st_size
         && copied == before.st_size && destination.flush();
    source.close();
    destination.close();
    if (ok) ok = ::unlink(nativePart.constData()) == 0;
    ::close(fd);
#endif
    if (!ok) {
        destination.close();
        QFile::remove(finalPath);
        *error = QStringLiteral("Generated WAV changed or could not be copied atomically");
        return false;
    }
    return true;
}

} // namespace

VoiceCloneController::VoiceCloneController(QString pluginRoot,
                                           QString modelsRoot,
                                           VoiceClonePackageManager* licenseManager,
                                           QObject* parent)
    : QObject(parent),
      pluginRoot_(QDir::fromNativeSeparators(QFileInfo(pluginRoot).canonicalFilePath())),
      modelsRoot_(QDir::fromNativeSeparators(QFileInfo(modelsRoot).canonicalFilePath())),
      licenseManager_(licenseManager)
{
    const QFileInfo pluginInfo(pluginRoot_);
    const QFileInfo modelsInfo(modelsRoot_);
    if (pluginRoot_.isEmpty() || modelsRoot_.isEmpty()
        || !pluginInfo.isDir() || !modelsInfo.isDir()
        || hasReparseAncestor(pluginInfo.absoluteFilePath())
        || hasReparseAncestor(modelsInfo.absoluteFilePath())) {
        setError(QStringLiteral("Voice-clone plugin and model roots must be safe existing directories"));
        return;
    }
    registryPath_ = QDir(pluginRoot_).filePath(QStringLiteral("registry/models.json"));
    outputRoot_ = QDir(pluginRoot_).filePath(QStringLiteral("cache"));
    QDir().mkpath(QDir(outputRoot_).filePath(QStringLiteral("requests")));
    outputRoot_ = QDir::fromNativeSeparators(QFileInfo(outputRoot_).canonicalFilePath());
    if (!isSafeContainedDirectory(pluginRoot_, outputRoot_)) {
        outputRoot_.clear();
        setError(QStringLiteral("Voice-clone output root failed containment checks"));
        return;
    }
    connect(&worker_, &VoiceCloneWorkerClient::readyChanged,
            this, &VoiceCloneController::workerReadyChanged);
    connect(&worker_, &VoiceCloneWorkerClient::responseReceived,
            this, &VoiceCloneController::handleResponse);
    connect(&worker_, &VoiceCloneWorkerClient::requestFailed,
            this, &VoiceCloneController::handleFailure);
    connect(&worker_, &VoiceCloneWorkerClient::workerTerminated, this, [this](const QString&) {
        loadRequestId_.clear();
        cancelTargets_.clear();
        const auto ids = generations_.keys();
        for (const QString& id : ids) cleanupGeneration(id);
        retryPendingCleanup();
        if (modelLoaded_) {
            modelLoaded_ = false;
            emit modelLoadedChanged();
        }
    });
    refreshModels();
}

VoiceCloneController::~VoiceCloneController()
{
    shutdown();
}

bool VoiceCloneController::configureAdapter(const QString& adapterId,
                                            const QString& adapterVersion,
                                            const QString& launcherId)
{
    static const QRegularExpression identifier(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$"));
    if (!identifier.match(adapterId).hasMatch()
        || !identifier.match(adapterVersion).hasMatch()) {
        setError(QStringLiteral("Adapter identity is invalid"));
        return false;
    }
    const QString adapterPackRoot = QDir(pluginRoot_).filePath(
        QStringLiteral("adapters/%1/%2").arg(adapterId, adapterVersion));
    const QString manifestPath = QDir(adapterPackRoot).filePath(QStringLiteral("adapter.json"));
    if (hasReparseAncestor(adapterPackRoot) || hasReparseAncestor(manifestPath)) {
        setError(QStringLiteral("Adapter Pack paths must not contain links or reparse points"));
        return false;
    }
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("Could not read Adapter manifest"));
        return false;
    }
    const auto parsed = parseAdapterManifest(file.readAll());
    if (!parsed.isValid()) {
        setError(parsed.error);
        return false;
    }
    if (parsed.manifest.adapterId != adapterId
        || parsed.manifest.adapterVersion != adapterVersion) {
        setError(QStringLiteral("Installed Adapter manifest identity mismatch"));
        return false;
    }
    const QString selectedLauncher = launcherId.isEmpty() ? parsed.manifest.defaultLauncherId : launcherId;
    const auto resolved = resolveAdapterLauncher(parsed.manifest, selectedLauncher, adapterPackRoot);
    if (!resolved.isValid()) {
        setError(resolved.error);
        return false;
    }
    adapterManifest_ = parsed.manifest;
    launcher_ = resolved;
    adapterPackRoot_ = adapterPackRoot;
    setError({});
    return true;
}

bool VoiceCloneController::selectModel(const QString& stableId)
{
    if (!loadRequestId_.isEmpty()) {
        worker_.abandonRequest(loadRequestId_);
        loadRequestId_.clear();
    }
    auto selected = modelEntries_.constEnd();
    for (auto it = modelEntries_.constBegin(); it != modelEntries_.constEnd(); ++it) {
        if (it->stableId == stableId) {
            selected = it;
            break;
        }
    }
    if (selected == modelEntries_.constEnd() || selected->modelDirectory.isEmpty()) {
        setError(QStringLiteral("Selected model or model root is invalid"));
        return false;
    }
    const QFileInfo rootInfo(selected->modelDirectory);
    if (!rootInfo.exists() || !rootInfo.isDir()
        || !isSafeContainedDirectory(modelsRoot_, rootInfo.absoluteFilePath())) {
        setError(QStringLiteral("Selected model directory was not confirmed by refresh"));
        return false;
    }
    if (!adapterManifest_.adapterId.isEmpty() && selected->adapterId != adapterManifest_.adapterId) {
        setError(QStringLiteral("Selected model does not match the configured Adapter"));
        return false;
    }
    selectedModel_ = *selected;
    selectedModelRoot_ = selected->modelDirectory;
    modelLoaded_ = false;
    emit modelLoadedChanged();
    setError({});
    return !selectedModelRoot_.isEmpty();
}

void VoiceCloneController::refreshModels()
{
    const VoiceCloneRegistry builtIn = VoiceCloneRegistry::loadBuiltIn(registryPath_);
    if (!builtIn.isValid()) {
        setError(builtIn.errorString());
        return;
    }
    const QStringList knownAdapters{QStringLiteral("qwen"),
                                    QStringLiteral("indextts25"),
                                    QStringLiteral("cosyvoice3")};
    const QString portableRoot = QDir(modelsRoot_).absoluteFilePath(QStringLiteral("../.."));
    const VoiceCloneDiscovery discovered = VoiceCloneRegistry::discoverUserModels(portableRoot, knownAdapters);
    VoiceCloneDiscovery validDiscovery;
    validDiscovery.models = discovered.models;
    const VoiceCloneRegistry merged = builtIn.mergeUserModels(validDiscovery);
    if (!merged.isValid()) {
        setError(merged.errorString());
        return;
    }
    modelEntries_.clear();
    models_.clear();
    for (const QString& id : merged.modelIds()) {
        const VoiceCloneModel model = merged.model(id);
        modelEntries_.append(model);
        models_.append(QVariantMap{{QStringLiteral("stableId"), model.stableId},
                                   {QStringLiteral("displayName"), model.displayName},
                                   {QStringLiteral("adapterId"), model.adapterId},
                                   {QStringLiteral("installState"), model.installState.isEmpty()
                                                                        ? QStringLiteral("built-in")
                                                                        : model.installState}});
    }
    for (const VoiceCloneDiagnostic& diagnostic : discovered.diagnostics) {
        models_.append(QVariantMap{{QStringLiteral("stableId"), QString{}},
                                   {QStringLiteral("displayName"),
                                    QFileInfo(diagnostic.manifestPath).dir().dirName()},
                                   {QStringLiteral("adapterId"), QString{}},
                                   {QStringLiteral("installState"), QStringLiteral("invalid")},
                                   {QStringLiteral("manifestPath"), diagnostic.manifestPath},
                                   {QStringLiteral("diagnostic"), diagnostic.message}});
    }
    emit modelsChanged();
}

bool VoiceCloneController::openModelDirectory()
{
    if (selectedModelRoot_.isEmpty()) return false;
    return QDesktopServices::openUrl(QUrl::fromLocalFile(selectedModelRoot_));
}

bool VoiceCloneController::startWorker()
{
    if (launcher_.absolutePath.isEmpty() || selectedModelRoot_.isEmpty() || outputRoot_.isEmpty()) {
        setError(QStringLiteral("Adapter and model must be selected before starting the Worker"));
        return false;
    }
    const bool started = worker_.start(adapterManifest_, launcher_, adapterPackRoot_,
                                       selectedModelRoot_, outputRoot_,
                                       requestTimeoutMs_);
    if (!started) setError(worker_.errorString());
    return started;
}

bool VoiceCloneController::restartWorker()
{
    retryPendingCleanup();
    modelLoaded_ = false;
    emit modelLoadedChanged();
    const bool restarted = worker_.restart();
    if (!restarted) setError(worker_.errorString());
    return restarted;
}

bool VoiceCloneController::loadModel()
{
    if (!worker_.isReady() || selectedModelRoot_.isEmpty()) return false;
    loadRequestId_ = worker_.sendRequest(WorkerOperation::Load,
                                         {{QStringLiteral("modelRoot"), selectedModelRoot_},
                                          {QStringLiteral("parameters"), QJsonObject{}}});
    return !loadRequestId_.isEmpty();
}

bool VoiceCloneController::unloadModel()
{
    if (!worker_.isReady()) return false;
    return !worker_.sendRequest(WorkerOperation::Unload).isEmpty();
}

QString VoiceCloneController::generate(const QString& text,
                                       const QString& referenceAudioPath,
                                       const QJsonObject& parameters)
{
    if (!worker_.isReady() || !modelLoaded_) {
        setError(QStringLiteral("Worker and model must be ready before generation"));
        return {};
    }
    const auto parameterValidation = validateParameters(liveSchema_, parameters);
    if (!parameterValidation.isValid()) {
        setError(parameterValidation.errorString());
        return {};
    }
    if (selectedModel_.requiresLicenseAcceptance
        && (!licenseManager_
            || !licenseManager_->hasLicenseAcceptance(selectedModel_.stableId,
                                                      selectedModel_.adapterId,
                                                      QUrl(selectedModel_.license.url),
                                                      selectedModel_.licenseRevision))) {
        setError(QStringLiteral("Model license acceptance is required before generation"));
        return {};
    }
    const QString requestId = QStringLiteral("g-%1")
                                  .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString relativeDirectory = QStringLiteral("requests/%1").arg(requestId);
    const QString directory = QDir(outputRoot_).filePath(relativeDirectory);
    if (!QDir().mkpath(directory) || !isSafeContainedDirectory(outputRoot_, directory)) {
        setError(QStringLiteral("Could not create a safe generation directory"));
        return {};
    }
    const QString finalPath = QDir(directory).filePath(QStringLiteral("generated.wav"));
    if (QFileInfo::exists(finalPath)) {
        removeTreeNoLinks(directory);
        setError(QStringLiteral("Could not reserve a unique output file"));
        return {};
    }
    const QString partialPath = finalPath + QStringLiteral(".part");
    generations_.insert(requestId, {directory, partialPath, finalPath});
    QJsonObject payload{{QStringLiteral("text"), text},
                        {QStringLiteral("outputPath"),
                         relativeDirectory + QStringLiteral("/generated.wav.part")},
                        {QStringLiteral("parameters"), parameters}};
    if (!referenceAudioPath.isEmpty())
        payload.insert(QStringLiteral("referenceAudioPath"), referenceAudioPath);
    if (worker_.sendRequest(WorkerOperation::Generate, payload, requestId).isEmpty()) {
        cleanupGeneration(requestId);
        setError(worker_.errorString());
        return {};
    }
    setError({});
    return requestId;
}

bool VoiceCloneController::cancel(const QString& requestId)
{
    if (!generations_.contains(requestId) || !worker_.hasPendingRequest(requestId)) return false;
    const QString cancelId = worker_.sendRequest(
        WorkerOperation::Cancel, {{QStringLiteral("targetRequestId"), requestId}});
    if (cancelId.isEmpty()) {
        worker_.abandonRequest(requestId);
        worker_.shutdown();
        cleanupGeneration(requestId);
        retryPendingCleanup();
        return false;
    }
    worker_.abandonRequest(requestId);
    cancelTargets_.insert(cancelId, requestId);
    return true;
}

void VoiceCloneController::shutdown()
{
    worker_.shutdown();
    loadRequestId_.clear();
    cancelTargets_.clear();
    const auto ids = generations_.keys();
    for (const QString& id : ids) cleanupGeneration(id);
    retryPendingCleanup();
    if (modelLoaded_) {
        modelLoaded_ = false;
        emit modelLoadedChanged();
    }
}

void VoiceCloneController::setRequestTimeoutMs(const int timeoutMs)
{
    if (timeoutMs > 0) requestTimeoutMs_ = timeoutMs;
}

bool VoiceCloneController::workerRunning() const { return worker_.isRunning(); }
bool VoiceCloneController::workerReady() const { return worker_.isReady(); }
bool VoiceCloneController::modelLoaded() const { return modelLoaded_; }
bool VoiceCloneController::hasPendingRequest(const QString& requestId) const { return worker_.hasPendingRequest(requestId); }
QString VoiceCloneController::requestDirectory(const QString& requestId) const
{
    if (pendingCleanup_.contains(requestId)) return pendingCleanup_.value(requestId);
    return generations_.contains(requestId)
               ? generations_.value(requestId).directory
               : QDir(outputRoot_).filePath(QStringLiteral("requests/%1").arg(requestId));
}
QString VoiceCloneController::errorString() const { return error_; }
QVariantList VoiceCloneController::basicParameters() const { return basicParameters_; }
QVariantList VoiceCloneController::advancedParameters() const { return advancedParameters_; }
bool VoiceCloneController::advancedSettingsAvailable() const { return !advancedParameters_.isEmpty(); }
QVariantList VoiceCloneController::models() const { return models_; }
int VoiceCloneController::pendingCleanupCount() const { return pendingCleanup_.size(); }

void VoiceCloneController::handleResponse(const VoiceCloneWorkerMessage& message)
{
    if (message.operation == WorkerOperation::Capabilities) {
        updateCapabilities(message.payload.value(QStringLiteral("schema")).toObject());
    } else if (message.operation == WorkerOperation::Load) {
        if (message.requestId != loadRequestId_) return;
        const bool loaded = message.payload.value(QStringLiteral("loaded")).toBool();
        if (message.requestId == loadRequestId_) loadRequestId_.clear();
        if (modelLoaded_ != loaded) {
            modelLoaded_ = loaded;
            emit modelLoadedChanged();
        }
        updateSelectedModelStatus(loaded ? QStringLiteral("ready") : QStringLiteral("invalid"),
                                  loaded ? QString{} : QStringLiteral("Adapter did not load the model"));
    } else if (message.operation == WorkerOperation::Unload) {
        if (modelLoaded_) {
            modelLoaded_ = false;
            emit modelLoadedChanged();
        }
    } else if (message.operation == WorkerOperation::Cancel) {
        const QString target = cancelTargets_.take(message.requestId);
        cleanupGeneration(target);
        if (pendingCleanup_.contains(target)) {
            worker_.shutdown();
            retryPendingCleanup();
        }
    } else if (message.operation == WorkerOperation::Generate) {
        QString output;
        QString error;
        if (!finalizeGeneration(message.requestId, &output, &error)) {
            handleFailure(message.requestId, QStringLiteral("unsafe-output"), error);
            return;
        }
        emit generationFinished(message.requestId, output);
    }
}

void VoiceCloneController::handleFailure(const QString& requestId,
                                         const QString& code,
                                         const QString& message)
{
    const QString cancelTarget = cancelTargets_.take(requestId);
    if (!cancelTarget.isEmpty()) cleanupGeneration(cancelTarget);
    cleanupGeneration(requestId);
    if (requestId == loadRequestId_) {
        loadRequestId_.clear();
        updateSelectedModelStatus(QStringLiteral("invalid"), message);
    }
    if (pendingCleanup_.contains(requestId) || pendingCleanup_.contains(cancelTarget)) {
        worker_.shutdown();
        retryPendingCleanup();
    }
    setError(message);
    emit requestFailed(requestId, code, message);
}

void VoiceCloneController::setError(const QString& error)
{
    if (error_ == error) return;
    error_ = error;
    emit errorChanged();
}

void VoiceCloneController::updateCapabilities(const QJsonObject& schema)
{
    const auto validation = validateCapabilitySchema(schema);
    if (!validation.isValid()) {
        setError(validation.errorString());
        return;
    }
    liveSchema_ = schema;
    basicParameters_.clear();
    advancedParameters_.clear();
    for (const QJsonValue& value : schema.value(QStringLiteral("parameters")).toArray()) {
        const QJsonObject control = value.toObject();
        if (control.value(QStringLiteral("group")).toString() == QStringLiteral("advanced"))
            advancedParameters_.append(parameterMap(control));
        else
            basicParameters_.append(parameterMap(control));
    }
    emit capabilitiesChanged();
}

bool VoiceCloneController::finalizeGeneration(const QString& requestId,
                                              QString* outputPath,
                                              QString* error)
{
    if (!generations_.contains(requestId)) {
        *error = QStringLiteral("Unknown generation request");
        return false;
    }
    const GenerationFiles files = generations_.take(requestId);
    const QFileInfo directoryInfo(files.directory);
    if (!directoryInfo.exists() || !isSafeContainedDirectory(outputRoot_, files.directory)
        || QFileInfo::exists(files.finalPath)) {
        cleanupDirectory(requestId, files.directory);
        *error = QStringLiteral("Worker output failed the exclusive-output safety check");
        return false;
    }
    if (!copyVerifiedPartToNewFile(files.partialPath, files.finalPath, error)) {
        cleanupDirectory(requestId, files.directory);
        return false;
    }
    if (!isSafeContainedDirectory(outputRoot_, files.directory)
        || isReparsePoint(QFileInfo(files.finalPath))) {
        cleanupDirectory(requestId, files.directory);
        *error = QStringLiteral("Generated WAV commit was not safe");
        return false;
    }
    *outputPath = files.finalPath;
    return true;
}

void VoiceCloneController::cleanupGeneration(const QString& requestId, const bool keepFinal)
{
    if (!generations_.contains(requestId)) return;
    const GenerationFiles files = generations_.take(requestId);
    QFile::remove(files.partialPath);
    if (!keepFinal) cleanupDirectory(requestId, files.directory);
}

void VoiceCloneController::cleanupDirectory(const QString& requestId, const QString& directory)
{
    if (!removeTreeNoLinks(directory)) {
        pendingCleanup_.insert(requestId, directory);
        setError(QStringLiteral("Temporary output cleanup is pending: %1").arg(directory));
    }
}

void VoiceCloneController::retryPendingCleanup()
{
    const auto ids = pendingCleanup_.keys();
    for (const QString& id : ids) {
        if (removeTreeNoLinks(pendingCleanup_.value(id))) pendingCleanup_.remove(id);
    }
    if (!pendingCleanup_.isEmpty())
        setError(QStringLiteral("Temporary output cleanup remains pending for %1 request(s)")
                     .arg(pendingCleanup_.size()));
}

void VoiceCloneController::updateSelectedModelStatus(const QString& state,
                                                     const QString& diagnostic)
{
    if (selectedModel_.stableId.isEmpty()) return;
    for (QVariant& value : models_) {
        QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("stableId")).toString() != selectedModel_.stableId) continue;
        row.insert(QStringLiteral("installState"), state);
        if (diagnostic.isEmpty())
            row.remove(QStringLiteral("diagnostic"));
        else
            row.insert(QStringLiteral("diagnostic"), diagnostic);
        value = row;
        emit modelsChanged();
        return;
    }
}

} // namespace agplayer::voice_clone

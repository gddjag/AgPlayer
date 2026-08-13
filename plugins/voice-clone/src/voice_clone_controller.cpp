#include "voice_clone_controller.hpp"

#include "voice_clone_capability_schema.hpp"
#include "voice_clone_package_manager.hpp"
#include "voice_clone_registry.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
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

} // namespace

VoiceCloneController::VoiceCloneController(QString portableRoot,
                                           QString builtInRegistryPath,
                                           VoiceClonePackageManager* licenseManager,
                                           QObject* parent)
    : QObject(parent),
      portableRoot_(QDir::fromNativeSeparators(QDir(portableRoot).absolutePath())),
      registryPath_(std::move(builtInRegistryPath)),
      licenseManager_(licenseManager)
{
    const QFileInfo portableInfo(portableRoot_);
    if (!portableInfo.exists() || !portableInfo.isDir()
        || hasReparseAncestor(portableInfo.absoluteFilePath())) {
        setError(QStringLiteral("Portable voice-clone root is not a safe directory"));
        return;
    }
    outputRoot_ = QDir(portableRoot_).filePath(QStringLiteral("cache/voice-clone"));
    QDir().mkpath(QDir(outputRoot_).filePath(QStringLiteral("requests")));
    outputRoot_ = QDir::fromNativeSeparators(QFileInfo(outputRoot_).canonicalFilePath());
    if (!isSafeContainedDirectory(portableRoot_, outputRoot_)) {
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
        const auto ids = generations_.keys();
        for (const QString& id : ids) cleanupGeneration(id);
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

bool VoiceCloneController::configureAdapter(const QString& manifestPath,
                                            const QString& adapterPackRoot,
                                            const QString& launcherId)
{
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

bool VoiceCloneController::selectModel(const QString& stableId, const QString& modelRoot)
{
    auto selected = modelEntries_.constEnd();
    for (auto it = modelEntries_.constBegin(); it != modelEntries_.constEnd(); ++it) {
        if (it->stableId == stableId) {
            selected = it;
            break;
        }
    }
    const QFileInfo rootInfo(modelRoot);
    if (selected == modelEntries_.constEnd() || !rootInfo.exists() || !rootInfo.isDir()) {
        setError(QStringLiteral("Selected model or model root is invalid"));
        return false;
    }
    if (!adapterManifest_.adapterId.isEmpty() && selected->adapterId != adapterManifest_.adapterId) {
        setError(QStringLiteral("Selected model does not match the configured Adapter"));
        return false;
    }
    selectedModel_ = *selected;
    selectedModelRoot_ = QDir::fromNativeSeparators(rootInfo.canonicalFilePath());
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
    const VoiceCloneDiscovery discovered = VoiceCloneRegistry::discoverUserModels(portableRoot_, knownAdapters);
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
                                                      selectedModel_.revision))) {
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
    QFile reservation(finalPath);
    if (!reservation.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        removeTreeNoLinks(directory);
        setError(QStringLiteral("Could not reserve a unique output file"));
        return {};
    }
    reservation.close();
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
    if (cancelId.isEmpty()) return false;
    worker_.abandonRequest(requestId);
    cleanupGeneration(requestId);
    return true;
}

void VoiceCloneController::shutdown()
{
    const auto ids = generations_.keys();
    for (const QString& id : ids) cleanupGeneration(id);
    worker_.shutdown();
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
    return generations_.contains(requestId)
               ? generations_.value(requestId).directory
               : QDir(outputRoot_).filePath(QStringLiteral("requests/%1").arg(requestId));
}
QString VoiceCloneController::errorString() const { return error_; }
QVariantList VoiceCloneController::basicParameters() const { return basicParameters_; }
QVariantList VoiceCloneController::advancedParameters() const { return advancedParameters_; }
bool VoiceCloneController::advancedSettingsAvailable() const { return !advancedParameters_.isEmpty(); }
QVariantList VoiceCloneController::models() const { return models_; }

void VoiceCloneController::handleResponse(const VoiceCloneWorkerMessage& message)
{
    if (message.operation == WorkerOperation::Capabilities) {
        updateCapabilities(message.payload.value(QStringLiteral("schema")).toObject());
    } else if (message.operation == WorkerOperation::Load) {
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
    cleanupGeneration(requestId);
    if (requestId == loadRequestId_) {
        loadRequestId_.clear();
        updateSelectedModelStatus(QStringLiteral("invalid"), message);
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
    const QFileInfo partialInfo(files.partialPath);
    const QFileInfo finalInfo(files.finalPath);
    if (!directoryInfo.exists() || !isSafeContainedDirectory(outputRoot_, files.directory)
        || !partialInfo.exists() || !partialInfo.isFile() || isReparsePoint(partialInfo)
        || isReparsePoint(finalInfo)) {
        removeTreeNoLinks(files.directory);
        *error = QStringLiteral("Worker output failed the reparse-point safety check");
        return false;
    }
    QFile source(files.partialPath);
    QSaveFile destination(files.finalPath);
    if (!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) {
        removeTreeNoLinks(files.directory);
        *error = QStringLiteral("Could not open the generated temporary WAV safely");
        return false;
    }
    while (!source.atEnd()) {
        const QByteArray chunk = source.read(64 * 1024);
        if (chunk.isEmpty() && source.error() != QFile::NoError) {
            destination.cancelWriting();
            removeTreeNoLinks(files.directory);
            *error = QStringLiteral("Could not read the generated temporary WAV");
            return false;
        }
        if (destination.write(chunk) != chunk.size()) {
            destination.cancelWriting();
            removeTreeNoLinks(files.directory);
            *error = QStringLiteral("Could not commit the generated WAV");
            return false;
        }
    }
    source.close();
    if (!destination.commit()
        || !isSafeContainedDirectory(outputRoot_, files.directory)
        || isReparsePoint(QFileInfo(files.finalPath))) {
        removeTreeNoLinks(files.directory);
        *error = QStringLiteral("Generated WAV commit was not safe");
        return false;
    }
    QFile::remove(files.partialPath);
    *outputPath = files.finalPath;
    return true;
}

void VoiceCloneController::cleanupGeneration(const QString& requestId, const bool keepFinal)
{
    if (!generations_.contains(requestId)) return;
    const GenerationFiles files = generations_.take(requestId);
    QFile::remove(files.partialPath);
    if (!keepFinal) removeTreeNoLinks(files.directory);
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

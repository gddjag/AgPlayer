#include "voice_clone_controller.hpp"

#include "voice_clone_capability_schema.hpp"
#include "voice_clone_package_manager.hpp"
#include "voice_clone_registry.hpp"
#include "voice_clone_runtime_package_manager.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>

#include <utility>

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

QJsonObject parameterDefaults(const QJsonObject& schema)
{
    QJsonObject defaults;
    for (const QJsonValue& value : schema.value(QStringLiteral("parameters")).toArray()) {
        const QJsonObject control = value.toObject();
        defaults.insert(control.value(QStringLiteral("key")).toString(),
                        control.value(QStringLiteral("default")));
    }
    return defaults;
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

bool copyPublishedResultSafely(const QString& sourcePath,
                               const QString& destinationPath,
                               QString* error)
{
    QSaveFile destination(destinationPath);
#ifdef Q_OS_WIN
    const std::wstring nativeSource = QDir::toNativeSeparators(sourcePath).toStdWString();
    const HANDLE source = CreateFileW(nativeSource.c_str(), GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
                                      nullptr);
    if (source == INVALID_HANDLE_VALUE) {
        *error = QStringLiteral("Could not open the generated result safely");
        return false;
    }
    FILE_ATTRIBUTE_TAG_INFO tagInfo{};
    LARGE_INTEGER before{};
    if (!GetFileInformationByHandleEx(source, FileAttributeTagInfo, &tagInfo, sizeof(tagInfo))
        || (tagInfo.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0
        || !GetFileSizeEx(source, &before) || before.QuadPart <= 0
        || !destination.open(QIODevice::WriteOnly)) {
        CloseHandle(source);
        *error = QStringLiteral("Generated result is not a regular readable file");
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
         && copied == before.QuadPart;
    CloseHandle(source);
#else
    const QByteArray nativeSource = QFile::encodeName(sourcePath);
    const int fd = ::open(nativeSource.constData(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        *error = QStringLiteral("Could not open the generated result safely");
        return false;
    }
    struct stat before{};
    QFile source;
    if (::fstat(fd, &before) != 0 || !S_ISREG(before.st_mode) || before.st_size <= 0
        || !source.open(fd, QIODevice::ReadOnly, QFileDevice::DontCloseHandle)
        || !destination.open(QIODevice::WriteOnly)) {
        source.close();
        ::close(fd);
        *error = QStringLiteral("Generated result is not a regular readable file");
        return false;
    }
    qint64 copied = 0;
    bool ok = true;
    while (!source.atEnd()) {
        const QByteArray chunk = source.read(64 * 1024);
        if ((chunk.isEmpty() && source.error() != QFile::NoError)
            || destination.write(chunk) != chunk.size()) {
            ok = false;
            break;
        }
        copied += chunk.size();
    }
    struct stat after{};
    ok = ok && ::fstat(fd, &after) == 0 && before.st_dev == after.st_dev
         && before.st_ino == after.st_ino && before.st_size == after.st_size
         && copied == before.st_size;
    source.close();
    ::close(fd);
#endif
    if (!ok || !destination.commit()) {
        destination.cancelWriting();
        *error = QStringLiteral("Generated result changed or could not be saved atomically");
        return false;
    }
    return true;
}

bool removePublishedResultSafely(const QString& path)
{
#ifdef Q_OS_WIN
    const std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
    const HANDLE file = CreateFileW(nativePath.c_str(), GENERIC_READ | DELETE,
                                    FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    FILE_ATTRIBUTE_TAG_INFO tagInfo{};
    LARGE_INTEGER size{};
    const bool regular = GetFileInformationByHandleEx(
                             file, FileAttributeTagInfo, &tagInfo, sizeof(tagInfo))
                         && (tagInfo.FileAttributes
                             & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0
                         && GetFileSizeEx(file, &size) && size.QuadPart > 0;
    FILE_DISPOSITION_INFO disposition{TRUE};
    const bool removed = regular
                         && SetFileInformationByHandle(file, FileDispositionInfo,
                                                       &disposition, sizeof(disposition)) != 0;
    CloseHandle(file);
    return removed;
#else
    const QByteArray nativePath = QFile::encodeName(path);
    const int fd = ::open(nativePath.constData(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return false;
    struct stat opened{};
    struct stat current{};
    const bool sameRegularFile = ::fstat(fd, &opened) == 0 && S_ISREG(opened.st_mode)
                                 && opened.st_size > 0
                                 && ::lstat(nativePath.constData(), &current) == 0
                                 && opened.st_dev == current.st_dev
                                 && opened.st_ino == current.st_ino;
    const bool removed = sameRegularFile && ::unlink(nativePath.constData()) == 0;
    ::close(fd);
    return removed;
#endif
}

} // namespace

VoiceCloneController::VoiceCloneController(QString pluginRoot,
                                           QString modelsRoot,
                                           VoiceClonePackageManager* licenseManager,
                                           const VoiceClonePackageValidationPolicy packageValidationPolicy,
                                           QObject* parent)
    : VoiceCloneController(std::move(pluginRoot), std::move(modelsRoot), licenseManager,
                           nullptr, packageValidationPolicy, parent)
{
}

VoiceCloneController::VoiceCloneController(
    QString pluginRoot, QString modelsRoot, VoiceClonePackageManager* licenseManager,
    VoiceCloneRuntimePackageManager* runtimeManager,
    const VoiceClonePackageValidationPolicy packageValidationPolicy, QObject* parent)
    : QObject(parent),
      pluginRoot_(QDir::fromNativeSeparators(QFileInfo(pluginRoot).canonicalFilePath())),
      modelsRoot_(QDir::fromNativeSeparators(QFileInfo(modelsRoot).canonicalFilePath())),
      licenseManager_(licenseManager),
      runtimeManager_(runtimeManager),
      packageValidationPolicy_(packageValidationPolicy)
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
    connect(&worker_, &VoiceCloneWorkerClient::readyChanged, this, [this] {
        emit workerReadyChanged();
        if (!activationInProgress_ || !worker_.isReady()
            || activationState_ != QStringLiteral("starting-worker")) {
            return;
        }
        setActivation(QStringLiteral("loading-model"),
                      QStringLiteral("Worker ready; loading selected model"));
        if (!loadModel()) {
            activationInProgress_ = false;
            const QString message = QStringLiteral("Worker was ready but the model could not be loaded");
            setError(message);
            setActivation(QStringLiteral("error"), message);
        }
    });
    connect(&worker_, &VoiceCloneWorkerClient::responseReceived,
            this, &VoiceCloneController::handleResponse);
    connect(&worker_, &VoiceCloneWorkerClient::requestFailed,
            this, &VoiceCloneController::handleFailure);
    connect(&worker_, &VoiceCloneWorkerClient::workerTerminated, this, [this](const QString& reason) {
        loadRequestId_.clear();
        cancelTargets_.clear();
        const auto ids = generations_.keys();
        for (const QString& id : ids) cleanupGeneration(id);
        retryPendingCleanup();
        if (modelLoaded_) {
            modelLoaded_ = false;
            emit modelLoadedChanged();
        }
        if (activationInProgress_) {
            activationInProgress_ = false;
            setActivation(QStringLiteral("error"), reason);
            if (downloadPhase_ == QStringLiteral("probe")) emit downloadChanged();
        }
    });
    if (licenseManager_ != nullptr) {
        connect(licenseManager_, &VoiceClonePackageManager::stateChanged, this, [this] {
            emit downloadChanged();
            if (licenseManager_->state() == VoiceClonePackageManager::Failed) {
                setError(licenseManager_->errorString());
            }
            if (licenseManager_->state() == VoiceClonePackageManager::Completed) {
                refreshModels();
                if (downloadPhase_ == QStringLiteral("model")
                    && !pendingDownloadStableId_.isEmpty()) {
                    setDownloadPhase(QStringLiteral("probe"));
                    if (!activateModel(pendingDownloadStableId_)) emit downloadChanged();
                }
            }
        });
        connect(licenseManager_, &VoiceClonePackageManager::progressChanged,
                this, &VoiceCloneController::downloadChanged);
    }
    if (runtimeManager_ != nullptr) {
        connect(runtimeManager_, &VoiceCloneRuntimePackageManager::stateChanged, this, [this] {
            emit downloadChanged();
            if (runtimeManager_->state() == VoiceCloneRuntimePackageManager::Failed) {
                setError(runtimeManager_->errorString());
            } else if (runtimeManager_->state() == VoiceCloneRuntimePackageManager::Completed
                       && downloadPhase_ == QStringLiteral("runtime")) {
                startPendingModelDownload();
            }
        });
        connect(runtimeManager_, &VoiceCloneRuntimePackageManager::progressChanged,
                this, &VoiceCloneController::downloadChanged);
        connect(runtimeManager_, &VoiceCloneRuntimePackageManager::installedResolved, this,
                [this](const quint64 requestId, const QString& root, const QString& error) {
            if (requestId != pendingRuntimeResolutionId_) return;
            const PendingRuntimeAction action = pendingRuntimeAction_;
            pendingRuntimeResolutionId_ = 0;
            pendingRuntimeAction_ = PendingRuntimeAction::None;
            runtimeResolutionCanceled_ = false;
            emit downloadChanged();
            if (action == PendingRuntimeAction::DownloadModel) {
                if (!root.isEmpty()) {
                    selectedRuntimeRoot_ = root;
                    startPendingModelDownload();
                } else {
                    startPendingRuntimeDownload();
                }
                return;
            }
            if (action != PendingRuntimeAction::StartWorker) return;
            if (root.isEmpty()) {
                runtimeDownloadRequired_ = true;
                activationInProgress_ = false;
                emit activationChanged();
                setError(error);
                updateSelectedModelStatus(QStringLiteral("needs-download"), error);
                setActivation(QStringLiteral("needs-download"), error);
                return;
            }
            selectedRuntimeRoot_ = root;
            if (runtimeDownloadRequired_) {
                runtimeDownloadRequired_ = false;
                emit activationChanged();
            }
            if (!launchWorker() && activationInProgress_) {
                activationInProgress_ = false;
                setActivation(QStringLiteral("error"), errorString());
            }
        });
    }
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
    selectedRuntimeRoot_.clear();
    setError({});
    return true;
}

bool VoiceCloneController::activateModel(const QString& stableId)
{
    if (runtimeDownloadRequired_) {
        runtimeDownloadRequired_ = false;
        emit activationChanged();
    }
    liveSchema_ = {};
    basicParameters_.clear();
    advancedParameters_.clear();
    emit capabilitiesChanged();
    shutdown();
    adapterManifest_ = {};
    launcher_ = {};
    adapterPackRoot_.clear();
    setActivation(QStringLiteral("selecting"), QStringLiteral("Selecting model"));
    auto requested = modelEntries_.constEnd();
    for (auto it = modelEntries_.constBegin(); it != modelEntries_.constEnd(); ++it) {
        if (it->stableId == stableId) {
            requested = it;
            break;
        }
    }
    if (requested != modelEntries_.constEnd() && requested->modelDirectory.isEmpty()) {
        selectedModel_ = *requested;
        selectedModelRoot_.clear();
        if (modelLoaded_) {
            modelLoaded_ = false;
            emit modelLoadedChanged();
        }
        emit licenseChanged();
        const QString message = QStringLiteral(
            "Model files are not installed or not ready; download them before activation");
        setError(message);
        updateSelectedModelStatus(QStringLiteral("needs-download"), message);
        setActivation(QStringLiteral("needs-download"), message);
        return false;
    }
    if (!selectModel(stableId)) {
        setActivation(QStringLiteral("error"), error_);
        return false;
    }

    const QString versionsRoot = QDir(pluginRoot_).filePath(
        QStringLiteral("adapters/%1").arg(selectedModel_.adapterId));
    const QFileInfoList versions = QDir(versionsRoot).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    for (const QFileInfo& version : versions) {
        if (!QFileInfo::exists(QDir(version.absoluteFilePath()).filePath(
                QStringLiteral("adapter.json")))) {
            continue;
        }
        if (!configureAdapter(selectedModel_.adapterId, version.fileName())) continue;
        activationInProgress_ = true;
        setActivation(QStringLiteral("starting-worker"),
                      QStringLiteral("Starting model Worker"));
        if (startWorker()) return true;
        activationInProgress_ = false;
    }
    const QString message = QStringLiteral(
        "Adapter runtime is not installed or not ready; download it before using this model");
    setError(message);
    updateSelectedModelStatus(QStringLiteral("needs-download"), message);
    setActivation(QStringLiteral("needs-download"), message);
    return false;
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
    emit licenseChanged();
    setError({});
    return !selectedModelRoot_.isEmpty();
}

bool VoiceCloneController::acceptSelectedLicense()
{
    const auto required = approvedRequiredLicenses(selectedModel_.stableId,
                                                   selectedModel_.adapterId);
    if (required.size() != 1) {
        setError(QStringLiteral("Every required license must be accepted individually"));
        return false;
    }
    return acceptSelectedLicenses({required.front().id});
}

bool VoiceCloneController::acceptSelectedLicenses(const QStringList& licenseIds)
{
    if (!selectedModel_.requiresLicenseAcceptance || licenseManager_ == nullptr) {
        setError(QStringLiteral("The selected model has no approved license gate"));
        return false;
    }
    const auto required = approvedRequiredLicenses(selectedModel_.stableId,
                                                   selectedModel_.adapterId);
    QSet<QString> requested(licenseIds.cbegin(), licenseIds.cend());
    if (requested.size() != licenseIds.size() || requested.size() != required.size()) {
        setError(QStringLiteral("Every required license must be accepted individually"));
        return false;
    }
    for (const VoiceClonePackageLicense& license : required) {
        if (!requested.contains(license.id)) {
            setError(QStringLiteral("Every required license must be accepted individually"));
            return false;
        }
    }
    for (const VoiceClonePackageLicense& license : required) {
        if (!licenseManager_->acceptLicenseIdentity(selectedModel_.stableId,
                                                    selectedModel_.adapterId,
                                                    license.id, license.url,
                                                    license.revision)) {
            setError(QStringLiteral("Could not persist every model license acceptance"));
            return false;
        }
    }
    if (licenseManager_->state() == VoiceClonePackageManager::LicenseRequired
        && licenseManager_->currentManifest().modelId == selectedModel_.stableId
        && !licenseManager_->retry()) {
        setError(QStringLiteral("Could not continue the accepted model download"));
        return false;
    }
    setError({});
    emit licenseChanged();
    return true;
}

bool VoiceCloneController::acceptSelectedLicenseIdentity(const QString& modelId,
                                                         const QString& adapterId,
                                                         const QUrl& licenseUrl,
                                                         const QString& revision)
{
    if (!selectedModel_.requiresLicenseAcceptance || licenseManager_ == nullptr
        || modelId != selectedModel_.stableId || adapterId != selectedModel_.adapterId
        || licenseUrl != QUrl(selectedModel_.license.url)
        || revision != selectedModel_.licenseRevision) {
        setError(QStringLiteral("Selected license identity does not match the active model"));
        return false;
    }
    QString licenseId;
    for (const VoiceClonePackageLicense& license : approvedRequiredLicenses(modelId, adapterId)) {
        if (license.url == licenseUrl && license.revision == revision) {
            licenseId = license.id;
            break;
        }
    }
    if (licenseId.isEmpty()
        || !licenseManager_->acceptLicenseIdentity(modelId, adapterId, licenseId,
                                                   licenseUrl, revision)) {
        setError(QStringLiteral("Could not persist selected model license acceptance"));
        return false;
    }
    setError({});
    emit licenseChanged();
    return true;
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
                                   {QStringLiteral("description"), model.description},
                                   {QStringLiteral("provider"), model.source.provider},
                                   {QStringLiteral("adapterId"), model.adapterId},
                                   {QStringLiteral("capabilityPreview"), model.capabilityPreview},
                                   {QStringLiteral("licenseName"), model.license.name},
                                   {QStringLiteral("licenseUrl"), model.license.url},
                                   {QStringLiteral("officialProjectUrl"), model.officialProjectUrl},
                                   {QStringLiteral("huggingFaceUrl"), model.huggingFaceUrl},
                                   {QStringLiteral("modelScopeUrl"), model.modelScopeUrl},
                                   {QStringLiteral("requiresLicenseAcceptance"),
                                    model.requiresLicenseAcceptance},
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

bool VoiceCloneController::downloadModel(const QString& stableId)
{
    if (licenseManager_ == nullptr) {
        setError(QStringLiteral("Model package manager is unavailable"));
        return false;
    }
    const QHash<QString, QString> manifests{
        {QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base"),
         QStringLiteral("qwen3-tts-0.6b.json")},
        {QStringLiteral("Qwen/Qwen3-TTS-12Hz-1.7B-Base"),
         QStringLiteral("qwen3-tts-1.7b.json")},
        {QStringLiteral("IndexTeam/IndexTTS-2.5"),
         QStringLiteral("indextts-2.5.json")},
        {QStringLiteral("FunAudioLLM/Fun-CosyVoice3-0.5B-2512"),
         QStringLiteral("fun-cosyvoice3.json")}};
    const QHash<QString, QString> packageIds{
        {QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base"),
         QStringLiteral("qwen3-tts-0.6b")},
        {QStringLiteral("Qwen/Qwen3-TTS-12Hz-1.7B-Base"),
         QStringLiteral("qwen3-tts-1.7b")},
        {QStringLiteral("IndexTeam/IndexTTS-2.5"), QStringLiteral("indextts-2.5")},
        {QStringLiteral("FunAudioLLM/Fun-CosyVoice3-0.5B-2512"),
         QStringLiteral("fun-cosyvoice3")}};
    const QString fileName = manifests.value(stableId);
    if (fileName.isEmpty()) {
        setError(QStringLiteral("No approved download manifest exists for this model"));
        return false;
    }
    const QString manifestPath = QDir(pluginRoot_).filePath(
        QStringLiteral("registry/downloads/%1").arg(fileName));
    if (hasReparseAncestor(manifestPath)) {
        setError(QStringLiteral("Model download manifest path is unsafe"));
        return false;
    }
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("Could not read the model download manifest"));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(QStringLiteral("Model download manifest is invalid JSON"));
        return false;
    }
    const VoiceClonePackageManifest manifest = VoiceClonePackageManifest::fromJson(
        document.object(), packageValidationPolicy_);
    const bool valid = manifest.isValid(packageValidationPolicy_);
    const bool packageIdentityMatches = packageValidationPolicy_
                                            != VoiceClonePackageValidationPolicy::OfficialOnly
                                        || manifest.packageId == packageIds.value(stableId);
    if (!valid || manifest.modelId != stableId || !packageIdentityMatches) {
        setError(valid
                     ? QStringLiteral("Model download manifest identity mismatch")
                     : manifest.errorString(packageValidationPolicy_));
        return false;
    }
    auto downloadModel = modelEntries_.constEnd();
    for (auto it = modelEntries_.constBegin(); it != modelEntries_.constEnd(); ++it) {
        if (it->stableId == stableId) {
            downloadModel = it;
            break;
        }
    }
    if (downloadModel == modelEntries_.constEnd()) {
        setError(QStringLiteral("The download model is absent from the approved registry"));
        return false;
    }
    selectedModel_ = *downloadModel;
    selectedModelRoot_.clear();
    modelLoaded_ = false;
    emit modelLoadedChanged();
    emit licenseChanged();
    pendingDownloadManifest_ = manifest;
    pendingDownloadStableId_ = stableId;
    if (selectedModel_.requiresLicenseAcceptance
        && !licenseManager_->hasRequiredLicenseAcceptances(stableId, manifest.adapterId)) {
        setError(QStringLiteral(
            "Every required model license must be accepted before downloading Runtime or model files"));
        setDownloadPhase(QStringLiteral("license"));
        return false;
    }
    if (runtimeManager_ == nullptr) {
        setDownloadPhase(QStringLiteral("idle"));
        licenseManager_->start(manifest);
        if (licenseManager_->state() == VoiceClonePackageManager::Failed) {
            setError(licenseManager_->errorString());
            return false;
        }
        setError({});
        return true;
    }

    const QString versionsRoot = QDir(pluginRoot_).filePath(
        QStringLiteral("adapters/%1").arg(manifest.adapterId));
    const QFileInfoList versions = QDir(versionsRoot).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    bool configured = false;
    for (const QFileInfo& version : versions) {
        if (configureAdapter(manifest.adapterId, version.fileName())) {
            configured = true;
            break;
        }
    }
    if (!configured) {
        pendingDownloadStableId_.clear();
        setError(QStringLiteral("No verified Adapter Pack is available for this model"));
        return false;
    }
    setDownloadPhase(QStringLiteral("runtime"));
    pendingRuntimeAction_ = PendingRuntimeAction::DownloadModel;
    runtimeResolutionCanceled_ = false;
    pendingRuntimeResolutionId_ = runtimeManager_->resolveInstalledAsync(
        adapterManifest_.runtime.id, adapterManifest_.adapterId,
        adapterManifest_.adapterVersion, adapterManifest_.protocolVersion);
    setError({});
    return pendingRuntimeResolutionId_ > 0;
}

bool VoiceCloneController::startPendingRuntimeDownload()
{
    if (runtimeManager_ == nullptr) {
        setError(QStringLiteral("Runtime package manager is unavailable"));
        return false;
    }
    const QString feedPath = QDir(pluginRoot_).filePath(QStringLiteral("config/runtime-feed.json"));
    if (hasReparseAncestor(feedPath)) {
        setError(QStringLiteral("Runtime feed path is unsafe"));
        return false;
    }
    QFile feedFile(feedPath);
    if (!feedFile.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("Official Runtime Pack feed is not installed"));
        return false;
    }
    const auto feed = VoiceCloneRuntimeFeed::fromJson(
        feedFile.readAll(), runtimeManager_->validationPolicy());
    if (!feed.isValid() || !feed.enabled) {
        setError(feed.isValid() ? QStringLiteral("Official Runtime Pack feed is disabled")
                                : feed.error);
        return false;
    }
    QVector<VoiceCloneRuntimePackageManifest> matches;
    for (const auto& runtime : feed.runtimes) {
        if (runtime.runtimeId == adapterManifest_.runtime.id
            && runtime.supports(adapterManifest_.adapterId,
                                adapterManifest_.adapterVersion,
                                adapterManifest_.protocolVersion)) {
            matches.append(runtime);
        }
    }
    if (matches.size() != 1) {
        setError(QStringLiteral("Runtime feed does not contain exactly one compatible Runtime Pack"));
        return false;
    }
    runtimeManager_->start(matches.constFirst());
    if (runtimeManager_->state() == VoiceCloneRuntimePackageManager::Failed) {
        setError(runtimeManager_->errorString());
        return false;
    }
    setError({});
    return true;
}

bool VoiceCloneController::startPendingModelDownload()
{
    if (licenseManager_ == nullptr || pendingDownloadStableId_.isEmpty()
        || !pendingDownloadManifest_.isValid(packageValidationPolicy_)) {
        setError(QStringLiteral("Pending model package is unavailable"));
        return false;
    }
    setDownloadPhase(QStringLiteral("model"));
    licenseManager_->start(pendingDownloadManifest_);
    if (licenseManager_->state() == VoiceClonePackageManager::Failed) {
        setError(licenseManager_->errorString());
        return false;
    }
    setError({});
    return true;
}

bool VoiceCloneController::pauseDownload()
{
    if (downloadPhase_ == QStringLiteral("runtime"))
        return runtimeManager_ != nullptr && runtimeManager_->pause();
    return licenseManager_ != nullptr && licenseManager_->pause();
}

bool VoiceCloneController::resumeDownload()
{
    if (downloadPhase_ == QStringLiteral("runtime")) {
        if (runtimeManager_ == nullptr
            || runtimeManager_->state() != VoiceCloneRuntimePackageManager::Paused) return false;
        const bool resumed = runtimeManager_->retry();
        if (resumed) setError({});
        return resumed;
    }
    if (licenseManager_ == nullptr
        || licenseManager_->state() != VoiceClonePackageManager::Paused) {
        return false;
    }
    const bool resumed = licenseManager_->retry();
    if (resumed) setError({});
    return resumed;
}

bool VoiceCloneController::cancelDownload()
{
    if (downloadPhase_ == QStringLiteral("runtime")) {
        if (runtimeManager_ == nullptr) return false;
        const auto before = runtimeManager_->state();
        if (before == VoiceCloneRuntimePackageManager::Completed
            || before == VoiceCloneRuntimePackageManager::Canceled) return false;
        runtimeResolutionCanceled_ = pendingRuntimeResolutionId_ > 0;
        pendingRuntimeResolutionId_ = 0;
        pendingRuntimeAction_ = PendingRuntimeAction::None;
        runtimeManager_->cancel();
        return runtimeManager_->state() == VoiceCloneRuntimePackageManager::Canceled;
    }
    if (licenseManager_ == nullptr) return false;
    const auto before = licenseManager_->state();
    if (before == VoiceClonePackageManager::Idle
        || before == VoiceClonePackageManager::Completed
        || before == VoiceClonePackageManager::Canceled) {
        return false;
    }
    licenseManager_->cancel();
    return licenseManager_->state() == VoiceClonePackageManager::Canceled;
}

bool VoiceCloneController::retryDownload()
{
    if (downloadPhase_ == QStringLiteral("runtime")) {
        if (runtimeManager_ == nullptr) return false;
        if (runtimeResolutionCanceled_ && !pendingDownloadStableId_.isEmpty()) {
            pendingRuntimeAction_ = PendingRuntimeAction::DownloadModel;
            runtimeResolutionCanceled_ = false;
            pendingRuntimeResolutionId_ = runtimeManager_->resolveInstalledAsync(
                adapterManifest_.runtime.id, adapterManifest_.adapterId,
                adapterManifest_.adapterVersion, adapterManifest_.protocolVersion);
            if (pendingRuntimeResolutionId_ > 0) {
                setError({});
                emit downloadChanged();
                return true;
            }
            return false;
        }
        if (runtimeManager_->state() == VoiceCloneRuntimePackageManager::Idle
            && !pendingDownloadStableId_.isEmpty()) {
            return downloadModel(pendingDownloadStableId_);
        }
        if (runtimeManager_->state() != VoiceCloneRuntimePackageManager::Failed
            && runtimeManager_->state() != VoiceCloneRuntimePackageManager::Canceled) return false;
        const bool retried = runtimeManager_->retry();
        if (retried) setError({});
        return retried;
    }
    if (downloadPhase_ == QStringLiteral("probe")
        && !pendingDownloadStableId_.isEmpty()) {
        setDownloadPhase(QStringLiteral("probe"));
        return activateModel(pendingDownloadStableId_);
    }
    if (licenseManager_ == nullptr
        || (licenseManager_->state() != VoiceClonePackageManager::Failed
            && licenseManager_->state() != VoiceClonePackageManager::Canceled)) {
        return false;
    }
    const bool retried = licenseManager_->retry();
    if (retried) setError({});
    return retried;
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
    if (runtimeManager_ != nullptr) {
        pendingRuntimeAction_ = PendingRuntimeAction::StartWorker;
        runtimeResolutionCanceled_ = false;
        pendingRuntimeResolutionId_ = runtimeManager_->resolveInstalledAsync(
            adapterManifest_.runtime.id, adapterManifest_.adapterId,
            adapterManifest_.adapterVersion, adapterManifest_.protocolVersion);
        setError({});
        return pendingRuntimeResolutionId_ > 0;
    }
    return launchWorker();
}

bool VoiceCloneController::launchWorker()
{
    const bool started = selectedRuntimeRoot_.isEmpty()
                             ? worker_.start(adapterManifest_, launcher_, adapterPackRoot_,
                                             selectedModelRoot_, outputRoot_, requestTimeoutMs_)
                             : worker_.start(adapterManifest_, launcher_, adapterPackRoot_,
                                             selectedRuntimeRoot_, selectedModelRoot_, outputRoot_,
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
                                          {QStringLiteral("parameters"),
                                           parameterDefaults(liveSchema_)}});
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
            || !licenseManager_->hasRequiredLicenseAcceptances(selectedModel_.stableId,
                                                               selectedModel_.adapterId))) {
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

bool VoiceCloneController::saveResult(const QString& outputPath,
                                      const QString& destinationPath)
{
    const QString sourcePath = QDir::fromNativeSeparators(
        QFileInfo(outputPath).absoluteFilePath());
    const QString absoluteDestination = QDir::fromNativeSeparators(
        QFileInfo(destinationPath).absoluteFilePath());
    const QFileInfo sourceInfo(sourcePath);
    if (!publishedResults_.contains(sourcePath) || destinationPath.isEmpty()
        || !sourceInfo.isFile() || isReparsePoint(sourceInfo)
        || !isSafeContainedDirectory(outputRoot_, sourceInfo.dir().absolutePath())) {
        setError(QStringLiteral("Only a published generated result can be saved"));
        return false;
    }
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    if (sourcePath.compare(absoluteDestination, sensitivity) == 0) {
        setError(QStringLiteral("Save destination must differ from the managed result"));
        return false;
    }
    QString error;
    if (!copyPublishedResultSafely(sourcePath, absoluteDestination, &error)) {
        setError(error);
        return false;
    }
    setError({});
    return true;
}

bool VoiceCloneController::deleteResult(const QString& outputPath)
{
    const QString path = QDir::fromNativeSeparators(
        QFileInfo(outputPath).absoluteFilePath());
    const QFileInfo info(path);
    if (!publishedResults_.contains(path) || !info.isFile() || isReparsePoint(info)
        || !isSafeContainedDirectory(outputRoot_, info.dir().absolutePath())) {
        setError(QStringLiteral("Only a published generated result can be deleted"));
        return false;
    }
    if (!removePublishedResultSafely(path)) {
        setError(QStringLiteral("Could not delete the generated result"));
        return false;
    }
    publishedResults_.remove(path);
    QDir().rmdir(info.dir().absolutePath());
    setError({});
    return true;
}

QUrl VoiceCloneController::resultFileUrl(const QString& resultPath) const
{
    return QUrl::fromLocalFile(resultPath);
}

void VoiceCloneController::shutdown()
{
    activationInProgress_ = false;
    pendingRuntimeResolutionId_ = 0;
    pendingRuntimeAction_ = PendingRuntimeAction::None;
    runtimeResolutionCanceled_ = false;
    if (runtimeManager_ != nullptr) runtimeManager_->cancelInstalledResolution();
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
QString VoiceCloneController::activationState() const { return activationState_; }
QString VoiceCloneController::activationMessage() const { return activationMessage_; }
bool VoiceCloneController::licenseAcceptanceRequired() const
{
    if (!selectedModel_.requiresLicenseAcceptance) return false;
    return licenseManager_ == nullptr
           || !licenseManager_->hasRequiredLicenseAcceptances(
               selectedModel_.stableId, selectedModel_.adapterId);
}
QString VoiceCloneController::currentLicenseName() const { return selectedModel_.license.name; }
QUrl VoiceCloneController::currentLicenseUrl() const { return QUrl(selectedModel_.license.url); }
QString VoiceCloneController::currentLicenseRevision() const
{
    return selectedModel_.licenseRevision;
}
QVariantList VoiceCloneController::currentLicenseRequirements() const
{
    QVariantList result;
    for (const VoiceClonePackageLicense& license
         : approvedRequiredLicenses(selectedModel_.stableId, selectedModel_.adapterId)) {
        result.append(QVariantMap{{QStringLiteral("id"), license.id},
                                  {QStringLiteral("name"), license.name},
                                  {QStringLiteral("url"), license.url},
                                  {QStringLiteral("revision"), license.revision},
                                  {QStringLiteral("spdx"), license.spdx},
                                  {QStringLiteral("useRestriction"), license.useRestriction},
                                  {QStringLiteral("requiredAcceptance"),
                                   license.requiredAcceptance}});
    }
    return result;
}
QString VoiceCloneController::downloadState() const
{
    if (downloadPhase_ == QStringLiteral("license")) return QStringLiteral("license-required");
    if (downloadPhase_ == QStringLiteral("runtime") && pendingRuntimeResolutionId_ > 0)
        return QStringLiteral("resolving");
    if (downloadPhase_ == QStringLiteral("runtime") && runtimeManager_ != nullptr) {
        switch (runtimeManager_->state()) {
        case VoiceCloneRuntimePackageManager::Idle:
            return !pendingDownloadStableId_.isEmpty() && !error_.isEmpty()
                       ? QStringLiteral("failed") : QStringLiteral("idle");
        case VoiceCloneRuntimePackageManager::Resolving: return QStringLiteral("resolving");
        case VoiceCloneRuntimePackageManager::Downloading: return QStringLiteral("downloading");
        case VoiceCloneRuntimePackageManager::Paused: return QStringLiteral("paused");
        case VoiceCloneRuntimePackageManager::Verifying:
        case VoiceCloneRuntimePackageManager::Extracting: return QStringLiteral("verifying");
        case VoiceCloneRuntimePackageManager::Committing: return QStringLiteral("committing");
        case VoiceCloneRuntimePackageManager::Completed: return QStringLiteral("completed");
        case VoiceCloneRuntimePackageManager::Canceled: return QStringLiteral("canceled");
        case VoiceCloneRuntimePackageManager::Failed: return QStringLiteral("failed");
        }
    }
    if (downloadPhase_ == QStringLiteral("probe")) {
        if (modelLoaded_ && worker_.isReady()) return QStringLiteral("completed");
        if (activationState_ == QStringLiteral("error")
            || activationState_ == QStringLiteral("needs-download")) return QStringLiteral("failed");
        return QStringLiteral("verifying");
    }
    if (downloadPhase_ == QStringLiteral("ready")) return QStringLiteral("completed");
    if (licenseManager_ == nullptr) return QStringLiteral("unavailable");
    switch (licenseManager_->state()) {
    case VoiceClonePackageManager::Idle: return QStringLiteral("idle");
    case VoiceClonePackageManager::LicenseRequired: return QStringLiteral("license-required");
    case VoiceClonePackageManager::Resolving: return QStringLiteral("resolving");
    case VoiceClonePackageManager::Downloading: return QStringLiteral("downloading");
    case VoiceClonePackageManager::Paused: return QStringLiteral("paused");
    case VoiceClonePackageManager::Verifying: return QStringLiteral("verifying");
    case VoiceClonePackageManager::Committing: return QStringLiteral("committing");
    case VoiceClonePackageManager::Completed: return QStringLiteral("completed");
    case VoiceClonePackageManager::Canceled: return QStringLiteral("canceled");
    case VoiceClonePackageManager::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}
QString VoiceCloneController::downloadModelId() const
{
    if (!pendingDownloadStableId_.isEmpty()) return pendingDownloadStableId_;
    return licenseManager_ == nullptr ? QString{} : licenseManager_->currentManifest().modelId;
}
QString VoiceCloneController::downloadError() const
{
    if (downloadPhase_ == QStringLiteral("license")) return error_;
    if (downloadPhase_ == QStringLiteral("runtime") && runtimeManager_ != nullptr) {
        const QString managerError = runtimeManager_->errorString();
        return managerError.isEmpty() ? error_ : managerError;
    }
    if (downloadPhase_ == QStringLiteral("probe")) return error_;
    return licenseManager_ == nullptr ? QString{} : licenseManager_->errorString();
}
int VoiceCloneController::downloadProgressPercent() const
{
    if (downloadPhase_ == QStringLiteral("license")) return -1;
    if (downloadPhase_ == QStringLiteral("runtime") && pendingRuntimeResolutionId_ > 0) return -1;
    if (downloadPhase_ == QStringLiteral("runtime") && runtimeManager_ != nullptr)
        return runtimeManager_->progressPercent();
    if (downloadPhase_ == QStringLiteral("probe")) return -1;
    if (downloadPhase_ == QStringLiteral("ready")) return 100;
    return licenseManager_ == nullptr ? -1 : licenseManager_->progressPercent();
}
bool VoiceCloneController::downloadInProgress() const
{
    if (downloadPhase_ == QStringLiteral("runtime") && pendingRuntimeResolutionId_ > 0) return true;
    if (downloadPhase_ == QStringLiteral("runtime") && runtimeManager_ != nullptr) {
        const auto state = runtimeManager_->state();
        return state == VoiceCloneRuntimePackageManager::Resolving
               || state == VoiceCloneRuntimePackageManager::Downloading
               || state == VoiceCloneRuntimePackageManager::Verifying
               || state == VoiceCloneRuntimePackageManager::Extracting
               || state == VoiceCloneRuntimePackageManager::Committing;
    }
    if (downloadPhase_ == QStringLiteral("probe"))
        return activationState_ != QStringLiteral("error")
               && activationState_ != QStringLiteral("needs-download")
               && !(modelLoaded_ && worker_.isReady());
    if (licenseManager_ == nullptr) return false;
    const auto state = licenseManager_->state();
    return state == VoiceClonePackageManager::Resolving
           || state == VoiceClonePackageManager::Downloading
           || state == VoiceClonePackageManager::Verifying
           || state == VoiceClonePackageManager::Committing;
}
QVariantList VoiceCloneController::basicParameters() const { return basicParameters_; }
QVariantList VoiceCloneController::advancedParameters() const { return advancedParameters_; }
bool VoiceCloneController::advancedSettingsAvailable() const { return !advancedParameters_.isEmpty(); }
QVariantList VoiceCloneController::models() const { return models_; }
int VoiceCloneController::pendingCleanupCount() const { return pendingCleanup_.size(); }

void VoiceCloneController::setDownloadPhase(const QString& phase)
{
    if (downloadPhase_ == phase) return;
    downloadPhase_ = phase;
    emit downloadChanged();
}

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
        if (activationInProgress_) {
            activationInProgress_ = false;
            if (loaded) {
                if (runtimeDownloadRequired_) {
                    runtimeDownloadRequired_ = false;
                    emit activationChanged();
                }
                setError({});
                setActivation(QStringLiteral("ready"), QStringLiteral("Model ready"));
                if (downloadPhase_ == QStringLiteral("probe")) {
                    setDownloadPhase(QStringLiteral("ready"));
                    pendingDownloadStableId_.clear();
                    pendingDownloadManifest_ = {};
                }
            } else {
                const QString diagnostic = QStringLiteral("Adapter did not load the model");
                setError(diagnostic);
                setActivation(QStringLiteral("error"), diagnostic);
                if (downloadPhase_ == QStringLiteral("probe")) emit downloadChanged();
            }
        }
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
        if (activationInProgress_) {
            activationInProgress_ = false;
            setActivation(QStringLiteral("error"), message);
            if (downloadPhase_ == QStringLiteral("probe")) emit downloadChanged();
        }
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

void VoiceCloneController::setActivation(const QString& state, const QString& message)
{
    if (activationState_ == state && activationMessage_ == message) return;
    activationState_ = state;
    activationMessage_ = message;
    emit activationChanged();
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
    publishedResults_.insert(QDir::fromNativeSeparators(
        QFileInfo(files.finalPath).absoluteFilePath()));
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

#include "voice_clone_host_controller.hpp"

#include "plugins/voice_clone_plugin_interface.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QSet>
#include <QSysInfo>
#include <QVersionNumber>

#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

constexpr auto kManifestFileName = "agplayer-voice-clone.json";
constexpr auto kPluginId = "agplayer.voice-clone";
constexpr int kProtocolVersion = 1;

QString currentPlatform()
{
#ifdef Q_OS_WIN
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QStringLiteral("unknown");
#endif
}

QString currentArchitecture()
{
    const QString architecture = QSysInfo::currentCpuArchitecture().toLower();
    if (architecture == QStringLiteral("amd64")) return QStringLiteral("x86_64");
    return architecture;
}

QString unknownField(const QJsonObject& object)
{
    static const QSet<QString> allowed{
        QStringLiteral("schemaVersion"),
        QStringLiteral("pluginId"),
        QStringLiteral("version"),
        QStringLiteral("availableVersion"),
        QStringLiteral("platform"),
        QStringLiteral("architecture"),
        QStringLiteral("minimumPlayerVersion"),
        QStringLiteral("protocolVersion"),
        QStringLiteral("library"),
    };
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowed.contains(it.key())) return it.key();
    }
    return {};
}

bool isSimpleFileName(const QString& value)
{
    return !value.trimmed().isEmpty()
           && QFileInfo(value).fileName() == value
           && !QDir::isAbsolutePath(value)
           && value != QStringLiteral(".")
           && value != QStringLiteral("..");
}

bool isValidVersion(const QString& value)
{
    qsizetype suffixIndex = 0;
    const QVersionNumber version = QVersionNumber::fromString(value, &suffixIndex);
    return !version.isNull() && suffixIndex == value.size();
}

bool jsonIntegerEquals(const QJsonValue& value, const int expected)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    return std::isfinite(number) && std::trunc(number) == number
           && number == static_cast<double>(expected);
}

bool pathIsWithin(const QString& root, const QString& candidate)
{
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    return candidate.compare(root, sensitivity) == 0
           || candidate.startsWith(root + QLatin1Char('/'), sensitivity);
}

bool isReparsePoint(const QFileInfo& info)
{
#ifdef Q_OS_WIN
    const std::wstring path =
        QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString();
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
           && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return info.isSymLink();
#endif
}

bool pathTraversesReparsePoint(const QString& path)
{
    QString current = QDir::fromNativeSeparators(
        QFileInfo(path).absoluteFilePath());
    while (!current.isEmpty()) {
        const QFileInfo info(current);
        if ((info.exists() || info.isSymLink()) && isReparsePoint(info)) {
            return true;
        }
        const QString parent = QDir::fromNativeSeparators(info.absolutePath());
        if (parent == current) break;
        current = parent;
    }
    return false;
}

QString currentPlayerVersion()
{
    const QString applicationVersion = QCoreApplication::applicationVersion();
    return applicationVersion.isEmpty() ? QStringLiteral(AGPLAYER_VERSION)
                                        : applicationVersion;
}

} // namespace

VoiceCloneHostController::VoiceCloneHostController(QObject* parent)
    : QObject(parent)
{
}

VoiceCloneHostController::~VoiceCloneHostController()
{
    closePlugin();
}

VoiceCloneHostController::State VoiceCloneHostController::state() const
{
    return state_;
}

QString VoiceCloneHostController::errorString() const { return error_; }
QString VoiceCloneHostController::pluginVersion() const { return pluginVersion_; }
QString VoiceCloneHostController::availableVersion() const { return availableVersion_; }
bool VoiceCloneHostController::pluginLoaded() const { return plugin_ != nullptr; }
QObject* VoiceCloneHostController::pluginController() const { return pluginController_; }
QUrl VoiceCloneHostController::mainQmlUrl() const { return mainQmlUrl_; }

QString VoiceCloneHostController::pluginRoot() const
{
    const QString testRoot = qEnvironmentVariable("AGPLAYER_VOICE_CLONE_ROOT");
    if (!testRoot.isEmpty()) return QDir::cleanPath(testRoot);
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("plugins/voice-clone"));
}

void VoiceCloneHostController::resetDiscovery()
{
    libraryPath_.clear();
    pluginVersion_.clear();
    availableVersion_.clear();
    manifestProtocolVersion_ = 0;
    error_.clear();
}

bool VoiceCloneHostController::unloadLibrary(QString* error)
{
    if (loader_ == nullptr) return true;
    if (!loader_->unload()) {
        if (error != nullptr) {
            const QString detail = loader_->errorString();
            *error = detail.isEmpty()
                         ? QStringLiteral("Plugin unload failed")
                         : QStringLiteral("Plugin unload failed: %1").arg(detail);
        }
        return false;
    }
    loader_.reset();
    return true;
}

void VoiceCloneHostController::setState(const State state, const QString& error)
{
    state_ = state;
    error_ = error;
    emit stateChanged();
}

void VoiceCloneHostController::refresh()
{
    if (pluginLoaded()) closePlugin();
    if (loader_ != nullptr) return;
    resetDiscovery();

    const QString root = pluginRoot();
    const QFileInfo rootInfo(root);
    if (pathTraversesReparsePoint(rootInfo.absoluteFilePath())) {
        setState(Invalid, QStringLiteral("Plugin root contains a link or reparse point"));
        return;
    }
    if (!rootInfo.exists()) {
        setState(Absent);
        return;
    }
    if (!rootInfo.isDir()) {
        setState(Invalid, QStringLiteral("Plugin root contains a link or reparse point"));
        return;
    }
    const QString canonicalRoot =
        QDir::fromNativeSeparators(rootInfo.canonicalFilePath());
    if (canonicalRoot.isEmpty()) {
        setState(Invalid, QStringLiteral("Plugin root could not be canonicalized"));
        return;
    }

    const QFileInfo manifestInfo(
        QDir(canonicalRoot).filePath(QLatin1String(kManifestFileName)));
    if (pathTraversesReparsePoint(manifestInfo.absoluteFilePath())) {
        setState(Invalid,
                 QStringLiteral("Plugin manifest contains a link or reparse point"));
        return;
    }
    if (!manifestInfo.exists()) {
        setState(Absent);
        return;
    }
    if (!manifestInfo.isFile()) {
        setState(Invalid,
                 QStringLiteral("Plugin manifest contains a link or reparse point"));
        return;
    }
    const QString canonicalManifest =
        QDir::fromNativeSeparators(manifestInfo.canonicalFilePath());
    if (canonicalManifest.isEmpty()
        || !pathIsWithin(canonicalRoot, canonicalManifest)) {
        setState(Invalid,
                 QStringLiteral("Plugin manifest canonical path escapes plugin root"));
        return;
    }
    QFile manifestFile(canonicalManifest);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        setState(Invalid, QStringLiteral("Cannot read plugin manifest"));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setState(Invalid, QStringLiteral("Invalid plugin manifest JSON"));
        return;
    }
    const QJsonObject manifest = document.object();
    const QString extraField = unknownField(manifest);
    if (!extraField.isEmpty()) {
        setState(Invalid, QStringLiteral("Unknown plugin manifest field: %1").arg(extraField));
        return;
    }
    if (!jsonIntegerEquals(manifest.value(QStringLiteral("schemaVersion")), 1)) {
        setState(Invalid, QStringLiteral("Unsupported plugin manifest schema"));
        return;
    }
    if (manifest.value(QStringLiteral("pluginId")).toString() != QLatin1String(kPluginId)) {
        setState(Invalid, QStringLiteral("Unexpected plugin identity"));
        return;
    }

    pluginVersion_ = manifest.value(QStringLiteral("version")).toString();
    const QJsonValue availableVersion =
        manifest.value(QStringLiteral("availableVersion"));
    if (!availableVersion.isUndefined() && !availableVersion.isString()) {
        setState(Invalid, QStringLiteral("Invalid plugin or player version"));
        return;
    }
    availableVersion_ = availableVersion.toString();
    if (availableVersion.isUndefined() || availableVersion_.trimmed().isEmpty()) {
        availableVersion_ = pluginVersion_;
    }
    const QString minimumPlayer =
        manifest.value(QStringLiteral("minimumPlayerVersion")).toString();
    if (!isValidVersion(pluginVersion_) || !isValidVersion(availableVersion_)
        || !isValidVersion(minimumPlayer)) {
        setState(Invalid, QStringLiteral("Invalid plugin or player version"));
        return;
    }
    if (manifest.value(QStringLiteral("platform")).toString() != currentPlatform()) {
        setState(Invalid, QStringLiteral("Plugin platform is incompatible"));
        return;
    }
    if (manifest.value(QStringLiteral("architecture")).toString().toLower()
        != currentArchitecture()) {
        setState(Invalid, QStringLiteral("Plugin architecture is incompatible"));
        return;
    }
    if (QVersionNumber::compare(QVersionNumber::fromString(minimumPlayer),
                                QVersionNumber::fromString(currentPlayerVersion())) > 0) {
        setState(Invalid, QStringLiteral("Plugin requires a newer player version"));
        return;
    }
    const QJsonValue protocolVersion = manifest.value(QStringLiteral("protocolVersion"));
    if (!jsonIntegerEquals(protocolVersion, kProtocolVersion)) {
        setState(Invalid, QStringLiteral("Plugin protocol is incompatible"));
        return;
    }
    manifestProtocolVersion_ = kProtocolVersion;

    const QString library = manifest.value(QStringLiteral("library")).toString();
    if (!isSimpleFileName(library)) {
        setState(Invalid, QStringLiteral("Plugin library path is invalid"));
        return;
    }
    const QFileInfo libraryInfo(QDir(canonicalRoot).filePath(library));
    if (pathTraversesReparsePoint(libraryInfo.absoluteFilePath())) {
        setState(Invalid,
                 QStringLiteral("Plugin library contains a link or reparse point"));
        return;
    }
    if (!libraryInfo.isFile()) {
        setState(Invalid, QStringLiteral("Plugin library is missing"));
        return;
    }
    const QString canonicalLibrary =
        QDir::fromNativeSeparators(libraryInfo.canonicalFilePath());
    if (canonicalLibrary.isEmpty()
        || !pathIsWithin(canonicalRoot, canonicalLibrary)) {
        setState(Invalid,
                 QStringLiteral("Plugin library canonical path escapes plugin root"));
        return;
    }
    libraryPath_ = canonicalLibrary;

    const bool updateAvailable =
        QVersionNumber::compare(QVersionNumber::fromString(availableVersion_),
                                QVersionNumber::fromString(pluginVersion_)) > 0;
    setState(updateAvailable ? UpdateAvailable : Compatible);
}

bool VoiceCloneHostController::openPlugin()
{
    if (pluginLoaded()) return true;
    if (state_ != Compatible && state_ != UpdateAvailable) return false;

    loader_ = std::make_unique<QPluginLoader>();
    loader_->setLoadHints(QLibrary::ResolveAllSymbolsHint);
    loader_->setFileName(libraryPath_);
    QObject* instance = loader_->instance();
    if (instance == nullptr) {
        const QString detail = loader_->errorString();
        loader_.reset();
        setState(Failed, detail.isEmpty() ? QStringLiteral("Plugin load failed") : detail);
        return false;
    }
    plugin_ = qobject_cast<AgPlayerVoiceClonePluginInterface*>(instance);
    if (plugin_ == nullptr) {
        QString unloadError;
        const bool unloaded = unloadLibrary(&unloadError);
        setState(Failed,
                 unloaded ? QStringLiteral("Plugin interface is incompatible")
                          : unloadError);
        return false;
    }

    const AgPlayerVoiceClonePluginMetadata metadata = plugin_->metadata();
    if (metadata.id != QLatin1String(kPluginId)
        || metadata.version != pluginVersion_
        || plugin_->protocolVersion() != manifestProtocolVersion_) {
        plugin_->shutdown();
        plugin_ = nullptr;
        QString unloadError;
        const bool unloaded = unloadLibrary(&unloadError);
        setState(Failed,
                 unloaded
                     ? QStringLiteral("Plugin metadata does not match its manifest")
                     : unloadError);
        return false;
    }
    pluginController_ = plugin_->controller();
    mainQmlUrl_ = plugin_->mainQmlUrl();
    if (pluginController_ == nullptr || mainQmlUrl_.isEmpty()
        || !mainQmlUrl_.isValid()) {
        plugin_->shutdown();
        plugin_ = nullptr;
        pluginController_ = nullptr;
        mainQmlUrl_ = {};
        QString unloadError;
        const bool unloaded = unloadLibrary(&unloadError);
        setState(Failed,
                 unloaded
                     ? QStringLiteral("Plugin did not expose a controller and QML URL")
                     : unloadError);
        return false;
    }
    setState(Loaded);
    return true;
}

void VoiceCloneHostController::closePlugin()
{
    if (plugin_ != nullptr) plugin_->shutdown();
    plugin_ = nullptr;
    pluginController_ = nullptr;
    mainQmlUrl_ = {};
    if (loader_ != nullptr) {
        QString unloadError;
        if (!unloadLibrary(&unloadError)) {
            setState(Failed, unloadError);
            return;
        }
    }
    if (state_ == Loaded) {
        const bool updateAvailable =
            QVersionNumber::compare(QVersionNumber::fromString(availableVersion_),
                                    QVersionNumber::fromString(pluginVersion_)) > 0;
        setState(updateAvailable ? UpdateAvailable : Compatible);
    }
}

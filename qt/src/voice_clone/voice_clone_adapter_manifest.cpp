#include "voice_clone_adapter_manifest.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace agplayer::voice_clone {
namespace {

QString unknownField(const QJsonObject& object, const QSet<QString>& allowed)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowed.contains(it.key())) return it.key();
    }
    return {};
}

bool isSafeRelativePath(const QString& path)
{
    const QString normalized = QDir::fromNativeSeparators(path.trimmed());
    if (normalized.isEmpty() || QDir::isAbsolutePath(normalized)
        || QFileInfo(normalized).isAbsolute() || normalized.contains(QLatin1Char(':'))) {
        return false;
    }
    const QString clean = QDir::cleanPath(normalized);
    return clean != QStringLiteral("..") && !clean.startsWith(QStringLiteral("../"))
           && clean != QStringLiteral(".");
}

bool isIdentifier(const QString& value)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$"));
    return pattern.match(value).hasMatch();
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
    const std::wstring path = QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString();
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
           && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return info.isSymLink();
#endif
}

QString validateExistingPath(const QString& root,
                             const QString& relativePath,
                             QString* canonicalRoot)
{
    const QFileInfo rootInfo(root);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        return QStringLiteral("Adapter Pack root must be an existing directory");
    }
    if (isReparsePoint(rootInfo)) {
        return QStringLiteral("Adapter Pack root must not be a link or reparse point");
    }
    *canonicalRoot = QDir::fromNativeSeparators(rootInfo.canonicalFilePath());
    if (canonicalRoot->isEmpty()) {
        return QStringLiteral("Adapter Pack root could not be canonicalized");
    }

    QString current = *canonicalRoot;
    const QStringList components = QDir::fromNativeSeparators(relativePath).split(
        QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& component : components) {
        current = QDir(current).filePath(component);
        const QFileInfo info(current);
        if (!info.exists() && !info.isSymLink()) break;
        if (isReparsePoint(info)) {
            return QStringLiteral("launcher path contains a link or reparse point");
        }
        const QString canonical = QDir::fromNativeSeparators(info.canonicalFilePath());
        if (canonical.isEmpty() || !pathIsWithin(*canonicalRoot, canonical)) {
            return QStringLiteral("launcher canonical path escapes the Adapter Pack root");
        }
    }
    return {};
}

} // namespace

AdapterManifestParseResult parseAdapterManifest(const QByteArray& json)
{
    AdapterManifestParseResult result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("invalid adapter manifest JSON: %1").arg(parseError.errorString());
        return result;
    }

    const QJsonObject root = document.object();
    const QString rootUnknown = unknownField(
        root,
        {QStringLiteral("schemaVersion"),
         QStringLiteral("adapterId"),
         QStringLiteral("adapterVersion"),
         QStringLiteral("protocolVersion"),
         QStringLiteral("runtime"),
         QStringLiteral("defaultLauncherId"),
         QStringLiteral("launchers")});
    if (!rootUnknown.isEmpty()) {
        result.error = QStringLiteral("unknown adapter manifest field: %1").arg(rootUnknown);
        return result;
    }
    if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != 1) {
        result.error = QStringLiteral("unsupported adapter manifest schema version");
        return result;
    }

    VoiceCloneAdapterManifest& manifest = result.manifest;
    manifest.adapterId = root.value(QStringLiteral("adapterId")).toString();
    manifest.adapterVersion = root.value(QStringLiteral("adapterVersion")).toString();
    manifest.protocolVersion = root.value(QStringLiteral("protocolVersion")).toInt(-1);
    manifest.defaultLauncherId = root.value(QStringLiteral("defaultLauncherId")).toString();
    if (!isIdentifier(manifest.adapterId) || !isIdentifier(manifest.adapterVersion)
        || manifest.protocolVersion != 1 || !isIdentifier(manifest.defaultLauncherId)) {
        result.error = QStringLiteral("adapterId, adapterVersion, protocolVersion, and defaultLauncherId are invalid");
        return result;
    }

    if (!root.value(QStringLiteral("runtime")).isObject()) {
        result.error = QStringLiteral("runtime must be an object");
        return result;
    }
    const QJsonObject runtime = root.value(QStringLiteral("runtime")).toObject();
    const QString runtimeUnknown = unknownField(
        runtime, {QStringLiteral("id"), QStringLiteral("root"), QStringLiteral("shared")});
    manifest.runtime.id = runtime.value(QStringLiteral("id")).toString();
    manifest.runtime.root = runtime.value(QStringLiteral("root")).toString();
    if (!runtimeUnknown.isEmpty() || !isIdentifier(manifest.runtime.id)
        || !runtime.value(QStringLiteral("shared")).isBool()
        || !isSafeRelativePath(manifest.runtime.root)) {
        result.error = runtimeUnknown.isEmpty()
                           ? QStringLiteral("runtime requires a trusted relative root")
                           : QStringLiteral("unknown runtime field: %1").arg(runtimeUnknown);
        return result;
    }
    manifest.runtime.shared = runtime.value(QStringLiteral("shared")).toBool();

    if (!root.value(QStringLiteral("launchers")).isArray()
        || root.value(QStringLiteral("launchers")).toArray().isEmpty()) {
        result.error = QStringLiteral("launchers must contain at least one listed launcher");
        return result;
    }
    QSet<QString> launcherIds;
    for (const QJsonValue& value : root.value(QStringLiteral("launchers")).toArray()) {
        if (!value.isObject()) {
            result.error = QStringLiteral("launcher must be an object");
            return result;
        }
        const QJsonObject object = value.toObject();
        const QString launcherUnknown = unknownField(
            object,
            {QStringLiteral("id"),
             QStringLiteral("kind"),
             QStringLiteral("path"),
             QStringLiteral("shared")});
        AdapterLauncher launcher{object.value(QStringLiteral("id")).toString(),
                                 object.value(QStringLiteral("kind")).toString(),
                                 object.value(QStringLiteral("path")).toString(),
                                 object.value(QStringLiteral("shared")).toBool()};
        if (!launcherUnknown.isEmpty() || !isIdentifier(launcher.id)
            || launcherIds.contains(launcher.id)
            || (launcher.kind != QStringLiteral("executable")
                && launcher.kind != QStringLiteral("pythonModule"))
            || !object.value(QStringLiteral("shared")).isBool()
            || !isSafeRelativePath(launcher.relativePath)) {
            result.error = launcherUnknown.isEmpty()
                               ? QStringLiteral("launcher must be uniquely listed with a trusted relative path")
                               : QStringLiteral("unknown launcher field: %1").arg(launcherUnknown);
            return result;
        }
        launcherIds.insert(launcher.id);
        manifest.launchers.append(std::move(launcher));
    }
    if (!launcherIds.contains(manifest.defaultLauncherId)) {
        result.error = QStringLiteral("default launcher is not listed");
    }
    return result;
}

AdapterLauncherResolution resolveAdapterLauncher(const VoiceCloneAdapterManifest& manifest,
                                                 const QString& launcherId,
                                                 const QString& adapterPackRoot)
{
    AdapterLauncherResolution result;
    auto launcher = manifest.launchers.constEnd();
    for (auto it = manifest.launchers.constBegin(); it != manifest.launchers.constEnd(); ++it) {
        if (it->id == launcherId) {
            launcher = it;
            break;
        }
    }
    if (launcher == manifest.launchers.constEnd()) {
        result.error = QStringLiteral("launcher is not listed by the trusted Adapter Pack");
        return result;
    }
    if (!isSafeRelativePath(launcher->relativePath) || adapterPackRoot.trimmed().isEmpty()) {
        result.error = QStringLiteral("launcher path is not a trusted relative path");
        return result;
    }

    const QString root = QDir::fromNativeSeparators(QDir(adapterPackRoot).absolutePath());
    QString canonicalRoot;
    result.error = validateExistingPath(root, launcher->relativePath, &canonicalRoot);
    if (!result.error.isEmpty()) return result;
    const QString candidate = QDir::fromNativeSeparators(
        QDir::cleanPath(QDir(canonicalRoot).absoluteFilePath(launcher->relativePath)));
    if (!pathIsWithin(canonicalRoot, candidate)) {
        result.error = QStringLiteral("launcher resolves outside the trusted Adapter Pack");
        return result;
    }
    result.launcher = *launcher;
    result.absolutePath = candidate;
    return result;
}

} // namespace agplayer::voice_clone

#include "voice_clone_package_manifest.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>

namespace agplayer::voice_clone {
namespace {

struct ApprovedModelIdentity {
    const char* modelId;
    const char* adapterId;
    bool requiresLicenseAcceptance;
    const char* licenseUrl;
    const char* licenseRevision;
};

constexpr ApprovedModelIdentity kApprovedModels[] = {
    {"Qwen/Qwen3-TTS-12Hz-0.6B-Base", "qwen", false, nullptr, nullptr},
    {"Qwen/Qwen3-TTS-12Hz-1.7B-Base", "qwen", false, nullptr, nullptr},
    {"IndexTeam/IndexTTS-2.5", "indextts25", true,
     "https://huggingface.co/IndexTeam/IndexTTS-2.5", "license-2026-08-13"},
    {"FunAudioLLM/Fun-CosyVoice3-0.5B-2512", "cosyvoice3", false, nullptr, nullptr},
};

QString unknownField(const QJsonObject& object, const QSet<QString>& allowed)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowed.contains(it.key())) return it.key();
    }
    return {};
}

bool isLoopbackHttp(const QUrl& url)
{
    if (url.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) != 0) return false;
    const QString host = url.host().toLower();
    return host == QStringLiteral("127.0.0.1")
           || host == QStringLiteral("localhost")
           || host == QStringLiteral("::1");
}

bool isAllowedDownloadUrl(const QUrl& url)
{
    return url.isValid() && !url.host().isEmpty()
           && (url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
               || isLoopbackHttp(url));
}

QString normalizedRelativePath(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

QString pathKey(const QString& path)
{
#ifdef Q_OS_WIN
    return path.toCaseFolded();
#else
    return path;
#endif
}

bool hasUnsafeWindowsComponent(const QString& path)
{
    static const QSet<QString> deviceNames{
        QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"),
        QStringLiteral("NUL"), QStringLiteral("COM1"), QStringLiteral("COM2"),
        QStringLiteral("COM3"), QStringLiteral("COM4"), QStringLiteral("COM5"),
        QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"),
        QStringLiteral("LPT3"), QStringLiteral("LPT4"), QStringLiteral("LPT5"),
        QStringLiteral("LPT6"), QStringLiteral("LPT7"), QStringLiteral("LPT8"),
        QStringLiteral("LPT9")};
    for (const QString& component : path.split(QLatin1Char('/'))) {
        if (component == QStringLiteral(".")) continue;
        if (component.endsWith(QLatin1Char('.')) || component.endsWith(QLatin1Char(' '))
            || component.contains(QLatin1Char(':'))
            || deviceNames.contains(component.section(QLatin1Char('.'), 0, 0).toUpper())) {
            return true;
        }
    }
    return false;
}

const ApprovedModelIdentity* approvedIdentity(const QString& modelId,
                                              const QString& adapterId)
{
    for (const ApprovedModelIdentity& approved : kApprovedModels) {
        if (modelId == QLatin1String(approved.modelId)
            && adapterId == QLatin1String(approved.adapterId)) {
            return &approved;
        }
    }
    return nullptr;
}

} // namespace

VoiceClonePackageManifest VoiceClonePackageManifest::fromJson(const QJsonObject& object)
{
    VoiceClonePackageManifest result;
    const QString rootUnknown = unknownField(
        object,
        {QStringLiteral("schemaVersion"), QStringLiteral("packageId"),
         QStringLiteral("modelId"), QStringLiteral("adapterId"),
         QStringLiteral("version"), QStringLiteral("revision"),
         QStringLiteral("license"), QStringLiteral("files")});
    if (!rootUnknown.isEmpty()) {
        result.parseError_ = QStringLiteral("unknown package field: %1").arg(rootUnknown);
        return result;
    }
    if (!object.value(QStringLiteral("schemaVersion")).isDouble()
        || object.value(QStringLiteral("schemaVersion")).toDouble() != 1.0) {
        result.parseError_ = QStringLiteral("unsupported package schema");
        return result;
    }
    if (!object.value(QStringLiteral("packageId")).isString()
        || !object.value(QStringLiteral("modelId")).isString()
        || !object.value(QStringLiteral("adapterId")).isString()
        || !object.value(QStringLiteral("version")).isString()
        || !object.value(QStringLiteral("revision")).isString()
        || !object.value(QStringLiteral("license")).isObject()
        || !object.value(QStringLiteral("files")).isArray()) {
        result.parseError_ = QStringLiteral("invalid package metadata types");
        return result;
    }

    result.packageId = object.value(QStringLiteral("packageId")).toString();
    result.modelId = object.value(QStringLiteral("modelId")).toString();
    result.adapterId = object.value(QStringLiteral("adapterId")).toString();
    result.version = object.value(QStringLiteral("version")).toString();
    result.revision = object.value(QStringLiteral("revision")).toString();
    const QJsonObject license = object.value(QStringLiteral("license")).toObject();
    const QString licenseUnknown = unknownField(
        license,
        {QStringLiteral("url"), QStringLiteral("revision")});
    if (!licenseUnknown.isEmpty()
        || !license.value(QStringLiteral("url")).isString()
        || !license.value(QStringLiteral("revision")).isString()) {
        result.parseError_ = licenseUnknown.isEmpty()
                                 ? QStringLiteral("invalid license metadata")
                                 : QStringLiteral("unknown license field: %1").arg(licenseUnknown);
        return result;
    }
    result.licenseUrl = QUrl(license.value(QStringLiteral("url")).toString());
    result.licenseRevision = license.value(QStringLiteral("revision")).toString();
    result.requiresLicenseAcceptance = result.licenseAcceptanceRequired();

    for (const QJsonValue& value : object.value(QStringLiteral("files")).toArray()) {
        if (!value.isObject()) {
            result.parseError_ = QStringLiteral("package file entry must be an object");
            return result;
        }
        const QJsonObject file = value.toObject();
        const QString fileUnknown = unknownField(
            file,
            {QStringLiteral("path"), QStringLiteral("url"), QStringLiteral("sha256")});
        if (!fileUnknown.isEmpty()
            || !file.value(QStringLiteral("path")).isString()
            || !file.value(QStringLiteral("url")).isString()
            || !file.value(QStringLiteral("sha256")).isString()) {
            result.parseError_ = fileUnknown.isEmpty()
                                     ? QStringLiteral("invalid package file metadata")
                                     : QStringLiteral("unknown package file field: %1")
                                           .arg(fileUnknown);
            return result;
        }
        result.files.append({file.value(QStringLiteral("path")).toString(),
                             QUrl(file.value(QStringLiteral("url")).toString()),
                             file.value(QStringLiteral("sha256")).toString().toLatin1()});
    }
    result.parseError_ = result.validationError();
    return result;
}

QString VoiceClonePackageManifest::validationError() const
{
    const QString foldedPackageId = packageId.toCaseFolded();
    if (packageId.trimmed().isEmpty() || QFileInfo(packageId).fileName() != packageId
        || QDir::isAbsolutePath(packageId) || packageId == QStringLiteral(".")
        || packageId == QStringLiteral("..") || hasUnsafeWindowsComponent(packageId)
        || foldedPackageId == QStringLiteral(".staging")
        || foldedPackageId == QStringLiteral(".rollback")
        || foldedPackageId.endsWith(QStringLiteral(".rollback"))
        || foldedPackageId == QStringLiteral("license-acceptances.json")) {
        return QStringLiteral("invalid packageId");
    }
    if (version.trimmed().isEmpty() || revision.trimmed().isEmpty()) {
        return QStringLiteral("missing package version or revision");
    }
    const ApprovedModelIdentity* approved = approvedIdentity(modelId, adapterId);
    if (approved == nullptr) {
        return QStringLiteral("unapproved modelId/adapterId package identity");
    }
    if (!licenseUrl.isValid()
        || licenseUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0
        || licenseUrl.host().isEmpty() || licenseRevision.trimmed().isEmpty()) {
        return QStringLiteral("invalid package license URL or revision");
    }
    if (approved->requiresLicenseAcceptance
        && (licenseUrl.toString(QUrl::FullyEncoded) != QLatin1String(approved->licenseUrl)
            || licenseRevision != QLatin1String(approved->licenseRevision))) {
        return QStringLiteral("package license identity does not match the approved Registry identity");
    }
    if (files.isEmpty()) return QStringLiteral("package has no files");

    static const QRegularExpression hashPattern(QStringLiteral("^[0-9a-fA-F]{64}$"));
    QSet<QString> targets;
    for (const VoiceClonePackageFile& file : files) {
        const QString normalized = normalizedRelativePath(file.relativePath);
        if (file.relativePath.trimmed().isEmpty() || QDir::isAbsolutePath(file.relativePath)
            || normalized.isEmpty() || normalized == QStringLiteral(".")
            || normalized == QStringLiteral("..")
            || normalized.startsWith(QStringLiteral("../"))
            || hasUnsafeWindowsComponent(QDir::fromNativeSeparators(file.relativePath))) {
            return QStringLiteral("package file path is absolute or contains ..: %1")
                .arg(file.relativePath);
        }
        if (!isAllowedDownloadUrl(file.url)) {
            return QStringLiteral("invalid package file URL: %1").arg(file.url.toString());
        }
        if (!hashPattern.match(QString::fromLatin1(file.sha256)).hasMatch()) {
            return QStringLiteral("invalid SHA-256 for %1").arg(file.relativePath);
        }
        const QString key = pathKey(normalized);
        if (targets.contains(key)) {
            return QStringLiteral("duplicate package target: %1").arg(normalized);
        }
        targets.insert(key);
    }
    return {};
}

bool VoiceClonePackageManifest::isValid() const
{
    return parseError_.isEmpty() && validationError().isEmpty();
}

QString VoiceClonePackageManifest::errorString() const
{
    return parseError_.isEmpty() ? validationError() : parseError_;
}

bool VoiceClonePackageManifest::licenseAcceptanceRequired() const
{
    const ApprovedModelIdentity* approved = approvedIdentity(modelId, adapterId);
    return approved != nullptr && approved->requiresLicenseAcceptance;
}

} // namespace agplayer::voice_clone

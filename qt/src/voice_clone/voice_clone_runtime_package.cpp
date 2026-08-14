#include "voice_clone_runtime_package.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QVersionNumber>

namespace agplayer::voice_clone {
namespace {

bool isIdentifier(const QString& value)
{
    static const QRegularExpression expression(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$"));
    return expression.match(value).hasMatch();
}

bool isVersion(const QString& value)
{
    static const QRegularExpression expression(QStringLiteral("^[0-9]+\\.[0-9]+\\.[0-9]+$"));
    return expression.match(value).hasMatch();
}

bool isSha256(const QString& value)
{
    static const QRegularExpression expression(QStringLiteral("^[0-9a-f]{64}$"));
    return expression.match(value).hasMatch();
}

bool isReservedWindowsName(const QString& component)
{
    const QString base = component.section(QLatin1Char('.'), 0, 0).toUpper();
    if (base == QStringLiteral("CON") || base == QStringLiteral("PRN")
        || base == QStringLiteral("AUX") || base == QStringLiteral("NUL")) return true;
    static const QRegularExpression device(QStringLiteral("^(COM|LPT)[1-9]$"));
    return device.match(base).hasMatch();
}

bool isSafeRuntimePath(const QString& value)
{
    if (value.isEmpty() || value.startsWith(QLatin1Char('/'))
        || value.startsWith(QLatin1Char('\\')) || value.contains(QLatin1Char(':'))
        || value.contains(QLatin1Char('\\')) || value.contains(QChar::Null)
        || value.endsWith(QLatin1Char('/'))) return false;
    const QStringList parts = value.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    static const QRegularExpression safeComponent(QStringLiteral("^[A-Za-z0-9+_.-]+$"));
    for (const QString& part : parts) {
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..")
            || part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' '))
            || !safeComponent.match(part).hasMatch() || isReservedWindowsName(part)) return false;
    }
    return true;
}

QString unknownField(const QJsonObject& object, const QSet<QString>& allowed)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        if (!allowed.contains(it.key())) return it.key();
    return {};
}

bool isHttpsUrl(const QUrl& url)
{
    return url.isValid() && url.scheme() == QStringLiteral("https") && !url.host().isEmpty()
           && url.userInfo().isEmpty() && url.query().isEmpty() && url.fragment().isEmpty();
}

QString approvedRuntimeError(const VoiceCloneRuntimePackageManifest& manifest)
{
    struct Approved { const char* packageId; const char* runtimeId; const char* adapterId; };
    static constexpr Approved approved[] = {
        {"runtime-qwen", "qwen-shared", "qwen"},
        {"runtime-indextts25", "indextts25-isolated", "indextts25"},
        {"runtime-cosyvoice3", "cosyvoice3-isolated", "cosyvoice3"},
    };
    const Approved* match = nullptr;
    for (const Approved& candidate : approved) {
        if (manifest.packageId == QLatin1String(candidate.packageId)) {
            match = &candidate;
            break;
        }
    }
    if (match == nullptr || manifest.runtimeId != QLatin1String(match->runtimeId)
        || manifest.compatibleAdapters.size() != 1
        || manifest.compatibleAdapters.constFirst().adapterId != QLatin1String(match->adapterId)
        || manifest.compatibleAdapters.constFirst().adapterVersion != QStringLiteral("1.0.0")) {
        return QStringLiteral("Runtime package identity or Adapter compatibility is not approved");
    }
    return {};
}

} // namespace

VoiceCloneRuntimePackageManifest VoiceCloneRuntimePackageManifest::fromJson(
    const QJsonObject& object, const VoiceCloneRuntimeValidationPolicy policy)
{
    VoiceCloneRuntimePackageManifest result;
    const QSet<QString> rootFields{
        QStringLiteral("schemaVersion"), QStringLiteral("packageId"),
        QStringLiteral("runtimeId"), QStringLiteral("version"),
        QStringLiteral("compatibleAdapters"), QStringLiteral("protocolVersion"),
        QStringLiteral("platform"), QStringLiteral("architecture"),
        QStringLiteral("minimumPlayerVersion"), QStringLiteral("archiveFormat"),
        QStringLiteral("installRoot"), QStringLiteral("publisher"),
        QStringLiteral("licenseNotices"), QStringLiteral("signature"),
        QStringLiteral("packageUrl"), QStringLiteral("packageBytes"),
        QStringLiteral("packageSha256"), QStringLiteral("installedBytes"),
        QStringLiteral("entryCount"), QStringLiteral("files")};
    const QString extra = unknownField(object, rootFields);
    if (!extra.isEmpty()) {
        result.parseError = QStringLiteral("Unknown runtime manifest field: %1").arg(extra);
        return result;
    }
    result.schemaVersion = object.value(QStringLiteral("schemaVersion")).toInt(-1);
    result.packageId = object.value(QStringLiteral("packageId")).toString();
    result.runtimeId = object.value(QStringLiteral("runtimeId")).toString();
    result.version = object.value(QStringLiteral("version")).toString();
    result.protocolVersion = object.value(QStringLiteral("protocolVersion")).toInt(-1);
    result.platform = object.value(QStringLiteral("platform")).toString();
    result.architecture = object.value(QStringLiteral("architecture")).toString();
    result.minimumPlayerVersion = object.value(QStringLiteral("minimumPlayerVersion")).toString();
    result.archiveFormat = object.value(QStringLiteral("archiveFormat")).toString();
    result.installRoot = object.value(QStringLiteral("installRoot")).toString();
    result.packageUrl = QUrl(object.value(QStringLiteral("packageUrl")).toString());
    result.packageBytes = object.value(QStringLiteral("packageBytes")).toVariant().toLongLong();
    result.packageSha256 = object.value(QStringLiteral("packageSha256")).toString();
    result.installedBytes = object.value(QStringLiteral("installedBytes")).toVariant().toLongLong();
    result.entryCount = object.value(QStringLiteral("entryCount")).toInt(-1);

    const QJsonObject publisher = object.value(QStringLiteral("publisher")).toObject();
    if (unknownField(publisher, {QStringLiteral("name"), QStringLiteral("url")}).isEmpty()) {
        result.publisherName = publisher.value(QStringLiteral("name")).toString();
        result.publisherUrl = QUrl(publisher.value(QStringLiteral("url")).toString());
    } else {
        result.parseError = QStringLiteral("Unknown publisher field");
        return result;
    }
    const QJsonObject signature = object.value(QStringLiteral("signature")).toObject();
    if (unknownField(signature, {QStringLiteral("status"), QStringLiteral("algorithm"),
                                 QStringLiteral("keyId")}).isEmpty()) {
        result.signatureStatus = signature.value(QStringLiteral("status")).toString();
        result.signatureAlgorithm = signature.value(QStringLiteral("algorithm")).toString();
        result.signatureKeyId = signature.value(QStringLiteral("keyId")).toString();
    } else {
        result.parseError = QStringLiteral("Unknown signature field");
        return result;
    }

    const QJsonArray adapters = object.value(QStringLiteral("compatibleAdapters")).toArray();
    for (const QJsonValue& value : adapters) {
        if (!value.isObject()) { result.parseError = QStringLiteral("Adapter compatibility must be an object"); return result; }
        const QJsonObject adapter = value.toObject();
        if (!unknownField(adapter, {QStringLiteral("adapterId"), QStringLiteral("adapterVersion")}).isEmpty()) {
            result.parseError = QStringLiteral("Unknown Adapter compatibility field"); return result;
        }
        result.compatibleAdapters.append({adapter.value(QStringLiteral("adapterId")).toString(),
                                          adapter.value(QStringLiteral("adapterVersion")).toString()});
    }
    const QJsonArray notices = object.value(QStringLiteral("licenseNotices")).toArray();
    for (const QJsonValue& value : notices) {
        if (!value.isObject()) { result.parseError = QStringLiteral("License notice must be an object"); return result; }
        const QJsonObject notice = value.toObject();
        if (!unknownField(notice, {QStringLiteral("name"), QStringLiteral("spdx"),
                                   QStringLiteral("url")}).isEmpty()) {
            result.parseError = QStringLiteral("Unknown license notice field"); return result;
        }
        result.licenseNotices.append({notice.value(QStringLiteral("name")).toString(),
                                      notice.value(QStringLiteral("spdx")).toString(),
                                      QUrl(notice.value(QStringLiteral("url")).toString())});
    }
    QSet<QString> paths;
    const QJsonArray files = object.value(QStringLiteral("files")).toArray();
    for (const QJsonValue& value : files) {
        if (!value.isObject()) { result.parseError = QStringLiteral("Runtime file must be an object"); return result; }
        const QJsonObject file = value.toObject();
        if (!unknownField(file, {QStringLiteral("path"), QStringLiteral("bytes"),
                                 QStringLiteral("sha256")}).isEmpty()) {
            result.parseError = QStringLiteral("Unknown runtime file field"); return result;
        }
        VoiceCloneRuntimeFile parsed{file.value(QStringLiteral("path")).toString(),
                                     file.value(QStringLiteral("bytes")).toVariant().toLongLong(),
                                     file.value(QStringLiteral("sha256")).toString()};
        const QString normalized = parsed.relativePath.toLower();
        if (!isSafeRuntimePath(parsed.relativePath) || paths.contains(normalized)) {
            result.parseError = QStringLiteral("Runtime file path is unsafe or duplicated"); return result;
        }
        paths.insert(normalized);
        result.files.append(parsed);
    }
    const QString validation = result.errorString(policy);
    if (!validation.isEmpty()) result.parseError = validation;
    return result;
}

bool VoiceCloneRuntimePackageManifest::isValid(const VoiceCloneRuntimeValidationPolicy policy) const
{
    return errorString(policy).isEmpty();
}

QString VoiceCloneRuntimePackageManifest::errorString(const VoiceCloneRuntimeValidationPolicy policy) const
{
    if (!parseError.isEmpty()) return parseError;
    if (schemaVersion != 1 || !isIdentifier(packageId) || !isIdentifier(runtimeId)
        || !isVersion(version) || protocolVersion != 1
        || platform != QStringLiteral("windows") || architecture != QStringLiteral("x86_64")
        || !isVersion(minimumPlayerVersion) || archiveFormat != QStringLiteral("zip")
        || installRoot != QStringLiteral("runtime/%1/%2").arg(runtimeId, version)
        || packageBytes <= 0 || !isSha256(packageSha256) || files.isEmpty()
        || publisherName != QStringLiteral("AG Player") || !isHttpsUrl(publisherUrl)
        || licenseNotices.isEmpty() || compatibleAdapters.isEmpty()) {
        return QStringLiteral("Runtime package manifest has missing or invalid required metadata");
    }
    const QString approvedError = approvedRuntimeError(*this);
    if (!approvedError.isEmpty()) return approvedError;
    for (const auto& adapter : compatibleAdapters)
        if (!isIdentifier(adapter.adapterId) || !isVersion(adapter.adapterVersion))
            return QStringLiteral("Runtime Adapter compatibility is invalid");
    for (const auto& notice : licenseNotices)
        if (notice.name.isEmpty() || notice.spdx.isEmpty() || !isHttpsUrl(notice.url))
            return QStringLiteral("Runtime license notice is invalid");
    bool hasPython = false;
    for (const auto& file : files) {
        if (!isSafeRuntimePath(file.relativePath) || file.bytes < 0 || !isSha256(file.sha256))
            return QStringLiteral("Runtime payload file is invalid");
        if (file.relativePath.compare(QStringLiteral("python.exe"), Qt::CaseInsensitive) == 0)
            hasPython = true;
    }
    if (!hasPython) return QStringLiteral("Runtime payload does not contain python.exe");
    constexpr qint64 maximumInstalledBytes = 64LL * 1024 * 1024 * 1024;
    constexpr int maximumEntryCount = 200000;
    if (installedBytes <= 0 || installedBytes > maximumInstalledBytes
        || entryCount <= 0 || entryCount > maximumEntryCount
        || entryCount != files.size()) {
        return QStringLiteral("Runtime declared installed size or entry count is invalid");
    }
    qint64 summedBytes = 0;
    for (const auto& file : files) {
        if (file.bytes > maximumInstalledBytes - summedBytes)
            return QStringLiteral("Runtime declared installed size exceeds supported limits");
        summedBytes += file.bytes;
    }
    if (summedBytes != installedBytes)
        return QStringLiteral("Runtime declared installed size differs from its file list");
    const QVersionNumber required = QVersionNumber::fromString(minimumPlayerVersion);
    const QVersionNumber current = QVersionNumber::fromString(QStringLiteral(AGPLAYER_VERSION));
    if (required.segmentCount() != 3 || current.segmentCount() != 3
        || QVersionNumber::compare(required, current) > 0) {
        return QStringLiteral("Runtime Pack requires a newer AG Player version");
    }
    if (policy == VoiceCloneRuntimeValidationPolicy::OfficialSignedOnly) {
        return QStringLiteral(
            "Production Runtime Pack signature verification unavailable; releases are disabled");
    } else if (!((packageUrl.scheme() == QStringLiteral("http") && packageUrl.isLocalFile() == false
                  && (packageUrl.host() == QStringLiteral("127.0.0.1")
                      || packageUrl.host() == QStringLiteral("localhost")))
                 || isHttpsUrl(packageUrl))
               || signatureStatus != QStringLiteral("unsigned-test")) {
        return QStringLiteral("Test Runtime Pack must use loopback/HTTPS and unsigned-test state");
    }
    return {};
}

bool VoiceCloneRuntimePackageManifest::supports(const QString& adapterId,
                                                const QString& adapterVersion,
                                                const int protocol) const
{
    if (protocolVersion != protocol) return false;
    for (const auto& adapter : compatibleAdapters)
        if (adapter.adapterId == adapterId && adapter.adapterVersion == adapterVersion) return true;
    return false;
}

QJsonObject VoiceCloneRuntimePackageManifest::toJson() const
{
    QJsonArray adapters;
    for (const auto& adapter : compatibleAdapters)
        adapters.append(QJsonObject{{QStringLiteral("adapterId"), adapter.adapterId},
                                    {QStringLiteral("adapterVersion"), adapter.adapterVersion}});
    QJsonArray notices;
    for (const auto& notice : licenseNotices)
        notices.append(QJsonObject{{QStringLiteral("name"), notice.name},
                                   {QStringLiteral("spdx"), notice.spdx},
                                   {QStringLiteral("url"), notice.url.toString()}});
    QJsonArray payload;
    for (const auto& file : files)
        payload.append(QJsonObject{{QStringLiteral("path"), file.relativePath},
                                   {QStringLiteral("bytes"), file.bytes},
                                   {QStringLiteral("sha256"), file.sha256}});
    return {{QStringLiteral("schemaVersion"), schemaVersion},
            {QStringLiteral("packageId"), packageId},
            {QStringLiteral("runtimeId"), runtimeId},
            {QStringLiteral("version"), version},
            {QStringLiteral("compatibleAdapters"), adapters},
            {QStringLiteral("protocolVersion"), protocolVersion},
            {QStringLiteral("platform"), platform},
            {QStringLiteral("architecture"), architecture},
            {QStringLiteral("minimumPlayerVersion"), minimumPlayerVersion},
            {QStringLiteral("archiveFormat"), archiveFormat},
            {QStringLiteral("installRoot"), installRoot},
            {QStringLiteral("publisher"), QJsonObject{{QStringLiteral("name"), publisherName},
                                                       {QStringLiteral("url"), publisherUrl.toString()}}},
            {QStringLiteral("licenseNotices"), notices},
            {QStringLiteral("signature"), QJsonObject{{QStringLiteral("status"), signatureStatus},
                                                       {QStringLiteral("algorithm"), signatureAlgorithm.isEmpty() ? QJsonValue::Null : QJsonValue(signatureAlgorithm)},
                                                       {QStringLiteral("keyId"), signatureKeyId.isEmpty() ? QJsonValue::Null : QJsonValue(signatureKeyId)}}},
            {QStringLiteral("packageUrl"), packageUrl.toString()},
            {QStringLiteral("packageBytes"), packageBytes},
            {QStringLiteral("packageSha256"), packageSha256},
            {QStringLiteral("installedBytes"), installedBytes},
            {QStringLiteral("entryCount"), entryCount},
            {QStringLiteral("files"), payload}};
}

VoiceCloneRuntimeFeed VoiceCloneRuntimeFeed::fromJson(
    const QByteArray& json, const VoiceCloneRuntimeValidationPolicy policy)
{
    VoiceCloneRuntimeFeed result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("Runtime feed is invalid JSON"); return result;
    }
    const QJsonObject object = document.object();
    if (!unknownField(object, {QStringLiteral("schemaVersion"), QStringLiteral("enabled"),
                               QStringLiteral("reason"), QStringLiteral("signature"),
                               QStringLiteral("runtimes")}).isEmpty()
        || object.value(QStringLiteral("schemaVersion")).toInt(-1) != 1
        || !object.value(QStringLiteral("enabled")).isBool()
        || object.value(QStringLiteral("reason")).toString().trimmed().isEmpty()
        || !object.value(QStringLiteral("runtimes")).isArray()) {
        result.error = QStringLiteral("Runtime feed schema is invalid"); return result;
    }
    result.enabled = object.value(QStringLiteral("enabled")).toBool();
    const QJsonObject signature = object.value(QStringLiteral("signature")).toObject();
    if (!unknownField(signature, {QStringLiteral("status"), QStringLiteral("algorithm"),
                                  QStringLiteral("keyId")}).isEmpty()) {
        result.error = QStringLiteral("Runtime feed signature schema is invalid"); return result;
    }
    result.signatureStatus = signature.value(QStringLiteral("status")).toString();
    if (!result.enabled && (result.signatureStatus != QStringLiteral("unsigned-test")
                            || !object.value(QStringLiteral("runtimes")).toArray().isEmpty())) {
        result.error = QStringLiteral("Disabled unsigned-test feed must not claim Runtime releases");
        return result;
    }
    if (result.enabled && policy == VoiceCloneRuntimeValidationPolicy::AllowLoopbackUnsignedTest
        && result.signatureStatus != QStringLiteral("unsigned-test")) {
        result.error = QStringLiteral("Injected test Runtime feed must be unsigned-test");
        return result;
    }
    if (result.enabled && policy == VoiceCloneRuntimeValidationPolicy::OfficialSignedOnly) {
        result.error = QStringLiteral(
            "Production Runtime feed signature verification unavailable; releases are disabled");
        return result;
    }
    for (const QJsonValue& value : object.value(QStringLiteral("runtimes")).toArray()) {
        if (!value.isObject()) { result.error = QStringLiteral("Runtime feed entry must be an object"); return result; }
        auto manifest = VoiceCloneRuntimePackageManifest::fromJson(value.toObject(), policy);
        if (!manifest.isValid(policy)) { result.error = manifest.errorString(policy); return result; }
        result.runtimes.append(manifest);
    }
    if (result.enabled && result.runtimes.isEmpty()) {
        result.error = QStringLiteral("Enabled Runtime feed contains no packages");
    }
    return result;
}

} // namespace agplayer::voice_clone

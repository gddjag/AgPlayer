#include "voice_clone_manifest.hpp"

#include "voice_clone_capability_schema.hpp"
#include "voice_clone_package_manifest.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <cmath>

namespace agplayer::voice_clone {
namespace {

QString unknownField(const QJsonObject& object, const QSet<QString>& allowed)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowed.contains(it.key())) return it.key();
    }
    return {};
}

bool isHttpsUrl(const QString& value)
{
    return value.startsWith(QStringLiteral("https://"));
}

bool isOfficialUrlForAdapter(const QString& value, const QString& adapterId)
{
    const QUrl url(value, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != QStringLiteral("https")
        || !url.userInfo().isEmpty() || (url.port(-1) != -1 && url.port() != 443)) {
        return false;
    }
    const QString host = url.host().toLower();
    const QStringList path = url.path(QUrl::FullyDecoded).split(
        QLatin1Char('/'), Qt::SkipEmptyParts);
    QString expectedOrganization;
    if (adapterId == QStringLiteral("qwen")) {
        expectedOrganization = host == QStringLiteral("github.com")
                                   ? QStringLiteral("QwenLM")
                                   : QStringLiteral("Qwen");
    } else if (adapterId == QStringLiteral("indextts25")) {
        expectedOrganization = host == QStringLiteral("github.com")
                                   ? QStringLiteral("index-tts")
                                   : QStringLiteral("IndexTeam");
    } else if (adapterId == QStringLiteral("cosyvoice3")) {
        expectedOrganization = QStringLiteral("FunAudioLLM");
    } else {
        return false;
    }

    if (host == QStringLiteral("github.com") || host == QStringLiteral("huggingface.co")) {
        return path.size() >= 2 && path[0] == expectedOrganization;
    }
    if (host == QStringLiteral("modelscope.cn")
        || host == QStringLiteral("www.modelscope.cn")) {
        const qsizetype organizationIndex = !path.isEmpty() && path[0] == QStringLiteral("models")
                                                ? 1
                                                : 0;
        return path.size() > organizationIndex + 1
               && path[organizationIndex] == expectedOrganization;
    }
    return false;
}

} // namespace

ManifestParseResult parseLocalModelManifest(const QByteArray& json)
{
    ManifestParseResult result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("invalid JSON manifest: %1").arg(parseError.errorString());
        return result;
    }
    const QString forbiddenName = QStringLiteral("Vibe") + QStringLiteral("Voice");
    if (QString::fromUtf8(document.toJson(QJsonDocument::Compact))
            .contains(forbiddenName, Qt::CaseInsensitive)) {
        result.error = QStringLiteral("manifest contains an unapproved product name");
        return result;
    }

    const QJsonObject root = document.object();
    const QString rootUnknown = unknownField(
        root,
        {QStringLiteral("schemaVersion"),
         QStringLiteral("stableId"),
         QStringLiteral("displayName"),
         QStringLiteral("description"),
         QStringLiteral("adapterId"),
         QStringLiteral("runtimeId"),
         QStringLiteral("revision"),
         QStringLiteral("source"),
         QStringLiteral("license"),
         QStringLiteral("licenses"),
         QStringLiteral("files"),
         QStringLiteral("referenceAudio"),
         QStringLiteral("capabilitySchema")});
    if (!rootUnknown.isEmpty()) {
        result.error = QStringLiteral("unknown manifest field: %1").arg(rootUnknown);
        return result;
    }
    if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != 1) {
        result.error = QStringLiteral("unsupported manifest schema version");
        return result;
    }

    VoiceCloneModel& model = result.model;
    model.stableId = root.value(QStringLiteral("stableId")).toString();
    model.displayName = root.value(QStringLiteral("displayName")).toString();
    model.description = root.value(QStringLiteral("description")).toString();
    model.adapterId = root.value(QStringLiteral("adapterId")).toString();
    model.runtimeId = root.value(QStringLiteral("runtimeId")).toString();
    model.revision = root.value(QStringLiteral("revision")).toString();
    model.capabilitySchema = root.value(QStringLiteral("capabilitySchema")).toObject();
    if (model.stableId.trimmed().isEmpty() || model.displayName.trimmed().isEmpty()
        || model.adapterId.trimmed().isEmpty() || model.runtimeId.trimmed().isEmpty()
        || model.revision.trimmed().isEmpty()) {
        result.error = QStringLiteral("manifest identity, adapter, runtime, and revision are required");
        return result;
    }
    static const QRegularExpression stableIdPattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*/[A-Za-z0-9][A-Za-z0-9._-]*$"));
    if (!stableIdPattern.match(model.stableId).hasMatch()) {
        result.error = QStringLiteral("invalid stableId: %1").arg(model.stableId);
        return result;
    }

    const QJsonObject source = root.value(QStringLiteral("source")).toObject();
    const QString sourceUnknown = unknownField(
        source, {QStringLiteral("provider"), QStringLiteral("url")});
    model.source = {source.value(QStringLiteral("provider")).toString(),
                    source.value(QStringLiteral("url")).toString()};
    if (!sourceUnknown.isEmpty() || model.source.provider.trimmed().isEmpty()
        || !isHttpsUrl(model.source.url)
        || !isOfficialUrlForAdapter(model.source.url, model.adapterId)) {
        result.error = sourceUnknown.isEmpty()
                           ? QStringLiteral("source URL is not an official URL for adapter %1")
                                 .arg(model.adapterId)
                           : QStringLiteral("unknown source field: %1").arg(sourceUnknown);
        return result;
    }

    const QJsonObject license = root.value(QStringLiteral("license")).toObject();
    const QString licenseUnknown = unknownField(
        license, {QStringLiteral("name"), QStringLiteral("url"), QStringLiteral("revision")});
    model.license = {license.value(QStringLiteral("name")).toString(),
                     license.value(QStringLiteral("url")).toString()};
    model.licenseRevision = license.value(QStringLiteral("revision")).toString();
    if (!licenseUnknown.isEmpty() || model.license.name.trimmed().isEmpty()
        || !isHttpsUrl(model.license.url)
        || !isOfficialUrlForAdapter(model.license.url, model.adapterId)) {
        result.error = licenseUnknown.isEmpty()
                           ? QStringLiteral("license URL is not an official URL for adapter %1")
                                 .arg(model.adapterId)
                           : QStringLiteral("unknown license field: %1").arg(licenseUnknown);
        return result;
    }
    const QString expectedLicense = model.adapterId == QStringLiteral("indextts25")
                                        ? QStringLiteral("bilibili Model Use License Agreement")
                                        : QStringLiteral("Apache-2.0");
    if (model.license.name != expectedLicense) {
        result.error = QStringLiteral("license name does not match adapter %1").arg(model.adapterId);
        return result;
    }
    if (model.adapterId == QStringLiteral("indextts25")
        && model.licenseRevision.trimmed().isEmpty()) {
        result.error = QStringLiteral("Index license revision is required");
        return result;
    }
    if (model.adapterId == QStringLiteral("indextts25")) {
        const QVector<VoiceClonePackageLicense> required = approvedRequiredLicenses(
            model.stableId, model.adapterId);
        if (required.size() != 2 || !root.value(QStringLiteral("licenses")).isArray()) {
            result.error = QStringLiteral("Index model requires the exact approved dual-license identities");
            return result;
        }
        const QJsonArray licenses = root.value(QStringLiteral("licenses")).toArray();
        if (licenses.size() != required.size()) {
            result.error = QStringLiteral("Index model requires the exact approved dual-license identities");
            return result;
        }
        QSet<QString> matched;
        QSet<QString> ids;
        for (const QJsonValue& value : licenses) {
            if (!value.isObject()) {
                result.error = QStringLiteral("Index license identity must be an object");
                return result;
            }
            const QJsonObject item = value.toObject();
            const QString itemUnknown = unknownField(
                item, {QStringLiteral("id"), QStringLiteral("name"),
                       QStringLiteral("url"), QStringLiteral("revision"),
                       QStringLiteral("spdx"), QStringLiteral("requiredAcceptance"),
                       QStringLiteral("useRestriction")});
            const QString id = item.value(QStringLiteral("id")).toString();
            if (!itemUnknown.isEmpty() || id.isEmpty() || ids.contains(id)
                || !item.value(QStringLiteral("name")).isString()
                || !item.value(QStringLiteral("url")).isString()
                || !item.value(QStringLiteral("revision")).isString()
                || !item.value(QStringLiteral("spdx")).isString()
                || !item.value(QStringLiteral("requiredAcceptance")).isBool()
                || !item.value(QStringLiteral("useRestriction")).isString()) {
                result.error = QStringLiteral("invalid or duplicate Index license identity");
                return result;
            }
            ids.insert(id);
            if (!item.value(QStringLiteral("requiredAcceptance")).toBool()) {
                result.error = QStringLiteral("Index license identity is not approved");
                return result;
            }
            bool exact = false;
            for (const VoiceClonePackageLicense& approved : required) {
                if (id == approved.id
                    && item.value(QStringLiteral("name")).toString() == approved.name
                    && QUrl(item.value(QStringLiteral("url")).toString()) == approved.url
                    && item.value(QStringLiteral("revision")).toString() == approved.revision
                    && item.value(QStringLiteral("spdx")).toString() == approved.spdx
                    && item.value(QStringLiteral("useRestriction")).toString()
                           == approved.useRestriction) {
                    matched.insert(id);
                    exact = true;
                    break;
                }
            }
            if (!exact) {
                result.error = QStringLiteral("Index license identity is not approved");
                return result;
            }
        }
        if (matched.size() != required.size()) {
            result.error = QStringLiteral("Index model requires the exact approved dual-license identities");
            return result;
        }
        const VoiceClonePackageLicense& primary = required.front();
        if (model.license.name != primary.name || QUrl(model.license.url) != primary.url
            || model.licenseRevision != primary.revision) {
            result.error = QStringLiteral("Index primary license identity is not approved");
            return result;
        }
    } else if (root.contains(QStringLiteral("licenses"))) {
        result.error = QStringLiteral("additional license gates are not approved for this adapter");
        return result;
    }

    if (!root.value(QStringLiteral("files")).isArray()
        || root.value(QStringLiteral("files")).toArray().isEmpty()) {
        result.error = QStringLiteral("files must contain at least one required data file");
        return result;
    }
    QSet<QString> filePaths;
    static const QRegularExpression shaPattern(QStringLiteral("^[0-9a-fA-F]{64}$"));
    for (const QJsonValue& value : root.value(QStringLiteral("files")).toArray()) {
        if (!value.isObject()) {
            result.error = QStringLiteral("file entry must be an object");
            return result;
        }
        const QJsonObject file = value.toObject();
        const QString fileUnknown = unknownField(
            file, {QStringLiteral("path"), QStringLiteral("sha256")});
        if (!fileUnknown.isEmpty()) {
            result.error = QStringLiteral("unknown file field: %1").arg(fileUnknown);
            return result;
        }
        const QString relativePath = file.value(QStringLiteral("path")).toString();
        const QString sha256 = file.value(QStringLiteral("sha256")).toString();
        if (relativePath.trimmed().isEmpty() || filePaths.contains(relativePath)) {
            result.error = QStringLiteral("invalid or duplicate file path: %1").arg(relativePath);
            return result;
        }
        if (!sha256.isEmpty() && !shaPattern.match(sha256).hasMatch()) {
            result.error = QStringLiteral("invalid SHA-256 for %1").arg(relativePath);
            return result;
        }
        filePaths.insert(relativePath);
        model.files.append({relativePath, sha256.toLower()});
    }

    if (root.contains(QStringLiteral("capabilitySchema"))) {
        if (!root.value(QStringLiteral("capabilitySchema")).isObject()) {
            result.error = QStringLiteral("capabilitySchema must be an object");
            return result;
        }
        const auto validation = validateCapabilitySchema(model.capabilitySchema);
        if (!validation.isValid()) {
            result.error = QStringLiteral("invalid capabilitySchema: %1").arg(validation.errorString());
            return result;
        }
    }
    if (root.contains(QStringLiteral("referenceAudio"))) {
        if (!root.value(QStringLiteral("referenceAudio")).isObject()) {
            result.error = QStringLiteral("referenceAudio must be an object");
            return result;
        }
        const QJsonObject reference = root.value(QStringLiteral("referenceAudio")).toObject();
        const QString referenceUnknown = unknownField(
            reference,
            {QStringLiteral("minimumSeconds"),
             QStringLiteral("maximumSeconds"),
             QStringLiteral("extensions")});
        if (!referenceUnknown.isEmpty()) {
            result.error = QStringLiteral("unknown referenceAudio field: %1").arg(referenceUnknown);
            return result;
        }
        if (!reference.contains(QStringLiteral("minimumSeconds"))
            || !reference.value(QStringLiteral("minimumSeconds")).isDouble()
            || !reference.contains(QStringLiteral("maximumSeconds"))
            || !reference.value(QStringLiteral("maximumSeconds")).isDouble()
            || !reference.contains(QStringLiteral("extensions"))
            || !reference.value(QStringLiteral("extensions")).isArray()) {
            result.error = QStringLiteral("referenceAudio requires numeric minimumSeconds, "
                                          "numeric maximumSeconds, and an extensions array");
            return result;
        }
        model.referenceAudio.minimumSeconds =
            reference.value(QStringLiteral("minimumSeconds")).toDouble();
        model.referenceAudio.maximumSeconds =
            reference.value(QStringLiteral("maximumSeconds")).toDouble();
        if (!std::isfinite(model.referenceAudio.minimumSeconds)
            || !std::isfinite(model.referenceAudio.maximumSeconds)
            || model.referenceAudio.minimumSeconds <= 0.0
            || model.referenceAudio.maximumSeconds < model.referenceAudio.minimumSeconds
            || model.referenceAudio.maximumSeconds > 300.0) {
            result.error = QStringLiteral("invalid referenceAudio duration range");
            return result;
        }
        const QJsonArray extensions = reference.value(QStringLiteral("extensions")).toArray();
        if (extensions.isEmpty()) {
            result.error = QStringLiteral("referenceAudio extensions must not be empty");
            return result;
        }
        QSet<QString> seenExtensions;
        static const QRegularExpression extensionPattern(QStringLiteral("^[A-Za-z0-9]{1,10}$"));
        for (const QJsonValue& extension : extensions) {
            const QString normalized = extension.toString().toLower();
            if (!extension.isString() || !extensionPattern.match(normalized).hasMatch()
                || seenExtensions.contains(normalized)) {
                result.error = QStringLiteral("invalid referenceAudio extension");
                return result;
            }
            seenExtensions.insert(normalized);
            model.referenceAudio.extensions.append(normalized);
        }
    }
    return result;
}

} // namespace agplayer::voice_clone

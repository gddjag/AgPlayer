#include "voice_clone_manifest.hpp"

#include "voice_clone_capability_schema.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

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
        || !isHttpsUrl(model.source.url)) {
        result.error = sourceUnknown.isEmpty()
                           ? QStringLiteral("invalid source")
                           : QStringLiteral("unknown source field: %1").arg(sourceUnknown);
        return result;
    }

    const QJsonObject license = root.value(QStringLiteral("license")).toObject();
    const QString licenseUnknown = unknownField(
        license, {QStringLiteral("name"), QStringLiteral("url")});
    model.license = {license.value(QStringLiteral("name")).toString(),
                     license.value(QStringLiteral("url")).toString()};
    if (!licenseUnknown.isEmpty() || model.license.name.trimmed().isEmpty()
        || !isHttpsUrl(model.license.url)) {
        result.error = licenseUnknown.isEmpty()
                           ? QStringLiteral("invalid license")
                           : QStringLiteral("unknown license field: %1").arg(licenseUnknown);
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
        model.referenceAudio.minimumSeconds =
            reference.value(QStringLiteral("minimumSeconds")).toDouble();
        model.referenceAudio.maximumSeconds =
            reference.value(QStringLiteral("maximumSeconds")).toDouble();
        if (model.referenceAudio.minimumSeconds < 0.0
            || model.referenceAudio.maximumSeconds < model.referenceAudio.minimumSeconds) {
            result.error = QStringLiteral("invalid referenceAudio duration range");
            return result;
        }
        const QJsonArray extensions = reference.value(QStringLiteral("extensions")).toArray();
        for (const QJsonValue& extension : extensions) {
            if (!extension.isString() || extension.toString().trimmed().isEmpty()) {
                result.error = QStringLiteral("invalid referenceAudio extension");
                return result;
            }
            model.referenceAudio.extensions.append(extension.toString().toLower());
        }
    }
    return result;
}

} // namespace agplayer::voice_clone

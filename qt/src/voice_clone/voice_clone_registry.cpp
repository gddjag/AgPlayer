#include "voice_clone_registry.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <iterator>
#include <utility>

namespace agplayer::voice_clone {
namespace {

struct ApprovedBuiltIn {
    const char* stableId;
    const char* adapterId;
    const char* runtimeId;
    bool licenseGate;
    const char* licenseName;
    const char* projectUrl;
    const char* huggingFaceUrl;
    const char* modelScopeUrl;
};

constexpr ApprovedBuiltIn kApprovedBuiltIns[] = {
    {"Qwen/Qwen3-TTS-12Hz-0.6B-Base",
     "qwen",
     "qwen",
     false,
     "Apache-2.0",
     "https://github.com/QwenLM/Qwen3-TTS",
     "https://huggingface.co/Qwen/Qwen3-TTS-12Hz-0.6B-Base",
     "https://www.modelscope.cn/models/Qwen/Qwen3-TTS-12Hz-0.6B-Base"},
    {"Qwen/Qwen3-TTS-12Hz-1.7B-Base",
     "qwen",
     "qwen",
     false,
     "Apache-2.0",
     "https://github.com/QwenLM/Qwen3-TTS",
     "https://huggingface.co/Qwen/Qwen3-TTS-12Hz-1.7B-Base",
     "https://www.modelscope.cn/models/Qwen/Qwen3-TTS-12Hz-1.7B-Base"},
    {"IndexTeam/IndexTTS-2.5",
     "indextts25",
     "indextts25",
     true,
     "bilibili Model Use License Agreement",
     "https://github.com/index-tts/index-tts",
     "https://huggingface.co/IndexTeam/IndexTTS-2.5",
     "https://modelscope.cn/models/IndexTeam/IndexTTS-2.5"},
    {"FunAudioLLM/Fun-CosyVoice3-0.5B-2512",
     "cosyvoice3",
     "cosyvoice3",
     false,
     "Apache-2.0",
     "https://github.com/FunAudioLLM/CosyVoice",
     "https://huggingface.co/FunAudioLLM/Fun-CosyVoice3-0.5B-2512",
     "https://www.modelscope.cn/models/FunAudioLLM/Fun-CosyVoice3-0.5B-2512"},
};

QString normalizedAbsolute(const QString& path)
{
    return QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
}

QString unknownField(const QJsonObject& object, const QSet<QString>& allowed)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowed.contains(it.key())) return it.key();
    }
    return {};
}

QString canonicalPath(const QString& path)
{
    return QDir::fromNativeSeparators(QFileInfo(path).canonicalFilePath());
}

bool isWithin(const QString& child, const QString& parent)
{
    const Qt::CaseSensitivity sensitivity =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    return child.compare(parent, sensitivity) == 0
           || child.startsWith(parent + QLatin1Char('/'), sensitivity);
}

bool isLinkOrJunction(const QFileInfo& info)
{
#ifdef Q_OS_WIN
    return info.isSymLink() || info.isJunction();
#else
    return info.isSymLink();
#endif
}

bool pathTraversesLink(const QString& base, const QString& relativePath)
{
    QString current = base;
    const QStringList parts = QDir::fromNativeSeparators(relativePath).split(
        QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        current = QDir(current).filePath(part);
        if (isLinkOrJunction(QFileInfo(current))) return true;
    }
    return false;
}

bool isExecutableDataPath(const QString& relativePath)
{
    static const QSet<QString> forbiddenExtensions = {
        QStringLiteral("exe"), QStringLiteral("com"), QStringLiteral("dll"),
        QStringLiteral("bat"), QStringLiteral("cmd"), QStringLiteral("ps1"),
        QStringLiteral("sh"),  QStringLiteral("py"),  QStringLiteral("js"),
        QStringLiteral("vbs"), QStringLiteral("msi"), QStringLiteral("scr")};
    return forbiddenExtensions.contains(QFileInfo(relativePath).suffix().toLower());
}

const ApprovedBuiltIn* approvedBuiltIn(const QString& stableId)
{
    for (const ApprovedBuiltIn& approved : kApprovedBuiltIns) {
        if (stableId == QLatin1String(approved.stableId)) return &approved;
    }
    return nullptr;
}

QString validateBuiltIn(const QJsonObject& object, VoiceCloneModel* model)
{
    const QString unknown = unknownField(
        object,
        {QStringLiteral("stableId"),
         QStringLiteral("displayName"),
         QStringLiteral("description"),
         QStringLiteral("provider"),
         QStringLiteral("revision"),
         QStringLiteral("adapterId"),
         QStringLiteral("runtimeId"),
         QStringLiteral("stable"),
         QStringLiteral("requiresLicenseAcceptance"),
         QStringLiteral("license"),
         QStringLiteral("officialUrls"),
         QStringLiteral("capabilityPreview")});
    if (!unknown.isEmpty()) return QStringLiteral("unknown built-in field: %1").arg(unknown);
    const QString stableId = object.value(QStringLiteral("stableId")).toString();
    const ApprovedBuiltIn* approved = approvedBuiltIn(stableId);
    if (!approved) return QStringLiteral("unapproved built-in model: %1").arg(stableId);

    const QString description = object.value(QStringLiteral("description")).toString();
    const QString forbiddenName = QStringLiteral("Vibe") + QStringLiteral("Voice");
    if (description.trimmed().isEmpty()
        || description.contains(forbiddenName, Qt::CaseInsensitive)) {
        return QStringLiteral("invalid product description for %1").arg(stableId);
    }
    const QJsonObject license = object.value(QStringLiteral("license")).toObject();
    const QJsonObject urls = object.value(QStringLiteral("officialUrls")).toObject();
    const QString licenseUnknown = unknownField(
        license, {QStringLiteral("name"), QStringLiteral("url")});
    const QString urlsUnknown = unknownField(
        urls,
        {QStringLiteral("project"),
         QStringLiteral("huggingFace"),
         QStringLiteral("modelScope")});
    if (!licenseUnknown.isEmpty() || !urlsUnknown.isEmpty()) {
        return QStringLiteral("unknown built-in URL or license field for %1").arg(stableId);
    }
    if (object.contains(QStringLiteral("provider"))
        && object.value(QStringLiteral("provider")).toString().trimmed().isEmpty()) {
        return QStringLiteral("invalid provider for %1").arg(stableId);
    }
    if (object.contains(QStringLiteral("revision"))
        && object.value(QStringLiteral("revision")).toString().trimmed().isEmpty()) {
        return QStringLiteral("invalid revision for %1").arg(stableId);
    }
    if (object.contains(QStringLiteral("capabilityPreview"))) {
        if (!object.value(QStringLiteral("capabilityPreview")).isArray()) {
            return QStringLiteral("invalid capability preview for %1").arg(stableId);
        }
        for (const QJsonValue& capability :
             object.value(QStringLiteral("capabilityPreview")).toArray()) {
            if (!capability.isString() || capability.toString().trimmed().isEmpty()) {
                return QStringLiteral("invalid capability preview for %1").arg(stableId);
            }
        }
    }
    const bool matches = object.value(QStringLiteral("stable")).toBool(false)
                         && object.value(QStringLiteral("adapterId")).toString()
                                == QLatin1String(approved->adapterId)
                         && object.value(QStringLiteral("runtimeId")).toString()
                                == QLatin1String(approved->runtimeId)
                         && object.value(QStringLiteral("requiresLicenseAcceptance")).toBool()
                                == approved->licenseGate
                         && license.value(QStringLiteral("name")).toString()
                                == QLatin1String(approved->licenseName)
                         && license.value(QStringLiteral("url")).toString()
                                == QLatin1String(approved->huggingFaceUrl)
                         && urls.value(QStringLiteral("project")).toString()
                                == QLatin1String(approved->projectUrl)
                         && urls.value(QStringLiteral("huggingFace")).toString()
                                == QLatin1String(approved->huggingFaceUrl)
                         && urls.value(QStringLiteral("modelScope")).toString()
                                == QLatin1String(approved->modelScopeUrl);
    if (!matches) return QStringLiteral("built-in metadata mismatch for %1").arg(stableId);

    model->stableId = stableId;
    model->displayName = object.value(QStringLiteral("displayName")).toString();
    model->description = description;
    model->adapterId = QLatin1String(approved->adapterId);
    model->runtimeId = QLatin1String(approved->runtimeId);
    model->revision = object.value(QStringLiteral("revision")).toString();
    model->source = {object.value(QStringLiteral("provider")).toString(),
                     QLatin1String(approved->huggingFaceUrl)};
    model->stable = true;
    model->requiresLicenseAcceptance = approved->licenseGate;
    model->license = {QLatin1String(approved->licenseName),
                      QLatin1String(approved->huggingFaceUrl)};
    model->officialProjectUrl = QLatin1String(approved->projectUrl);
    model->huggingFaceUrl = QLatin1String(approved->huggingFaceUrl);
    model->modelScopeUrl = QLatin1String(approved->modelScopeUrl);
    for (const QJsonValue& capability :
         object.value(QStringLiteral("capabilityPreview")).toArray()) {
        model->capabilityPreview.append(capability.toString());
    }
    return {};
}

void addDiagnostic(VoiceCloneDiscovery* result, const QString& path, const QString& message)
{
    result->diagnostics.append({path, message});
}

} // namespace

QString VoiceCloneDiscovery::errorString() const
{
    QStringList messages;
    for (const VoiceCloneDiagnostic& diagnostic : diagnostics) {
        messages.append(diagnostic.manifestPath.isEmpty()
                            ? diagnostic.message
                            : QStringLiteral("%1: %2").arg(diagnostic.manifestPath,
                                                             diagnostic.message));
    }
    return messages.join(QLatin1Char('\n'));
}

VoiceCloneRegistry VoiceCloneRegistry::loadBuiltIn(const QString& registryPath)
{
    VoiceCloneRegistry result;
    QFile file(registryPath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error_ = QStringLiteral("cannot open built-in registry: %1").arg(registryPath);
        return result;
    }
    const QByteArray bytes = file.readAll();
    const QString forbiddenName = QStringLiteral("Vibe") + QStringLiteral("Voice");
    if (QString::fromUtf8(bytes).contains(forbiddenName, Qt::CaseInsensitive)) {
        result.error_ = QStringLiteral("registry contains an unapproved product name");
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error_ = QStringLiteral("invalid built-in registry JSON: %1").arg(parseError.errorString());
        return result;
    }
    if (QString::fromUtf8(document.toJson(QJsonDocument::Compact))
            .contains(forbiddenName, Qt::CaseInsensitive)) {
        result.error_ = QStringLiteral("registry contains an unapproved product name");
        return result;
    }
    const QJsonObject root = document.object();
    const QString rootUnknown = unknownField(
        root, {QStringLiteral("schemaVersion"), QStringLiteral("models")});
    if (!rootUnknown.isEmpty()) {
        result.error_ = QStringLiteral("unknown built-in registry field: %1").arg(rootUnknown);
        return result;
    }
    if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != 1
        || !root.value(QStringLiteral("models")).isArray()) {
        result.error_ = QStringLiteral("unsupported built-in registry schema");
        return result;
    }
    const QJsonArray models = root.value(QStringLiteral("models")).toArray();
    if (models.size() != static_cast<qsizetype>(std::size(kApprovedBuiltIns))) {
        result.error_ = QStringLiteral("built-in registry must contain exactly four approved models");
        return result;
    }
    QSet<QString> ids;
    for (const QJsonValue& value : models) {
        if (!value.isObject()) {
            result.error_ = QStringLiteral("built-in model must be an object");
            return result;
        }
        VoiceCloneModel model;
        const QString error = validateBuiltIn(value.toObject(), &model);
        if (!error.isEmpty()) {
            result.error_ = error;
            return result;
        }
        if (ids.contains(model.stableId)) {
            result.error_ = QStringLiteral("duplicate built-in model: %1").arg(model.stableId);
            return result;
        }
        ids.insert(model.stableId);
        result.models_.append(std::move(model));
    }
    return result;
}

VoiceCloneDiscovery VoiceCloneRegistry::discoverUserModels(
    const QString& portableRoot, const QStringList& trustedAdapterIds)
{
    VoiceCloneDiscovery result;
    const QString rootCanonical = canonicalPath(portableRoot);
    if (rootCanonical.isEmpty()) {
        addDiagnostic(&result, portableRoot, QStringLiteral("scan root does not exist"));
        return result;
    }
    const QString modelRootPath = QDir(portableRoot).filePath(QStringLiteral("models/voice-clone"));
    if (!QFileInfo::exists(modelRootPath)) return result;
    if (isLinkOrJunction(QFileInfo(modelRootPath))) {
        addDiagnostic(&result, modelRootPath, QStringLiteral("model root link or junction is forbidden"));
        return result;
    }
    const QString modelRootCanonical = canonicalPath(modelRootPath);
    if (modelRootCanonical.isEmpty() || !isWithin(modelRootCanonical, rootCanonical)) {
        addDiagnostic(&result, modelRootPath, QStringLiteral("model root escapes scan root"));
        return result;
    }

    QStringList manifests;
    const QDir modelRoot(modelRootCanonical);
    const QFileInfoList modelDirectories = modelRoot.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& modelDirectory : modelDirectories) {
        if (isLinkOrJunction(modelDirectory)) {
            addDiagnostic(&result, modelDirectory.filePath(),
                          QStringLiteral("model directory link or junction is forbidden"));
            continue;
        }
        const QFileInfoList revisions = QDir(modelDirectory.filePath()).entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
            QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& revision : revisions) {
            if (isLinkOrJunction(revision)) {
                addDiagnostic(&result, revision.filePath(),
                              QStringLiteral("revision link or junction is forbidden"));
                continue;
            }
            const QString manifest = QDir(revision.filePath()).filePath(
                QStringLiteral("agplayer-model.json"));
            if (QFileInfo(manifest).isFile()) manifests.append(manifest);
        }
    }

    QSet<QString> discoveredIds;
    for (const QString& manifestPath : manifests) {
        const QFileInfo manifestInfo(manifestPath);
        if (isLinkOrJunction(manifestInfo)) {
            addDiagnostic(&result, manifestPath, QStringLiteral("manifest link or junction is forbidden"));
            continue;
        }
        QFile manifestFile(manifestPath);
        if (!manifestFile.open(QIODevice::ReadOnly)) {
            addDiagnostic(&result, manifestPath, QStringLiteral("cannot read manifest"));
            continue;
        }
        ManifestParseResult parsed = parseLocalModelManifest(manifestFile.readAll());
        if (!parsed.isValid()) {
            addDiagnostic(&result, manifestPath, parsed.error);
            continue;
        }
        VoiceCloneModel& model = parsed.model;
        if (approvedBuiltIn(model.stableId)) {
            addDiagnostic(&result, manifestPath,
                          QStringLiteral("user model cannot replace built-in ID: %1").arg(model.stableId));
            continue;
        }
        if (discoveredIds.contains(model.stableId)) {
            addDiagnostic(&result, manifestPath,
                          QStringLiteral("duplicate stable ID: %1").arg(model.stableId));
            continue;
        }
        discoveredIds.insert(model.stableId);
        if (!trustedAdapterIds.contains(model.adapterId)) {
            addDiagnostic(&result, manifestPath,
                          QStringLiteral("unknown adapter: %1").arg(model.adapterId));
            continue;
        }
        if (model.runtimeId != model.adapterId) {
            addDiagnostic(&result, manifestPath,
                          QStringLiteral("runtime is not trusted for adapter: %1").arg(model.adapterId));
            continue;
        }

        const QString modelDirectory = canonicalPath(manifestInfo.absolutePath());
        if (modelDirectory.isEmpty() || !isWithin(modelDirectory, modelRootCanonical)) {
            addDiagnostic(&result, manifestPath, QStringLiteral("manifest escapes model root"));
            continue;
        }
        bool allHashed = true;
        bool filesValid = true;
        for (const VoiceCloneRequiredFile& required : model.files) {
            const QString normalized = QDir::fromNativeSeparators(required.relativePath);
            if (QDir::isAbsolutePath(normalized)) {
                addDiagnostic(&result, manifestPath,
                              QStringLiteral("absolute file path is forbidden: %1").arg(required.relativePath));
                filesValid = false;
                break;
            }
            const QStringList parts = normalized.split(QLatin1Char('/'), Qt::KeepEmptyParts);
            if (parts.contains(QStringLiteral(".."))) {
                addDiagnostic(&result, manifestPath,
                              QStringLiteral("file path contains ..: %1").arg(required.relativePath));
                filesValid = false;
                break;
            }
            if (normalized.isEmpty() || parts.contains(QString()) || parts.contains(QStringLiteral("."))) {
                addDiagnostic(&result, manifestPath,
                              QStringLiteral("invalid relative file path: %1").arg(required.relativePath));
                filesValid = false;
                break;
            }
            if (isExecutableDataPath(normalized)) {
                addDiagnostic(&result, manifestPath,
                              QStringLiteral("executable file is not data-only: %1").arg(required.relativePath));
                filesValid = false;
                break;
            }
            if (pathTraversesLink(modelDirectory, normalized)) {
                addDiagnostic(&result, manifestPath,
                              QStringLiteral("file path traverses a link or junction: %1").arg(required.relativePath));
                filesValid = false;
                break;
            }
            const QString lexicalPath = normalizedAbsolute(QDir(modelDirectory).filePath(normalized));
            if (!isWithin(lexicalPath, modelDirectory)) {
                addDiagnostic(&result, manifestPath,
                              QStringLiteral("file path escapes model directory: %1").arg(required.relativePath));
                filesValid = false;
                break;
            }
            const QFileInfo fileInfo(lexicalPath);
            if (!fileInfo.exists() || !fileInfo.isFile()) {
                addDiagnostic(&result, manifestPath,
                              QStringLiteral("missing required file: %1").arg(required.relativePath));
                filesValid = false;
                break;
            }
            const QString fileCanonical = canonicalPath(lexicalPath);
            if (fileCanonical.isEmpty() || !isWithin(fileCanonical, modelDirectory)) {
                addDiagnostic(&result, manifestPath,
                              QStringLiteral("file link or junction escapes model directory: %1")
                                  .arg(required.relativePath));
                filesValid = false;
                break;
            }
            if (required.sha256.isEmpty()) {
                allHashed = false;
                continue;
            }
            QFile file(fileCanonical);
            if (!file.open(QIODevice::ReadOnly)) {
                addDiagnostic(&result, manifestPath,
                              QStringLiteral("cannot read required file: %1").arg(required.relativePath));
                filesValid = false;
                break;
            }
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (!hash.addData(&file)
                || QString::fromLatin1(hash.result().toHex()) != required.sha256) {
                addDiagnostic(&result, manifestPath,
                              QStringLiteral("SHA-256 mismatch for %1").arg(required.relativePath));
                filesValid = false;
                break;
            }
        }
        if (!filesValid) continue;
        model.installState = allHashed ? QStringLiteral("ready")
                                       : QStringLiteral("local-unverified");
        result.models.append(std::move(model));
    }
    return result;
}

QStringList VoiceCloneRegistry::modelIds() const
{
    QStringList ids;
    for (const VoiceCloneModel& model : models_) ids.append(model.stableId);
    return ids;
}

VoiceCloneModel VoiceCloneRegistry::model(const QString& stableId) const
{
    for (const VoiceCloneModel& model : models_) {
        if (model.stableId == stableId) return model;
    }
    return {};
}

} // namespace agplayer::voice_clone

#include "voice_clone_package_manifest.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <limits>
#include <tuple>

namespace agplayer::voice_clone {
namespace {

struct ApprovedModelIdentity {
    const char* modelId;
    const char* adapterId;
    bool requiresLicenseAcceptance;
    const char* modelRevision;
    const char* licenseUrl;
    const char* licenseRevision;
    const char* repository;
    const char* primaryLicenseId;
    const char* primaryLicenseName;
    const char* primaryLicenseSpdx;
};

constexpr ApprovedModelIdentity kApprovedModels[] = {
    {"Qwen/Qwen3-TTS-12Hz-0.6B-Base", "qwen", false,
     "5d83992436eae1d760afd27aff78a71d676296fc",
     "https://github.com/QwenLM/Qwen3-TTS/blob/022e286b98fbec7e1e916cb940cdf532cd9f488e/LICENSE",
     "022e286b98fbec7e1e916cb940cdf532cd9f488e",
     "Qwen/Qwen3-TTS-12Hz-0.6B-Base", "apache-2.0", "Apache-2.0", "Apache-2.0"},
    {"Qwen/Qwen3-TTS-12Hz-1.7B-Base", "qwen", false,
     "fd4b254389122332181a7c3db7f27e918eec64e3",
     "https://github.com/QwenLM/Qwen3-TTS/blob/022e286b98fbec7e1e916cb940cdf532cd9f488e/LICENSE",
     "022e286b98fbec7e1e916cb940cdf532cd9f488e",
     "Qwen/Qwen3-TTS-12Hz-1.7B-Base", "apache-2.0", "Apache-2.0", "Apache-2.0"},
    {"IndexTeam/IndexTTS-2.5", "indextts25", true,
     "c39ce5ba981572cb187443877ff559dfb246ce63",
     "https://huggingface.co/IndexTeam/IndexTTS-2.5/blob/c39ce5ba981572cb187443877ff559dfb246ce63/LICENSE",
     "c39ce5ba981572cb187443877ff559dfb246ce63", "IndexTeam/IndexTTS-2.5",
     "bilibili-model-use-license", "bilibili Model Use License Agreement",
     "LicenseRef-Bilibili-Model-Use"},
    {"FunAudioLLM/Fun-CosyVoice3-0.5B-2512", "cosyvoice3", false,
     "29e01c4e8d000f4bcd70751be16fa94bf3d85a18",
     "https://github.com/FunAudioLLM/CosyVoice/blob/074ca6dc9e80a2f424f1f74b48bdd7d3fea531cc/LICENSE",
     "074ca6dc9e80a2f424f1f74b48bdd7d3fea531cc",
     "FunAudioLLM/Fun-CosyVoice3-0.5B-2512", "apache-2.0", "Apache-2.0", "Apache-2.0"},
};

constexpr const char* kMaskGctLicenseId = "maskgct-cc-by-nc-4.0";
constexpr const char* kMaskGctLicenseUrl =
    "https://huggingface.co/amphion/MaskGCT/blob/265c6cef07625665d0c28d2faafb1415562379dc/README.md";
constexpr const char* kMaskGctLicenseRevision = "265c6cef07625665d0c28d2faafb1415562379dc";

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

bool isAllowedDownloadUrl(const VoiceClonePackageFile& file,
                          const QString& repository,
                          const QString& revision,
                          const VoiceClonePackageValidationPolicy policy)
{
    const QUrl& url = file.url;
    if (policy == VoiceClonePackageValidationPolicy::AllowLoopback
        && isLoopbackHttp(url)) {
        return url.isValid() && !url.host().isEmpty();
    }
    if (!url.isValid() || url.scheme() != QStringLiteral("https")
        || url.host() != QStringLiteral("huggingface.co") || !url.userInfo().isEmpty()
        || (url.port(-1) != -1 && url.port() != 443) || !url.fragment().isEmpty()) {
        return false;
    }
    const QString effectiveRepository = file.sourceRepository.isEmpty()
                                            ? repository : file.sourceRepository;
    const QString effectiveRevision = file.sourceRevision.isEmpty()
                                          ? revision : file.sourceRevision;
    const QString effectivePath = file.sourcePath.isEmpty()
                                      ? file.relativePath : file.sourcePath;
    const QString expectedPath = QStringLiteral("/%1/resolve/%2/%3")
                                     .arg(effectiveRepository, effectiveRevision,
                                          QDir::fromNativeSeparators(effectivePath));
    const QString query = url.query(QUrl::FullyDecoded);
    return url.path(QUrl::FullyDecoded) == expectedPath
           && (query.isEmpty() || query == QStringLiteral("download=true"));
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

QString approvedPackageId(const QString& modelId)
{
    static const QHash<QString, QString> packageIds{
        {QStringLiteral("Qwen/Qwen3-TTS-12Hz-0.6B-Base"),
         QStringLiteral("qwen3-tts-0.6b")},
        {QStringLiteral("Qwen/Qwen3-TTS-12Hz-1.7B-Base"),
         QStringLiteral("qwen3-tts-1.7b")},
        {QStringLiteral("IndexTeam/IndexTTS-2.5"), QStringLiteral("indextts-2.5")},
        {QStringLiteral("FunAudioLLM/Fun-CosyVoice3-0.5B-2512"),
         QStringLiteral("fun-cosyvoice3")},
    };
    return packageIds.value(modelId);
}

bool isApprovedFileSource(const QString& modelId,
                          const QString& repository,
                          const QString& revision)
{
    if (modelId != QStringLiteral("IndexTeam/IndexTTS-2.5")) return false;
    static const QHash<QString, QString> approved{
        {QStringLiteral("facebook/w2v-bert-2.0"),
         QStringLiteral("da985ba0987f70aaeb84a80f2851cfac8c697a7b")},
        {QStringLiteral("amphion/MaskGCT"),
         QStringLiteral("265c6cef07625665d0c28d2faafb1415562379dc")},
        {QStringLiteral("funasr/campplus"),
         QStringLiteral("e4b6ede7ce16997aff4ae69fbca1f0175e2afede")},
        {QStringLiteral("nvidia/bigvgan_v2_22khz_80band_256x"),
         QStringLiteral("633ff708ed5b74903e86ff1298cf4a98e921c513")}};
    return approved.value(repository) == revision;
}

QByteArray approvedFileGraphDigest(const QString& packageId)
{
    static const QHash<QString, QByteArray> approved{
        {QStringLiteral("qwen3-tts-0.6b"),
         QByteArrayLiteral("c4431d0a6eab74a9515b7e0843be4ff9b68eb9a56277c931b04d833e9cc59232")},
        {QStringLiteral("qwen3-tts-1.7b"),
         QByteArrayLiteral("68560411f5a15d2a85f00765ca5230982056a3babf5a66667bfe3cdc8e5c3eb7")},
        {QStringLiteral("indextts-2.5"),
         QByteArrayLiteral("47db9bb86068249a112d96cfa7ebba704f8c9f77c1505c2e6672f7df801ccc61")},
        {QStringLiteral("fun-cosyvoice3"),
         QByteArrayLiteral("6246043fbd2bcec79d8eded67cf7919368a8396223aeec7d52807dd01c4e3f2e")},
    };
    return approved.value(packageId);
}

void appendCanonicalField(QByteArray* bytes, const QString& value)
{
    const QByteArray encoded = value.toUtf8();
    bytes->append(QByteArray::number(encoded.size()));
    bytes->append(':');
    bytes->append(encoded);
    bytes->append('\n');
}

QVector<VoiceClonePackageLicense> approvedPackageLicenses(const QString& modelId,
                                                          const QString& adapterId)
{
    const ApprovedModelIdentity* identity = approvedIdentity(modelId, adapterId);
    if (identity == nullptr) return {};
    QVector<VoiceClonePackageLicense> result{{QString::fromLatin1(identity->primaryLicenseId),
                                              QString::fromLatin1(identity->primaryLicenseName),
                                              QUrl(QString::fromLatin1(identity->licenseUrl)),
                                              QString::fromLatin1(identity->licenseRevision),
                                              QString::fromLatin1(identity->primaryLicenseSpdx),
                                              identity->requiresLicenseAcceptance,
                                              identity->requiresLicenseAcceptance
                                                  ? QStringLiteral("custom-terms") : QString{}}};
    if (!identity->requiresLicenseAcceptance) return result;
    result.append({QString::fromLatin1(kMaskGctLicenseId), QStringLiteral("CC-BY-NC-4.0"),
                   QUrl(QString::fromLatin1(kMaskGctLicenseUrl)),
                   QString::fromLatin1(kMaskGctLicenseRevision),
                   QStringLiteral("CC-BY-NC-4.0"), true,
                   QStringLiteral("non-commercial-only")});
    result.append({QStringLiteral("w2v-bert-mit"), QStringLiteral("MIT"),
                   QUrl(QStringLiteral("https://huggingface.co/facebook/w2v-bert-2.0/blob/da985ba0987f70aaeb84a80f2851cfac8c697a7b/README.md")),
                   QStringLiteral("da985ba0987f70aaeb84a80f2851cfac8c697a7b"),
                   QStringLiteral("MIT"), false, {}});
    result.append({QStringLiteral("campplus-apache-2.0"), QStringLiteral("Apache-2.0"),
                   QUrl(QStringLiteral("https://huggingface.co/funasr/campplus/blob/e4b6ede7ce16997aff4ae69fbca1f0175e2afede/README.md")),
                   QStringLiteral("e4b6ede7ce16997aff4ae69fbca1f0175e2afede"),
                   QStringLiteral("Apache-2.0"), false, {}});
    result.append({QStringLiteral("bigvgan-mit"), QStringLiteral("MIT"),
                   QUrl(QStringLiteral("https://huggingface.co/nvidia/bigvgan_v2_22khz_80band_256x/blob/633ff708ed5b74903e86ff1298cf4a98e921c513/LICENSE")),
                   QStringLiteral("633ff708ed5b74903e86ff1298cf4a98e921c513"),
                   QStringLiteral("MIT"), false, {}});
    return result;
}

} // namespace

QVector<VoiceClonePackageLicense> approvedRequiredLicenses(const QString& modelId,
                                                           const QString& adapterId)
{
    QVector<VoiceClonePackageLicense> result;
    QVector<VoiceClonePackageLicense> licenses = approvedPackageLicenses(modelId, adapterId);
    if (licenses.isEmpty() && adapterId == QStringLiteral("indextts25")) {
        licenses = approvedPackageLicenses(QStringLiteral("IndexTeam/IndexTTS-2.5"),
                                           QStringLiteral("indextts25"));
    }
    for (const VoiceClonePackageLicense& license : licenses) {
        if (license.requiredAcceptance) result.append(license);
    }
    return result;
}

VoiceClonePackageManifest VoiceClonePackageManifest::fromJson(
    const QJsonObject& object,
    const VoiceClonePackageValidationPolicy policy)
{
    VoiceClonePackageManifest result;
    const QString rootUnknown = unknownField(
        object,
        {QStringLiteral("schemaVersion"), QStringLiteral("packageId"),
         QStringLiteral("modelId"), QStringLiteral("adapterId"),
         QStringLiteral("version"), QStringLiteral("revision"),
         QStringLiteral("model"),
         QStringLiteral("fileGraphSha256"),
         QStringLiteral("source"), QStringLiteral("licenses"),
         QStringLiteral("totalBytes"), QStringLiteral("files")});
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
        || !object.value(QStringLiteral("model")).isObject()
        || !object.value(QStringLiteral("source")).isObject()
        || !object.value(QStringLiteral("licenses")).isArray()
        || !object.value(QStringLiteral("totalBytes")).isDouble()
        || !object.value(QStringLiteral("files")).isArray()) {
        result.parseError_ = QStringLiteral("invalid package metadata types");
        return result;
    }

    result.packageId = object.value(QStringLiteral("packageId")).toString();
    result.modelId = object.value(QStringLiteral("modelId")).toString();
    result.adapterId = object.value(QStringLiteral("adapterId")).toString();
    result.version = object.value(QStringLiteral("version")).toString();
    result.revision = object.value(QStringLiteral("revision")).toString();
    const QJsonObject model = object.value(QStringLiteral("model")).toObject();
    const QString modelUnknown = unknownField(
        model, {QStringLiteral("displayName"), QStringLiteral("description")});
    if (!modelUnknown.isEmpty() || !model.value(QStringLiteral("displayName")).isString()
        || !model.value(QStringLiteral("description")).isString()) {
        result.parseError_ = modelUnknown.isEmpty()
                                 ? QStringLiteral("invalid package model metadata")
                                 : QStringLiteral("unknown package model field: %1").arg(modelUnknown);
        return result;
    }
    result.modelDisplayName = model.value(QStringLiteral("displayName")).toString();
    result.modelDescription = model.value(QStringLiteral("description")).toString();
    if (object.contains(QStringLiteral("fileGraphSha256"))) {
        if (!object.value(QStringLiteral("fileGraphSha256")).isString()) {
            result.parseError_ = QStringLiteral("invalid package fileGraphSha256");
            return result;
        }
        result.fileGraphSha256 = object.value(QStringLiteral("fileGraphSha256"))
                                     .toString().toLatin1().toLower();
    }
    result.totalBytes = object.value(QStringLiteral("totalBytes")).toVariant().toLongLong();
    const QJsonObject source = object.value(QStringLiteral("source")).toObject();
    const QString sourceUnknown = unknownField(
        source, {QStringLiteral("provider"), QStringLiteral("repository"),
                 QStringLiteral("url")});
    if (!sourceUnknown.isEmpty() || !source.value(QStringLiteral("provider")).isString()
        || !source.value(QStringLiteral("repository")).isString()
        || !source.value(QStringLiteral("url")).isString()) {
        result.parseError_ = sourceUnknown.isEmpty()
                                 ? QStringLiteral("invalid package source metadata")
                                 : QStringLiteral("unknown package source field: %1").arg(sourceUnknown);
        return result;
    }
    result.sourceProvider = source.value(QStringLiteral("provider")).toString();
    result.sourceRepository = source.value(QStringLiteral("repository")).toString();
    result.sourceUrl = QUrl(source.value(QStringLiteral("url")).toString());

    const QJsonArray licenseArray = object.value(QStringLiteral("licenses")).toArray();
    for (const QJsonValue& value : licenseArray) {
        if (!value.isObject()) {
            result.parseError_ = QStringLiteral("package license entry must be an object");
            return result;
        }
        const QJsonObject license = value.toObject();
        const QString licenseUnknown = unknownField(
            license, {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("url"),
                      QStringLiteral("revision"), QStringLiteral("spdx"),
                      QStringLiteral("requiredAcceptance"), QStringLiteral("useRestriction")});
        if (!licenseUnknown.isEmpty() || !license.value(QStringLiteral("id")).isString()
            || !license.value(QStringLiteral("name")).isString()
            || !license.value(QStringLiteral("url")).isString()
            || !license.value(QStringLiteral("revision")).isString()
            || !license.value(QStringLiteral("spdx")).isString()
            || !license.value(QStringLiteral("requiredAcceptance")).isBool()
            || !license.value(QStringLiteral("useRestriction")).isString()) {
            result.parseError_ = licenseUnknown.isEmpty()
                                     ? QStringLiteral("invalid license metadata")
                                     : QStringLiteral("unknown license field: %1").arg(licenseUnknown);
            return result;
        }
        result.licenses.append({license.value(QStringLiteral("id")).toString(),
                                license.value(QStringLiteral("name")).toString(),
                                QUrl(license.value(QStringLiteral("url")).toString()),
                                license.value(QStringLiteral("revision")).toString(),
                                license.value(QStringLiteral("spdx")).toString(),
                                license.value(QStringLiteral("requiredAcceptance")).toBool(),
                                license.value(QStringLiteral("useRestriction")).toString()});
    }
    if (!result.licenses.isEmpty()) {
        result.licenseUrl = result.licenses.front().url;
        result.licenseRevision = result.licenses.front().revision;
    }

    for (const QJsonValue& value : object.value(QStringLiteral("files")).toArray()) {
        if (!value.isObject()) {
            result.parseError_ = QStringLiteral("package file entry must be an object");
            return result;
        }
        const QJsonObject file = value.toObject();
        const QString fileUnknown = unknownField(
            file,
            {QStringLiteral("path"), QStringLiteral("url"), QStringLiteral("sha256"),
             QStringLiteral("sizeBytes"), QStringLiteral("source")});
        if (!fileUnknown.isEmpty()
            || !file.value(QStringLiteral("path")).isString()
            || !file.value(QStringLiteral("url")).isString()
            || !file.value(QStringLiteral("sha256")).isString()
            || !file.value(QStringLiteral("sizeBytes")).isDouble()) {
            result.parseError_ = fileUnknown.isEmpty()
                                     ? QStringLiteral("invalid package file metadata")
                                     : QStringLiteral("unknown package file field: %1")
                                           .arg(fileUnknown);
            return result;
        }
        VoiceClonePackageFile entry{file.value(QStringLiteral("path")).toString(),
                                    QUrl(file.value(QStringLiteral("url")).toString()),
                                    file.value(QStringLiteral("sha256")).toString().toLatin1(),
                                    file.value(QStringLiteral("sizeBytes")).toVariant().toLongLong()};
        if (file.contains(QStringLiteral("source"))) {
            if (!file.value(QStringLiteral("source")).isObject()) {
                result.parseError_ = QStringLiteral("invalid package file source metadata");
                return result;
            }
            const QJsonObject fileSource = file.value(QStringLiteral("source")).toObject();
            const QString fileSourceUnknown = unknownField(
                fileSource, {QStringLiteral("repository"), QStringLiteral("revision"),
                         QStringLiteral("path")});
            if (!fileSourceUnknown.isEmpty()
                || !fileSource.value(QStringLiteral("repository")).isString()
                || !fileSource.value(QStringLiteral("revision")).isString()
                || !fileSource.value(QStringLiteral("path")).isString()) {
                result.parseError_ = QStringLiteral("invalid package file source metadata");
                return result;
            }
            entry.sourceRepository = fileSource.value(QStringLiteral("repository")).toString();
            entry.sourceRevision = fileSource.value(QStringLiteral("revision")).toString();
            entry.sourcePath = fileSource.value(QStringLiteral("path")).toString();
        }
        result.files.append(entry);
    }
    result.requiresLicenseAcceptance = result.licenseAcceptanceRequired();
    result.parseError_ = result.validationError(policy);
    return result;
}

QString VoiceClonePackageManifest::validationError(
    const VoiceClonePackageValidationPolicy policy) const
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
    if (version.trimmed().isEmpty() || revision.trimmed().isEmpty()
        || modelDisplayName.trimmed().isEmpty() || modelDescription.trimmed().isEmpty()) {
        return QStringLiteral("missing package version or revision");
    }
    const ApprovedModelIdentity* approved = approvedIdentity(modelId, adapterId);
    if (approved == nullptr) {
        return QStringLiteral("unapproved modelId/adapterId package identity");
    }
    if (policy == VoiceClonePackageValidationPolicy::OfficialOnly
        && packageId != approvedPackageId(modelId)) {
        return QStringLiteral("official model packageId does not match the approved package");
    }
    if (sourceProvider != QStringLiteral("hugging-face")
        || sourceRepository != QLatin1String(approved->repository)
        || sourceUrl != QUrl(QStringLiteral("https://huggingface.co/%1").arg(sourceRepository))) {
        return QStringLiteral("package source does not match the approved official repository");
    }
    static const QRegularExpression revisionPattern(QStringLiteral("^[0-9a-f]{40}$"));
    if (!revisionPattern.match(revision).hasMatch())
        return QStringLiteral("package revision must be an immutable commit");
    if (revision != QLatin1String(approved->modelRevision))
        return QStringLiteral("package revision does not match the approved model revision");
    if (licenses.isEmpty()) return QStringLiteral("package has no licenses");
    static const QRegularExpression licenseIdPattern(QStringLiteral("^[a-z0-9][a-z0-9._-]*$"));
    QSet<QString> licenseIds;
    QSet<QString> matchingApprovedLicenses;
    const QVector<VoiceClonePackageLicense> expectedLicenses =
        approvedPackageLicenses(modelId, adapterId);
    for (const VoiceClonePackageLicense& license : licenses) {
        if (!licenseIdPattern.match(license.id).hasMatch() || license.name.trimmed().isEmpty()
            || !license.url.isValid() || license.url.scheme() != QStringLiteral("https")
            || license.url.host().isEmpty() || license.revision.trimmed().isEmpty()
            || license.spdx.trimmed().isEmpty() || licenseIds.contains(license.id)) {
            return QStringLiteral("invalid or duplicate package license identity");
        }
        licenseIds.insert(license.id);
        if (license.requiredAcceptance && !approved->requiresLicenseAcceptance)
            return QStringLiteral("license gate is not approved for this model");
        for (const VoiceClonePackageLicense& expected : expectedLicenses) {
            if (license.id == expected.id && license.name == expected.name
                && license.url == expected.url && license.revision == expected.revision
                && license.spdx == expected.spdx
                && license.requiredAcceptance == expected.requiredAcceptance
                && license.useRestriction == expected.useRestriction) {
                matchingApprovedLicenses.insert(expected.id);
            }
        }
    }
    if (licenses.size() != expectedLicenses.size()
        || matchingApprovedLicenses.size() != expectedLicenses.size()) {
        return QStringLiteral("package license set does not match approved identities");
    }
    if (files.isEmpty()) return QStringLiteral("package has no files");

    static const QRegularExpression hashPattern(QStringLiteral("^[0-9a-fA-F]{64}$"));
    QSet<QString> targets;
    qint64 fileBytes = 0;
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
        if (!isAllowedDownloadUrl(file, sourceRepository, revision, policy)) {
            return QStringLiteral("invalid package file URL: %1").arg(file.url.toString());
        }
        const bool hasAuxiliarySource = !file.sourceRepository.isEmpty()
                                        || !file.sourceRevision.isEmpty()
                                        || !file.sourcePath.isEmpty();
        if (hasAuxiliarySource
            && (file.sourceRepository.trimmed().isEmpty()
                || file.sourceRevision.trimmed().isEmpty()
                || file.sourcePath.trimmed().isEmpty()
                || !isApprovedFileSource(modelId, file.sourceRepository, file.sourceRevision)
                || QDir::isAbsolutePath(file.sourcePath)
                || normalizedRelativePath(file.sourcePath).startsWith(QStringLiteral("../"))
                || hasUnsafeWindowsComponent(
                    QDir::fromNativeSeparators(file.sourcePath)))) {
            return QStringLiteral("package file uses an incomplete or unapproved auxiliary source");
        }
        if (!hashPattern.match(QString::fromLatin1(file.sha256)).hasMatch()) {
            return QStringLiteral("invalid SHA-256 for %1").arg(file.relativePath);
        }
        if (file.expectedBytes < 0)
            return QStringLiteral("invalid expected size for %1").arg(file.relativePath);
        if (file.expectedBytes > (std::numeric_limits<qint64>::max)() - fileBytes)
            return QStringLiteral("package size overflows");
        fileBytes += file.expectedBytes;
        const QString key = pathKey(normalized);
        if (targets.contains(key)) {
            return QStringLiteral("duplicate package target: %1").arg(normalized);
        }
        targets.insert(key);
    }
    if (totalBytes < 0 || totalBytes != fileBytes)
        return QStringLiteral("package totalBytes does not match file sizes");
    const QByteArray approvedGraph = approvedFileGraphDigest(packageId);
    if (!approvedGraph.isEmpty()
        && (fileGraphSha256 != approvedGraph
            || calculatedFileGraphSha256() != approvedGraph)) {
        return QStringLiteral("official package file graph does not match the approved digest");
    }
    if (approvedGraph.isEmpty() && !fileGraphSha256.isEmpty()) {
        return QStringLiteral("fileGraphSha256 is reserved for approved official packages");
    }
    return {};
}

QByteArray VoiceClonePackageManifest::calculatedFileGraphSha256() const
{
    QByteArray canonical("agplayer-file-graph-v1\n");
    appendCanonicalField(&canonical, modelDisplayName);
    appendCanonicalField(&canonical, modelDescription);

    QVector<VoiceClonePackageLicense> sortedLicenses = licenses;
    std::sort(sortedLicenses.begin(), sortedLicenses.end(),
              [](const VoiceClonePackageLicense& left,
                 const VoiceClonePackageLicense& right) {
                  return left.id < right.id;
              });
    appendCanonicalField(&canonical, QString::number(sortedLicenses.size()));
    for (const VoiceClonePackageLicense& license : sortedLicenses) {
        appendCanonicalField(&canonical, license.id);
        appendCanonicalField(&canonical, license.name);
        appendCanonicalField(&canonical, license.url.toString(QUrl::FullyEncoded));
        appendCanonicalField(&canonical, license.revision);
        appendCanonicalField(&canonical, license.spdx);
        appendCanonicalField(&canonical,
                             license.requiredAcceptance ? QStringLiteral("1")
                                                        : QStringLiteral("0"));
        appendCanonicalField(&canonical, license.useRestriction);
    }

    struct CanonicalFile {
        QString repository;
        QString revision;
        QString remotePath;
        QString relativePath;
        QByteArray sha256;
        qint64 size = -1;
    };
    QVector<CanonicalFile> sortedFiles;
    sortedFiles.reserve(files.size());
    for (const VoiceClonePackageFile& file : files) {
        sortedFiles.append({file.sourceRepository.isEmpty() ? sourceRepository
                                                             : file.sourceRepository,
                            file.sourceRevision.isEmpty() ? revision : file.sourceRevision,
                            normalizedRelativePath(file.sourcePath.isEmpty()
                                                       ? file.relativePath : file.sourcePath),
                            normalizedRelativePath(file.relativePath),
                            file.sha256.toLower(), file.expectedBytes});
    }
    std::sort(sortedFiles.begin(), sortedFiles.end(),
              [](const CanonicalFile& left, const CanonicalFile& right) {
                  return std::tie(left.repository, left.revision, left.remotePath,
                                  left.relativePath)
                         < std::tie(right.repository, right.revision, right.remotePath,
                                    right.relativePath);
              });
    appendCanonicalField(&canonical, QString::number(sortedFiles.size()));
    for (const CanonicalFile& file : sortedFiles) {
        appendCanonicalField(&canonical, file.repository);
        appendCanonicalField(&canonical, file.revision);
        appendCanonicalField(&canonical, file.remotePath);
        appendCanonicalField(&canonical, file.relativePath);
        appendCanonicalField(&canonical, QString::fromLatin1(file.sha256));
        appendCanonicalField(&canonical, QString::number(file.size));
    }
    return QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex();
}

bool VoiceClonePackageManifest::isValid(const VoiceClonePackageValidationPolicy policy) const
{
    return parseError_.isEmpty() && validationError(policy).isEmpty();
}

QString VoiceClonePackageManifest::errorString(
    const VoiceClonePackageValidationPolicy policy) const
{
    return parseError_.isEmpty() ? validationError(policy) : parseError_;
}

bool VoiceClonePackageManifest::licenseAcceptanceRequired() const
{
    for (const VoiceClonePackageLicense& license : licenses) {
        if (license.requiredAcceptance) return true;
    }
    return false;
}

} // namespace agplayer::voice_clone

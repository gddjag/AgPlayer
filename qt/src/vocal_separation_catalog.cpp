#include "vocal_separation_catalog.hpp"

#include <QJsonArray>
#include <QSet>

namespace {

constexpr qint64 kMaxCustomModelBytes = 1024LL * 1024LL * 1024LL;

VocalDownloadFile modelFile(const QString& name, const QString& url,
                            qint64 bytes, const QString& sha256)
{
    return {name, QUrl(url), bytes, sha256};
}

CustomManifestValidationResult reject(const QString& error)
{
    return {false, error};
}

bool hasOnlyKeys(const QJsonObject& object, const QSet<QString>& allowed)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowed.contains(it.key())) {
            return false;
        }
    }
    return true;
}

bool isSafeFileName(const QString& name)
{
    return !name.isEmpty() && name == name.trimmed() && !name.startsWith('.')
        && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\'))
        && !name.contains(QStringLiteral(".."))
        && name.endsWith(QStringLiteral(".onnx"), Qt::CaseInsensitive);
}

bool isSha256(const QString& value)
{
    if (value.size() != 64) {
        return false;
    }
    for (const QChar character : value) {
        if (!((character >= QLatin1Char('0') && character <= QLatin1Char('9'))
              || (character >= QLatin1Char('a') && character <= QLatin1Char('f')))) {
            return false;
        }
    }
    return true;
}

bool validShape(const QJsonArray& shape)
{
    if (shape.size() != 3 || shape.at(0).toInteger(-1) != 1
        || shape.at(1).toInteger(-1) != 2) {
        return false;
    }
    const qint64 samples = shape.at(2).toInteger(-1);
    return samples >= 256 && samples <= 1'048'576 && samples % 256 == 0;
}

} // namespace

QList<VocalModelCard> VocalSeparationCatalog::models()
{
    return {
        {QStringLiteral("uvr-mdxnet-kara"), VocalModelFamily::Mdx,
         {modelFile(QStringLiteral("UVR_MDXNET_KARA.onnx"),
                    QStringLiteral("https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/UVR_MDXNET_KARA.onnx"),
                    29'704'436,
                    QStringLiteral("e3167c87333a48548413e972a286bf40bf5694001d2853861eb1435953f02d63"))},
         {QStringLiteral("vocals"), QStringLiteral("instrumental")},
         QStringLiteral("UVR public model repository"),
         QStringLiteral("Recommended for karaoke-style vocal removal; requires about 30 MB disk space."),
         QStringLiteral("MDX KARA"),
         QStringLiteral("人声移除与卡拉 OK 伴奏"),
         QStringLiteral("极速轻量"),
         QStringLiteral("低配优先"),
         QStringLiteral("TRvlvr / UVR Community"),
         QStringLiteral("https://github.com/TRvlvr/model_repo/releases/tag/all_public_uvr_models")},
        {QStringLiteral("uvr-mdx-net-inst-hq3"), VocalModelFamily::Mdx,
         {modelFile(QStringLiteral("UVR-MDX-NET-Inst_HQ_3.onnx"),
                    QStringLiteral("https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/UVR-MDX-NET-Inst_HQ_3.onnx"),
                    66'759'214,
                    QStringLiteral("317554b07fe1ea5279a77f2b1520a41ea4b93432560c4ffd08792c30fddf9adc"))},
         {QStringLiteral("vocals"), QStringLiteral("instrumental")},
         QStringLiteral("UVR public model repository"),
         QStringLiteral("Higher-quality MDX option; requires about 67 MB disk space."),
         QStringLiteral("MDX Inst HQ 3"),
         QStringLiteral("高质量人声与伴奏二轨分离"),
         QStringLiteral("KTV 纯净伴奏"),
         QStringLiteral("推荐"),
         QStringLiteral("TRvlvr / Ultimate Vocal Remover"),
         QStringLiteral("https://github.com/TRvlvr/model_repo/releases/tag/all_public_uvr_models")},
        {QStringLiteral("htdemucs-ft-fp16"), VocalModelFamily::Demucs,
         {modelFile(QStringLiteral("htdemucs_ft_bass_fp16weights.onnx"),
                    QStringLiteral("https://huggingface.co/StemSplitio/htdemucs-ft-onnx/resolve/main/htdemucs_ft_bass_fp16weights.onnx"),
                    165'612'636,
                    QStringLiteral("b533037176b14b2df31c92a5d5b3d5660d0811b9b360d3db761964768b079961")),
          modelFile(QStringLiteral("htdemucs_ft_drums_fp16weights.onnx"),
                    QStringLiteral("https://huggingface.co/StemSplitio/htdemucs-ft-onnx/resolve/main/htdemucs_ft_drums_fp16weights.onnx"),
                    165'612'636,
                    QStringLiteral("047764dff888cfb87da917013377d4ec7a134f7419cbe486d9c339aa17975ddd")),
          modelFile(QStringLiteral("htdemucs_ft_other_fp16weights.onnx"),
                    QStringLiteral("https://huggingface.co/StemSplitio/htdemucs-ft-onnx/resolve/main/htdemucs_ft_other_fp16weights.onnx"),
                    165'612'636,
                    QStringLiteral("b739171a7057b3107bb0711c6222d4a619b41b13a8f04026431d30f32ad2bd71")),
          modelFile(QStringLiteral("htdemucs_ft_vocals_fp16weights.onnx"),
                    QStringLiteral("https://huggingface.co/StemSplitio/htdemucs-ft-onnx/resolve/main/htdemucs_ft_vocals_fp16weights.onnx"),
                    165'612'636,
                    QStringLiteral("0cbe651f535415c9d26a7bb614f7d322dd5a080fa0298f2e50f478030a994dce"))},
         {QStringLiteral("vocals"), QStringLiteral("instrumental"),
          QStringLiteral("drums"), QStringLiteral("bass"), QStringLiteral("other")},
         QStringLiteral("Original model Meta Demucs; ONNX conversion StemSplit"),
         QStringLiteral("Five-stem model; requires about 663 MB disk space and substantially more RAM."),
         QStringLiteral("HTDemucs FT FP16"),
         QStringLiteral("鼓组、贝斯、其他、人声与派生伴奏五轨分离"),
         QStringLiteral("标准音质"),
         QStringLiteral("高品质"),
         QStringLiteral("Meta Demucs / StemSplit ONNX conversion"),
         QStringLiteral("https://huggingface.co/StemSplitio/htdemucs-ft-onnx")},
    };
}

bool isSafeModelId(const QString& id)
{
    if (id.isEmpty() || id != id.trimmed() || id.size() > 96
        || id.startsWith('.') || id.contains(QStringLiteral(".."))) {
        return false;
    }
    for (const QChar character : id) {
        if (!(character.isLetterOrNumber() || character == QLatin1Char('-')
              || character == QLatin1Char('_') || character == QLatin1Char('.'))) {
            return false;
        }
    }
    return true;
}

QUrl vocalDomesticMirrorUrl(const QUrl& source)
{
    if (source.scheme() != QStringLiteral("https")
        || source.host().compare(QStringLiteral("huggingface.co"),
                                 Qt::CaseInsensitive) != 0) {
        return {};
    }
    QUrl mirror(source);
    mirror.setHost(QStringLiteral("hf-mirror.com"));
    return mirror;
}

VocalRuntimePackage VocalSeparationCatalog::directMlRuntime()
{
    return {QStringLiteral("onnxruntime-directml-1.24.4"),
            QUrl(QStringLiteral("https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/1.24.4/microsoft.ml.onnxruntime.directml.1.24.4.nupkg")),
            12'458'649,
            QStringLiteral("57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681")};
}

VocalRuntimePackage VocalSeparationCatalog::nativeRuntime()
{
#ifdef Q_OS_MACOS
    // 1.20+ macOS release binaries require 13.3 or newer. Both native slices
    // in this pinned Microsoft package declare macOS 11.0 in LC_BUILD_VERSION.
#ifdef Q_PROCESSOR_ARM_64
    const QString id = QStringLiteral("onnxruntime-macos-arm64-1.18.1");
#else
    const QString id = QStringLiteral("onnxruntime-macos-x86_64-1.18.1");
#endif
    return {id,
            QUrl(QStringLiteral("https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/1.18.1/microsoft.ml.onnxruntime.1.18.1.nupkg")),
            103'790'494,
            QStringLiteral("e1fdeb5359407c948569905b4eb76af50b8bdb38b2bd3f2ad6516fe4a7186d6a")};
#else
    return directMlRuntime();
#endif
}

CustomManifestValidationResult validateCustomModelManifest(const QJsonObject& manifest)
{
    static const QSet<QString> manifestKeys{
        QStringLiteral("id"), QStringLiteral("family"), QStringLiteral("stems"),
        QStringLiteral("files"), QStringLiteral("profile")};
    static const QSet<QString> fileKeys{
        QStringLiteral("name"), QStringLiteral("bytes"), QStringLiteral("sha256"),
        QStringLiteral("shape"), QStringLiteral("role")};
    if (!hasOnlyKeys(manifest, manifestKeys)
        || !isSafeModelId(manifest.value(QStringLiteral("id")).toString())) {
        return reject(QStringLiteral("Manifest contains unsupported fields or has no id"));
    }
    const QString family = manifest.value(QStringLiteral("family")).toString();
    if (family != QStringLiteral("MDX") && family != QStringLiteral("Demucs")) {
        return reject(QStringLiteral("Only MDX and Demucs ONNX manifests are supported"));
    }
    const QJsonArray stems = manifest.value(QStringLiteral("stems")).toArray();
    const QStringList expectedStems = family == QStringLiteral("MDX")
        ? QStringList{QStringLiteral("vocals"), QStringLiteral("instrumental")}
        : QStringList{QStringLiteral("vocals"), QStringLiteral("instrumental"),
                      QStringLiteral("drums"), QStringLiteral("bass"), QStringLiteral("other")};
    QStringList suppliedStems;
    for (const QJsonValue& stem : stems) {
        suppliedStems.push_back(stem.toString());
    }
    if (suppliedStems != expectedStems) {
        return reject(QStringLiteral("Manifest declares unsupported stems"));
    }
    const QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
    if (files.isEmpty() || (family == QStringLiteral("MDX") && files.size() != 1)
        || (family == QStringLiteral("Demucs") && files.size() != 4)) {
        return reject(QStringLiteral("Manifest has an invalid ONNX file set"));
    }
    QSet<QString> names;
    for (const QJsonValue& value : files) {
        const QJsonObject file = value.toObject();
        if (file.isEmpty() || !hasOnlyKeys(file, fileKeys)) {
            return reject(QStringLiteral("Manifest file contains unsupported fields"));
        }
        const QString name = file.value(QStringLiteral("name")).toString();
        const qint64 bytes = file.value(QStringLiteral("bytes")).toInteger(-1);
        if (!isSafeFileName(name) || names.contains(name.toCaseFolded())
            || bytes <= 0 || bytes > kMaxCustomModelBytes
            || !isSha256(file.value(QStringLiteral("sha256")).toString())
            || !validShape(file.value(QStringLiteral("shape")).toArray())) {
            return reject(QStringLiteral("Manifest contains an invalid ONNX file"));
        }
        names.insert(name.toCaseFolded());
    }
    return {true, {}};
}

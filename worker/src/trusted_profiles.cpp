#include "trusted_profiles.hpp"

#include <algorithm>
#include <limits>

namespace agplayer::separation {
namespace {

QStringList normalized(QStringList hashes)
{
    for (QString& hash : hashes) hash = hash.toLower();
    std::sort(hashes.begin(), hashes.end());
    return hashes;
}

const QVector<TrustedModelProfile>& profiles()
{
    static const QVector<TrustedModelProfile> value{
        {QStringLiteral("uvr-mdxnet-kara"), QStringLiteral("mdx"),
         {QStringLiteral("e3167c87333a48548413e972a286bf40bf5694001d2853861eb1435953f02d63")},
         {29'704'436},
         13,
         {{QStringLiteral("input"), TensorElementType::Float32, {-1, 4, 2048, 256}}},
         {{QStringLiteral("output"), TensorElementType::Float32, {-1, 4, 2048, 256}}}},
        {QStringLiteral("uvr-mdx-net-inst-hq3"), QStringLiteral("mdx"),
         {QStringLiteral("317554b07fe1ea5279a77f2b1520a41ea4b93432560c4ffd08792c30fddf9adc")},
         {66'759'214},
         13,
         {{QStringLiteral("input"), TensorElementType::Float32, {-1, 4, 3072, 256}}},
         {{QStringLiteral("output"), TensorElementType::Float32, {-1, 4, 3072, 256}}}},
        // UVR publisher contracts; full-file SHA-256 binds local discovery to
        // the reviewed artifact rather than inferring semantics from its name.
        {QStringLiteral("kim-vocal-2"), QStringLiteral("mdx"),
         {QStringLiteral("ce74ef3b6a6024ce44211a07be9cf8bc6d87728cc852a68ab34eb8e58cde9c8b")},
         {66'759'214}, 13,
         {{QStringLiteral("input"), TensorElementType::Float32, {-1, 4, 3072, 256}}},
         {{QStringLiteral("output"), TensorElementType::Float32, {-1, 4, 3072, 256}}}},
        {QStringLiteral("uvr-mdx-net-inst-hq1"), QStringLiteral("mdx"),
         {QStringLiteral("38a045c4ded87e3bf97b609ec5be7910e8a7cecec455f507227ab12b5e29f7f9")},
         {66'759'214}, 13,
         {{QStringLiteral("input"), TensorElementType::Float32, {-1, 4, 3072, 256}}},
         {{QStringLiteral("output"), TensorElementType::Float32, {-1, 4, 3072, 256}}}},
        {QStringLiteral("uvr-mdx-net-voc-ft"), QStringLiteral("mdx"),
         {QStringLiteral("534b2070fcc7df514b13ef660dc8cbb328679c2374d04354a5c42bb14ecce111")},
         {66'762'490}, 13,
         {{QStringLiteral("input"), TensorElementType::Float32, {-1, 4, 3072, 256}}},
         {{QStringLiteral("output"), TensorElementType::Float32, {-1, 4, 3072, 256}}}},
        {QStringLiteral("htdemucs-ft-fp16"), QStringLiteral("demucs"),
         {QStringLiteral("b533037176b14b2df31c92a5d5b3d5660d0811b9b360d3db761964768b079961"),
          QStringLiteral("047764dff888cfb87da917013377d4ec7a134f7419cbe486d9c339aa17975ddd"),
          QStringLiteral("b739171a7057b3107bb0711c6222d4a619b41b13a8f04026431d30f32ad2bd71"),
          QStringLiteral("0cbe651f535415c9d26a7bb614f7d322dd5a080fa0298f2e50f478030a994dce")},
         {165'612'636, 165'612'636, 165'612'636, 165'612'636},
         17,
         {{QStringLiteral("mix"), TensorElementType::Float32, {1, 2, 343980}}},
         {{QStringLiteral("stems"), TensorElementType::Float32, {1, 4, 2, 343980}}}},
    };
    return value;
}

ContractValidationResult reject(const QString& code, const QString& message)
{
    return {false, code, message};
}

ContractValidationResult compareTensors(const QVector<TensorContract>& expected,
                                        const QVector<TensorContract>& actual)
{
    if (expected.size() != actual.size()) {
        return reject(QStringLiteral("tensor_count_mismatch"),
                      QStringLiteral("Model tensor count does not match its trusted profile"));
    }
    for (qsizetype index = 0; index < expected.size(); ++index) {
        if (expected.at(index).name != actual.at(index).name) {
            return reject(QStringLiteral("tensor_name_mismatch"),
                          QStringLiteral("Model tensor name does not match its trusted profile"));
        }
        if (expected.at(index).type != actual.at(index).type) {
            return reject(QStringLiteral("tensor_type_mismatch"),
                          QStringLiteral("Model tensor type does not match its trusted profile"));
        }
        if (expected.at(index).shape != actual.at(index).shape) {
            return reject(QStringLiteral("tensor_shape_mismatch"),
                          QStringLiteral("Model tensor shape does not match its trusted profile"));
        }
    }
    return {true, {}, {}};
}

} // namespace

std::optional<TrustedModelProfile> trustedProfileForHashes(const QStringList& sha256)
{
    const QStringList wanted = normalized(sha256);
    for (const TrustedModelProfile& profile : profiles()) {
        if (normalized(profile.sha256) == wanted) return profile;
    }
    return std::nullopt;
}

CustomProfileResolution customProfileForDeclaration(
    const QString& profileId, const QStringList& sha256,
    const QVector<qint64>& expectedSizeBytes, const QStringList& roles)
{
    const auto profileIt = std::find_if(
        profiles().cbegin(), profiles().cend(),
        [&profileId](const TrustedModelProfile& profile) {
            return profile.id == profileId;
        });
    if (profileIt == profiles().cend()) {
        return {false, QStringLiteral("model_profile_unknown"),
                QStringLiteral("Custom model names an unknown execution profile"), {}};
    }
    const bool demucs = profileIt->family == QStringLiteral("demucs");
    const qsizetype expectedFiles = demucs ? 4 : 1;
    if (sha256.size() != expectedFiles
        || expectedSizeBytes.size() != expectedFiles) {
        return {false, QStringLiteral("model_declaration_invalid"),
                QStringLiteral("Custom model file declaration is incomplete"), {}};
    }
    constexpr qint64 kMaximumModelFileBytes = 1024LL * 1024LL * 1024LL;
    for (qsizetype index = 0; index < sha256.size(); ++index) {
        const QString hash = sha256.at(index);
        if (hash.size() != 64
            || !std::all_of(hash.cbegin(), hash.cend(), [](const QChar ch) {
                return ch.isDigit()
                    || (ch.toLower() >= QLatin1Char('a')
                        && ch.toLower() <= QLatin1Char('f'));
            })
            || expectedSizeBytes.at(index) <= 0
            || expectedSizeBytes.at(index) > kMaximumModelFileBytes) {
            return {false, QStringLiteral("model_declaration_invalid"),
                    QStringLiteral("Custom model fingerprint or size is invalid"), {}};
        }
    }
    if ((!demucs && !roles.isEmpty())
        || (demucs && normalized(roles)
            != normalized({QStringLiteral("drums"), QStringLiteral("bass"),
                           QStringLiteral("other"), QStringLiteral("vocals")}))) {
        return {false, QStringLiteral("model_declaration_invalid"),
                QStringLiteral("Custom model roles do not match the execution profile"), {}};
    }
    TrustedModelProfile resolved = *profileIt;
    resolved.sha256 = sha256;
    for (QString& hash : resolved.sha256) hash = hash.toLower();
    resolved.expectedSizeBytes = expectedSizeBytes;
    return {true, {}, {}, resolved};
}

QVector<TrustedModelFile> trustedFilesForProfile(
    const TrustedModelProfile& profile)
{
    QVector<TrustedModelFile> result;
    if (profile.sha256.size() != profile.expectedSizeBytes.size()) return result;
    result.reserve(profile.sha256.size());
    for (qsizetype index = 0; index < profile.sha256.size(); ++index) {
        result.push_back({profile.sha256.at(index),
                          profile.expectedSizeBytes.at(index)});
    }
    return result;
}

QVector<TrustedModelFile> allTrustedModelFiles()
{
    QVector<TrustedModelFile> result;
    for (const TrustedModelProfile& profile : profiles()) {
        result += trustedFilesForProfile(profile);
    }
    return result;
}

int trustedDemucsRowForHash(const QString& sha256)
{
    const QString normalizedHash = sha256.toLower();
    if (normalizedHash == QStringLiteral(
            "047764dff888cfb87da917013377d4ec7a134f7419cbe486d9c339aa17975ddd")) {
        return 0; // drums
    }
    if (normalizedHash == QStringLiteral(
            "b533037176b14b2df31c92a5d5b3d5660d0811b9b360d3db761964768b079961")) {
        return 1; // bass
    }
    if (normalizedHash == QStringLiteral(
            "b739171a7057b3107bb0711c6222d4a619b41b13a8f04026431d30f32ad2bd71")) {
        return 2; // other
    }
    if (normalizedHash == QStringLiteral(
            "0cbe651f535415c9d26a7bb614f7d322dd5a080fa0298f2e50f478030a994dce")) {
        return 3; // vocals
    }
    return -1;
}

ContractValidationResult validateModelMetadata(const TrustedModelProfile& trusted,
                                               const ModelMetadata& actual)
{
    if (actual.opset != trusted.minimumOpset) {
        return reject(QStringLiteral("opset_mismatch"),
                      QStringLiteral("Model opset does not match its trusted profile"));
    }
    const ContractValidationResult inputs = compareTensors(trusted.inputs, actual.inputs);
    if (!inputs.ok) return inputs;
    return compareTensors(trusted.outputs, actual.outputs);
}

namespace {

bool readBufferVarint(const QByteArray& buffer, qsizetype* offset, quint64* value)
{
    *value = 0;
    for (int shift = 0; shift < 64 && *offset < buffer.size(); shift += 7) {
        const quint8 octet = static_cast<quint8>(buffer.at((*offset)++));
        *value |= static_cast<quint64>(octet & 0x7fU) << shift;
        if ((octet & 0x80U) == 0) return true;
    }
    return false;
}

int parseOpsetImport(const QByteArray& message)
{
    qsizetype offset = 0;
    QByteArray domain;
    int version = -1;
    while (offset < message.size()) {
        quint64 tag = 0;
        if (!readBufferVarint(message, &offset, &tag)) return -1;
        const quint64 field = tag >> 3U;
        const quint64 wire = tag & 7U;
        if (field == 1 && wire == 2) {
            quint64 length = 0;
            if (!readBufferVarint(message, &offset, &length)
                || length > static_cast<quint64>(message.size() - offset)) return -1;
            domain = message.mid(offset, static_cast<qsizetype>(length));
            offset += static_cast<qsizetype>(length);
        } else if (field == 2 && wire == 0) {
            quint64 parsedVersion = 0;
            if (!readBufferVarint(message, &offset, &parsedVersion)
                || parsedVersion > static_cast<quint64>((std::numeric_limits<int>::max)())) {
                return -1;
            }
            version = static_cast<int>(parsedVersion);
        } else if (wire == 0) {
            quint64 ignored = 0;
            if (!readBufferVarint(message, &offset, &ignored)) return -1;
        } else if (wire == 1) {
            offset += 8;
        } else if (wire == 2) {
            quint64 length = 0;
            if (!readBufferVarint(message, &offset, &length)
                || length > static_cast<quint64>(message.size() - offset)) return -1;
            offset += static_cast<qsizetype>(length);
        } else if (wire == 5) {
            offset += 4;
        } else {
            return -1;
        }
        if (offset > message.size()) return -1;
    }
    return domain.isEmpty() ? version : -1;
}

} // namespace

int readOnnxDefaultOpset(const QByteArray& modelBytes)
{
    qsizetype offset = 0;
    while (offset < modelBytes.size()) {
        quint64 tag = 0;
        if (!readBufferVarint(modelBytes, &offset, &tag)) return -1;
        const quint64 field = tag >> 3U;
        const quint64 wire = tag & 7U;
        if (wire == 0) {
            quint64 ignored = 0;
            if (!readBufferVarint(modelBytes, &offset, &ignored)) return -1;
        } else if (wire == 1) {
            offset += 8;
        } else if (wire == 2) {
            quint64 length = 0;
            if (!readBufferVarint(modelBytes, &offset, &length)
                || length > static_cast<quint64>(modelBytes.size() - offset)) return -1;
            if (field == 8) {
                if (length > 4096) return -1;
                const QByteArray import = modelBytes.mid(
                    offset, static_cast<qsizetype>(length));
                const int opset = parseOpsetImport(import);
                if (opset >= 0) return opset;
            }
            offset += static_cast<qsizetype>(length);
        } else if (wire == 5) {
            offset += 4;
        } else {
            return -1;
        }
        if (offset > modelBytes.size()) return -1;
    }
    return -1;
}

} // namespace agplayer::separation

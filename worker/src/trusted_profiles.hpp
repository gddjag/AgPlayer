#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace agplayer::separation {

enum class TensorElementType {
    Float32,
    Float16,
};

struct TensorContract {
    QString name;
    TensorElementType type = TensorElementType::Float32;
    QVector<qint64> shape;

    bool operator==(const TensorContract& other) const
    {
        return name == other.name && type == other.type && shape == other.shape;
    }
};

struct TrustedModelProfile {
    QString id;
    QString family;
    QStringList sha256;
    QVector<qint64> expectedSizeBytes;
    int minimumOpset = 0;
    QVector<TensorContract> inputs;
    QVector<TensorContract> outputs;
};

struct TrustedModelFile {
    QString sha256;
    qint64 expectedSizeBytes = 0;
};

struct ModelMetadata {
    int opset = 0;
    QVector<TensorContract> inputs;
    QVector<TensorContract> outputs;
};

struct ContractValidationResult {
    bool ok = false;
    QString code;
    QString message;
};

struct CustomProfileResolution {
    bool ok = false;
    QString code;
    QString message;
    TrustedModelProfile profile;
};

[[nodiscard]] std::optional<TrustedModelProfile>
trustedProfileForHashes(const QStringList& sha256);
[[nodiscard]] CustomProfileResolution customProfileForDeclaration(
    const QString& profileId, const QStringList& sha256,
    const QVector<qint64>& expectedSizeBytes, const QStringList& roles);
[[nodiscard]] QVector<TrustedModelFile> allTrustedModelFiles();
[[nodiscard]] QVector<TrustedModelFile>
trustedFilesForProfile(const TrustedModelProfile& profile);
[[nodiscard]] int trustedDemucsRowForHash(const QString& sha256);
[[nodiscard]] ContractValidationResult
validateModelMetadata(const TrustedModelProfile& trusted,
                      const ModelMetadata& actual);
[[nodiscard]] int readOnnxDefaultOpset(const QByteArray& modelBytes);

} // namespace agplayer::separation

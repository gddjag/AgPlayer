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
    int minimumOpset = 0;
    QVector<TensorContract> inputs;
    QVector<TensorContract> outputs;
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

[[nodiscard]] std::optional<TrustedModelProfile>
trustedProfileForHashes(const QStringList& sha256);
[[nodiscard]] int trustedDemucsRowForHash(const QString& sha256);
[[nodiscard]] ContractValidationResult
validateModelMetadata(const TrustedModelProfile& trusted,
                      const ModelMetadata& actual);
[[nodiscard]] int readOnnxDefaultOpset(const QByteArray& modelBytes);

} // namespace agplayer::separation

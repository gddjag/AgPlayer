#include "separation_protocol.hpp"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

#include <optional>

namespace agplayer::separation {
namespace {

ProtocolParseResult reject(const QString& code, const QString& message,
                           const QString& requestId = {})
{
    ProtocolParseResult result;
    result.error = {code, message, requestId};
    return result;
}

std::optional<ProtocolType> protocolType(const QString& name)
{
    if (name == QStringLiteral("hello")) return ProtocolType::Hello;
    if (name == QStringLiteral("probe")) return ProtocolType::Probe;
    if (name == QStringLiteral("start")) return ProtocolType::Start;
    if (name == QStringLiteral("progress")) return ProtocolType::Progress;
    if (name == QStringLiteral("cancel")) return ProtocolType::Cancel;
    if (name == QStringLiteral("result")) return ProtocolType::Result;
    if (name == QStringLiteral("error")) return ProtocolType::Error;
    if (name == QStringLiteral("shutdown")) return ProtocolType::Shutdown;
    return std::nullopt;
}

} // namespace

QString protocolTypeName(ProtocolType type)
{
    switch (type) {
    case ProtocolType::Hello: return QStringLiteral("hello");
    case ProtocolType::Probe: return QStringLiteral("probe");
    case ProtocolType::Start: return QStringLiteral("start");
    case ProtocolType::Progress: return QStringLiteral("progress");
    case ProtocolType::Cancel: return QStringLiteral("cancel");
    case ProtocolType::Result: return QStringLiteral("result");
    case ProtocolType::Error: return QStringLiteral("error");
    case ProtocolType::Shutdown: return QStringLiteral("shutdown");
    }
    return QStringLiteral("error");
}

ProtocolParseResult parseProtocolMessage(const QByteArray& line)
{
    if (line.size() > kMaximumProtocolLineBytes) {
        return reject(QStringLiteral("message_too_large"),
                      QStringLiteral("Protocol message exceeds the 1 MiB limit"));
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return reject(QStringLiteral("malformed_json"),
                      QStringLiteral("Input is not valid JSON"));
    }
    if (!document.isObject()) {
        return reject(QStringLiteral("malformed_message"),
                      QStringLiteral("Protocol message must be a JSON object"));
    }

    const QJsonObject object = document.object();
    static const QSet<QString> allowedKeys{
        QStringLiteral("protocol"), QStringLiteral("requestId"),
        QStringLiteral("type"), QStringLiteral("payload")};
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowedKeys.contains(it.key())) {
            return reject(QStringLiteral("malformed_message"),
                          QStringLiteral("Protocol message contains unsupported fields"));
        }
    }

    const QJsonValue requestValue = object.value(QStringLiteral("requestId"));
    const QString requestId = requestValue.toString();
    if (!requestValue.isString() || requestId.isEmpty() || requestId.size() > 128
        || requestId.contains(QChar::Null)) {
        return reject(QStringLiteral("invalid_request_id"),
                      QStringLiteral("requestId must be a non-empty string of at most 128 characters"));
    }

    const QJsonValue versionValue = object.value(QStringLiteral("protocol"));
    if (!versionValue.isDouble()
        || versionValue.toInt(-1) != kSeparationProtocolVersion) {
        return reject(QStringLiteral("incompatible_protocol"),
                      QStringLiteral("Only separation protocol version %1 is supported")
                          .arg(kSeparationProtocolVersion),
                      requestId);
    }

    const QJsonValue typeValue = object.value(QStringLiteral("type"));
    if (!typeValue.isString()) {
        return reject(QStringLiteral("malformed_message"),
                      QStringLiteral("type must be a string"), requestId);
    }
    const std::optional<ProtocolType> type = protocolType(typeValue.toString());
    if (!type) {
        return reject(QStringLiteral("unknown_message"),
                      QStringLiteral("Message type is not supported"), requestId);
    }

    const QJsonValue payloadValue = object.value(QStringLiteral("payload"));
    if (!payloadValue.isUndefined() && !payloadValue.isObject()) {
        return reject(QStringLiteral("malformed_message"),
                      QStringLiteral("payload must be an object"), requestId);
    }

    ProtocolParseResult result;
    result.ok = true;
    result.message = {kSeparationProtocolVersion, requestId, *type,
                      payloadValue.toObject()};
    return result;
}

QByteArray encodeProtocolMessage(ProtocolType type, const QString& requestId,
                                 const QJsonObject& payload)
{
    const QJsonObject object{
        {QStringLiteral("protocol"), kSeparationProtocolVersion},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("type"), protocolTypeName(type)},
        {QStringLiteral("payload"), payload},
    };
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

} // namespace agplayer::separation

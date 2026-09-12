#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace agplayer::separation {

constexpr int kSeparationProtocolVersion = 2;
constexpr qsizetype kMaximumProtocolLineBytes = 1024 * 1024;

enum class ProtocolType {
    Hello,
    Probe,
    Start,
    Progress,
    Cancel,
    Result,
    Error,
    Shutdown,
};

struct ProtocolMessage {
    int protocol = kSeparationProtocolVersion;
    QString requestId;
    ProtocolType type = ProtocolType::Error;
    QJsonObject payload;
};

struct ProtocolError {
    QString code;
    QString message;
    QString requestId;
};

struct ProtocolParseResult {
    bool ok = false;
    ProtocolMessage message;
    ProtocolError error;
};

[[nodiscard]] QString protocolTypeName(ProtocolType type);
[[nodiscard]] ProtocolParseResult parseProtocolMessage(const QByteArray& line);
[[nodiscard]] QByteArray encodeProtocolMessage(ProtocolType type,
                                               const QString& requestId,
                                               const QJsonObject& payload = {});

} // namespace agplayer::separation

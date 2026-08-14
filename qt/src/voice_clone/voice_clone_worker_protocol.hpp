#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace agplayer::voice_clone {

enum class WorkerMessageKind {
    Request,
    Response,
    Progress,
    Error,
    Unknown,
};

enum class WorkerOperation {
    Hello,
    Capabilities,
    Load,
    Generate,
    Cancel,
    Unload,
    Shutdown,
    Unknown,
};

struct StructuredWorkerError {
    QString code;
    QString message;
    bool retryable = false;
    QJsonObject details;
};

struct VoiceCloneWorkerMessage {
    WorkerMessageKind kind = WorkerMessageKind::Unknown;
    WorkerOperation operation = WorkerOperation::Unknown;
    QString requestId;
    QString adapterId;
    QString adapterVersion;
    int protocolVersion = 0;
    QJsonObject payload;
    QString stage;
    std::optional<double> progress;
    StructuredWorkerError workerError;
    QString resolvedOutputPath;
};

struct WorkerMessageDecodeResult {
    VoiceCloneWorkerMessage message;
    QString error;

    bool isValid() const { return error.isEmpty(); }
};

QString workerOperationName(WorkerOperation operation);
QByteArray encodeWorkerMessage(const VoiceCloneWorkerMessage& message);
WorkerMessageDecodeResult decodeWorkerMessage(const QByteArray& json,
                                              const QString& outputRoot = {});

} // namespace agplayer::voice_clone

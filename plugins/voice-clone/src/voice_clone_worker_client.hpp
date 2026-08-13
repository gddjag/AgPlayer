#pragma once

#include "voice_clone_adapter_manifest.hpp"
#include "voice_clone_worker_protocol.hpp"

#include <QHash>
#include <QObject>
#include <QProcess>

class QLocalServer;
class QLocalSocket;
class QTimer;

namespace agplayer::voice_clone {

class VoiceCloneWorkerClient final : public QObject {
    Q_OBJECT

public:
    explicit VoiceCloneWorkerClient(QObject* parent = nullptr);
    ~VoiceCloneWorkerClient() override;

    bool start(const VoiceCloneAdapterManifest& manifest,
               const AdapterLauncherResolution& launcher,
               const QString& adapterPackRoot,
               const QString& modelRoot,
               const QString& outputRoot,
               int requestTimeoutMs);
    bool restart();
    void shutdown();

    QString sendRequest(WorkerOperation operation,
                        const QJsonObject& payload = {},
                        const QString& requestId = {});
    bool abandonRequest(const QString& requestId);

    bool isRunning() const;
    bool isReady() const;
    bool hasPendingRequest(const QString& requestId) const;
    QString errorString() const;

signals:
    void readyChanged();
    void responseReceived(const agplayer::voice_clone::VoiceCloneWorkerMessage& message);
    void progressReceived(const agplayer::voice_clone::VoiceCloneWorkerMessage& message);
    void requestFailed(const QString& requestId, const QString& code, const QString& message);
    void workerTerminated(const QString& reason);

private:
    struct PendingRequest {
        WorkerOperation operation = WorkerOperation::Unknown;
        QTimer* timer = nullptr;
    };

    void acceptConnection();
    void readFrames();
    void handleMessage(const VoiceCloneWorkerMessage& message);
    void failWorker(const QString& code, const QString& message);
    void finishPending(const QString& requestId);
    void clearTransport();
    QString nextRequestId() const;

    VoiceCloneAdapterManifest manifest_;
    AdapterLauncherResolution launcher_;
    QString modelRoot_;
    QString outputRoot_;
    QString adapterPackRoot_;
    int requestTimeoutMs_ = 10000;
    QLocalServer* server_ = nullptr;
    QLocalSocket* socket_ = nullptr;
    QTimer* handshakeTimer_ = nullptr;
    QProcess process_;
    QByteArray inputBuffer_;
    QHash<QString, PendingRequest> pending_;
    QString error_;
    QString helloRequestId_;
    QString capabilitiesRequestId_;
    bool ready_ = false;
    bool shuttingDown_ = false;
};

} // namespace agplayer::voice_clone

Q_DECLARE_METATYPE(agplayer::voice_clone::VoiceCloneWorkerMessage)

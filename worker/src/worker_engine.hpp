#pragma once

#include "cancellation_token.hpp"

#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QThreadPool>
#include <QElapsedTimer>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>

namespace agplayer::separation {

using ProgressCallback = std::function<void(double, const QString&)>;

struct BackendResult {
    bool ok = false;
    QString code;
    QString message;
    QJsonObject payload;
};

class WorkerBackend {
public:
    virtual ~WorkerBackend() = default;
    virtual BackendResult probe(const QJsonObject& payload) = 0;
    virtual BackendResult probeCancellable(const QJsonObject& payload, const CancellationToken&) { return probe(payload); }
    virtual BackendResult separate(const QJsonObject& payload,
                                   const CancellationToken& cancelled,
                                   const ProgressCallback& progress) = 0;
};

class WorkerEngine final : public QObject {
    Q_OBJECT

public:
    explicit WorkerEngine(std::shared_ptr<WorkerBackend> backend,
                          QObject* parent = nullptr);
    ~WorkerEngine() override;

    WorkerEngine(const WorkerEngine&) = delete;
    WorkerEngine& operator=(const WorkerEngine&) = delete;

public slots:
    void acceptLine(const QByteArray& line);

signals:
    void messageReady(const QByteArray& line);
    void shutdownReady();

private:
    struct JobContext {
        QString requestId;
        quint64 generation = 0;
        std::shared_ptr<CancellationToken> cancelled;
        double lastProgress = 0.0;
        QString lastStage = QStringLiteral("validation");
        QElapsedTimer lastActivity;
    };

    void sendError(const QString& requestId, const QString& code,
                   const QString& message,
                   const QJsonObject& diagnostics = {});
    void startProbe(const QString& requestId, const QJsonObject& payload);
    void startJob(const QString& requestId, const QJsonObject& payload);
    void deliverProgress(const QString& requestId, quint64 generation,
                         double fraction, const QString& stage);
    void finishProbe(const QString& requestId, quint64 generation,
                     const BackendResult& result);
    void finishJob(const QString& requestId, quint64 generation,
                   const BackendResult& result);
    void taskFinished();
    void rememberCancelledRequest(const QString& requestId);
    void forgetCancelledRequest(const QString& requestId);

    std::shared_ptr<WorkerBackend> backend_;
    QThreadPool threadPool_;
    QTimer heartbeatTimer_;
    std::shared_ptr<JobContext> activeJob_;
    QSet<QString> cancelledRequests_;
    QQueue<QString> cancelledRequestOrder_;
    quint64 generation_ = 0;
    int pendingTasks_ = 0;
    bool shuttingDown_ = false;
    QString shutdownRequestId_;
};

} // namespace agplayer::separation

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

namespace agplayer::separation {
enum class ProtocolType;
}

class SeparationProcessClient final : public QObject {
    Q_OBJECT

public:
    enum State { Stopped, Starting, Ready, Busy, Cancelling, Error };
    Q_ENUM(State)

    struct Deadlines {
        int helloMs = 3000;
        int heartbeatMs = 30'000;
        int cancelGraceMs = 1500;
    };

    explicit SeparationProcessClient(QString program,
                                     QStringList arguments = {},
                                     Deadlines deadlines = {},
                                     QObject* parent = nullptr);
    ~SeparationProcessClient() override;

    State state() const noexcept;
    bool isProcessRunning() const noexcept;
    QString activeRequestId() const;

    bool startProbe(const QJsonObject& payload);
    bool startJob(const QJsonObject& payload);
    bool retryLast();
    void cancel();

signals:
    void stateChanged();
    void probeReceived(const QJsonObject& payload);
    void progressReceived(double fraction, const QString& stage);
    void resultReceived(const QJsonObject& payload);
    void failed(const QString& message, bool retryable);
    void cancelled();
    void staleMessageIgnored(const QString& requestId);

private:
    bool begin(agplayer::separation::ProtocolType type,
               const QJsonObject& payload, bool remember);
    void setState(State state);
    void send(agplayer::separation::ProtocolType type,
              const QString& requestId, const QJsonObject& payload = {});
    void readStandardOutput();
    void handleLine(const QByteArray& line);
    void beginTimeoutFailure(const QString& message);
    void finishFailure(const QString& message);
    void finishCancellation();
    void requestShutdown();

    QString program_;
    QStringList arguments_;
    Deadlines deadlines_;
    QProcess process_;
    QTimer helloTimer_;
    QTimer heartbeatTimer_;
    QTimer exitTimer_;
    QByteArray buffer_;
    State state_ = Stopped;
    agplayer::separation::ProtocolType pendingType_;
    QJsonObject pendingPayload_;
    agplayer::separation::ProtocolType lastType_;
    QJsonObject lastPayload_;
    bool hasLastRequest_ = false;
    QString helloRequestId_;
    QString activeRequestId_;
    QString timeoutFailure_;
    bool userCancellation_ = false;
    bool completing_ = false;
    bool failureEmitted_ = false;
    bool shutdownSent_ = false;
};

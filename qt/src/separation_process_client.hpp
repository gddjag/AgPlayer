#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

#include <memory>

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
    bool canAcceptRequest() const noexcept;
    QString activeRequestId() const;

    bool startProbe(const QJsonObject& payload);
    bool setWorker(QString program, QStringList arguments = {});
    bool startJob(const QJsonObject& payload);
    void cancel();

signals:
    void stateChanged();
    void requestAvailabilityChanged();
    void probeReceived(const QJsonObject& payload);
    void progressReceived(double fraction, const QString& stage);
    void resultReceived(const QJsonObject& payload);
    void failed(const QString& message, bool retryable);
    void cancelled();
    void staleMessageIgnored(const QString& requestId);

private:
#ifdef Q_OS_WIN
    struct WindowsJob;
#endif

    bool begin(agplayer::separation::ProtocolType type,
               const QJsonObject& payload);
    void setState(State state);
    void send(agplayer::separation::ProtocolType type,
              const QString& requestId, const QJsonObject& payload = {});
    void readStandardOutput();
    void handleLine(const QByteArray& line);
    void beginTimeoutFailure(const QString& message);
    void finishFailure(const QString& message);
    void finishCancellation();
    void requestShutdown();
    void terminateProcess();

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
    QString helloRequestId_;
    QString activeRequestId_;
    QString timeoutFailure_;
    bool userCancellation_ = false;
    bool completing_ = false;
    bool failureEmitted_ = false;
    bool shutdownSent_ = false;
    QString pendingFailure_;
    quint64 processGeneration_ = 0;
#ifdef Q_OS_UNIX
    qint64 processGroup_ = 0;
#endif
#ifdef Q_OS_WIN
    std::unique_ptr<WindowsJob> windowsJob_;
#endif
};

#pragma once

#include "vocal_separation_catalog.hpp"

#include <QObject>

class QNetworkAccessManager;
class QNetworkReply;

enum class VocalDownloadState {
    Idle,
    Downloading,
    Paused,
    Verifying,
    Complete,
    Cancelled,
    Failed,
};

class VocalDownloadStateMachine {
public:
    VocalDownloadState state() const;
    bool start();
    bool pause();
    bool resume();
    void cancel();
    void fail();
    void verify();
    void complete();

private:
    VocalDownloadState m_state = VocalDownloadState::Idle;
};

struct VocalInstallResult {
    bool ok = false;
    QString error;
};

class VocalSeparationInstaller {
public:
    static QString partPath(const QString& destination);
    static qint64 resumeOffset(const QString& destination);
    static bool hasDiskSpace(const QString& destination, qint64 bytesRequired);
    static VocalInstallResult activateVerifiedPart(const VocalDownloadFile& file,
                                                   const QString& destination);
    static VocalInstallResult installDirectMlRuntime(const QString& nupkgPath,
                                                     const QString& runtimeRoot);
};

class VocalSeparationDownloader final : public QObject {
    Q_OBJECT

public:
    explicit VocalSeparationDownloader(QNetworkAccessManager* network,
                                       QObject* parent = nullptr);

    VocalDownloadState state() const;
    QString error() const;
    void start(const VocalDownloadFile& file, const QString& destination);
    void pause();
    void resume();
    void cancel();

signals:
    void stateChanged(VocalDownloadState state);
    void progressChanged(qint64 received, qint64 total);
    void finished(const VocalInstallResult& result);

private:
    void issueRequest();
    void setState(VocalDownloadState state);
    void finishFailure(const QString& error);

    QNetworkAccessManager* m_network = nullptr;
    QNetworkReply* m_reply = nullptr;
    VocalDownloadFile m_file;
    QString m_destination;
    QString m_error;
    qint64 m_resumeOffset = 0;
    int m_attempt = 0;
    bool m_pausing = false;
    bool m_cancelling = false;
    VocalDownloadStateMachine m_state;
};

#pragma once

#include "vocal_separation_catalog.hpp"

#include <QObject>
#include <QFutureWatcher>
#include <QLockFile>
#include <QTimer>

#include <atomic>
#include <memory>

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
    static QString runtimeVersionDirectory(const QString& runtimeRoot);
    static QString runtimeLibraryPath(const QString& runtimeRoot);
    static QString partPath(const QString& destination);
    static qint64 resumeOffset(const QString& destination);
    static bool hasDiskSpace(const QString& destination, qint64 bytesRequired);
    static bool isVerifiedFile(const VocalDownloadFile& file,
                               const QString& path,
                               const std::shared_ptr<std::atomic_bool>& cancellation = {});
    static VocalInstallResult activateVerifiedPart(const VocalDownloadFile& file,
                                                   const QString& destination);
    static VocalInstallResult deleteModelFiles(const VocalModelCard& model,
                                               const QString& modelsRoot);
    static bool runtimeDirectoryIsVerified(const QString& runtimeDirectory,
                                           const QString& expectedArchiveSha256,
                                           const std::shared_ptr<std::atomic_bool>& cancellation = {});
    static VocalInstallResult installDirectMlRuntime(const QString& nupkgPath,
                                                     const QString& runtimeRoot,
                                                     const std::shared_ptr<std::atomic_bool>& cancellation = {});
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
    void verifyAndActivate(quint64 operation);
    void verifyExistingDestination(quint64 operation);
    void issueRequest(quint64 operation);
    void setState(VocalDownloadState state);
    void finishFailure(const QString& error);

    QNetworkAccessManager* m_network = nullptr;
    QNetworkReply* m_reply = nullptr;
    VocalDownloadFile m_file;
    QString m_destination;
    QString m_error;
    qint64 m_resumeOffset = 0;
    bool m_acceptResponseBody = false;
    int m_attempt = 0;
    quint64 m_operation = 0;
    QFutureWatcher<bool>* m_verificationWatcher = nullptr;
    QTimer m_retryTimer;
    VocalDownloadStateMachine m_state;
    std::unique_ptr<QLockFile> m_destinationLock;
};

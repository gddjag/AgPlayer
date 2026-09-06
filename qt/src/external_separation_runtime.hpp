#pragma once

#include "vocal_separation_installer.hpp"
#include <QProcess>

// Optional environment, never installed into the player or the system Python.
class ExternalSeparationRuntime final : public QObject {
    Q_OBJECT
public:
    explicit ExternalSeparationRuntime(QString root, QNetworkAccessManager* network,
                                       QObject* parent = nullptr);
    ~ExternalSeparationRuntime() override;
    bool ready() const;
    QString python() const;
    QString workerScript() const;
    bool busy() const { return busy_; }
    bool paused() const { return paused_; }
    bool start();
    void pause();
    void resume();
    void cancel();
signals:
    void progress(double fraction, const QString& detail);
    void changed();
    void finished(bool success, const QString& error);
private:
    bool workerMatchesBundle() const;
    bool synchronizeWorker();
    void advance();
    void launch(const QString& program, const QStringList& arguments);
    void fail(const QString& error);
    void stopInstaller();
    QString root_;
    VocalSeparationDownloader downloader_;
    QProcess process_;
    QFutureWatcher<bool> cacheVerification_;
    QByteArray output_;
    QByteArray bundledWorker_;
    int step_ = 0;
    bool busy_ = false;
    bool paused_ = false;
    bool mirror_ = false;
    bool archiveMirror_ = false;
};

// Pinned native CUDA DLLs only. No Python installation and no dependency on VR.
class CudaSeparationRuntime final : public QObject {
    Q_OBJECT
public:
    CudaSeparationRuntime(QString root, QNetworkAccessManager* network, QObject* parent = nullptr);
    ~CudaSeparationRuntime() override;
    bool ready() const { return ready_; }
    bool nvidiaAvailable() const { return nvidiaAvailable_; }
    bool checking() const { return phase_ == -1 && work_.isRunning(); }
    bool busy() const { return busy_; }
    bool paused() const { return paused_; }
    QString libraryPath() const;
    bool start();
    void pause();
    void resume();
    void cancel();
signals:
    void progress(double fraction, const QString& detail);
    void changed();
    void finished(bool success, const QString& error);
private:
    void advance();
    void fail(const QString& error);
    QString root_;
    QList<VocalDownloadFile> archives_;
    QString activeDirectory_ = QStringLiteral("native");
    QList<VocalDownloadFile> dlls_;
    VocalSeparationDownloader downloader_;
    QFutureWatcher<VocalInstallResult> work_;
    std::shared_ptr<std::atomic_bool> cancellation_;
    int phase_ = -1;
    int index_ = 0;
    bool ready_ = false;
    bool nvidiaAvailable_ = false;
    bool busy_ = false;
    bool paused_ = false;
    bool mirror_ = false;
    bool resumeRequested_ = false;
};

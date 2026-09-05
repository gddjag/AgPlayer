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

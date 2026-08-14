#pragma once

#include <QHash>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

struct PluginInstallDefinition {
    QString id;
    QString name;
    QString description;
    QString downloadUrl;
    QString downloadFileName;
};

struct PluginInstallState {
    bool installed = false;
    bool downloading = false;
    int progress = 0;
    QString status;
    QString lastError;
};

class PluginInstallManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString rootPath READ rootPath CONSTANT)
    Q_PROPERTY(QString sharedRuntimePath READ sharedRuntimePath NOTIFY sharedRuntimePathChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(int activeProgress READ activeProgress NOTIFY activeProgressChanged)
    Q_PROPERTY(QString activePluginId READ activePluginId NOTIFY activePluginChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit PluginInstallManager(QObject* parent = nullptr);

    QString rootPath() const;
    QString sharedRuntimePath() const;

    bool isBusy() const noexcept { return busy_; }
    int activeProgress() const noexcept { return activeProgress_; }
    QString activePluginId() const { return activePlugin_; }
    QString statusText() const { return statusText_; }
    QString lastError() const { return lastError_; }

    Q_INVOKABLE QVariantList pluginCatalog() const;
    Q_INVOKABLE QVariantMap pluginInfo(const QString& pluginId) const;
    Q_INVOKABLE bool isInstalled(const QString& pluginId) const;
    Q_INVOKABLE QString pluginPath(const QString& pluginId) const;
    Q_INVOKABLE void installPlugin(const QString& pluginId);
    Q_INVOKABLE void uninstallPlugin(const QString& pluginId);

signals:
    void pluginStateChanged(const QString& pluginId);
    void busyChanged();
    void activePluginChanged();
    void activeProgressChanged();
    void statusTextChanged();
    void lastErrorChanged();
    void sharedRuntimePathChanged();

private slots:
    void onDownloadReadyRead();
    void onDownloadProgress(qint64 received, qint64 total);
    void onDownloadFinished();

private:
    void loadCatalog();
    void loadInstalledState();
    void ensurePaths();
    void setBusy(bool busy, const QString& pluginId = {});
    void setActiveState(const QString& pluginId, const PluginInstallState& state);
    void clearActiveState(const QString& pluginId);
    void beginOfflineInstall(const QString& pluginId);
    void beginDownload(const QString& pluginId, const QUrl& url);
    void finishInstall(const QString& pluginId, const QString& reason);
    void failInstall(const QString& pluginId, const QString& message);
    void writeInstalledMarker(const QString& pluginId);

    QString rootPath_;
    QString sharedRuntimePath_;
    QString activePlugin_;
    QString statusText_;
    QString lastError_;
    int activeProgress_ = 0;
    bool busy_ = false;

    QHash<QString, PluginInstallDefinition> catalog_;
    QHash<QString, PluginInstallState> states_;

    class QNetworkAccessManager* networkAccessManager_ = nullptr;
    class QNetworkReply* activeReply_ = nullptr;
    class QFile* activeFile_ = nullptr;
    QString activeDownloadPlugin_;
};

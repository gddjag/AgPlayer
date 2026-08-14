#include "plugin_install_manager.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QString sanitizePath(const QString& base, const QString& pluginId)
{
    return QDir(base).filePath(pluginId);
}

} // namespace

PluginInstallManager::PluginInstallManager(QObject* parent)
    : QObject(parent)
    , networkAccessManager_(new QNetworkAccessManager(this))
{
    rootPath_ = QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation)).filePath(QStringLiteral("agplayer-plugins"));
    sharedRuntimePath_ = QDir(rootPath_).filePath(QStringLiteral("shared-runtime"));
    loadCatalog();
    loadInstalledState();
    ensurePaths();
}

QString PluginInstallManager::rootPath() const
{
    return rootPath_;
}

QString PluginInstallManager::sharedRuntimePath() const
{
    return sharedRuntimePath_;
}

void PluginInstallManager::loadCatalog()
{
    catalog_.clear();
    PluginInstallDefinition separation;
    separation.id = QStringLiteral("vocal-separation");
    separation.name = QStringLiteral("人声伴奏分离");
    separation.description =
        QStringLiteral("人声伴奏分离插件（需下载运行包）。安装后在插件页可启动。当前版本以下载管理壳为先。");
    separation.downloadUrl = {};
    separation.downloadFileName = QStringLiteral("vocal-separation.bin");

    PluginInstallDefinition clone;
    clone.id = QStringLiteral("voice-clone");
    clone.name = QStringLiteral("人声克隆");
    clone.description =
        QStringLiteral("人声克隆插件（需下载运行包）。安装后在插件页可使用，当前版本先提供环境与安装壳。");
    clone.downloadUrl = {};
    clone.downloadFileName = QStringLiteral("voice-clone.bin");

    catalog_.insert(separation.id, std::move(separation));
    catalog_.insert(clone.id, std::move(clone));
}

void PluginInstallManager::ensurePaths()
{
    QDir().mkpath(rootPath_);
    QDir().mkpath(sharedRuntimePath_);

    const QString bundledFfmpeg = QDir(QCoreApplication::applicationDirPath())
                                      .filePath(QStringLiteral("ffmpeg.exe"));
    const QString sharedFfmpeg =
        QDir(sharedRuntimePath_).filePath(QStringLiteral("ffmpeg.exe"));
    if (QFile::exists(bundledFfmpeg) && !QFile::exists(sharedFfmpeg)) {
        QFile::copy(bundledFfmpeg, sharedFfmpeg);
    }
    emit sharedRuntimePathChanged();
}

void PluginInstallManager::loadInstalledState()
{
    states_.clear();
    for (auto it = catalog_.cbegin(); it != catalog_.cend(); ++it) {
        PluginInstallState state;
        const QString path = pluginPath(it.key());
        const QString marker = QDir(path).filePath(QStringLiteral("installed.json"));
        if (QFile::exists(marker)) {
            state.installed = true;
            state.status = QStringLiteral("已安装");
            state.progress = 100;
        } else {
            state.status = QStringLiteral("未安装");
            state.progress = 0;
        }
        states_.insert(it.key(), state);
    }
}

QVariantList PluginInstallManager::pluginCatalog() const
{
    QVariantList items;
    for (auto it = catalog_.cbegin(); it != catalog_.cend(); ++it) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), it->id);
        item.insert(QStringLiteral("name"), it->name);
        item.insert(QStringLiteral("description"), it->description);
        item.insert(QStringLiteral("downloadUrl"), it->downloadUrl);
        item.insert(QStringLiteral("installed"), states_.value(it->id).installed);
        item.insert(QStringLiteral("downloading"), states_.value(it->id).downloading);
        item.insert(QStringLiteral("status"), states_.value(it->id).status);
        item.insert(QStringLiteral("progress"), states_.value(it->id).progress);
        item.insert(QStringLiteral("path"), pluginPath(it->id));
        items.append(item);
    }
    return items;
}

QVariantMap PluginInstallManager::pluginInfo(const QString& pluginId) const
{
    QVariantMap info;
    const auto def = catalog_.value(pluginId);
    const auto state = states_.value(pluginId);
    info.insert(QStringLiteral("id"), pluginId);
    info.insert(QStringLiteral("name"), def.name);
    info.insert(QStringLiteral("description"), def.description);
    info.insert(QStringLiteral("downloadUrl"), def.downloadUrl);
    info.insert(QStringLiteral("installed"), state.installed);
    info.insert(QStringLiteral("downloading"), state.downloading);
    info.insert(QStringLiteral("status"), state.status);
    info.insert(QStringLiteral("progress"), state.progress);
    info.insert(QStringLiteral("lastError"), state.lastError);
    info.insert(QStringLiteral("path"), pluginPath(pluginId));
    return info;
}

bool PluginInstallManager::isInstalled(const QString& pluginId) const
{
    return states_.value(pluginId).installed;
}

QString PluginInstallManager::pluginPath(const QString& pluginId) const
{
    return sanitizePath(rootPath_, pluginId);
}

void PluginInstallManager::setBusy(bool busy, const QString& pluginId)
{
    const bool changed = (busy_ != busy);
    busy_ = busy;
    if (changed) {
        emit busyChanged();
    }

    if (!pluginId.isEmpty() && activePlugin_ != pluginId) {
        activePlugin_ = pluginId;
        emit activePluginChanged();
    }
    if (!busy_) {
        activeProgress_ = 0;
        emit activeProgressChanged();
        statusText_.clear();
        emit statusTextChanged();
        lastError_.clear();
        emit lastErrorChanged();
        activePlugin_.clear();
        emit activePluginChanged();
    }
}

void PluginInstallManager::setActiveState(const QString& pluginId,
                                         const PluginInstallState& state)
{
    states_.insert(pluginId, state);
    emit pluginStateChanged(pluginId);
}

void PluginInstallManager::clearActiveState(const QString& pluginId)
{
    PluginInstallState reset;
    const bool currentlyInstalled = states_.value(pluginId).installed;
    if (currentlyInstalled) {
        reset.installed = true;
        reset.status = QStringLiteral("已安装");
        reset.progress = 100;
    } else {
        reset.status = QStringLiteral("未安装");
    }
    states_.insert(pluginId, reset);
    emit pluginStateChanged(pluginId);
}

void PluginInstallManager::writeInstalledMarker(const QString& pluginId)
{
    const PluginInstallDefinition def = catalog_.value(pluginId);
    const QString markerPath = QDir(pluginPath(pluginId)).filePath(QStringLiteral("installed.json"));
    QSaveFile marker(markerPath);
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        failInstall(pluginId, QStringLiteral("无法写入插件安装标记"));
        return;
    }
    const QJsonObject metadata{{QStringLiteral("pluginId"), pluginId},
                              {QStringLiteral("name"), def.name},
                              {QStringLiteral("description"), def.description},
                              {QStringLiteral("downloadUrl"), def.downloadUrl},
                              {QStringLiteral("installedAt"),
                               QDateTime::currentDateTimeUtc()
                                   .toString(Qt::ISODate)},
                              {QStringLiteral("sharedRuntime"), sharedRuntimePath_}};
    marker.write(QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    if (!marker.commit()) {
        failInstall(pluginId, QStringLiteral("写入安装标记失败"));
    }
}

void PluginInstallManager::finishInstall(const QString& pluginId, const QString& reason)
{
    PluginInstallState state = states_.value(pluginId);
    state.installed = true;
    state.downloading = false;
    state.progress = 100;
    state.status = reason;
    state.lastError.clear();
    setActiveState(pluginId, state);
    writeInstalledMarker(pluginId);
    setBusy(false);
}

void PluginInstallManager::failInstall(const QString& pluginId, const QString& message)
{
    PluginInstallState state = states_.value(pluginId);
    state.downloading = false;
    state.status = QStringLiteral("安装失败");
    state.lastError = message;
    state.progress = 0;
    setActiveState(pluginId, state);
    lastError_ = message;
    emit lastErrorChanged();

    if (!activeDownloadPlugin_.isEmpty()) {
        QDir(pluginPath(activeDownloadPlugin_)).remove(
            QStringLiteral("download.bin.part"));
    }

    setBusy(false);
}

void PluginInstallManager::beginOfflineInstall(const QString& pluginId)
{
    if (!QDir().mkpath(pluginPath(pluginId))) {
        failInstall(pluginId, QStringLiteral("插件目录创建失败"));
        return;
    }
    finishInstall(pluginId, QStringLiteral("安装完成（离线占位）"));
}

void PluginInstallManager::beginDownload(const QString& pluginId, const QUrl& url)
{
    if (!url.isValid() || url.isRelative()) {
        failInstall(pluginId, QStringLiteral("下载地址无效"));
        return;
    }
    activeDownloadPlugin_ = pluginId;
    PluginInstallState state = states_.value(pluginId);
    state.downloading = true;
    state.status = QStringLiteral("下载中");
    state.progress = 0;
    setActiveState(pluginId, state);

    if (!QDir().mkpath(pluginPath(pluginId))) {
        failInstall(pluginId, QStringLiteral("插件目录创建失败"));
        return;
    }
    const QString downloadTemp = QDir(pluginPath(pluginId)).filePath(QStringLiteral("download.bin.part"));
    auto* file = new QFile(downloadTemp);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        failInstall(pluginId, QStringLiteral("下载文件打开失败"));
        delete file;
        return;
    }
    activeFile_ = file;

    activeReply_ = networkAccessManager_->get(QNetworkRequest(url));
    if (!activeReply_) {
        failInstall(pluginId, QStringLiteral("启动下载失败"));
        delete activeFile_;
        activeFile_ = nullptr;
        return;
    }
    connect(activeReply_, &QNetworkReply::readyRead,
            this, &PluginInstallManager::onDownloadReadyRead);
    connect(activeReply_, &QNetworkReply::downloadProgress,
            this, &PluginInstallManager::onDownloadProgress);
    connect(activeReply_, &QNetworkReply::finished,
            this, &PluginInstallManager::onDownloadFinished);
}

void PluginInstallManager::installPlugin(const QString& pluginId)
{
    const auto definition = catalog_.value(pluginId);
    if (definition.id.isEmpty()) {
        lastError_ = QStringLiteral("未知插件");
        emit lastErrorChanged();
        return;
    }

    if (busy_) {
        lastError_ = QStringLiteral("当前有任务进行中，请先完成后再试");
        emit lastErrorChanged();
        return;
    }

    if (isInstalled(pluginId)) {
        return;
    }

    setBusy(true, pluginId);
    statusText_ = QStringLiteral("安装中");
    emit statusTextChanged();

    if (definition.downloadUrl.isEmpty()) {
        beginOfflineInstall(pluginId);
        return;
    }
    beginDownload(pluginId, QUrl(definition.downloadUrl));
}

void PluginInstallManager::uninstallPlugin(const QString& pluginId)
{
    const auto definition = catalog_.value(pluginId);
    if (definition.id.isEmpty()) {
        lastError_ = QStringLiteral("未知插件");
        emit lastErrorChanged();
        return;
    }

    if (busy_) {
        lastError_ = QStringLiteral("当前有任务进行中，请先完成后再试");
        emit lastErrorChanged();
        return;
    }

    if (!isInstalled(pluginId)) {
        return;
    }

    setBusy(true, pluginId);
    const QString path = pluginPath(pluginId);
    QDir pathDir(path);
    if (pathDir.exists()) {
        pathDir.removeRecursively();
    }

    PluginInstallState state;
    state.status = QStringLiteral("未安装");
    state.lastError.clear();
    state.progress = 0;
    setActiveState(pluginId, state);
    setBusy(false);
}

void PluginInstallManager::onDownloadReadyRead()
{
    if (activeFile_ != nullptr && activeFile_->isOpen()) {
        activeFile_->write(activeReply_->readAll());
    }
}

void PluginInstallManager::onDownloadProgress(qint64 received, qint64 total)
{
    if (total <= 0) {
        return;
    }
    activeProgress_ = int((received * 100) / total);
    emit activeProgressChanged();

    if (!activeDownloadPlugin_.isEmpty()) {
        PluginInstallState state = states_.value(activeDownloadPlugin_);
        state.downloading = true;
        state.progress = activeProgress_;
        state.status = QStringLiteral("下载中");
        setActiveState(activeDownloadPlugin_, state);
    }
}

void PluginInstallManager::onDownloadFinished()
{
    auto* const reply = qobject_cast<QNetworkReply*>(sender());
    if (reply == nullptr || reply != activeReply_) {
        setBusy(false);
        return;
    }

    if (activeDownloadPlugin_.isEmpty()) {
        setBusy(false);
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        failInstall(activeDownloadPlugin_, reply->errorString());
        reply->deleteLater();
        activeReply_ = nullptr;
        activeDownloadPlugin_.clear();
        activeProgress_ = 0;
        if (activeFile_ != nullptr) {
            activeFile_->close();
            delete activeFile_;
            activeFile_ = nullptr;
        }
        return;
    }

    if (activeFile_ != nullptr) {
        activeFile_->close();
        const QString destination =
            QDir(pluginPath(activeDownloadPlugin_)).filePath(QStringLiteral("payload.bin"));
        const QString source =
            QDir(pluginPath(activeDownloadPlugin_)).filePath(QStringLiteral("download.bin.part"));
        QFile::remove(destination);
        if (!QFile::rename(source, destination)) {
            failInstall(activeDownloadPlugin_, QStringLiteral("保存下载文件失败"));
            activeFile_->close();
            activeFile_->remove();
            delete activeFile_;
            activeFile_ = nullptr;
            reply->deleteLater();
            activeReply_ = nullptr;
            activeDownloadPlugin_.clear();
            return;
        }
        delete activeFile_;
        activeFile_ = nullptr;
    }

    if (reply != nullptr) {
        reply->deleteLater();
        activeReply_ = nullptr;
    }

    finishInstall(activeDownloadPlugin_, QStringLiteral("安装完成"));
    activeDownloadPlugin_.clear();
}

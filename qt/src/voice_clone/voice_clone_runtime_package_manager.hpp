#pragma once

#include "voice_clone_runtime_package.hpp"

#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>

#include <functional>
#include <memory>

namespace agplayer::voice_clone {

struct VoiceCloneRuntimeResolution {
    QString root;
    VoiceCloneRuntimePackageManifest manifest;
    QString error;
    bool isValid() const { return error.isEmpty() && !root.isEmpty(); }
};

class VoiceCloneRuntimePackageManager final : public QObject {
    Q_OBJECT

public:
    struct DeploymentOperations {
        std::function<bool(const QString&, const QString&)> renameDirectory;
        std::function<bool(const QString&)> removeDirectory;
    };

    enum State {
        Idle,
        Resolving,
        Downloading,
        Paused,
        Verifying,
        Extracting,
        Committing,
        Completed,
        Canceled,
        Failed,
    };
    Q_ENUM(State)

    explicit VoiceCloneRuntimePackageManager(
        QString runtimeRoot,
        VoiceCloneRuntimeValidationPolicy policy =
            VoiceCloneRuntimeValidationPolicy::OfficialSignedOnly,
        QObject* parent = nullptr);
    VoiceCloneRuntimePackageManager(
        QString runtimeRoot, QNetworkAccessManager* network,
        DeploymentOperations deploymentOperations,
        VoiceCloneRuntimeValidationPolicy policy,
        QObject* parent = nullptr);
    ~VoiceCloneRuntimePackageManager() override;

    void start(const VoiceCloneRuntimePackageManifest& manifest);
    bool pause();
    void cancel();
    bool retry();

    State state() const { return state_; }
    QString phase() const;
    QString errorString() const { return error_; }
    qint64 transferredBytes() const { return transferredBytes_; }
    qint64 totalBytes() const { return totalBytes_; }
    int progressPercent() const;
    VoiceCloneRuntimeValidationPolicy validationPolicy() const { return policy_; }
    QString partialArchivePath(const VoiceCloneRuntimePackageManifest& manifest) const;
    QString resumeMetadataPath(const VoiceCloneRuntimePackageManifest& manifest) const;
    VoiceCloneRuntimeResolution resolveInstalled(const QString& runtimeId,
                                                 const QString& adapterId,
                                                 const QString& adapterVersion,
                                                 int protocolVersion) const;

signals:
    void stateChanged();
    void progressChanged();

private:
    void setState(State state, const QString& error = {});
    bool prepareRoot(QString* error);
    void beginHead();
    void beginGet();
    void consumeData();
    void finishDownload();
    bool writeResumeMetadata(qint64 bytes);
    bool resumeMetadataMatches(qint64 bytes) const;
    bool inspectAndExtract(QString* error);
    bool validateExtracted(QString* error) const;
    bool writeInstalledMarker(QString* error) const;
    bool commit(QString* error);
    void closeReply();
    void fail(const QString& error);

    QString runtimeRoot_;
    QString stagingRoot_;
    QString targetRoot_;
    QString archivePath_;
    QString resumePath_;
    VoiceCloneRuntimeValidationPolicy policy_;
    VoiceCloneRuntimePackageManifest manifest_;
    State state_ = Idle;
    QString error_;
    std::unique_ptr<QNetworkAccessManager> ownedNetwork_;
    QNetworkAccessManager* network_ = nullptr;
    DeploymentOperations deploymentOperations_;
    QNetworkReply* reply_ = nullptr;
    QFile archive_;
    qint64 resumeOffset_ = 0;
    qint64 transferredBytes_ = 0;
    qint64 totalBytes_ = -1;
    quint64 generation_ = 0;
};

} // namespace agplayer::voice_clone

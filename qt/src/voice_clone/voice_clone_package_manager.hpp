#pragma once

#include "voice_clone_package_manifest.hpp"

#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVector>

#include <memory>
#include <functional>

class QNetworkReply;

namespace agplayer::voice_clone {

class VoiceCloneController;

class VoiceClonePackageManager final : public QObject {
    Q_OBJECT

public:
    struct DeploymentOperations {
        std::function<bool(const QString&, const QString&)> renameDirectory;
        std::function<bool(const QString&)> removeDirectory;
    };

    enum State {
        Idle,
        LicenseRequired,
        Resolving,
        Downloading,
        Paused,
        Verifying,
        Committing,
        Completed,
        Canceled,
        Failed,
    };
    Q_ENUM(State)

    explicit VoiceClonePackageManager(QString installRoot, QObject* parent = nullptr);
    VoiceClonePackageManager(QString installRoot,
                             VoiceClonePackageValidationPolicy validationPolicy,
                             QObject* parent = nullptr);
    VoiceClonePackageManager(QString installRoot,
                             QNetworkAccessManager* networkAccessManager,
                             QObject* parent);
    VoiceClonePackageManager(QString installRoot,
                             QNetworkAccessManager* networkAccessManager,
                             VoiceClonePackageValidationPolicy validationPolicy,
                             QObject* parent = nullptr);
    VoiceClonePackageManager(QString installRoot,
                             QNetworkAccessManager* networkAccessManager,
                             DeploymentOperations deploymentOperations,
                             QObject* parent);
    VoiceClonePackageManager(QString installRoot,
                             QNetworkAccessManager* networkAccessManager,
                             DeploymentOperations deploymentOperations,
                             VoiceClonePackageValidationPolicy validationPolicy,
                             QObject* parent = nullptr);
    ~VoiceClonePackageManager() override;

    State state() const;
    QString errorString() const;
    qint64 transferredBytes() const;
    qint64 totalBytes() const;
    bool isProgressIndeterminate() const;
    int progressPercent() const;
    VoiceClonePackageManifest currentManifest() const;

    QString stagingDirectory() const;
    QString partialFilePath() const;
    QString resumeMetadataPath() const;
    QString licenseAcceptancePath() const;

    void start(const VoiceClonePackageManifest& manifest);
    bool pause();
    void cancel();
    bool retry();
    bool acceptLicense(const QUrl& licenseUrl, const QString& revision);
    bool acceptLicense(const QString& licenseId,
                       const QUrl& licenseUrl,
                       const QString& revision);
    bool hasLicenseAcceptance(const QString& modelId,
                              const QString& adapterId,
                              const QUrl& licenseUrl,
                              const QString& revision) const;
    bool hasLicenseAcceptance(const QString& modelId,
                              const QString& adapterId,
                              const QString& licenseId,
                              const QUrl& licenseUrl,
                              const QString& revision) const;
    bool hasRequiredLicenseAcceptances(const QString& modelId,
                                       const QString& adapterId) const;

    static bool commitStagingDirectory(const QString& stagingDirectory,
                                       const QString& targetDirectory,
                                       QString* error = nullptr,
                                       const DeploymentOperations& operations = {});

signals:
    void stateChanged();
    void progressChanged();

private:
    friend class VoiceCloneController;
    bool acceptLicenseIdentity(const QString& modelId,
                               const QString& adapterId,
                               const QString& licenseId,
                               const QUrl& licenseUrl,
                               const QString& revision);
    void setState(State state, const QString& error = {});
    void resolveNextFile();
    void beginDownloads();
    void downloadNextFile();
    void consumeReplyData();
    void finishCurrentDownload();
    void fail(const QString& error);
    bool preparePaths(QString* error);
    bool writeInstalledModelManifest(QString* error) const;
    bool validateStaging(QString* error) const;
    bool hasAcceptedCurrentLicense(QString* error = nullptr) const;
    bool writeResumeMetadata(const VoiceClonePackageFile& file, qint64 total);
    bool resumeMetadataMatches(const VoiceClonePackageFile& file) const;
    void removeCurrentPartial();
    void removeStagingSafely();
    void updateProgress(qint64 currentFileBytes);

    QString installRoot_;
    QString stagingDirectory_;
    QString targetDirectory_;
    QString partialFilePath_;
    QString resumeMetadataPath_;
    VoiceClonePackageManifest manifest_;
    State state_ = Idle;
    QString error_;
    std::unique_ptr<QNetworkAccessManager> ownedNetwork_;
    QNetworkAccessManager* network_ = nullptr;
    DeploymentOperations deploymentOperations_;
    VoiceClonePackageValidationPolicy validationPolicy_ =
        VoiceClonePackageValidationPolicy::OfficialOnly;
    QNetworkReply* reply_ = nullptr;
    QFile partialFile_;
    QVector<qint64> resolvedSizes_;
    qsizetype currentIndex_ = 0;
    qint64 completedBytes_ = 0;
    qint64 currentResumeOffset_ = 0;
    qint64 transferredBytes_ = 0;
    qint64 totalBytes_ = -1;
    bool pauseRequested_ = false;
    bool cancelRequested_ = false;
    quint64 operationGeneration_ = 0;
};

} // namespace agplayer::voice_clone

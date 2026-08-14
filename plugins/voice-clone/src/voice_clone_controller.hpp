#pragma once

#include "voice_clone_adapter_manifest.hpp"
#include "voice_clone_manifest.hpp"
#include "voice_clone_package_manifest.hpp"
#include "voice_clone_runtime_package.hpp"
#include "voice_clone_worker_client.hpp"

#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QSet>
#include <QUrl>

namespace agplayer::voice_clone {

class VoiceClonePackageManager;
class VoiceCloneRuntimePackageManager;

class VoiceCloneController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList basicParameters READ basicParameters NOTIFY capabilitiesChanged)
    Q_PROPERTY(QVariantList advancedParameters READ advancedParameters NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool advancedSettingsAvailable READ advancedSettingsAvailable NOTIFY capabilitiesChanged)
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(bool workerReady READ workerReady NOTIFY workerReadyChanged)
    Q_PROPERTY(bool modelLoaded READ modelLoaded NOTIFY modelLoadedChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
    Q_PROPERTY(QString activationState READ activationState NOTIFY activationChanged)
    Q_PROPERTY(QString activationMessage READ activationMessage NOTIFY activationChanged)
    Q_PROPERTY(bool licenseAcceptanceRequired READ licenseAcceptanceRequired NOTIFY licenseChanged)
    Q_PROPERTY(QString currentLicenseName READ currentLicenseName NOTIFY licenseChanged)
    Q_PROPERTY(QUrl currentLicenseUrl READ currentLicenseUrl NOTIFY licenseChanged)
    Q_PROPERTY(QString currentLicenseRevision READ currentLicenseRevision NOTIFY licenseChanged)
    Q_PROPERTY(QVariantList currentLicenseRequirements READ currentLicenseRequirements NOTIFY licenseChanged)
    Q_PROPERTY(QString downloadState READ downloadState NOTIFY downloadChanged)
    Q_PROPERTY(QString downloadPhase READ downloadPhase NOTIFY downloadChanged)
    Q_PROPERTY(bool runtimeDownloadRequired READ runtimeDownloadRequired NOTIFY activationChanged)
    Q_PROPERTY(QString downloadModelId READ downloadModelId NOTIFY downloadChanged)
    Q_PROPERTY(QString downloadError READ downloadError NOTIFY downloadChanged)
    Q_PROPERTY(int downloadProgressPercent READ downloadProgressPercent NOTIFY downloadChanged)
    Q_PROPERTY(bool downloadInProgress READ downloadInProgress NOTIFY downloadChanged)

public:
    explicit VoiceCloneController(QString pluginRoot,
                                  QString modelsRoot,
                                  VoiceClonePackageManager* licenseManager,
                                  VoiceClonePackageValidationPolicy packageValidationPolicy =
                                      VoiceClonePackageValidationPolicy::OfficialOnly,
                                  QObject* parent = nullptr);
    VoiceCloneController(QString pluginRoot,
                         QString modelsRoot,
                         VoiceClonePackageManager* licenseManager,
                         VoiceCloneRuntimePackageManager* runtimeManager,
                         VoiceClonePackageValidationPolicy packageValidationPolicy,
                         QObject* parent = nullptr);
    ~VoiceCloneController() override;

    bool configureAdapter(const QString& adapterId,
                          const QString& adapterVersion,
                          const QString& launcherId = {});
    Q_INVOKABLE bool activateModel(const QString& stableId);
    Q_INVOKABLE bool selectModel(const QString& stableId);
    Q_INVOKABLE bool acceptSelectedLicense();
    Q_INVOKABLE bool acceptSelectedLicenses(const QStringList& licenseIds);
    bool acceptSelectedLicenseIdentity(const QString& modelId,
                                       const QString& adapterId,
                                       const QUrl& licenseUrl,
                                       const QString& revision);
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE bool downloadModel(const QString& stableId);
    Q_INVOKABLE bool pauseDownload();
    Q_INVOKABLE bool resumeDownload();
    Q_INVOKABLE bool cancelDownload();
    Q_INVOKABLE bool retryDownload();
    Q_INVOKABLE bool openModelDirectory();

    Q_INVOKABLE bool startWorker();
    Q_INVOKABLE bool restartWorker();
    Q_INVOKABLE bool loadModel();
    Q_INVOKABLE bool unloadModel();
    Q_INVOKABLE QString generate(const QString& text,
                                 const QString& referenceAudioPath,
                                 const QJsonObject& parameters);
    Q_INVOKABLE bool cancel(const QString& requestId);
    Q_INVOKABLE bool saveResult(const QString& outputPath,
                                const QString& destinationPath);
    Q_INVOKABLE bool deleteResult(const QString& outputPath);
    Q_INVOKABLE QUrl resultFileUrl(const QString& resultPath) const;
    void shutdown();

    void setRequestTimeoutMs(int timeoutMs);
    bool workerRunning() const;
    bool workerReady() const;
    bool modelLoaded() const;
    bool hasPendingRequest(const QString& requestId) const;
    QString requestDirectory(const QString& requestId) const;
    QString errorString() const;
    QString activationState() const;
    QString activationMessage() const;
    bool licenseAcceptanceRequired() const;
    QString currentLicenseName() const;
    QUrl currentLicenseUrl() const;
    QString currentLicenseRevision() const;
    QVariantList currentLicenseRequirements() const;
    QString downloadState() const;
    QString downloadPhase() const { return downloadPhase_; }
    bool runtimeDownloadRequired() const { return runtimeDownloadRequired_; }
    QString downloadModelId() const;
    QString downloadError() const;
    int downloadProgressPercent() const;
    bool downloadInProgress() const;
    QVariantList basicParameters() const;
    QVariantList advancedParameters() const;
    bool advancedSettingsAvailable() const;
    QVariantList models() const;
    int pendingCleanupCount() const;

signals:
    void capabilitiesChanged();
    void modelsChanged();
    void workerReadyChanged();
    void modelLoadedChanged();
    void errorChanged();
    void activationChanged();
    void licenseChanged();
    void downloadChanged();
    void generationFinished(const QString& requestId, const QString& outputPath);
    void requestFailed(const QString& requestId, const QString& code, const QString& message);

private:
    struct GenerationFiles {
        QString directory;
        QString partialPath;
        QString finalPath;
    };

    void handleResponse(const VoiceCloneWorkerMessage& message);
    void handleFailure(const QString& requestId, const QString& code, const QString& message);
    void setError(const QString& error);
    void updateCapabilities(const QJsonObject& schema);
    bool finalizeGeneration(const QString& requestId, QString* outputPath, QString* error);
    void cleanupGeneration(const QString& requestId, bool keepFinal = false);
    void cleanupDirectory(const QString& requestId, const QString& directory);
    void retryPendingCleanup();
    void updateSelectedModelStatus(const QString& state, const QString& diagnostic = {});
    void setActivation(const QString& state, const QString& message = {});
    void setDownloadPhase(const QString& phase);
    bool startPendingModelDownload();
    bool startPendingRuntimeDownload();
    bool launchWorker();

    enum class PendingRuntimeAction { None, DownloadModel, StartWorker };

    QString pluginRoot_;
    QString modelsRoot_;
    QString registryPath_;
    QString outputRoot_;
    VoiceClonePackageManager* licenseManager_ = nullptr;
    VoiceCloneRuntimePackageManager* runtimeManager_ = nullptr;
    VoiceClonePackageValidationPolicy packageValidationPolicy_ =
        VoiceClonePackageValidationPolicy::OfficialOnly;
    VoiceCloneWorkerClient worker_;
    VoiceCloneAdapterManifest adapterManifest_;
    AdapterLauncherResolution launcher_;
    QString adapterPackRoot_;
    QString selectedRuntimeRoot_;
    QVector<VoiceCloneModel> modelEntries_;
    VoiceCloneModel selectedModel_;
    QString selectedModelRoot_;
    QJsonObject liveSchema_;
    QVariantList basicParameters_;
    QVariantList advancedParameters_;
    QVariantList models_;
    QHash<QString, GenerationFiles> generations_;
    QHash<QString, QString> pendingCleanup_;
    QHash<QString, QString> cancelTargets_;
    QSet<QString> publishedResults_;
    QString loadRequestId_;
    QString error_;
    QString activationState_ = QStringLiteral("idle");
    QString activationMessage_;
    VoiceClonePackageManifest pendingDownloadManifest_;
    QString pendingDownloadStableId_;
    QString downloadPhase_ = QStringLiteral("idle");
    int requestTimeoutMs_ = 10000;
    bool modelLoaded_ = false;
    bool activationInProgress_ = false;
    bool runtimeDownloadRequired_ = false;
    PendingRuntimeAction pendingRuntimeAction_ = PendingRuntimeAction::None;
    quint64 pendingRuntimeResolutionId_ = 0;
    bool runtimeResolutionCanceled_ = false;
};

} // namespace agplayer::voice_clone

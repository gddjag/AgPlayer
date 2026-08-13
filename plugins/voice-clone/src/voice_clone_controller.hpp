#pragma once

#include "voice_clone_adapter_manifest.hpp"
#include "voice_clone_manifest.hpp"
#include "voice_clone_worker_client.hpp"

#include <QHash>
#include <QObject>
#include <QVariantList>

namespace agplayer::voice_clone {

class VoiceClonePackageManager;

class VoiceCloneController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList basicParameters READ basicParameters NOTIFY capabilitiesChanged)
    Q_PROPERTY(QVariantList advancedParameters READ advancedParameters NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool advancedSettingsAvailable READ advancedSettingsAvailable NOTIFY capabilitiesChanged)
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(bool workerReady READ workerReady NOTIFY workerReadyChanged)
    Q_PROPERTY(bool modelLoaded READ modelLoaded NOTIFY modelLoadedChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)

public:
    explicit VoiceCloneController(QString portableRoot,
                                  QString builtInRegistryPath,
                                  VoiceClonePackageManager* licenseManager,
                                  QObject* parent = nullptr);
    ~VoiceCloneController() override;

    bool configureAdapter(const QString& manifestPath,
                          const QString& adapterPackRoot,
                          const QString& launcherId = {});
    bool selectModel(const QString& stableId, const QString& modelRoot);
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE bool openModelDirectory();

    bool startWorker();
    bool restartWorker();
    bool loadModel();
    bool unloadModel();
    QString generate(const QString& text,
                     const QString& referenceAudioPath,
                     const QJsonObject& parameters);
    bool cancel(const QString& requestId);
    void shutdown();

    void setRequestTimeoutMs(int timeoutMs);
    bool workerRunning() const;
    bool workerReady() const;
    bool modelLoaded() const;
    bool hasPendingRequest(const QString& requestId) const;
    QString requestDirectory(const QString& requestId) const;
    QString errorString() const;
    QVariantList basicParameters() const;
    QVariantList advancedParameters() const;
    bool advancedSettingsAvailable() const;
    QVariantList models() const;

signals:
    void capabilitiesChanged();
    void modelsChanged();
    void workerReadyChanged();
    void modelLoadedChanged();
    void errorChanged();
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
    void updateSelectedModelStatus(const QString& state, const QString& diagnostic = {});

    QString portableRoot_;
    QString registryPath_;
    QString outputRoot_;
    VoiceClonePackageManager* licenseManager_ = nullptr;
    VoiceCloneWorkerClient worker_;
    VoiceCloneAdapterManifest adapterManifest_;
    AdapterLauncherResolution launcher_;
    QString adapterPackRoot_;
    QVector<VoiceCloneModel> modelEntries_;
    VoiceCloneModel selectedModel_;
    QString selectedModelRoot_;
    QJsonObject liveSchema_;
    QVariantList basicParameters_;
    QVariantList advancedParameters_;
    QVariantList models_;
    QHash<QString, GenerationFiles> generations_;
    QString loadRequestId_;
    QString error_;
    int requestTimeoutMs_ = 10000;
    bool modelLoaded_ = false;
};

} // namespace agplayer::voice_clone

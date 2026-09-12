#pragma once

#include "separation_process_client.hpp"
#include "vocal_separation_catalog.hpp"
#include "vocal_separation_history.hpp"
#include "vocal_separation_installer.hpp"
#include "external_separation_runtime.hpp"

#include <QHash>
#include <QFutureWatcher>
#include <QFileSystemWatcher>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <optional>

class AudioPreviewController;
class ImportController;
class LibraryModel;
class PlaylistModel;
class VocalSeparationDownloader;
class VocalSeparationControllerTestDriver;
class WaveformProvider;

struct VocalSeparationControllerOptions {
    QString workerProgram;
    QStringList workerArguments;
    QString dataRoot;
    QString outputDirectory;
    QString runtimeLibraryPath;
    QList<VocalModelCard> catalog;
    SeparationProcessClient::Deadlines deadlines;
    bool verifyRuntimeIntegrity = true;
};

class VocalSeparationController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap inputInfo READ inputInfo NOTIFY inputInfoChanged)
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(QString selectedModelId READ selectedModelId
                   NOTIFY selectedModelIdChanged)
    Q_PROPERTY(DeviceMode deviceMode READ deviceMode NOTIFY deviceModeChanged)
    Q_PROPERTY(QVariantList availableDevices READ availableDevices
                   NOTIFY availableDevicesChanged)
    Q_PROPERTY(JobState jobState READ jobState NOTIFY jobStateChanged)
    Q_PROPERTY(QString stage READ stage NOTIFY jobStateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadProgressChanged)
    Q_PROPERTY(QString downloadingModelId READ downloadingModelId NOTIFY downloadStateChanged)
    Q_PROPERTY(bool downloadBusy READ downloadBusy NOTIFY downloadStateChanged)
    Q_PROPERTY(QVariantList runtimeConfigurations READ runtimeConfigurations NOTIFY downloadStateChanged)
    Q_PROPERTY(QString downloadSource READ downloadSource NOTIFY downloadStateChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString outputFormat READ outputFormat NOTIFY outputFormatChanged)
    Q_PROPERTY(QString outputDirectory READ outputDirectory
                   NOTIFY outputDirectoryChanged)
    Q_PROPERTY(QString modelStorageDirectory READ modelStorageDirectory
                   NOTIFY modelStorageDirectoryChanged)
    Q_PROPERTY(bool runtimeReady READ runtimeReady NOTIFY startEligibilityChanged)
    Q_PROPERTY(bool cudaRuntimeReady READ cudaRuntimeReady NOTIFY startEligibilityChanged)
    Q_PROPERTY(QString actualProvider READ actualProvider NOTIFY actualExecutionChanged)
    Q_PROPERTY(QString actualDevice READ actualDevice NOTIFY actualExecutionChanged)
    Q_PROPERTY(QString fallbackReason READ fallbackReason NOTIFY actualExecutionChanged)
    Q_PROPERTY(double outputGain READ outputGain NOTIFY actualExecutionChanged)
    Q_PROPERTY(bool canStart READ canStart NOTIFY startEligibilityChanged)
    Q_PROPERTY(QString startDisabledReason READ startDisabledReason
                   NOTIFY startEligibilityChanged)
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY startEligibilityChanged)
    Q_PROPERTY(QVariantList stems READ stems NOTIFY stemsChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(ResultPreviewMode resultPreviewMode READ resultPreviewMode
                   NOTIFY resultPreviewChanged)
    Q_PROPERTY(StemKind resultPreviewSoloKind READ resultPreviewSoloKind
                   NOTIFY resultPreviewChanged)

public:
    bool cudaRuntimeReady() const { return cudaRuntime_->ready(); }
    QString actualProvider() const { return actualProvider_; }
    QString actualDevice() const { return actualDevice_; }
    QString fallbackReason() const { return fallbackReason_; }
    double outputGain() const { return outputGain_; }
    Q_INVOKABLE bool configureGpuRuntime(const QString& modelId);
    enum class ModelState {
        NotInstalled,
        PendingVerification,
        Downloading,
        Paused,
        Verifying,
        Installed,
        ModelFailed,
    };
    Q_ENUM(ModelState)

    enum class JobState {
        Idle,
        Probing,
        Running,
        Cancelling,
        Completed,
        Cancelled,
        JobFailed,
    };
    Q_ENUM(JobState)

    enum class StemKind {
        Original,
        Vocals,
        Accompaniment,
        Drums,
        Bass,
        Other,
    };
    Q_ENUM(StemKind)

    enum class DeviceMode { Auto, CPU, GPU };
    Q_ENUM(DeviceMode)

    enum class ResultPreviewMode { None, Mix, Solo };
    Q_ENUM(ResultPreviewMode)

    explicit VocalSeparationController(
        AudioPreviewController* preview,
        WaveformProvider* waveformProvider,
        LibraryModel* library,
        ImportController* importer,
        PlaylistModel* playlists,
        VocalSeparationControllerOptions options = {},
        QObject* parent = nullptr);
    ~VocalSeparationController() override;

    QVariantMap inputInfo() const;
    QVariantList models() const;
    QString selectedModelId() const;
    DeviceMode deviceMode() const noexcept;
    QVariantList availableDevices() const;
    JobState jobState() const noexcept;
    QString stage() const;
    double progress() const noexcept;
    double downloadProgress() const noexcept;
    QString downloadingModelId() const;
    bool downloadBusy() const noexcept;
    QVariantList runtimeConfigurations() const;
    QString downloadSource() const;
    QString error() const;
    QString outputFormat() const;
    QString outputDirectory() const;
    QString modelStorageDirectory() const;
    bool runtimeReady() const;
    bool canStart() const;
    QString startDisabledReason() const;
    bool canRetry() const noexcept;
    QVariantList stems() const;
    QVariantList history() const;
    ResultPreviewMode resultPreviewMode() const noexcept;
    StemKind resultPreviewSoloKind() const noexcept;

    Q_INVOKABLE bool selectInput(const QUrl& url);
    Q_INVOKABLE bool clearInput();
    Q_INVOKABLE bool verifyInstalledModels();
    Q_INVOKABLE bool dropInput(const QList<QUrl>& urls);
    Q_INVOKABLE bool downloadModel(const QString& modelId);
    Q_INVOKABLE bool downloadModelFromMirror(const QString& modelId);
    Q_INVOKABLE bool configureRuntime(const QString& modelIdForUi);
    Q_INVOKABLE void pauseDownload();
    Q_INVOKABLE void resumeDownload();
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void pauseConfiguration(const QString& taskId);
    Q_INVOKABLE void resumeConfiguration(const QString& taskId);
    Q_INVOKABLE void cancelConfiguration(const QString& taskId);
    Q_INVOKABLE bool deleteModel(const QString& modelId);
    Q_INVOKABLE bool selectModel(const QString& modelId);
    Q_INVOKABLE bool setStemSelected(StemKind kind, bool selected);
    Q_INVOKABLE bool selectDevice(DeviceMode mode);
    Q_INVOKABLE bool selectOutputFormat(const QString& format);
    Q_INVOKABLE bool selectOutputDirectory(const QUrl& directory);
    Q_INVOKABLE bool probeDevices(bool force = false);
    Q_INVOKABLE bool start();
    Q_INVOKABLE void reportStartDisabledReason();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool retry();
    Q_INVOKABLE bool previewInput();
    Q_INVOKABLE bool previewStem(StemKind kind);
    Q_INVOKABLE bool toggleResultMix(qint64 positionMs);
    Q_INVOKABLE bool previewStemAt(StemKind kind, qint64 positionMs);
    Q_INVOKABLE bool setStemPreviewVolume(StemKind kind, double volume);
    Q_INVOKABLE bool exportStem(StemKind kind, const QUrl& destination);
    Q_INVOKABLE bool exportStemToOutputDirectory(StemKind kind);
    Q_INVOKABLE bool exportSelected(const QUrl& destinationDirectory);
    Q_INVOKABLE bool exportAll(const QUrl& destinationDirectory);
    Q_INVOKABLE bool addStemToPlaylist(StemKind kind,
                                       const QString& playlistId);
    Q_INVOKABLE bool addSelectedToPlaylist(const QString& playlistId);
    Q_INVOKABLE bool openOutputDirectory();
    Q_INVOKABLE bool selectHistoryInput(const QString& localPath);
    Q_INVOKABLE bool openHistoryOutputDirectory(const QString& localPath);
    Q_INVOKABLE bool openModelDirectory();
    Q_INVOKABLE bool selectModelDirectory(const QUrl& directory);

signals:
    void actualExecutionChanged();
    void inputInfoChanged();
    void modelsChanged();
    void selectedModelIdChanged();
    void deviceModeChanged();
    void availableDevicesChanged();
    void jobStateChanged();
    void progressChanged();
    void downloadProgressChanged();
    void downloadStateChanged();
    void downloadSourcesExhausted(const QVariantMap& outcome);
    void errorChanged();
    void outputFormatChanged();
    void outputDirectoryChanged();
    void modelStorageDirectoryChanged();
    void startEligibilityChanged();
    void stemsChanged();
    void historyChanged();
    void resultPreviewChanged();
    void playlistOperationFinished(bool success, const QString& diagnostic);

private:
    friend class VocalSeparationControllerTestDriver;

    enum class RequestKind { Probe, Separation };

    struct ActiveRequestContext {
        RequestKind kind = RequestKind::Probe;
        QString inputPath{};
        QString modelId{};
        QString outputRoot{};
        QString outputFormat{};
        DeviceMode device = DeviceMode::Auto;
        QList<StemKind> stemKinds{};
        QStringList stemNames{};
        QStringList stemLabels{};
        quint64 resultGeneration = 0;
    };

    struct WaveformWork {
        QString path;
        QString trackId;
        StemKind kind = StemKind::Original;
        quint64 resultGeneration = 0;
    };

    struct PlaylistOperation {
        QString playlistId;
        QStringList paths;
        QStringList addedTrackIds;
    };

    struct DownloadItem {
        VocalDownloadFile file;
        QString destination;
        bool runtimeArchive = false;
        QUrl mirrorUrl;
        bool mirrorAttempted = false;
    };

    struct CustomModelBinding {
        QString profileId;
        QStringList sha256;
        QVector<qint64> bytes;
        QStringList roles;
    };

    enum class VerificationPurpose { None, Refresh, Download, Start, Probe };

    struct VerificationResult {
        QSet<QString> verifiedModels;
        QSet<QString> checkedModels;
        QSet<QString> verifiedFiles;
        QHash<QString, QString> modelFingerprints;
        bool runtimeVerified = false;
        bool runtimeChecked = false;
        QString runtimeFingerprint;
    };

    struct ModelDirectoryIndex {
        QHash<QString, QStringList> filesByName;
        QStringList directories;
        QStringList manifests;
    };

    struct ModelConfiguration {
        QString modelId;
        QString state = QStringLiteral("checking");
        QString error;
        QString detail;
        double progress = -1;
        qint64 completedBytes = 0;
        qint64 totalBytes = 0;
        bool mirror = false;
        quint64 generation = 0;
        QList<DownloadItem> queue;
        std::unique_ptr<VocalSeparationDownloader> downloader;
        QPointer<QFutureWatcher<VerificationResult>> verification;
        std::shared_ptr<std::atomic_bool> cancellation;
    };
    bool beginModelConfiguration(const QString& modelId, bool mirror);
    void advanceModelConfiguration(const std::shared_ptr<ModelConfiguration>& task);
    bool ensureSharedRuntime();
    bool modelConfigurationBusy(const QString& modelId) const;
    QVariantMap configurationFields(const QString& modelId) const;
    void configurationChanged();

    const VocalModelCard* selectedModel() const;
    const VocalModelCard* modelForId(const QString& modelId) const;
    QString modelDirectory(const QString& modelId) const;
    static ModelDirectoryIndex buildModelDirectoryIndex(const QString& root);
    QStringList modelFilePaths(const VocalModelCard& model) const;
    QString runtimeDirectory() const;
    bool modelInstalled(const VocalModelCard& model) const;
    bool modelFilesPresent(const VocalModelCard& model) const;
    bool deviceAvailable(DeviceMode mode) const;
    bool canConfigureModel(const QString& modelId) const;
    bool downloadConflictsWithModel(const QString& modelId) const;
    QString probeFingerprint(const QString& modelId) const;
    bool beginVerification(VerificationPurpose purpose,
                           const VocalModelCard* model = nullptr);
    void finishVerification(quint64 generation,
                            const VerificationResult& result);
    bool launchProbe();
    bool launchSeparation(const ActiveRequestContext& context);
    bool beginSeparationRequest(ActiveRequestContext context);
    void failRequest(const ActiveRequestContext& context,
                     const QString& error, const QString& stage);
    void invalidateRetry();
    void refreshModels();
    void rebuildStems();
    void setJobState(JobState state, const QString& stage = {});
    void setError(const QString& error);
    bool beginModelDownload(const QString& modelId, bool preferDomesticMirror);
    void handleDownloadFailure(const VocalInstallResult& result,
                               bool startRetry);
    void finishExhaustedDownload(const QString& source,
                                 const QString& diagnostic);
    void startNextDownload();
    void rebuildModelDirectoryWatcher();
    void scheduleModelDirectoryScan();
    void scanModelDirectory();
    void discoverCustomModels(const QStringList& manifests);
    void handleProbe(const QJsonObject& payload);
    void handleResult(const QJsonObject& payload);
    void analyzeNextWaveform();
    void handleWaveform(const QString& path, const QVariantMap& layers);
    void handleWaveformFailure(const QString& path, const QString& trackId,
                               qulonglong generation, int errorCode);
    void clearPublishedResult();
    void resetInputSession();
    void stopPreviewForCurrentInputOrResult();
    QList<StemKind> resultMixKinds() const;
    bool startResultPreview(const QList<StemKind>& kinds, qint64 positionMs,
                            ResultPreviewMode mode, StemKind soloKind);
    void resetResultPreviewState();
    bool togglePreviewPath(const QString& path, const QString& root = {});
    bool requestInFlight() const noexcept;
    QString pathForStem(StemKind kind) const;
    QStringList selectedStemNames() const;
    QList<StemKind> selectedStemKinds() const;
    bool exportKinds(const QList<StemKind>& kinds,
                     const QUrl& destinationDirectory);
    bool addPathsToPlaylist(const QStringList& paths,
                            const QString& playlistId);
    bool playlistExists(const QString& playlistId) const;
    void finishPlaylistOperation(bool success, const QString& diagnostic);
    static QVariantList boundedPeaks(const QVariantList& peaks);
    static bool atomicCopyNoOverwrite(const QString& source,
                                      const QString& sourceRoot,
                                      const QString& destination);

    QPointer<AudioPreviewController> preview_;
    QPointer<WaveformProvider> waveformProvider_;
    QPointer<LibraryModel> library_;
    QPointer<ImportController> importer_;
    QPointer<PlaylistModel> playlists_;
    VocalSeparationControllerOptions options_;
    QList<VocalModelCard> baseCatalog_;
    VocalSeparationHistoryStore historyStore_;
    SeparationProcessClient process_;
    QNetworkAccessManager network_;
    std::unique_ptr<VocalSeparationDownloader> downloader_;
    std::unique_ptr<ExternalSeparationRuntime> externalRuntime_;
    std::unique_ptr<CudaSeparationRuntime> cudaRuntime_;
    QHash<QString, std::shared_ptr<ModelConfiguration>> modelConfigurations_;
    QHash<QString, double> runtimeProgress_;
    QHash<QString, QString> runtimeDetails_, runtimeErrors_;
    QString actualProvider_, actualDevice_, fallbackReason_;
    double outputGain_ = 1.0;
    QHash<QString, QString> validatedGpuProviders_;
    QHash<QString, QJsonObject> deviceProbeCache_;
    QHash<QString, QString> deviceProbeFingerprints_;
    QString activeProbeFingerprint_;
    QFileSystemWatcher modelDirectoryWatcher_;
    QTimer modelDirectoryScanTimer_;
    QFutureWatcher<VerificationResult>* verificationWatcher_ = nullptr;
    QFutureWatcher<ModelDirectoryIndex>* modelDirectoryIndexWatcher_ = nullptr;
    QFutureWatcher<VocalInstallResult>* runtimeInstallerWatcher_ = nullptr;
    std::shared_ptr<std::atomic_bool> verificationCancellation_;
    std::shared_ptr<std::atomic_bool> runtimeInstallCancellation_;
    VerificationPurpose verificationPurpose_ = VerificationPurpose::None;
    quint64 verificationGeneration_ = 0;
    QString verifyingModelId_;
    QSet<QString> verifiedModelIds_;
    QSet<QString> verifiedOrRejectedModelIds_;
    QHash<QString, QString> modelVerificationFingerprints_;
    bool runtimeVerified_ = false;
    bool runtimeVerificationKnown_ = false;
    QString runtimeVerificationFingerprint_;
    std::optional<QString> deferredRuntimeConfigurationModelId_;
    bool runtimeOnlyDownload_ = false;
    QList<DownloadItem> downloadQueue_;
    QString downloadingModelId_;
    QString failedDownloadModelId_;
    QString downloadSource_;
    bool preferDomesticMirror_ = false;
    double downloadProgress_ = 0.0;
    qint64 completedDownloadBytes_ = 0;
    qint64 totalDownloadBytes_ = 0;
    QHash<int, double> stemPreviewVolumes_;
    QList<StemKind> resultPreviewMixKinds_;
    QVariantMap inputInfo_;
    QVariantList models_;
    QVariantList rejectedCustomModels_;
    QHash<QString, CustomModelBinding> customModelBindings_;
    QVariantList availableDevices_;
    QVariantList stems_;
    QVariantList history_;
    QString selectedModelId_;
    QString outputFormat_ = QStringLiteral("wav");
    QString outputDirectory_;
    QString modelStorageDirectory_;
    QHash<QString, QStringList> indexedModelFiles_;
    bool modelDirectoryRescanPending_ = false;
    bool verifyAllModelsOnNextScan_ = false;
    bool deviceProbePending_ = false;
    bool pendingStartVerification_ = false;
    bool deviceChosenByUser_ = false;
    DeviceMode deviceMode_ = DeviceMode::Auto;
    JobState jobState_ = JobState::Idle;
    QString stage_;
    double progress_ = 0.0;
    QString error_;
    ResultPreviewMode resultPreviewMode_ = ResultPreviewMode::None;
    StemKind resultPreviewSoloKind_ = StemKind::Original;
    std::optional<ActiveRequestContext> activeRequest_;
    std::optional<ActiveRequestContext> failedRequest_;
    QString publishedOutputRoot_;
    QList<WaveformWork> waveformQueue_;
    quint64 resultGeneration_ = 0;
    quint64 inputWaveformGeneration_ = 0;
    QString inputWaveformTrackId_;
    std::optional<PlaylistOperation> playlistOperation_;
};

#pragma once

#include "separation_process_client.hpp"
#include "vocal_separation_catalog.hpp"
#include "vocal_separation_history.hpp"

#include <QHash>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

class AudioPreviewController;
class ImportController;
class LibraryModel;
class PlaylistModel;
class VocalSeparationDownloader;
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
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QVariantList stems READ stems NOTIFY stemsChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)

public:
    enum class ModelState {
        NotInstalled,
        Downloading,
        Paused,
        Verifying,
        Installed,
        Failed,
    };
    Q_ENUM(ModelState)

    enum class JobState {
        Idle,
        Probing,
        Running,
        Cancelling,
        Completed,
        Cancelled,
        Failed,
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
    QString error() const;
    QVariantList stems() const;
    QVariantList history() const;

    Q_INVOKABLE bool selectInput(const QUrl& url);
    Q_INVOKABLE bool dropInput(const QList<QUrl>& urls);
    Q_INVOKABLE bool downloadModel(const QString& modelId);
    Q_INVOKABLE void pauseDownload();
    Q_INVOKABLE void resumeDownload();
    Q_INVOKABLE bool deleteModel(const QString& modelId);
    Q_INVOKABLE bool selectModel(const QString& modelId);
    Q_INVOKABLE bool setStemSelected(StemKind kind, bool selected);
    Q_INVOKABLE bool selectDevice(DeviceMode mode);
    Q_INVOKABLE bool selectOutputFormat(const QString& format);
    Q_INVOKABLE bool selectOutputDirectory(const QUrl& directory);
    Q_INVOKABLE bool probeDevices();
    Q_INVOKABLE bool start();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool retry();
    Q_INVOKABLE bool previewInput();
    Q_INVOKABLE bool previewStem(StemKind kind);
    Q_INVOKABLE bool exportStem(StemKind kind, const QUrl& destination);
    Q_INVOKABLE bool exportSelected(const QUrl& destinationDirectory);
    Q_INVOKABLE bool addStemToPlaylist(StemKind kind,
                                       const QString& playlistId);
    Q_INVOKABLE bool addSelectedToPlaylist(const QString& playlistId);
    Q_INVOKABLE bool openOutputDirectory();
    Q_INVOKABLE bool openModelDirectory();

signals:
    void inputInfoChanged();
    void modelsChanged();
    void selectedModelIdChanged();
    void deviceModeChanged();
    void availableDevicesChanged();
    void jobStateChanged();
    void progressChanged();
    void errorChanged();
    void stemsChanged();
    void historyChanged();

private:
    struct DownloadItem {
        VocalDownloadFile file;
        QString destination;
        bool runtimeArchive = false;
    };

    const VocalModelCard* selectedModel() const;
    const VocalModelCard* modelForId(const QString& modelId) const;
    QString modelDirectory(const QString& modelId) const;
    QString runtimeDirectory() const;
    bool modelInstalled(const VocalModelCard& model) const;
    bool runtimeReady() const;
    void refreshModels();
    void rebuildStems();
    void setJobState(JobState state, const QString& stage = {});
    void setError(const QString& error);
    void startNextDownload();
    void handleProbe(const QJsonObject& payload);
    void handleResult(const QJsonObject& payload);
    void analyzeNextWaveform();
    void handleWaveform(const QString& path, const QVariantMap& layers);
    QString pathForStem(StemKind kind) const;
    QStringList selectedStemNames() const;
    QList<StemKind> selectedStemKinds() const;
    bool addPathsToPlaylist(const QStringList& paths,
                            const QString& playlistId);
    static QVariantList boundedPeaks(const QVariantList& peaks);
    static bool atomicCopyNoOverwrite(const QString& source,
                                      const QString& destination);

    QPointer<AudioPreviewController> preview_;
    QPointer<WaveformProvider> waveformProvider_;
    QPointer<LibraryModel> library_;
    QPointer<ImportController> importer_;
    QPointer<PlaylistModel> playlists_;
    VocalSeparationControllerOptions options_;
    VocalSeparationHistoryStore historyStore_;
    SeparationProcessClient process_;
    QNetworkAccessManager network_;
    std::unique_ptr<VocalSeparationDownloader> downloader_;
    QList<DownloadItem> downloadQueue_;
    QString downloadingModelId_;
    QVariantMap inputInfo_;
    QVariantList models_;
    QVariantList availableDevices_;
    QVariantList stems_;
    QVariantList history_;
    QString selectedModelId_;
    QString outputFormat_ = QStringLiteral("wav");
    QString outputDirectory_;
    DeviceMode deviceMode_ = DeviceMode::Auto;
    JobState jobState_ = JobState::Idle;
    QString stage_;
    double progress_ = 0.0;
    QString error_;
    bool lastRequestWasProbe_ = false;
    QList<StemKind> activeStemKinds_;
    QStringList waveformQueue_;
    QHash<QString, StemKind> waveformKinds_;
};

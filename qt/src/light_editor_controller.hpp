#pragma once

#include <agplayer/c_api.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <QMutex>
#include <QHash>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

template <typename T>
class QFutureWatcher;

// LightEditor: QML singleton for basic audio editing (trim, fade, gain).
// Wraps ag_multitrack_edit. When a file is loaded, duration is read via
// ag_metadata_open so the QML trim sliders can be bounded correctly.
class LightEditor final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool importBusy READ importBusy NOTIFY importBusyChanged)
    Q_PROPERTY(QString inputFileName READ inputFileName NOTIFY inputFileChanged)
    Q_PROPERTY(bool hasInput READ hasInput NOTIFY inputFileChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY inputFileChanged)
    Q_PROPERTY(QString inputFormat READ inputFormat NOTIFY inputFileChanged)
    Q_PROPERTY(int inputSampleRate READ inputSampleRate NOTIFY inputFileChanged)
    Q_PROPERTY(int inputChannels READ inputChannels NOTIFY inputFileChanged)
    Q_PROPERTY(QVariantList waveformPeaks READ waveformPeaks NOTIFY waveformPeaksChanged)

    // Multi-track model
    Q_PROPERTY(int trackCount READ trackCount CONSTANT)
    Q_PROPERTY(int selectedTrack READ selectedTrack WRITE setSelectedTrack NOTIFY selectedTrackChanged)
    Q_PROPERTY(QVariantList trackNames READ trackNames NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList trackHasFiles READ trackHasFiles NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList trackPeaks READ trackPeaks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged)
    Q_PROPERTY(int clipCount READ clipCount NOTIFY tracksChanged)
    Q_PROPERTY(QString selectedClipId READ selectedClipId WRITE setSelectedClipId
                   NOTIFY selectedClipChanged)
    Q_PROPERTY(QVariantList selectedClipIds READ selectedClipIds
                   NOTIFY selectedClipChanged)
    Q_PROPERTY(double targetBpm READ targetBpm WRITE setTargetBpm NOTIFY targetBpmChanged)
    Q_PROPERTY(bool snapEnabled READ snapEnabled WRITE setSnapEnabled NOTIFY snapEnabledChanged)
    Q_PROPERTY(int snapDivision READ snapDivision WRITE setSnapDivision
                   NOTIFY projectSettingsChanged)
    Q_PROPERTY(QString timeSignature READ timeSignature WRITE setTimeSignature
                   NOTIFY projectSettingsChanged)
    Q_PROPERTY(QString projectKey READ projectKey WRITE setProjectKey
                   NOTIFY projectSettingsChanged)
    Q_PROPERTY(bool loopEnabled READ loopEnabled WRITE setLoopEnabled
                   NOTIFY projectSettingsChanged)
    Q_PROPERTY(qint64 loopStartMs READ loopStartMs WRITE setLoopStartMs
                   NOTIFY projectSettingsChanged)
    Q_PROPERTY(qint64 loopEndMs READ loopEndMs WRITE setLoopEndMs
                   NOTIFY projectSettingsChanged)
    Q_PROPERTY(bool rippleEditing READ rippleEditing WRITE setRippleEditing
                   NOTIFY projectSettingsChanged)
    Q_PROPERTY(bool autoCrossfade READ autoCrossfade WRITE setAutoCrossfade
                   NOTIFY projectSettingsChanged)
    Q_PROPERTY(bool keepPitch READ keepPitch WRITE setKeepPitch NOTIFY keepPitchChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    Q_PROPERTY(bool hasClipboard READ hasClipboard NOTIFY clipboardChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectStateChanged)
    Q_PROPERTY(bool projectDirty READ projectDirty NOTIFY projectStateChanged)

public:
    explicit LightEditor(QObject* parent = nullptr);
    ~LightEditor() override;

    double progress() const noexcept;
    bool busy() const noexcept;
    bool importBusy() const noexcept;
    QString inputFileName() const noexcept;
    bool hasInput() const noexcept;
    qint64 durationMs() const noexcept;
    QString inputFormat() const noexcept;
    int inputSampleRate() const noexcept;
    int inputChannels() const noexcept;
    QVariantList waveformPeaks() const noexcept;

    int trackCount() const noexcept;
    int selectedTrack() const noexcept;
    void setSelectedTrack(int value);
    QVariantList trackNames() const noexcept;
    QVariantList trackHasFiles() const noexcept;
    QVariantList trackPeaks() const noexcept;
    QVariantList tracks() const noexcept;
    int clipCount() const noexcept;
    QString selectedClipId() const;
    QVariantList selectedClipIds() const;
    void setSelectedClipId(const QString& value);
    double targetBpm() const noexcept;
    void setTargetBpm(double value);
    bool snapEnabled() const noexcept;
    void setSnapEnabled(bool value);
    int snapDivision() const noexcept;
    void setSnapDivision(int value);
    QString timeSignature() const;
    void setTimeSignature(const QString& value);
    QString projectKey() const;
    void setProjectKey(const QString& value);
    bool loopEnabled() const noexcept;
    void setLoopEnabled(bool value);
    qint64 loopStartMs() const noexcept;
    void setLoopStartMs(qint64 value);
    qint64 loopEndMs() const noexcept;
    void setLoopEndMs(qint64 value);
    bool rippleEditing() const noexcept;
    void setRippleEditing(bool value);
    bool autoCrossfade() const noexcept;
    void setAutoCrossfade(bool value);
    bool keepPitch() const noexcept;
    void setKeepPitch(bool value);
    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    bool hasClipboard() const noexcept;
    QString projectPath() const;
    bool projectDirty() const noexcept;
    void setOverwriteExisting(bool value) noexcept { overwriteExisting_ = value; }

    Q_INVOKABLE void loadFile(const QUrl& url);
    Q_INVOKABLE void loadFileToTrack(int trackIndex, const QUrl& url);
    Q_INVOKABLE int loadFiles(const QList<QUrl>& urls);
    Q_INVOKABLE void queueFiles(const QList<QUrl>& urls);
    Q_INVOKABLE QVariantList waveformPeaksForClip(const QString& clipId) const;
    Q_INVOKABLE bool relinkClipById(const QString& clipId, const QUrl& url,
                                    bool relinkMatchingSources = true);
    Q_INVOKABLE void start(qint64 trimStartMs, qint64 trimEndMs,
                           int fadeInMs, int fadeOutMs, double gain,
                           const QString& outputDir,
                           const QString& outputFormat = QString(),
                           int outputSampleRate = 0,
                           int outputChannels = 0);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void clearTrack(int trackIndex);
    Q_INVOKABLE bool moveClip(int trackIndex, qint64 timelineStartMs);
    Q_INVOKABLE bool trimClip(int trackIndex, qint64 inMs, qint64 outMs);
    Q_INVOKABLE bool moveClipById(const QString& clipId, qint64 timelineStartMs);
    Q_INVOKABLE bool moveClipToTrackById(const QString& clipId,
                                         int targetTrackIndex,
                                         qint64 timelineStartMs);
    Q_INVOKABLE bool selectClip(const QString& clipId, bool additive,
                                bool rangeSelection);
    Q_INVOKABLE int selectClipsInRange(qint64 startMs, qint64 endMs,
                                       int firstTrack, int lastTrack,
                                       bool additive);
    Q_INVOKABLE void selectAllClips();
    Q_INVOKABLE void clearClipSelection();
    Q_INVOKABLE bool moveSelectedClips(qint64 deltaMs, int trackDelta);
    Q_INVOKABLE bool trimClipById(const QString& clipId, qint64 inMs, qint64 outMs);
    Q_INVOKABLE bool trimClipEdgeById(const QString& clipId, qint64 inMs,
                                      qint64 outMs, bool trimLeft,
                                      bool bypassSnap = false);
    Q_INVOKABLE bool setClipLoopById(const QString& clipId,
                                     const QString& loopMode,
                                     qint64 timelineDurationMs);
    Q_INVOKABLE bool setClipFadesById(const QString& clipId,
                                      int fadeInMs, int fadeOutMs);
    Q_INVOKABLE bool setClipFadeCurvesById(const QString& clipId,
                                           const QString& fadeInCurve,
                                           const QString& fadeOutCurve);
    Q_INVOKABLE bool setClipGainById(const QString& clipId, double gain);
    Q_INVOKABLE bool setClipMutedById(const QString& clipId, bool muted);
    Q_INVOKABLE bool setClipPitchById(const QString& clipId,
                                      double semitones,
                                      double cents);
    Q_INVOKABLE bool setClipFormantModeById(const QString& clipId, int mode);
    Q_INVOKABLE bool setClipTransientProtectionById(const QString& clipId,
                                                    double amount);
    Q_INVOKABLE bool setClipHighQualityById(const QString& clipId, bool enabled);
    Q_INVOKABLE bool setClipTargetBpmById(const QString& clipId, double value);
    Q_INVOKABLE bool setClipKeepPitchById(const QString& clipId, bool value);
    Q_INVOKABLE bool setClipBeatAlignedById(const QString& clipId, bool value);
    Q_INVOKABLE bool deleteClipById(const QString& clipId);
    Q_INVOKABLE bool copySelectedClip();
    Q_INVOKABLE bool cutSelectedClip();
    Q_INVOKABLE bool pasteClip();
    Q_INVOKABLE bool duplicateSelectedClip(qint64 timelineStartMs = -1);
    Q_INVOKABLE bool duplicateClipToTrackById(const QString& clipId,
                                              int targetTrackIndex,
                                              qint64 timelineStartMs);
    Q_INVOKABLE bool deleteSelectedClip();
    Q_INVOKABLE bool splitSelectedClip(qint64 projectPositionMs);
    Q_INVOKABLE bool mergeSelectedClip();
    Q_INVOKABLE bool cropSelectedClip(qint64 projectPositionMs);
    Q_INVOKABLE void setTrackMuted(int trackIndex, bool value);
    Q_INVOKABLE void setTrackSolo(int trackIndex, bool value);
    Q_INVOKABLE void setTrackLocked(int trackIndex, bool value);
    Q_INVOKABLE bool setTrackVolume(int trackIndex, double value);
    Q_INVOKABLE bool setTrackPan(int trackIndex, double value);
    Q_INVOKABLE bool setTrackName(int trackIndex, const QString& value);
    Q_INVOKABLE bool setTrackColor(int trackIndex, const QString& value);
    Q_INVOKABLE bool setTrackCollapsed(int trackIndex, bool value);
    Q_INVOKABLE bool moveTrack(int from, int to);
    Q_INVOKABLE bool saveProject(const QUrl& url);
    Q_INVOKABLE bool loadProject(const QUrl& url);
    Q_INVOKABLE bool autosaveNow();
    Q_INVOKABLE bool setTrackTargetBpm(int trackIndex, double value);
    Q_INVOKABLE bool setTrackKeepPitch(int trackIndex, bool value);
    Q_INVOKABLE bool setTrackBeatAligned(int trackIndex, bool value);
    Q_INVOKABLE void analyzeTrackBpm(int trackIndex);
    Q_INVOKABLE void analyzeClipBpmById(const QString& clipId);
    Q_INVOKABLE void unifyBpm(bool alignBeats);
    Q_INVOKABLE void exportProject(const QString& outputDir,
                                   const QString& outputFormat,
                                   int outputSampleRate,
                                   int outputChannels);
    Q_INVOKABLE void exportProjectScope(const QString& scope,
                                        qint64 rangeStartMs,
                                        qint64 rangeEndMs,
                                        const QString& outputDir,
                                        const QString& outputFormat,
                                        int outputSampleRate,
                                        int outputChannels);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

signals:
    void progressChanged();
    void busyChanged();
    void importBusyChanged();
    void filesQueued(int loaded);
    void inputFileChanged();
    void waveformPeaksChanged();
    void waveformAnalysisCompleted(const QString& clipId);
    void lightEditCompleted(const QString& outputPath);
    void errorOccurred(const QString& message);
    void selectedTrackChanged();
    void tracksChanged();
    void targetBpmChanged();
    void snapEnabledChanged();
    void projectSettingsChanged();
    void keepPitchChanged();
    void undoStateChanged();
    void clipboardChanged();
    void selectedClipChanged();
    void trackError(int trackIndex, const QString& message);
    void projectStateChanged();
    void autosaveWritten(const QString& path);
    void autosaveRecovered(const QString& path);

private:
    struct Track {
        QString clipId;
        int laneIndex = 0;
        QString path;
        QString renderPath;
        QString name;
        QString format;
        qint64 durationMs = 0;
        qint64 timelineStartMs = 0;
        qint64 inMs = 0;
        qint64 outMs = 0;
        QString loopMode = QStringLiteral("OneShot");
        qint64 timelineDurationMs = 0;
        int fadeInMs = 0;
        int fadeOutMs = 0;
        QString fadeInCurve = QStringLiteral("EqualPower");
        QString fadeOutCurve = QStringLiteral("EqualPower");
        bool autoFadeIn = false;
        bool autoFadeOut = false;
        double gain = 1.0;
        double originalBpm = 0.0;
        double bpmConfidence = 0.0;
        double targetBpm = 0.0;
        double speedRatio = 1.0;
        bool keepPitch = true;
        double pitchSemitones = 0.0;
        double finePitchCents = 0.0;
        int formantMode = 0;
        double transientProtection = 0.0;
        bool highQuality = false;
        bool muted = false;
        bool solo = false;
        bool locked = false;
        bool aligned = false;
        QString trackName;
        QString color;
        double volume = 1.0;
        double pan = 0.0;
        bool collapsed = false;
        int sampleRate = 0;
        int channels = 0;
        QVariantList peaks;
        bool waveformPending = false;
    };

    struct EditorState {
        std::vector<Track> tracks;
        std::vector<Track> extraClips;
        int selectedTrack = 0;
        QString selectedClipId;
        QStringList selectedClipIds;
    };

    std::vector<Track> tracks_;
    std::vector<Track> extraClips_;
    std::vector<EditorState> undoStack_;
    std::vector<EditorState> redoStack_;
    std::optional<Track> clipboard_;
    int selectedTrack_ = 0;
    QString selectedClipId_;
    QStringList selectedClipIds_;
    double targetBpm_ = 128.0;
    bool snapEnabled_ = true;
    int snapDivision_ = 4;
    QString timeSignature_ = QStringLiteral("4/4");
    QString projectKey_ = QStringLiteral("C");
    bool loopEnabled_ = false;
    qint64 loopStartMs_ = 0;
    qint64 loopEndMs_ = 0;
    bool rippleEditing_ = false;
    bool keepPitch_ = true;
    // Light edit is deliberately a six-lane loop arranger, not a full DAW.
    static constexpr int kTrackCount = 6;
    static constexpr int kMaximumUndoStates = 100;

    std::atomic<bool> busy_{false};
    bool importBusy_ = false;
    std::atomic<double> progress_{0.0};
    std::atomic<ag_cancel_token*> token_{nullptr};
    QMutex tokenMutex_;
    QPointer<QFutureWatcher<int>> watcher_;
    QPointer<QFutureWatcher<QList<QUrl>>> importDiscoveryWatcher_;
    QList<QUrl> pendingImportUrls_;
    QList<QUrl> pendingImportFiles_;
    bool pendingImportUndo_ = false;
    bool overwriteExisting_ = false;
    QString projectPath_;
    bool projectDirty_ = false;
    bool loadingProject_ = false;
    bool autoCrossfade_ = true;
    QTimer autosaveTimer_;
    QThreadPool waveformPool_;
    QHash<QString, quint64> waveformGenerations_;
    QHash<QString, std::shared_ptr<ag_cancel_token>> waveformCancelTokens_;
    quint64 nextWaveformGeneration_ = 0;
    QString pendingExportScope_ = QStringLiteral("project");
    qint64 pendingExportRangeStartMs_ = 0;
    qint64 pendingExportRangeEndMs_ = 0;

    const Track& currentTrack() const;
    Track& currentTrack();
    bool isValidTrackIndex(int index) const noexcept;
    int appendFilesToTimeline(const QList<QUrl>& files);
    void beginQueuedImportDiscovery();
    void processQueuedImportChunk();
    void loadPathIntoTrack(const QString& path, int trackIndex);
    Track makeClip(const QString& path, int trackIndex) const;
    void requestWaveformAnalysis(const QString& clipId, const QString& path);
    QString waveformCachePath(const QString& path) const;
    Track* findClip(const QString& clipId);
    const Track* findClip(const QString& clipId) const;
    qint64 laneEndMs(int trackIndex) const;
    QVariantMap clipMap(const Track& clip) const;
    QStringList clipIdsInTimelineOrder() const;
    void clearAutomaticCrossfadesForLane(int trackIndex);
    void applyAutoCrossfadesForLane(int trackIndex);
    void selectOnly(const QString& clipId);
    void pushUndoState();
    void restoreState(EditorState state);
    void emitEditorStateChanged();
    void markProjectDirty();
    bool writeProject(const QString& path) const;

    void setBusy(bool value);
    void setImportBusy(bool value);
    void setProgress(double value);
    QString computeOutputPath(const QString& firstInputPath,
                              const QString& outputDir,
                              const QString& outputFormat,
                              int loadedCount) const;
};

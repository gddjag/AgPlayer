#pragma once

#include <QObject>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include "editor_timeline_math.hpp"

#include <array>
#include <optional>

struct ag_player;
class LibraryModel;
class PlaybackControllerTest;

class PlaybackController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY positionMsChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY durationMsChanged)
    Q_PROPERTY(float volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
    Q_PROPERTY(Mode mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(qint64 trackIndex READ trackIndex NOTIFY trackIndexChanged)
    Q_PROPERTY(qint64 trackCount READ trackCount NOTIFY trackCountChanged)
    Q_PROPERTY(QString currentTrackId READ currentTrackId NOTIFY currentTrackIdChanged)
    Q_PROPERTY(QStringList queueTrackIds READ queueTrackIds NOTIFY queueTrackIdsChanged)
    Q_PROPERTY(QString lyrics READ lyrics NOTIFY lyricsChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(bool deviceLost READ deviceLost NOTIFY deviceLostChanged)
    Q_PROPERTY(QStringList outputDevices READ outputDevices
                   NOTIFY outputDevicesChanged)
    Q_PROPERTY(QStringList outputDeviceIds READ outputDeviceIds
                   NOTIFY outputDevicesChanged)
    Q_PROPERTY(bool exclusiveModeActive READ exclusiveModeActive
                   NOTIFY exclusiveModeActiveChanged)
    Q_PROPERTY(QVariantList spectrum READ spectrum NOTIFY spectrumChanged)
    Q_PROPERTY(bool replayGainClippingWarning READ replayGainClippingWarning
                   NOTIFY replayGainClippingWarningChanged)
    Q_PROPERTY(qint64 selectionStartMs READ selectionStartMs
                   NOTIFY selectionStartMsChanged)
    Q_PROPERTY(qint64 selectionEndMs READ selectionEndMs
                   NOTIFY selectionEndMsChanged)
    Q_PROPERTY(bool selectionLoopEnabled READ selectionLoopEnabled
                   NOTIFY selectionLoopEnabledChanged)
    Q_PROPERTY(double speedRatio READ speedRatio NOTIFY tempoChanged)
    Q_PROPERTY(double sourceBpm READ sourceBpm NOTIFY tempoChanged)
    Q_PROPERTY(double targetBpm READ targetBpm NOTIFY tempoChanged)
    Q_PROPERTY(bool keepPitch READ keepPitch NOTIFY tempoChanged)
    Q_PROPERTY(bool scratchActive READ scratchActive
                   NOTIFY scratchStatusChanged)
    Q_PROPERTY(bool scratchReady READ scratchReady
                   NOTIFY scratchStatusChanged)
    Q_PROPERTY(bool scratchBuffering READ scratchBuffering
                   NOTIFY scratchStatusChanged)
    Q_PROPERTY(qint64 cuePositionMs READ cuePositionMs NOTIFY cueChanged)
    Q_PROPERTY(bool cueAuditioning READ cueAuditioning NOTIFY cueChanged)
    Q_PROPERTY(double beatGridBpm READ beatGridBpm NOTIFY beatGridChanged)
    Q_PROPERTY(qint64 beatGridOffsetMs READ beatGridOffsetMs
                   NOTIFY beatGridChanged)
    Q_PROPERTY(bool beatGridCalibrated READ beatGridCalibrated
                   NOTIFY beatGridChanged)
    Q_PROPERTY(bool beatGridEstimatedBpm READ beatGridEstimatedBpm
                   NOTIFY beatGridChanged)
    Q_PROPERTY(QVariantList hotCuePositions READ hotCuePositions
                   NOTIFY hotCuePositionsChanged)

public:
    static constexpr int PollIntervalMs = 17;
    static constexpr int IdlePollIntervalMs = 100;

    enum State { Stopped, Loading, Playing, Paused, Error };
    Q_ENUM(State)

    enum Mode { Sequential, RepeatOne, Shuffle, RepeatAll };
    Q_ENUM(Mode)

    explicit PlaybackController(ag_player* player = nullptr,
                                LibraryModel* library = nullptr,
                                QObject* parent = nullptr);
    ~PlaybackController() override;

    State state() const noexcept;
    qint64 positionMs() const noexcept;
    qint64 durationMs() const noexcept;
    float volume() const noexcept;
    bool muted() const noexcept;
    Mode mode() const noexcept;
    qint64 trackIndex() const noexcept;
    qint64 trackCount() const noexcept;
    QString currentTrackId() const;
    QStringList queueTrackIds() const;
    QString lyrics() const;
    QString errorMessage() const;
    bool deviceLost() const noexcept;
    QStringList outputDevices() const;
    QStringList outputDeviceIds() const;
    bool exclusiveModeActive() const noexcept;
    QVariantList spectrum() const;
    bool replayGainClippingWarning() const noexcept;
    qint64 selectionStartMs() const noexcept;
    qint64 selectionEndMs() const noexcept;
    bool selectionLoopEnabled() const noexcept;
    double speedRatio() const noexcept;
    double sourceBpm() const noexcept;
    double targetBpm() const noexcept;
    bool keepPitch() const noexcept;
    bool scratchActive() const noexcept;
    bool scratchReady() const noexcept;
    bool scratchBuffering() const noexcept;
    qint64 cuePositionMs() const noexcept;
    bool cueAuditioning() const noexcept;
    double beatGridBpm() const noexcept;
    qint64 beatGridOffsetMs() const noexcept;
    bool beatGridCalibrated() const noexcept;
    bool beatGridEstimatedBpm() const noexcept;
    QVariantList hotCuePositions() const;

    void setLibraryModel(LibraryModel* library);
    void setPlayer(ag_player* player);
    [[nodiscard]] ag_player* playerHandle() const noexcept { return player_; }
    [[nodiscard]] bool acquireEditorOutput() noexcept;
    void releaseEditorOutput(bool resumePrevious = true) noexcept;
    bool editorOutputOwned() const noexcept { return editorOutputOwned_; }
    bool pauseForPlaybackHandoff();

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void commitSelection(qint64 startMs, qint64 endMs);
    Q_INVOKABLE void adjustSelection(qint64 startMs, qint64 endMs);
    Q_INVOKABLE void disableSelectionLoopAndSeek(qint64 positionMs);
    Q_INVOKABLE void clearSelection();
    // Full PCM analysis supplies the exact decoded-frame duration used by both
    // the playback snapshot and the waveform pixel timeline.
    Q_INVOKABLE bool applyWaveformDuration(const QString& trackId,
                                           qint64 durationMs);
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE bool queueNext(const QString& trackId);
    Q_INVOKABLE bool restoreQueue(const QStringList& trackIds,
                                  const QString& currentTrackId);
    Q_INVOKABLE bool playTrackIds(const QStringList& trackIds,
                                  const QString& currentTrackId);
    Q_INVOKABLE void setVolume(float volume);
    Q_INVOKABLE void volumeUp(float step = 0.05F);
    Q_INVOKABLE void volumeDown(float step = 0.05F);
    Q_INVOKABLE void toggleMuted();
    Q_INVOKABLE void setMode(Mode mode);
    Q_INVOKABLE void cycleMode();
    Q_INVOKABLE void loadRow(int row);
    Q_INVOKABLE void playRow(int row);
    Q_INVOKABLE void toggleFavorite();
    Q_INVOKABLE void toggleFavorite(int row);
    Q_INVOKABLE void retryDevice();
    Q_INVOKABLE void refreshOutputDevices();
    Q_INVOKABLE bool setOutputDevice(const QString& deviceId,
                                     bool exclusive);
    Q_INVOKABLE bool setTransitionFadeMs(int milliseconds);
    Q_INVOKABLE bool setMatchTrackSampleRate(bool enabled);
    Q_INVOKABLE bool setReplayGainSettings(int mode, bool clipProtection);
    Q_INVOKABLE void setSpeedRatio(double ratio);
    Q_INVOKABLE void setTargetBpm(double bpm);
    Q_INVOKABLE void resetTempo();
    Q_INVOKABLE void setKeepPitch(bool keepPitch);
    Q_INVOKABLE bool beginScratch();
    Q_INVOKABLE bool updateScratch(double signedRate);
    Q_INVOKABLE bool endScratch();
    Q_INVOKABLE bool cancelScratch();
    Q_INVOKABLE void cuePress();
    Q_INVOKABLE void cueRelease();
    Q_INVOKABLE void cancelCue();
    Q_INVOKABLE void clearCue();
    Q_INVOKABLE void jumpToCue();
    Q_INVOKABLE void activateHotCue(int slot);
    Q_INVOKABLE void clearHotCue(int slot);
    Q_INVOKABLE void setBeatGridFirstBeat();
    Q_INVOKABLE void nudgeBeatGrid(qint64 deltaMs);
    Q_INVOKABLE void setBeatGridBpm(double bpm);
    Q_INVOKABLE void resetBeatGrid();
    Q_INVOKABLE void applyBeatGridWaveform(const QString& trackId, double bpm,
                                         qint64 durationMs, const QVariantList& peaks);
    Q_INVOKABLE void setBeatGridAutoPositionEnabled(bool enabled);

signals:
    // The externally owned core handle must outlive this controller.
    void aboutToBeDestroyed();
    // Synchronous, same-thread arbitration; a receiver may veto a failed handoff.
    void playbackRequested(bool* accepted);
    void stateChanged();
    void positionMsChanged();
    void durationMsChanged();
    void volumeChanged();
    void mutedChanged();
    void modeChanged();
    void trackIndexChanged();
    void trackCountChanged();
    void currentTrackIdChanged();
    void queueTrackIdsChanged();
    void lyricsChanged();
    void errorMessageChanged();
    void deviceLostChanged();
    void outputDevicesChanged();
    void exclusiveModeActiveChanged();
    void spectrumChanged();
    void replayGainClippingWarningChanged();
    void selectionStartMsChanged();
    void selectionEndMsChanged();
    void selectionLoopEnabledChanged();
    void seekCommitted(qint64 positionMs);
    void tempoChanged();
    void scratchStatusChanged();
    void cueChanged();
    void beatGridChanged();
    void hotCuePositionsChanged();

private:
    friend class PlaybackControllerTest;

    enum class EditorOutputStep {
        AcquireStop,
        AcquireStopAfterCall,
        AcquireTimePitch,
        RestoreStop,
        RestoreQueue,
        RestoreMode,
        RestoreTimePitch,
        RestoreSeek,
        RestorePlay,
        RestorePause,
    };

    struct PlaybackSessionSnapshot final {
        QStringList queueTrackIds;
        QString currentTrackId;
        qint64 positionMs{};
        State state{Stopped};
        Mode mode{Sequential};
        qsizetype scopeSize{};
        bool allowFallback{};
        double speedRatio{1.0};
        bool keepPitch{true};
    };

    struct DeckState final {
        qint64 cuePositionMs{-1};
        double beatGridBpmOverride{};
        qint64 beatGridOffsetMs{};
        bool beatGridCalibrated{};
        std::array<qint64, 8> hotCuePositions{
            -1, -1, -1, -1, -1, -1, -1, -1};
    };

    void pollSnapshot();
    void pollSpectrum();
    bool prepareRow(int row);
    bool applyReplayGainForTrack(const QString& trackId);
    bool applyTimePitch(double ratio, bool keepPitch);
    bool restoreEditorSession() noexcept;
    bool shouldFailEditorOutputStep(EditorOutputStep step) noexcept;
    void syncTimePitchFromCore();
    void refreshSourceBpm();
    void refreshAutomaticBeatGrid();
    void preserveAutomaticBeatGrid(DeckState& deck);
    void syncAutomaticCueToBeatGrid(bool reliable);
    void cancelBeatGridAutoPosition();
    void tryAlignBeatGridStart();
    void loadDeckStateStore();
    bool saveDeckStateStore();
    QString deckStateFilePath() const;
    DeckState currentDeckState() const noexcept;
    bool hasPersistentCurrentTrack() const noexcept;
    void invalidateCueHoldForTrackChange();
    void finishCueAudition(bool returnToCue);
    static bool validBeatGridBpm(double bpm) noexcept;
    static bool deckStateIsEmpty(const DeckState& deck) noexcept;
    bool setSelection(qint64 startMs, qint64 endMs, bool loopEnabled);
    void setErrorMessage(QString message);
    void runCommand(int result);

    ag_player* player_ = nullptr;
    QPointer<LibraryModel> library_;
    QMetaObject::Connection playRequestedConnection_;
    QMetaObject::Connection libraryDataChangedConnection_;
    QMetaObject::Connection libraryResetConnection_;
    QTimer pollTimer_;
    State state_ = Stopped;
    qint64 positionMs_ = 0;
    qint64 durationMs_ = 0;
    float volume_ = 1.0F;
    bool muted_ = false;
    Mode mode_ = Sequential;
    qint64 trackIndex_ = -1;
    qint64 trackCount_ = 0;
    QString currentTrackId_;
    QString lyrics_;
    QString errorMessage_;
    QStringList queueTrackIds_;
    QString lastHistoryTrackId_;
    bool deviceLost_ = false;
    QStringList outputDevices_;
    QStringList outputDeviceIds_;
    bool exclusiveModeActive_ = false;
    QVariantList spectrum_;
    int replayGainMode_ = 0;
    bool replayGainClipProtection_ = true;
    bool replayGainClippingWarning_ = false;
    qint64 selectionStartMs_ = 0;
    qint64 selectionEndMs_ = 0;
    bool selectionLoopEnabled_ = false;
    double speedRatio_ = 1.0;
    double sourceBpm_ = 0.0;
    bool keepPitch_ = true;
    bool scratchActive_ = false;
    bool scratchReady_ = false;
    bool scratchBuffering_ = false;
    QHash<QString, DeckState> deckStates_;
    // Only the active track retains its shared waveform envelope. Manual
    // calibration remains in the existing per-track persistent deck store.
    QVariantList beatGridPeaks_;
    qint64 beatGridWaveformDurationMs_ = 0;
    double beatGridWaveformBpm_ = 0;
    BeatGridEstimate automaticBeatGrid_;
    qint64 automaticCuePositionMs_ = -1;
    bool automaticCueSuppressed_ = false;
    bool beatGridAutoPositionEnabled_ = false;
    bool beatGridAutoPositionPending_ = false;
    QString beatGridAutoPositionCancelledTrackId_;
    bool cueHeld_ = false;
    bool cueAuditioning_ = false;
    QString cueAuditionTrackId_;
    qint64 cueAuditionReturnMs_ = -1;
    bool editorOutputOwned_ = false;
    bool editorRestorePending_ = false;
    qsizetype activeScopeSize_{};
    bool activeScopeAllowsFallback_{};
    std::optional<PlaybackSessionSnapshot> editorSessionSnapshot_;
    std::optional<EditorOutputStep> editorOutputFailureStepForTesting_;
    std::optional<EditorOutputStep> editorOutputSecondFailureStepForTesting_;
};

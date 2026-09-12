#pragma once

#include "../../../core/src/audio_editor/audio_document.hpp"
#include "../../../core/src/audio_editor/audio_file_analyzer.hpp"
#include "editor_action_model.hpp"
#include "editor_viewport.hpp"
#include "editor_playback_adapter.hpp"
#include "project_document.hpp"
#include "selection_drag_controller.hpp"
#include "../../../core/src/audio_editor/time_pitch_session.hpp"
#include "../../../core/src/audio_editor/document_writer.hpp"
#include "../../../core/src/audio_editor/noise_reducer.hpp"
#include "../../../core/src/audio_editor/peak_pyramid.hpp"
#include "../bpm_analyzer.hpp"

#include <agplayer/c_api.h>

#include <QObject>
#include <QFutureWatcher>
#include <QList>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

enum class EditorSessionState {
    Empty,
    Ready,
    Playing,
    Previewing,
    Processing,
    Saving,
    Exporting,
    Error
};
Q_DECLARE_METATYPE(EditorSessionState)

class PlaybackController;

class AudioEditorController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(EditorActionModel* actions READ actions CONSTANT)
    Q_PROPERTY(EditorViewport* viewport READ viewport CONSTANT)
    Q_PROPERTY(EditorSessionState state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool hasDocument READ hasDocument NOTIFY documentChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY documentChanged)
    Q_PROPERTY(qint64 totalFrames READ totalFrames NOTIFY documentChanged)
    Q_PROPERTY(qint64 selectionStart READ selectionStart NOTIFY documentChanged)
    Q_PROPERTY(qint64 selectionEnd READ selectionEnd NOTIFY documentChanged)
    Q_PROPERTY(qint64 selectionFrames READ selectionFrames NOTIFY documentChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY documentChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY documentChanged)
    Q_PROPERTY(QString formatName READ formatName NOTIFY documentChanged)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY documentChanged)
    Q_PROPERTY(int channels READ channels NOTIFY documentChanged)
    Q_PROPERTY(int bitsPerSample READ bitsPerSample NOTIFY documentChanged)
    Q_PROPERTY(qint64 bitRate READ bitRate NOTIFY documentChanged)
    Q_PROPERTY(QVariantList channelPeaks READ channelPeaks NOTIFY waveformChanged)
    Q_PROPERTY(QVariantList viewportChannelPeaks READ viewportChannelPeaks
                   NOTIFY waveformChanged)
    Q_PROPERTY(QVariantList timelineEventViews READ timelineEventViews
                   NOTIFY documentChanged)
    Q_PROPERTY(QString selectedEventId READ selectedEventId
                   NOTIFY selectedEventChanged)
    Q_PROPERTY(QString activeTool READ activeTool NOTIFY toolChanged)
    Q_PROPERTY(bool formantPreservationSupported
                   READ formantPreservationSupported NOTIFY documentChanged)
    Q_PROPERTY(bool formantPreservation READ formantPreservation
                   WRITE setFormantPreservation NOTIFY timePitchChanged)
    Q_PROPERTY(bool bpmDetectionSupported READ bpmDetectionSupported
                   NOTIFY documentChanged)
    Q_PROPERTY(bool timePitchSupported READ timePitchSupported
                   NOTIFY documentChanged)
    Q_PROPERTY(bool playbackSupported READ playbackSupported
                   NOTIFY documentChanged)
    Q_PROPERTY(bool editorPlaybackOwnsPlayer READ editorPlaybackOwnsPlayer
                   NOTIFY playbackOwnershipChanged)
    Q_PROPERTY(bool exportSupported READ exportSupported NOTIFY documentChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY playbackChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY documentChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY playbackChanged)
    Q_PROPERTY(bool trackMuted READ trackMuted WRITE setTrackMuted
                   NOTIFY trackMixChanged)
    Q_PROPERTY(bool trackSolo READ trackSolo WRITE setTrackSolo
                   NOTIFY trackMixChanged)
    Q_PROPERTY(double trackGainDb READ trackGainDb WRITE setTrackGainDb
                   NOTIFY trackMixChanged)
    Q_PROPERTY(bool loopEnabled READ loopEnabled WRITE setLoopEnabled
                   NOTIFY playbackChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString lastExportPath READ lastExportPath
                   NOTIFY exportResultChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectChanged)
    Q_PROPERTY(QVariantList projectIssues READ projectIssues NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap projectExportSettings READ projectExportSettingsMap
                   WRITE setProjectExportSettingsMap NOTIFY projectChanged)
    Q_PROPERTY(qint64 playheadFrame READ playheadFrame NOTIFY playbackChanged)
    Q_PROPERTY(double originalBpm READ originalBpm NOTIFY timePitchChanged)
    Q_PROPERTY(double targetBpm READ targetBpm NOTIFY timePitchChanged)
    Q_PROPERTY(bool bpmBusy READ bpmBusy NOTIFY bpmChanged)
    Q_PROPERTY(double bpmResult READ bpmResult NOTIFY bpmChanged)
    Q_PROPERTY(QString bpmError READ bpmError NOTIFY bpmChanged)
    Q_PROPERTY(double speedPercent READ speedPercent NOTIFY timePitchChanged)
    Q_PROPERTY(bool keepPitch READ keepPitch WRITE setKeepPitch NOTIFY timePitchChanged)
    Q_PROPERTY(int pitchCents READ pitchCents NOTIFY timePitchChanged)
    Q_PROPERTY(bool timePitchPreviewActive READ timePitchPreviewActive
                   NOTIFY timePitchChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool noiseReductionActive READ noiseReductionActive
                   NOTIFY stateChanged)

public:
    explicit AudioEditorController(QObject* parent = nullptr);
    explicit AudioEditorController(ag_audio_backend backend,
                                   QObject* parent = nullptr);
    ~AudioEditorController() override;
    // Passing nullptr detaches a borrowed player while it is still alive.
    // A standalone player owned by this editor is unaffected.
    void setPlaybackController(PlaybackController* controller);
    bool pauseForPlaybackHandoff();
    [[nodiscard]] ag_player* playerHandleForTesting() const noexcept
    { return player_; }

    [[nodiscard]] EditorActionModel* actions() noexcept { return &actions_; }
    [[nodiscard]] EditorViewport* viewport() noexcept { return &viewport_; }
    [[nodiscard]] EditorSessionState state() const noexcept { return state_; }
    [[nodiscard]] bool hasDocument() const noexcept { return has_document_; }
    [[nodiscard]] bool modified() const noexcept { return modified_; }
    [[nodiscard]] qint64 totalFrames() const noexcept
    { return document_.totalFrames(); }
    [[nodiscard]] qint64 selectionStart() const noexcept;
    [[nodiscard]] qint64 selectionEnd() const noexcept;
    [[nodiscard]] qint64 selectionFrames() const noexcept;
    [[nodiscard]] QString filePath() const { return source_path_; }
    [[nodiscard]] QString fileName() const;
    [[nodiscard]] QString formatName() const { return format_name_; }
    [[nodiscard]] int sampleRate() const noexcept
    { return sample_rate_; }
    [[nodiscard]] int channels() const noexcept
    { return channels_; }
    [[nodiscard]] int bitsPerSample() const noexcept { return bits_per_sample_; }
    [[nodiscard]] qint64 bitRate() const noexcept { return bit_rate_; }
    [[nodiscard]] QVariantList channelPeaks() const;
    [[nodiscard]] QVariantList viewportChannelPeaks() const
    { return viewport_channel_peaks_; }
    [[nodiscard]] QVariantList timelineEventViews() const;
    [[nodiscard]] QString selectedEventId() const { return selected_event_id_; }
    [[nodiscard]] QString activeTool() const { return active_tool_; }
    [[nodiscard]] bool formantPreservationSupported() const noexcept
    { return (!has_document_ || document_.totalFrames() > 0)
        && channels_ >= 0 && channels_ <= 2; }
    [[nodiscard]] bool formantPreservation() const noexcept
    { return time_pitch_.formantPreservation(); }
    [[nodiscard]] bool bpmDetectionSupported() const noexcept
    { return (!has_document_ || document_.totalFrames() > 0)
        && channels_ >= 0 && channels_ <= 2; }
    [[nodiscard]] bool timePitchSupported() const noexcept
    { return (!has_document_ || document_.totalFrames() > 0)
        && channels_ >= 0 && channels_ <= 2; }
    [[nodiscard]] bool playbackSupported() const noexcept
    { return player_ != nullptr
        && (!has_document_ || document_.totalFrames() > 0)
        && channels_ >= 0 && channels_ <= 2; }
    [[nodiscard]] bool editorPlaybackOwnsPlayer() const noexcept
    { return editor_playback_owns_player_; }
    [[nodiscard]] bool exportSupported() const noexcept
    { return has_document_ && document_.totalFrames() > 0; }
    [[nodiscard]] bool playing() const noexcept { return playing_; }
    [[nodiscard]] qint64 positionMs() const noexcept { return position_ms_; }
    [[nodiscard]] qint64 durationMs() const noexcept;
    [[nodiscard]] double volume() const noexcept { return volume_; }
    [[nodiscard]] bool trackMuted() const noexcept { return track_muted_; }
    [[nodiscard]] bool trackSolo() const noexcept { return track_solo_; }
    [[nodiscard]] double trackGainDb() const noexcept { return track_gain_db_; }
    [[nodiscard]] bool loopEnabled() const noexcept { return loop_enabled_; }
    [[nodiscard]] QString errorMessage() const { return error_message_; }
    [[nodiscard]] double progress() const noexcept { return progress_; }
    [[nodiscard]] QString lastExportPath() const { return last_export_path_; }
    [[nodiscard]] QString projectPath() const { return project_path_; }
    [[nodiscard]] QVariantList projectIssues() const { return project_issues_; }
    [[nodiscard]] const agplayer::editor::ProjectExportSettings&
    projectExportSettings() const noexcept { return project_export_settings_; }
    [[nodiscard]] QVariantMap projectExportSettingsMap() const;
    [[nodiscard]] qint64 playheadFrame() const noexcept { return playhead_frame_; }
    [[nodiscard]] double originalBpm() const noexcept { return time_pitch_.originalBpm(); }
    [[nodiscard]] double targetBpm() const noexcept { return time_pitch_.targetBpm(); }
    [[nodiscard]] bool bpmBusy() const noexcept { return bpm_busy_; }
    [[nodiscard]] double bpmResult() const noexcept { return bpm_result_; }
    [[nodiscard]] QString bpmError() const { return bpm_error_; }
    [[nodiscard]] double speedPercent() const noexcept { return time_pitch_.speedPercent(); }
    [[nodiscard]] bool keepPitch() const noexcept { return time_pitch_.keepPitch(); }
    [[nodiscard]] int pitchCents() const noexcept { return time_pitch_.pitchCents(); }
    [[nodiscard]] bool timePitchPreviewActive() const noexcept
    { return time_pitch_preview_active_; }
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool loading() const noexcept { return document_loading_; }
    [[nodiscard]] bool noiseReductionActive() const noexcept
    { return noise_reduction_watcher_ != nullptr; }
    [[nodiscard]] quint64 viewportWaveformGeneration() const noexcept
    { return viewport_waveform_generation_; }
    [[nodiscard]] bool sourcePeakCacheActiveForTesting() const noexcept
    {
        return source_peak_cache_cancel_token_
            && !source_peak_cache_cancel_token_->load(
                std::memory_order_acquire);
    }
    [[nodiscard]] quint64 sourcePeakCacheGenerationForTesting() const noexcept
    { return source_peak_cache_generation_; }
    [[nodiscard]] quint64 documentLoadGenerationForTesting() const noexcept
    { return document_load_generation_; }
    [[nodiscard]] quint64 timelineRevisionForTesting() const noexcept
    { return document_.timelineSnapshot().revision; }
    [[nodiscard]] std::uint64_t historyStateIdForTesting() const noexcept
    { return document_.historyStateId(); }
    [[nodiscard]] bool handoffServicesCreatedForTesting() const noexcept
    { return handoff_assets_ && selection_drag_controller_; }
    void setViewportWaveformTaskObserverForTesting(
        std::function<void(bool)> observer)
    { viewport_waveform_task_observer_ = std::move(observer); }
    void clearViewportSourcePeaksForTesting();
    void setSourcePeakCacheTaskObserverForTesting(
        std::function<void(bool)> observer)
    { source_peak_cache_task_observer_ = std::move(observer); }
    void setDocumentLoadTaskObserverForTesting(
        std::function<void(bool)> observer)
    { document_load_task_observer_ = std::move(observer); }
    void setBpmTaskObserverForTesting(std::function<void(bool)> observer)
    { bpm_task_observer_ = std::move(observer); }
    void cancelAndWaitForBpmTaskForTesting();
    [[nodiscard]] EditorAction* action(const QString& id) noexcept
    {
        return actions_.action(id);
    }
    Q_INVOKABLE bool createUntitledDocument(
        quint32 sampleRate, quint32 channels, qint64 frames);
    Q_INVOKABLE bool clearDocument();
    Q_INVOKABLE bool openFile(const QUrl& source);
    Q_INVOKABLE bool openDroppedUrls(const QList<QUrl>& urls);
    bool openFileWhenReady(const QUrl& source, QObject* context,
                           std::function<void()> onLoaded);
    Q_INVOKABLE bool confirmDiscardAndOpen();
    Q_INVOKABLE void cancelDiscardAndOpen();
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl& target);
    Q_INVOKABLE bool saveProject(const QUrl& target);
    Q_INVOKABLE bool saveProjectAs(const QUrl& target);
    Q_INVOKABLE bool openProject(const QUrl& source);
    Q_INVOKABLE bool relinkProjectSource(const QString& sourceId,
                                         const QUrl& replacement);
    bool relinkProjectSource(quint64 sourceId, const QUrl& replacement);
    bool setProjectExportSettings(
        const agplayer::editor::ProjectExportSettings& settings);
    Q_INVOKABLE void setProjectExportSettingsMap(const QVariantMap& settings);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE bool exportTo(const QUrl& target, bool selectionOnly = false,
                              const QString& codecName = QString(),
                              int sampleRate = 0, int channels = 0,
                              qint64 bitRate = 0, bool keepMetadata = true,
                              bool variableBitRate = true, int quality = 80);
    Q_INVOKABLE bool exportToConfiguredDirectory(bool selectionOnly = false);
    Q_INVOKABLE bool setSelection(qint64 startFrame, qint64 endFrame);
    Q_INVOKABLE bool clearSelection();
    Q_INVOKABLE void selectEvent(const QString& id);
    Q_INVOKABLE void clearEventSelection();
    Q_INVOKABLE bool clearTimeline();
    Q_INVOKABLE void setViewportWaveformDevicePixelRatio(
        double devicePixelRatio);
    struct ViewportWaveformResult final {
        std::vector<std::vector<float>> peaks;
        bool hasVisibleEvent{};
        bool hasUnavailableVisibleEvent{};
    };
    Q_INVOKABLE bool beginSelectionHandoff(double sceneX, double sceneY);
    Q_INVOKABLE void updateSelectionHandoff(double sceneX, double sceneY);
    Q_INVOKABLE void releaseSelectionHandoff();
    Q_INVOKABLE void cancelSelectionHandoff();
    bool moveEvent(quint64 id, qint64 timelineStart);
    bool trimEvent(quint64 id, qint64 sourceStart,
                   qint64 sourceEnd, qint64 timelineStart);
    bool splitEvent(quint64 id, qint64 frame);
    bool mergeEvents(quint64 left, quint64 right);
    Q_INVOKABLE bool moveEvent(const QString& id, qint64 timelineStart);
    Q_INVOKABLE bool trimEvent(const QString& id, qint64 sourceStart,
                               qint64 sourceEnd, qint64 timelineStart);
    Q_INVOKABLE bool trimSharedBoundary(const QString& leftId,
                                        const QString& rightId,
                                        qint64 sourceBoundary);
    Q_INVOKABLE bool splitEvent(const QString& id, qint64 frame);
    Q_INVOKABLE bool setEventFadeIn(const QString& id, qint64 frames);
    Q_INVOKABLE bool setEventFadeOut(const QString& id, qint64 frames);
    Q_INVOKABLE bool setEventFadeCurve(const QString& id, bool fadeIn,
                                       const QString& curveName);
    Q_INVOKABLE bool setEventGain(const QString& id, double gain);
    Q_INVOKABLE bool addEnvelopePoint(const QString& id, qint64 offset,
                                      double gain);
    Q_INVOKABLE bool moveEnvelopePoint(const QString& id,
                                       qint64 originalOffset,
                                       qint64 offset, double gain);
    Q_INVOKABLE bool removeEnvelopePoint(const QString& id, qint64 offset);
    Q_INVOKABLE bool beginEventGainGesture(const QString& id);
    Q_INVOKABLE bool updateEventGainGesture(double gain);
    Q_INVOKABLE bool endEventGainGesture();
    Q_INVOKABLE bool cancelEventGainGesture();
    Q_INVOKABLE bool beginEnvelopePointGesture(const QString& id,
                                               qint64 offset);
    Q_INVOKABLE bool updateEnvelopePointGesture(qint64 offset, double gain);
    Q_INVOKABLE bool commitEnvelopePointGesture(qint64 offset, double gain);
    Q_INVOKABLE bool endEnvelopePointGesture();
    Q_INVOKABLE bool cancelEnvelopePointGesture();
    Q_INVOKABLE bool beginEventGesture(const QString& id,
                                       const QString& operation,
                                       bool duplicate = false);
    Q_INVOKABLE bool beginSharedBoundaryGesture(const QString& leftId,
                                                const QString& rightId);
    Q_INVOKABLE bool endEventGesture();
    Q_INVOKABLE bool cancelEventGesture();
    Q_INVOKABLE bool setActiveTool(const QString& tool);
    Q_INVOKABLE bool clearTransientState();
    Q_INVOKABLE bool actionEnabled(const QString& id) const noexcept;
    Q_INVOKABLE bool triggerAction(const QString& id);
    Q_INVOKABLE bool playPause();
    Q_INVOKABLE bool stopPlayback();
    Q_INVOKABLE bool seekMs(qint64 value);
    Q_INVOKABLE bool seekPlayback(qint64 value) { return seekMs(value); }
    Q_INVOKABLE bool seekFrame(qint64 frame);
    Q_INVOKABLE void setVolume(double value);
    Q_INVOKABLE void setTrackMuted(bool value);
    Q_INVOKABLE void setTrackSolo(bool value);
    Q_INVOKABLE void setTrackGainDb(double value);
    Q_INVOKABLE void setLoopEnabled(bool enabled);
    Q_INVOKABLE bool detectBpm();
    Q_INVOKABLE void setOriginalBpm(double value);
    Q_INVOKABLE bool setTargetBpm(double value);
    Q_INVOKABLE bool setSpeedPercent(double value);
    Q_INVOKABLE bool resetTimePitch();
    Q_INVOKABLE void setKeepPitch(bool value);
    Q_INVOKABLE void setFormantPreservation(bool value);
    Q_INVOKABLE bool setPitch(int semitones, int cents);
    Q_INVOKABLE void cancelOperation();
    Q_INVOKABLE void deactivate();
    Q_INVOKABLE void activate();
    Q_INVOKABLE bool reduceNoise();

signals:
    void playbackRequested(bool* accepted);
    void stateChanged();
    void documentChanged();
    void waveformChanged();
    void activated();
    void deactivated();
    void toolChanged();
    void playbackChanged();
    void playbackOwnershipChanged();
    void trackMixChanged();
    void errorMessageChanged();
    void progressChanged();
    void projectChanged();
    void timePitchChanged();
    void bpmChanged();
    void openRequested();
    void saveAsRequested();
    void saveProjectAsRequested();
    void exportRequested();
    void exportDirectoryRequested();
    void exportSucceeded(const QString& path);
    void exportResultChanged();
    void discardConfirmationRequested();
    void loadingChanged();
    void selectedEventChanged();

private:
    explicit AudioEditorController(std::optional<ag_audio_backend> backend,
                                   QObject* parent);
    struct NoiseReductionFinalizeResult;
    using SourcePeakPyramids = std::unordered_map<std::string,
        std::shared_ptr<const agplayer::editor::PeakPyramid>>;
    struct ViewportWaveformJob final {
        quint64 generation{};
        std::shared_ptr<std::atomic_bool> cancelToken;
        std::function<ViewportWaveformResult()> work;
    };
    struct SourcePeakCacheResult final {
        SourcePeakPyramids pyramids;
    };
    struct SourcePeakCacheJob final {
        quint64 generation{};
        std::shared_ptr<std::atomic_bool> cancelToken;
        std::vector<agplayer::editor::AudioSource> sources;
    };
    enum class DocumentLoadKind { OpenFile, OpenProject, Relink };
    struct DocumentLoadJob final {
        quint64 generation{};
        std::shared_ptr<std::atomic_bool> cancelToken;
        DocumentLoadKind kind{DocumentLoadKind::OpenFile};
        QString path;
        quint64 sourceId{};
        agplayer::editor::TimelineSnapshot relinkSnapshot;
        std::vector<agplayer::editor::Marker> relinkMarkers;
        std::optional<agplayer::editor::Selection> relinkSelection;
        std::vector<agplayer::editor::ProjectSourceRecord> relinkSources;
    };
    struct DocumentLoadOutcome final {
        DocumentLoadKind kind{DocumentLoadKind::OpenFile};
        QString path;
        quint64 sourceId{};
        bool success{};
        QString message;
        agplayer::editor::AudioFileAnalysis analysis;
        std::shared_ptr<agplayer::editor::ProjectLoadResult> project;
        std::shared_ptr<agplayer::editor::AudioDocument> relinkDocument;
        std::vector<agplayer::editor::ProjectSourceRecord> relinkSources;
    };
    struct BpmJob final {
        quint64 generation{};
        std::shared_ptr<std::atomic_bool> cancelToken;
        agplayer::editor::TimelineSnapshot snapshot;
    };
    void refreshActions();
    void requestViewportWaveform();
    void startViewportWaveformJob(ViewportWaveformJob job);
    void clearViewportWaveformState(bool clearPublished = true);
    void refreshSourcePeakCachesAsync();
    void startSourcePeakCacheJob(SourcePeakCacheJob job);
    void cancelSourcePeakCacheJob();
    bool enqueueDocumentLoad(DocumentLoadJob job);
    void startDocumentLoadJob(DocumentLoadJob job);
    void cancelDocumentLoad();
    void applyDocumentLoadOutcome(DocumentLoadOutcome outcome);
    void startBpmJob(BpmJob job);
    void cancelBpmDetection(bool publishCancelled);
    void adoptBpm(double bpm);
    void setDocumentLoading(bool loading);
    [[nodiscard]] double effectivePlaybackVolume() const noexcept;
    void updatePlaybackMix() noexcept;
    void applyTrackMix(agplayer::editor::TimelineSnapshot& snapshot) const noexcept;
    bool preparePlayback();
    void releaseEditorPlaybackOutput() noexcept;
    [[nodiscard]] qint64 currentPlaybackTimelineFrame() const noexcept;
    void finishTimePitchChange(bool wasPlaying, qint64 timelineFrame);
    void ensureSelectionHandoffServices();
    void pollPlayback();
    void setState(EditorSessionState value);
    void setError(QString message);
    void setProgress(double value);
    void markProjectClean() noexcept;
    void markProjectDirty() noexcept;
    void markEditorSettingsDirty();
    [[nodiscard]] bool updatePersistedPlayhead(qint64 frame,
                                                qint64 positionMs) noexcept;
    [[nodiscard]] bool syncModifiedFromHistory() noexcept;
    void finishTimelineMutation();
    void clearMissingEventSelection();
    void syncProjectSourcesAndIssues();
    [[nodiscard]] std::optional<quint64> nextProjectSourceId() const;
    [[nodiscard]] static std::optional<agplayer::editor::EventId>
    parseEventId(const QString& id) noexcept;
    [[nodiscard]] agplayer::editor::TimelineSnapshot
    timelineSnapshotForView() const;
    void setViewportDocumentFrames(qint64 frames) noexcept;
    [[nodiscard]] bool projectSourcesOnline() const noexcept;
    bool requireOnlineProjectSources();
    void syncPrimarySourceSummary();
    [[nodiscard]] QString generatedMediaDirectory() const;
    [[nodiscard]] QString uniqueGeneratedMediaPath(
        const QString& prefix) const;
    bool exportWithSettings(const QUrl& target, bool selectionOnly,
                            const QString& codecName, int sampleRate,
                            int channels, qint64 bitRate, bool keepMetadata,
                            bool variableBitRate, int quality,
                            bool usePersistedDefaults);

    EditorActionModel actions_;
    EditorViewport viewport_;
    agplayer::editor::AudioDocument document_;
    ag_player* player_{};
    PlaybackController* playback_controller_{};
    bool editor_playback_owns_player_{};
    bool owns_player_{};
    std::unique_ptr<EditorPlaybackAdapter> playback_adapter_;
    std::unique_ptr<HandoffAssetManager> handoff_assets_;
    std::unique_ptr<SelectionDragController> selection_drag_controller_;
    bool playback_prepared_{};
    QTimer playback_timer_;
    QString source_path_;
    QString project_path_;
    QUrl pending_open_url_;
    bool pending_open_is_project_{};
    bool pending_clear_document_{};
    QString format_name_;
    int sample_rate_{};
    int channels_{};
    int bits_per_sample_{};
    qint64 bit_rate_{};
    QVariantList channel_peaks_;
    std::shared_ptr<const agplayer::editor::PeakPyramid> primary_peak_pyramid_;
    SourcePeakPyramids source_peak_pyramids_;
    QVariantList viewport_channel_peaks_;
    QString selected_event_id_;
    QFutureWatcherBase* viewport_waveform_watcher_ = nullptr;
    quint64 viewport_waveform_generation_ = 0;
    std::shared_ptr<std::atomic_bool> viewport_waveform_cancel_token_;
    std::optional<ViewportWaveformJob> pending_viewport_waveform_job_;
    std::function<void(bool)> viewport_waveform_task_observer_;
    QFutureWatcher<SourcePeakCacheResult>* source_peak_cache_watcher_{};
    quint64 source_peak_cache_generation_{};
    std::shared_ptr<std::atomic_bool> source_peak_cache_cancel_token_;
    std::optional<SourcePeakCacheJob> pending_source_peak_cache_job_;
    std::function<void(bool)> source_peak_cache_task_observer_;
    QFutureWatcher<DocumentLoadOutcome>* document_load_watcher_{};
    quint64 document_load_generation_{};
    std::shared_ptr<std::atomic_bool> document_load_cancel_token_;
    std::optional<DocumentLoadJob> pending_document_load_job_;
    std::function<void(bool)> document_load_task_observer_;
    bool document_loading_{};

    EditorSessionState state_{EditorSessionState::Empty};
    bool has_document_{};
    bool modified_{};
    bool playing_{};
    bool loop_enabled_{};
    qint64 position_ms_{};
    qint64 playhead_frame_{};
    double volume_{1.0};
    bool track_muted_{};
    bool track_solo_{};
    double track_gain_db_{};
    QString error_message_;
    double progress_{};
    QString last_export_path_;
    agplayer::editor::ProjectExportSettings project_export_settings_;
    std::vector<agplayer::editor::ProjectSourceRecord> project_sources_;
    QVariantList project_issues_;
    QVariantList known_project_issues_;
    agplayer::editor::TimePitchSession time_pitch_;
    QFutureWatcher<agplayer::editor::WriteResult>* write_watcher_{};
    QFutureWatcher<agplayer::editor::TimePitchResult>* time_pitch_watcher_{};
    QFutureWatcher<BpmAnalyzeResult>* bpm_watcher_{};
    quint64 bpm_generation_{};
    std::shared_ptr<std::atomic_bool> bpm_cancel_token_;
    std::optional<BpmJob> pending_bpm_job_;
    std::function<void(bool)> bpm_task_observer_;
    bool bpm_busy_{};
    double bpm_result_{};
    QString bpm_error_;
    QFutureWatcher<NoiseReductionFinalizeResult>*
        noise_reduction_watcher_{};
    std::atomic_bool operation_cancelled_{false};
    bool time_pitch_preview_active_{};
    bool allow_document_replace_{};
    bool suppress_persisted_state_tracking_{};
    bool viewport_persisted_dirty_{};
    bool playhead_persisted_dirty_{};
    bool forced_project_dirty_{};
    std::optional<std::uint64_t> saved_history_state_;
    std::optional<agplayer::editor::Selection> saved_selection_;
    qint64 saved_playhead_frame_{};
    qint64 saved_visible_start_frame_{};
    qint64 saved_visible_end_frame_{};
    qreal viewport_waveform_device_pixel_ratio_{1.0};
    agplayer::editor::ProjectExportSettings saved_export_settings_;

    enum class EventGestureKind {
        None,
        Move,
        Trim,
        SharedBoundary,
        FadeOut,
        Gain,
        EnvelopePoint
    };
    struct EventGesture final {
        EventGestureKind kind{EventGestureKind::None};
        agplayer::editor::EventId id{};
        agplayer::editor::EventId secondaryId{};
        bool duplicate{};
        bool pending{};
        qint64 timelineStart{};
        qint64 sourceStart{};
        qint64 sourceEnd{};
        qint64 fadeOut{};
        double originalGain{1.0};
        double gain{1.0};
        qint64 originalEnvelopeOffset{};
        qint64 envelopeOffset{};
        double originalEnvelopeGain{1.0};
        double envelopeGain{1.0};
    };
    EventGesture event_gesture_;
    QString active_tool_{QStringLiteral("select")};
};

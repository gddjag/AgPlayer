#pragma once

#include "../../../core/src/audio_editor/audio_document.hpp"
#include "editor_action_model.hpp"
#include "editor_viewport.hpp"
#include "../../../core/src/audio_editor/time_pitch_session.hpp"
#include "../../../core/src/audio_editor/recording_session.hpp"
#include "../../../core/src/audio_editor/document_writer.hpp"
#include "../../../core/src/audio_editor/noise_reducer.hpp"
#include "../bpm_analyzer.hpp"

#include <agplayer/c_api.h>

#include <QObject>
#include <QFutureWatcher>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

namespace agplayer {
namespace editor {
class AudioDocument;
struct DocumentSnapshot;
struct AudioSpan;
} // namespace editor
}

class PlaybackController;

enum class EditorSessionState {
    Empty,
    Ready,
    Playing,
    Recording,
    RecordingPaused,
    Finalizing,
    Previewing,
    Processing,
    Saving,
    Exporting,
    Error
};
Q_DECLARE_METATYPE(EditorSessionState)

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
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY playbackChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY documentChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY playbackChanged)
    Q_PROPERTY(bool loopEnabled READ loopEnabled WRITE setLoopEnabled
                   NOTIFY playbackChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(double originalBpm READ originalBpm NOTIFY timePitchChanged)
    Q_PROPERTY(double targetBpm READ targetBpm NOTIFY timePitchChanged)
    Q_PROPERTY(double speedPercent READ speedPercent NOTIFY timePitchChanged)
    Q_PROPERTY(bool keepPitch READ keepPitch WRITE setKeepPitch NOTIFY timePitchChanged)
    Q_PROPERTY(int pitchCents READ pitchCents NOTIFY timePitchChanged)
    Q_PROPERTY(bool timePitchPreviewActive READ timePitchPreviewActive
                   NOTIFY timePitchChanged)
    Q_PROPERTY(QVariantList recordingDevices READ recordingDevices NOTIFY recordingDevicesChanged)
    Q_PROPERTY(QVariantList exportFormats READ exportFormats CONSTANT)
    Q_PROPERTY(QString recordingDeviceId READ recordingDeviceId
                   NOTIFY recordingPreferencesChanged)
    Q_PROPERTY(int recordingSampleRate READ recordingSampleRate
                   NOTIFY recordingPreferencesChanged)
    Q_PROPERTY(int recordingChannels READ recordingChannels
                   NOTIFY recordingPreferencesChanged)
    Q_PROPERTY(bool recordingMonitor READ recordingMonitor
                   NOTIFY recordingPreferencesChanged)
    Q_PROPERTY(QString recordingDirectory READ recordingDirectory
                   NOTIFY recordingPreferencesChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool recordingPaused READ recordingPaused NOTIFY recordingChanged)
    Q_PROPERTY(double inputLevel READ inputLevel NOTIFY recordingChanged)
    Q_PROPERTY(qint64 recordingFrames READ recordingFrames NOTIFY recordingChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool noiseReductionActive READ noiseReductionActive NOTIFY stateChanged)

public:
    explicit AudioEditorController(
        ag_audio_backend backend = AG_AUDIO_BACKEND_DEFAULT,
        QObject* parent = nullptr);
    ~AudioEditorController() override;

    [[nodiscard]] EditorActionModel* actions() noexcept { return &actions_; }
    [[nodiscard]] EditorViewport* viewport() noexcept { return &viewport_; }
    [[nodiscard]] EditorSessionState state() const noexcept { return state_; }
    [[nodiscard]] bool hasDocument() const noexcept { return has_document_; }
    [[nodiscard]] bool modified() const noexcept { return modified_; }
    [[nodiscard]] qint64 totalFrames() const noexcept
    { return recording() ? recordingFrames() : document_.totalFrames(); }
    [[nodiscard]] qint64 selectionStart() const noexcept;
    [[nodiscard]] qint64 selectionEnd() const noexcept;
    [[nodiscard]] qint64 selectionFrames() const noexcept;
    [[nodiscard]] QString filePath() const { return source_path_; }
    [[nodiscard]] QString fileName() const;
    [[nodiscard]] QString formatName() const { return format_name_; }
    [[nodiscard]] int sampleRate() const noexcept
    { return recording() ? recording_sample_rate_ : sample_rate_; }
    [[nodiscard]] int channels() const noexcept
    { return recording() ? recording_channels_ : channels_; }
    [[nodiscard]] int bitsPerSample() const noexcept { return bits_per_sample_; }
    [[nodiscard]] qint64 bitRate() const noexcept { return bit_rate_; }
    [[nodiscard]] QVariantList channelPeaks() const;
    [[nodiscard]] QVariantList viewportChannelPeaks() const
    { return viewport_channel_peaks_; }
    [[nodiscard]] bool playing() const noexcept { return playing_; }
    [[nodiscard]] qint64 positionMs() const noexcept { return position_ms_; }
    [[nodiscard]] qint64 durationMs() const noexcept;
    [[nodiscard]] double volume() const noexcept { return volume_; }
    [[nodiscard]] bool loopEnabled() const noexcept { return loop_enabled_; }
    [[nodiscard]] QString errorMessage() const { return error_message_; }
    [[nodiscard]] double progress() const noexcept { return progress_; }
    [[nodiscard]] double originalBpm() const noexcept { return time_pitch_.originalBpm(); }
    [[nodiscard]] double targetBpm() const noexcept { return time_pitch_.targetBpm(); }
    [[nodiscard]] double speedPercent() const noexcept { return time_pitch_.speedPercent(); }
    [[nodiscard]] bool keepPitch() const noexcept { return time_pitch_.keepPitch(); }
    [[nodiscard]] int pitchCents() const noexcept { return time_pitch_.pitchCents(); }
    [[nodiscard]] bool timePitchPreviewActive() const noexcept
    { return time_pitch_preview_active_; }
    [[nodiscard]] QVariantList recordingDevices() const { return recording_devices_; }
    [[nodiscard]] QVariantList exportFormats() const { return export_formats_; }
    [[nodiscard]] QString recordingDeviceId() const { return recording_device_id_; }
    [[nodiscard]] int recordingSampleRate() const noexcept { return recording_sample_rate_; }
    [[nodiscard]] int recordingChannels() const noexcept { return recording_channels_; }
    [[nodiscard]] bool recordingMonitor() const noexcept { return recording_monitor_; }
    [[nodiscard]] QString recordingDirectory() const { return recording_directory_; }
    [[nodiscard]] bool recording() const noexcept;
    [[nodiscard]] bool recordingPaused() const noexcept;
    [[nodiscard]] double inputLevel() const noexcept;
    [[nodiscard]] qint64 recordingFrames() const noexcept;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool noiseReductionActive() const noexcept
    { return noise_reduction_watcher_ != nullptr; }
    [[nodiscard]] EditorAction* action(const QString& id) noexcept
    {
        return actions_.action(id);
    }
    void setMainPlaybackController(PlaybackController* playback) noexcept;

    Q_INVOKABLE bool createUntitledDocument(
        quint32 sampleRate, quint32 channels, qint64 frames);
    Q_INVOKABLE bool createRecordingDocument(quint32 sampleRate, quint32 channels);
    Q_INVOKABLE bool clearDocument();
    Q_INVOKABLE bool openFile(const QUrl& source);
    Q_INVOKABLE bool confirmDiscardAndOpen();
    Q_INVOKABLE void cancelDiscardAndOpen();
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl& target);
    Q_INVOKABLE bool exportTo(const QUrl& target, bool selectionOnly = false,
                              const QString& codecName = QString(),
                              int sampleRate = 0, int channels = 0,
                              qint64 bitRate = 0, bool keepMetadata = true,
                              bool variableBitRate = true, int quality = 80);
    Q_INVOKABLE bool setSelection(qint64 startFrame, qint64 endFrame);
    Q_INVOKABLE bool clearSelection();
    Q_INVOKABLE bool insertSilence(qint64 frame, qint64 frameCount);
    Q_INVOKABLE bool applyGain(double decibels);
    Q_INVOKABLE bool reduceNoise();
    Q_INVOKABLE bool actionEnabled(const QString& id) const noexcept;
    Q_INVOKABLE bool triggerAction(const QString& id);
    Q_INVOKABLE bool playPause();
    Q_INVOKABLE bool stopPlayback();
    Q_INVOKABLE bool seekMs(qint64 value);
    Q_INVOKABLE void setVolume(double value);
    Q_INVOKABLE void setLoopEnabled(bool enabled);
    Q_INVOKABLE bool detectBpm();
    Q_INVOKABLE void setOriginalBpm(double value);
    Q_INVOKABLE bool setTargetBpm(double value);
    Q_INVOKABLE bool setSpeedPercent(double value);
    Q_INVOKABLE void setKeepPitch(bool value);
    Q_INVOKABLE bool setPitch(int semitones, int cents);
    Q_INVOKABLE bool applyTimePitch();
    Q_INVOKABLE void refreshRecordingDevices();
    Q_INVOKABLE bool startRecording(const QUrl& target,
                                    const QString& deviceId,
                                    int recordingSampleRate,
                                    int recordingChannels,
                                    bool monitor,
                                    bool insertAtCursor);
    Q_INVOKABLE bool startRecordingToTemporaryFile(
        const QString& deviceId, int recordingSampleRate,
        int recordingChannels, bool monitor, bool insertAtCursor);
    Q_INVOKABLE bool pauseRecording();
    Q_INVOKABLE bool resumeRecording();
    Q_INVOKABLE bool stopRecording();
    Q_INVOKABLE bool cancelRecording();
    Q_INVOKABLE void cancelOperation();

signals:
    void stateChanged();
    void documentChanged();
    void waveformChanged();
    void playbackChanged();
    void errorMessageChanged();
    void progressChanged();
    void timePitchChanged();
    void recordingDevicesChanged();
    void recordingPreferencesChanged();
    void recordingChanged();
    void openRequested();
    void saveAsRequested();
    void exportRequested();
    void newRecordingRequested();
    void discardConfirmationRequested();

private:
    struct RecordingFinalizeResult;
    struct PreviewRenderResult;
    void refreshActions();
    [[nodiscard]] QVariantList buildExportFormats() const;
    void rebuildEditorPeaks();
    void requestViewportWaveform();
    void clearViewportWaveformCache();
    [[nodiscard]] QString activeWaveformCacheKey(
        qint64 startFrame, qint64 endFrame, qint64 targetPointCount,
        int mode) const;
    [[nodiscard]] QVariantList toVariantPeaks(
        const std::vector<std::vector<float>>& channels) const;
    bool preparePlayback();
    void startPreparedPlayback();
    bool runDocumentCommand(const agplayer::editor::EditCommand& command,
                            bool modifiesDocument = true);
    void pollPlayback();
    void setState(EditorSessionState value);
    void setError(QString message);
    void setProgress(double value);

    EditorActionModel actions_;
    EditorViewport viewport_;
    agplayer::editor::AudioDocument document_;
    ag_audio_backend backend_{AG_AUDIO_BACKEND_DEFAULT};
    ag_player* player_{};
    QTimer playback_timer_;
    QTemporaryDir preview_directory_;
    QString source_path_;
    QString playback_path_;
    QUrl pending_open_url_;
    QString format_name_;
    int sample_rate_{};
    int channels_{};
    int bits_per_sample_{};
    qint64 bit_rate_{};
    QVariantList source_channel_peaks_;
    QVariantList channel_peaks_;
    QVariantList viewport_channel_peaks_;
    qint64 viewport_cache_version_ = 1;
    qint64 viewport_cache_size_bytes_ = 0;
    qint64 viewport_cache_size_limit_ = 16LL * 1024LL * 1024LL;
    QFutureWatcherBase* viewport_waveform_watcher_ = nullptr;
    quint64 viewport_waveform_generation_ = 0;
    std::shared_ptr<std::atomic_bool> viewport_waveform_cancel_token_;

    struct ViewportWaveformCacheEntry {
        qint64 start_frame{};
        qint64 end_frame{};
        qint64 source_start_frame{};
        qint64 target_frames_per_point{};
        int mode{};
        int sample_rate{};
        qint64 bytes{};
        std::vector<std::vector<float>> channels;
        std::list<QString>::iterator lru_iterator{};
    };
    std::unordered_map<QString, std::shared_ptr<ViewportWaveformCacheEntry>>
        viewport_waveform_cache_;
    std::list<QString> viewport_waveform_lru_;

    EditorSessionState state_{EditorSessionState::Empty};
    bool has_document_{};
    bool modified_{};
    bool playing_{};
    bool loop_enabled_{};
    qint64 position_ms_{};
    double volume_{1.0};
    QString error_message_;
    double progress_{};
    agplayer::editor::TimePitchSession time_pitch_;
    agplayer::editor::RecordingSession recording_session_;
    QFutureWatcher<agplayer::editor::WriteResult>* write_watcher_{};
    QFutureWatcher<agplayer::editor::TimePitchResult>* time_pitch_watcher_{};
    QFutureWatcher<bool>* recording_start_watcher_{};
    QFutureWatcher<RecordingFinalizeResult>* recording_stop_watcher_{};
    QFutureWatcher<BpmAnalyzeResult>* bpm_watcher_{};
    QFutureWatcher<agplayer::editor::NoiseReductionResult>* noise_reduction_watcher_{};
    QFutureWatcher<PreviewRenderResult>* preview_watcher_{};
    std::atomic_bool operation_cancelled_{false};
    QVariantList recording_devices_;
    QVariantList export_formats_;
    QString recording_device_id_;
    QString recording_directory_;
    int recording_sample_rate_{48'000};
    int recording_channels_{2};
    bool recording_monitor_{};
    QTimer recording_timer_;
    QVariantList live_recording_peaks_;
    bool insert_recording_at_cursor_{};
    bool time_pitch_preview_active_{};
    bool allow_document_replace_{};
    qint64 recording_insert_frame_{};
    QPointer<PlaybackController> main_playback_;
    std::atomic_uint64_t preview_generation_{0};
};

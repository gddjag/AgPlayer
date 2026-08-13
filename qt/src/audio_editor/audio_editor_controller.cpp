#include "audio_editor_controller.hpp"

#include "audio_editor/audio_file_analyzer.hpp"
#include "audio_editor/document_renderer.hpp"
#include "audio_editor/document_writer.hpp"
#include "bpm_analyzer.hpp"

#include <QFileInfo>
#include <QDateTime>

#include <algorithm>
#include <cmath>
#include <filesystem>

using agplayer::editor::AudioDocument;
using agplayer::editor::AudioFileAnalysis;
using agplayer::editor::AudioFileAnalyzer;
using agplayer::editor::AudioSource;
using agplayer::editor::DocumentRenderer;
using agplayer::editor::DocumentWriter;
using agplayer::editor::EditCommand;
using agplayer::editor::Selection;
using agplayer::editor::WriteRequest;

namespace {

QVariantList to_variant_peaks(
    const std::vector<std::vector<float>>& channels)
{
    QVariantList result;
    for (const auto& channel : channels) {
        QVariantList values;
        values.reserve(static_cast<qsizetype>(channel.size()));
        for (const float value : channel) {
            values.append(value);
        }
        result.append(QVariant::fromValue(values));
    }
    return result;
}

QString local_path(const QUrl& url)
{
    return url.isLocalFile() ? url.toLocalFile() : QString{};
}

} // namespace

AudioEditorController::AudioEditorController(
    const ag_audio_backend backend, QObject* parent)
    : QObject(parent), actions_(this), viewport_(this), backend_(backend)
{
    ag_player_config config{};
    config.backend = backend_;
    config.buffer_frames = 0;
    if (ag_player_create_with_config(&config, &player_) != AG_OK) {
        player_ = nullptr;
    }
    playback_timer_.setInterval(17);
    connect(&playback_timer_, &QTimer::timeout,
            this, &AudioEditorController::pollPlayback);
    recording_timer_.setInterval(33);
    connect(&recording_timer_, &QTimer::timeout, this, [this] {
        emit recordingChanged();
    });
    refreshRecordingDevices();
    refreshActions();
}

AudioEditorController::~AudioEditorController()
{
    if (recording()) (void)recording_session_.stop();
    if (player_) {
        ag_player_stop(player_);
        ag_player_destroy(player_);
    }
}

bool AudioEditorController::recording() const noexcept
{
    using agplayer::editor::RecordingState;
    const auto value = recording_session_.state();
    return value == RecordingState::Recording
        || value == RecordingState::Paused
        || value == RecordingState::Finalizing;
}

bool AudioEditorController::recordingPaused() const noexcept
{
    return recording_session_.state()
        == agplayer::editor::RecordingState::Paused;
}

double AudioEditorController::inputLevel() const noexcept
{
    return std::clamp(static_cast<double>(recording_session_.peak()), 0.0, 1.0);
}

qint64 AudioEditorController::recordingFrames() const noexcept
{
    return recording_session_.framesCaptured();
}

qint64 AudioEditorController::selectionStart() const noexcept
{
    const auto selection = document_.snapshot().selection;
    return selection ? selection->start : -1;
}

qint64 AudioEditorController::selectionEnd() const noexcept
{
    const auto selection = document_.snapshot().selection;
    return selection ? selection->end : -1;
}

qint64 AudioEditorController::selectionFrames() const noexcept
{
    const auto selection = document_.snapshot().selection;
    return selection ? selection->end - selection->start : 0;
}

QString AudioEditorController::fileName() const
{
    return source_path_.isEmpty() ? tr("未命名音频")
                                  : QFileInfo(source_path_).fileName();
}

qint64 AudioEditorController::durationMs() const noexcept
{
    return sample_rate_ > 0 ? totalFrames() * 1'000 / sample_rate_ : 0;
}

bool AudioEditorController::createUntitledDocument(
    const quint32 sampleRate, const quint32 channels, const qint64 frames)
{
    auto candidate = AudioDocument::fromSource(
        AudioSource{std::filesystem::path{}, sampleRate, channels, frames});
    if (candidate.totalFrames() <= 0) {
        return false;
    }
    stopPlayback();
    document_ = std::move(candidate);
    source_path_.clear();
    playback_path_.clear();
    format_name_ = QStringLiteral("WAV");
    sample_rate_ = static_cast<int>(sampleRate);
    channels_ = static_cast<int>(channels);
    bits_per_sample_ = 24;
    bit_rate_ = 0;
    source_channel_peaks_.clear();
    channel_peaks_.clear();
    has_document_ = true;
    modified_ = false;
    viewport_.setDocumentFrames(frames);
    setState(EditorSessionState::Ready);
    refreshActions();
    emit waveformChanged();
    emit documentChanged();
    return true;
}

bool AudioEditorController::openFile(const QUrl& source)
{
    const QString path = local_path(source);
    if (path.isEmpty()) {
        setError(tr("请选择本地音频文件"));
        return false;
    }
    stopPlayback();
    setProgress(0.05);
    const AudioFileAnalysis analysis = AudioFileAnalyzer::analyze(
        std::filesystem::path(path.toStdWString()), 2'048);
    if (!analysis.success) {
        setProgress(0.0);
        setError(QString::fromStdString(analysis.message));
        return false;
    }
    document_ = AudioDocument::fromSource(analysis.source);
    source_path_ = path;
    playback_path_ = path;
    format_name_ = QString::fromStdString(analysis.format).toUpper();
    sample_rate_ = static_cast<int>(analysis.source.sample_rate);
    channels_ = static_cast<int>(analysis.source.channels);
    bits_per_sample_ = analysis.bits_per_sample;
    bit_rate_ = analysis.bit_rate;
    source_channel_peaks_ = to_variant_peaks(analysis.channel_peaks);
    channel_peaks_ = source_channel_peaks_;
    has_document_ = true;
    modified_ = false;
    position_ms_ = 0;
    viewport_.setDocumentFrames(document_.totalFrames());
    setState(EditorSessionState::Ready);
    setProgress(1.0);
    setError({});
    refreshActions();
    emit waveformChanged();
    emit playbackChanged();
    emit documentChanged();
    return true;
}

bool AudioEditorController::save()
{
    if (source_path_.isEmpty()) {
        emit saveAsRequested();
        return false;
    }
    return saveAs(QUrl::fromLocalFile(source_path_));
}

bool AudioEditorController::saveAs(const QUrl& target)
{
    const QString path = local_path(target);
    if (!has_document_ || path.isEmpty()) {
        setError(tr("保存路径无效"));
        return false;
    }
    stopPlayback();
    setState(EditorSessionState::Saving);
    setProgress(0.0);
    WriteRequest request;
    request.snapshot = document_.snapshot();
    request.output_path = std::filesystem::path(path.toStdWString());
    DocumentWriter writer;
    const auto result = writer.write(request, nullptr,
        [this](const float value) { setProgress(value); });
    if (!result.ok()) {
        setState(EditorSessionState::Error);
        setError(QString::fromStdString(result.message));
        return false;
    }
    source_path_ = path;
    playback_path_ = path;
    modified_ = false;
    setProgress(1.0);
    setState(EditorSessionState::Ready);
    setError({});
    refreshActions();
    emit documentChanged();
    return true;
}

bool AudioEditorController::exportTo(
    const QUrl& target, const bool selectionOnly, const QString& codecName)
{
    const QString path = local_path(target);
    const auto selection = document_.snapshot().selection;
    if (!has_document_ || path.isEmpty() || (selectionOnly && !selection)) {
        setError(tr("导出范围或路径无效"));
        return false;
    }
    setState(EditorSessionState::Exporting);
    setProgress(0.0);
    WriteRequest request;
    request.snapshot = document_.snapshot();
    request.output_path = std::filesystem::path(path.toStdWString());
    request.codec_name = codecName.toStdString();
    if (selectionOnly) {
        request.range = selection;
    }
    DocumentWriter writer;
    const auto result = writer.write(request, nullptr,
        [this](const float value) { setProgress(value); });
    if (!result.ok()) {
        setState(EditorSessionState::Error);
        setError(QString::fromStdString(result.message));
        return false;
    }
    setProgress(1.0);
    setState(EditorSessionState::Ready);
    setError({});
    return true;
}

bool AudioEditorController::setSelection(
    const qint64 startFrame, const qint64 endFrame)
{
    if (!has_document_ || !document_.setSelection({startFrame, endFrame})) {
        return false;
    }
    refreshActions();
    emit documentChanged();
    return true;
}

bool AudioEditorController::clearSelection()
{
    if (!document_.clearSelection()) {
        return false;
    }
    refreshActions();
    emit documentChanged();
    return true;
}

bool AudioEditorController::addMarker(const QString& name, const qint64 frame)
{
    if (!has_document_ || !document_.addMarker({name.toStdString(), frame})) {
        return false;
    }
    modified_ = true;
    refreshActions();
    emit documentChanged();
    return true;
}

bool AudioEditorController::insertSilence(
    const qint64 frame, const qint64 frameCount)
{
    return runDocumentCommand(EditCommand::insertSilence(frame, frameCount));
}

bool AudioEditorController::applyGain(const double decibels)
{
    if (!std::isfinite(decibels) || decibels < -60.0 || decibels > 24.0) {
        return false;
    }
    return runDocumentCommand(EditCommand::gain(
        static_cast<float>(std::pow(10.0, decibels / 20.0))));
}

bool AudioEditorController::normalize()
{
    float peak = 0.0F;
    for (const QVariant& channelValue : channel_peaks_) {
        for (const QVariant& value : channelValue.toList()) {
            peak = std::max(peak, static_cast<float>(std::abs(value.toDouble())));
        }
    }
    return peak > 0.0F && runDocumentCommand(
        EditCommand::gain(0.89125094F / peak));
}

bool AudioEditorController::detectBpm()
{
    if (!has_document_ || source_path_.isEmpty()) return false;
    const BpmAnalyzeResult result = analyze_bpm(source_path_);
    if (result.bpm <= 0.0) {
        setError(tr("BPM 检测失败"));
        return false;
    }
    time_pitch_.setOriginalBpm(result.bpm);
    emit timePitchChanged();
    return true;
}

void AudioEditorController::setOriginalBpm(const double value)
{
    time_pitch_.setOriginalBpm(value);
    emit timePitchChanged();
}

bool AudioEditorController::setTargetBpm(const double value)
{
    const bool changed = time_pitch_.setTargetBpm(value);
    if (changed) {
        stopPlayback();
        playback_path_.clear();
        time_pitch_preview_active_ = false;
        emit timePitchChanged();
    }
    return changed;
}

bool AudioEditorController::setSpeedPercent(const double value)
{
    const bool changed = time_pitch_.setSpeedPercent(value);
    if (changed) {
        stopPlayback();
        playback_path_.clear();
        time_pitch_preview_active_ = false;
        emit timePitchChanged();
    }
    return changed;
}

void AudioEditorController::setKeepPitch(const bool value)
{
    if (time_pitch_.keepPitch() == value) return;
    time_pitch_.setKeepPitch(value);
    stopPlayback();
    playback_path_.clear();
    time_pitch_preview_active_ = false;
    emit timePitchChanged();
}

bool AudioEditorController::setPitch(const int semitones, const int cents)
{
    const bool changed = time_pitch_.setPitch(semitones, cents);
    if (changed) {
        stopPlayback();
        playback_path_.clear();
        time_pitch_preview_active_ = false;
        emit timePitchChanged();
    }
    return changed;
}

bool AudioEditorController::applyTimePitch()
{
    if (!has_document_ || !preview_directory_.isValid()) return false;
    stopPlayback();
    setState(EditorSessionState::Processing);
    const auto snapshot = document_.snapshot();
    const auto range = snapshot.selection;
    const QString output = preview_directory_.filePath(
        QStringLiteral("processed-%1.wav").arg(
            QDateTime::currentMSecsSinceEpoch()));
    const auto result = time_pitch_.process(
        snapshot, std::filesystem::path(output.toStdWString()), range,
        nullptr, [this](const float value) { setProgress(value); });
    if (!result.success) {
        setState(EditorSessionState::Error);
        setError(QString::fromStdString(result.message));
        return false;
    }
    const Selection replacement = range.value_or(
        Selection{0, document_.totalFrames()});
    if (!document_.replaceRangeWithSource(result.source, replacement)) {
        setState(EditorSessionState::Error);
        setError(tr("无法提交速度与音高处理结果"));
        return false;
    }
    modified_ = true;
    playback_path_.clear();
    (void)time_pitch_.setSpeedPercent(100.0);
    (void)time_pitch_.setPitch(0, 0);
    time_pitch_preview_active_ = false;
    viewport_.setDocumentFrames(document_.totalFrames());
    const AudioFileAnalysis analysis = AudioFileAnalyzer::analyze(
        result.source.path, 2'048);
    if (analysis.success) {
        source_channel_peaks_ = to_variant_peaks(analysis.channel_peaks);
    }
    rebuildEditorPeaks();
    setProgress(1.0);
    setState(EditorSessionState::Ready);
    setError({});
    refreshActions();
    emit waveformChanged();
    emit documentChanged();
    emit timePitchChanged();
    return true;
}

void AudioEditorController::refreshRecordingDevices()
{
    QVariantList result;
    for (const auto& device : agplayer::editor::RecordingSession::inputDevices()) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), QString::fromStdString(device.id));
        entry.insert(QStringLiteral("name"), QString::fromUtf8(device.name));
        entry.insert(QStringLiteral("isDefault"), device.is_default);
        result.append(entry);
    }
    recording_devices_ = std::move(result);
    emit recordingDevicesChanged();
}

bool AudioEditorController::startRecording(
    const QUrl& target, const QString& deviceId,
    const int recordingSampleRate, const int recordingChannels,
    const bool monitor, const bool insertAtCursor)
{
    const QString path = local_path(target);
    if (path.isEmpty()) {
        setError(tr("请选择录音保存位置"));
        return false;
    }
    if (recording()) return false;
    stopPlayback();
    agplayer::editor::RecordingConfig config;
    config.output_path = std::filesystem::path(path.toStdWString());
    config.device_id = deviceId.toStdString();
    config.sample_rate = static_cast<std::uint32_t>(recordingSampleRate);
    config.channels = static_cast<std::uint32_t>(recordingChannels);
    config.monitor = monitor;
    insert_recording_at_cursor_ = insertAtCursor && has_document_;
    recording_insert_frame_ = position_ms_ * sample_rate_ / 1'000;
    if (!recording_session_.start(config)) {
        setError(tr("无法启动录音设备，请检查设备与权限"));
        return false;
    }
    recording_timer_.start();
    setState(EditorSessionState::Recording);
    setError({});
    emit recordingChanged();
    return true;
}

bool AudioEditorController::pauseRecording()
{
    if (!recording_session_.pause()) return false;
    setState(EditorSessionState::RecordingPaused);
    emit recordingChanged();
    return true;
}

bool AudioEditorController::resumeRecording()
{
    if (!recording_session_.resume()) return false;
    setState(EditorSessionState::Recording);
    emit recordingChanged();
    return true;
}

bool AudioEditorController::stopRecording()
{
    if (!recording()) return false;
    setState(EditorSessionState::Finalizing);
    recording_timer_.stop();
    const auto result = recording_session_.stop();
    if (!result.success) {
        setState(EditorSessionState::Error);
        setError(QString::fromStdString(result.message));
        emit recordingChanged();
        return false;
    }
    const auto analysis = AudioFileAnalyzer::analyze(result.path, 2'048);
    if (!analysis.success) {
        setState(EditorSessionState::Error);
        setError(QString::fromStdString(analysis.message));
        emit recordingChanged();
        return false;
    }
    if (insert_recording_at_cursor_) {
        if (!document_.insertSource(analysis.source, recording_insert_frame_)) {
            setState(EditorSessionState::Error);
            setError(tr("录音完成，但无法插入当前文档"));
            emit recordingChanged();
            return false;
        }
        modified_ = true;
        playback_path_.clear();
        viewport_.setDocumentFrames(document_.totalFrames());
        rebuildEditorPeaks();
    } else {
        document_ = AudioDocument::fromSource(analysis.source);
        source_path_ = QString::fromStdWString(result.path.wstring());
        playback_path_ = source_path_;
        format_name_ = QStringLiteral("WAV");
        sample_rate_ = static_cast<int>(analysis.source.sample_rate);
        channels_ = static_cast<int>(analysis.source.channels);
        bits_per_sample_ = 24;
        bit_rate_ = sample_rate_ * channels_ * bits_per_sample_;
        source_channel_peaks_ = to_variant_peaks(analysis.channel_peaks);
        channel_peaks_ = source_channel_peaks_;
        has_document_ = true;
        modified_ = false;
        viewport_.setDocumentFrames(document_.totalFrames());
    }
    setState(EditorSessionState::Ready);
    refreshActions();
    emit waveformChanged();
    emit documentChanged();
    emit recordingChanged();
    return true;
}

bool AudioEditorController::actionEnabled(const QString& id) const noexcept
{
    const EditorAction* const item = actions_.action(id);
    return item != nullptr && item->enabled;
}

bool AudioEditorController::triggerAction(const QString& id)
{
    EditorAction* const item = action(id);
    if (!item || !item->enabled) {
        return false;
    }
    if (id == QStringLiteral("editor.open")) {
        emit openRequested();
        return true;
    }
    if (id == QStringLiteral("editor.newRecording")) {
        emit newRecordingRequested();
        return true;
    }
    if (id == QStringLiteral("editor.save")) return save();
    if (id == QStringLiteral("editor.export")) {
        emit exportRequested();
        return true;
    }
    if (id == QStringLiteral("editor.moreMenu")) {
        emit moreMenuRequested();
        return true;
    }
    bool changed = false;
    if (id == QStringLiteral("editor.undo")) changed = document_.undo();
    else if (id == QStringLiteral("editor.redo")) changed = document_.redo();
    else if (id == QStringLiteral("editor.cut")) changed = document_.apply(EditCommand::cutSelection());
    else if (id == QStringLiteral("editor.copy")) changed = document_.apply(EditCommand::copySelection());
    else if (id == QStringLiteral("editor.paste")) {
        const qint64 frame = position_ms_ * sample_rate_ / 1'000;
        changed = document_.apply(EditCommand::pasteAt(frame));
    }
    else if (id == QStringLiteral("editor.deleteSelection")) changed = document_.apply(EditCommand::deleteSelection());
    else if (id == QStringLiteral("editor.cropToSelection")) changed = document_.apply(EditCommand::cropToSelection());
    else if (id == QStringLiteral("editor.silenceSelection")) changed = document_.apply(EditCommand::silenceSelection());
    else if (id == QStringLiteral("editor.fadeIn")) changed = document_.apply(EditCommand::fadeIn());
    else if (id == QStringLiteral("editor.fadeOut")) changed = document_.apply(EditCommand::fadeOut());
    if (!changed) return false;
    if (id != QStringLiteral("editor.copy")) {
        stopPlayback();
        modified_ = true;
        playback_path_.clear();
        viewport_.setDocumentFrames(document_.totalFrames());
        rebuildEditorPeaks();
    }
    refreshActions();
    emit waveformChanged();
    emit documentChanged();
    return true;
}

bool AudioEditorController::runDocumentCommand(
    const EditCommand& command, const bool modifiesDocument)
{
    const bool changed = document_.apply(command);
    if (!changed) return false;
    if (modifiesDocument) {
        stopPlayback();
        modified_ = true;
        playback_path_.clear();
        viewport_.setDocumentFrames(document_.totalFrames());
        rebuildEditorPeaks();
    }
    refreshActions();
    emit waveformChanged();
    emit documentChanged();
    return true;
}

bool AudioEditorController::preparePlayback()
{
    if (!has_document_ || !player_) return false;
    if (playback_path_.isEmpty()) {
        if (!preview_directory_.isValid()) {
            setError(tr("无法创建预览目录"));
            return false;
        }
        const bool processed = std::abs(time_pitch_.speedPercent() - 100.0) > 0.001
            || time_pitch_.pitchCents() != 0;
        const QString path = preview_directory_.filePath(
            processed ? QStringLiteral("time-pitch-preview.wav")
                      : QStringLiteral("preview.wav"));
        if (processed) {
            const auto result = time_pitch_.process(
                document_.snapshot(), std::filesystem::path(path.toStdWString()));
            if (!result.success) {
                setError(QString::fromStdString(result.message));
                return false;
            }
        } else {
            const auto rendered = DocumentRenderer{}.renderFloatWav(
                document_.snapshot(), std::nullopt,
                std::filesystem::path(path.toStdWString()));
            if (!rendered.success) {
                setError(QString::fromStdString(rendered.message));
                return false;
            }
        }
        playback_path_ = path;
        if (time_pitch_preview_active_ != processed) {
            time_pitch_preview_active_ = processed;
            emit timePitchChanged();
        }
    }
    if (ag_player_load(player_, playback_path_.toUtf8().constData()) != AG_OK) {
        setError(tr("无法载入编辑预览"));
        return false;
    }
    ag_player_set_volume(player_, static_cast<float>(volume_));
    if (position_ms_ > 0) ag_player_seek(player_, position_ms_);
    return true;
}

bool AudioEditorController::playPause()
{
    if (!has_document_ || !player_) return false;
    if (playing_) {
        if (ag_player_pause(player_) != AG_OK) return false;
        playing_ = false;
        playback_timer_.stop();
        setState(EditorSessionState::Ready);
        emit playbackChanged();
        return true;
    }
    ag_playback_snapshot snapshot{};
    if (ag_player_snapshot(player_, &snapshot) != AG_OK
        || snapshot.state == AG_STOPPED || snapshot.state == AG_ERROR) {
        if (!preparePlayback()) return false;
    }
    if (ag_player_play(player_) != AG_OK) return false;
    playing_ = true;
    playback_timer_.start();
    setState(EditorSessionState::Playing);
    emit playbackChanged();
    return true;
}

bool AudioEditorController::stopPlayback()
{
    if (!player_) return false;
    const bool wasActive = playing_ || position_ms_ != 0;
    ag_player_stop(player_);
    playback_timer_.stop();
    playing_ = false;
    position_ms_ = 0;
    if (has_document_ && state_ == EditorSessionState::Playing) {
        setState(EditorSessionState::Ready);
    }
    if (wasActive) emit playbackChanged();
    return true;
}

bool AudioEditorController::seekMs(const qint64 value)
{
    if (!has_document_ || value < 0 || value > durationMs()) return false;
    position_ms_ = value;
    if (player_) {
        const qint64 preview_position = time_pitch_preview_active_
            ? static_cast<qint64>(std::llround(
                static_cast<double>(value) * 100.0
                / time_pitch_.speedPercent()))
            : value;
        ag_player_seek(player_, preview_position);
    }
    emit playbackChanged();
    return true;
}

void AudioEditorController::setVolume(const double value)
{
    const double bounded = std::clamp(value, 0.0, 1.0);
    if (qFuzzyCompare(volume_, bounded)) return;
    volume_ = bounded;
    if (player_) ag_player_set_volume(player_, static_cast<float>(volume_));
    emit playbackChanged();
}

void AudioEditorController::setLoopEnabled(const bool enabled)
{
    if (loop_enabled_ == enabled) return;
    loop_enabled_ = enabled;
    emit playbackChanged();
}

void AudioEditorController::pollPlayback()
{
    if (!player_) return;
    ag_playback_snapshot snapshot{};
    if (ag_player_snapshot(player_, &snapshot) != AG_OK) return;
    position_ms_ = time_pitch_preview_active_
        ? static_cast<qint64>(std::llround(
            static_cast<double>(snapshot.position_ms)
            * time_pitch_.speedPercent() / 100.0))
        : snapshot.position_ms;
    if (loop_enabled_) {
        const auto selection = document_.snapshot().selection;
        if (selection && sample_rate_ > 0) {
            const qint64 start = selection->start * 1'000 / sample_rate_;
            const qint64 end = selection->end * 1'000 / sample_rate_;
            if (position_ms_ >= end) {
                ag_player_seek(player_, start);
                position_ms_ = start;
            }
        }
    }
    if (snapshot.state != AG_PLAYING) {
        playing_ = false;
        playback_timer_.stop();
        setState(EditorSessionState::Ready);
    }
    emit playbackChanged();
}

void AudioEditorController::rebuildEditorPeaks()
{
    if (source_channel_peaks_.isEmpty() || document_.totalFrames() <= 0) {
        channel_peaks_.clear();
        return;
    }
    const qsizetype points = source_channel_peaks_.constFirst().toList().size() / 2;
    QVariantList rebuilt;
    for (int channel = 0; channel < channels_; ++channel) {
        const QVariantList source = source_channel_peaks_[
            std::min<qsizetype>(static_cast<qsizetype>(channel),
                                source_channel_peaks_.size() - 1)].toList();
        QVariantList values;
        values.reserve(points * 2);
        const auto spans = document_.spans();
        qint64 cursor = 0;
        auto span = spans.cbegin();
        for (qsizetype point = 0; point < points; ++point) {
            const qint64 frame = document_.totalFrames() * point
                / std::max<qsizetype>(1, points - 1);
            while (span != spans.cend() && frame >= cursor + span->frame_count) {
                cursor += span->frame_count;
                ++span;
            }
            if (span == spans.cend() || span->silent || !span->source) {
                values.append(0.0F);
                values.append(0.0F);
                continue;
            }
            const qint64 local = frame - cursor;
            const qint64 sourceFrame = span->source_start + local;
            const qsizetype sourcePoint = static_cast<qsizetype>(std::clamp<qint64>(
                sourceFrame * points / std::max<qint64>(1, span->source->total_frames),
                0, points - 1));
            const float ratio = span->frame_count <= 1 ? 0.0F
                : static_cast<float>(local) / static_cast<float>(span->frame_count - 1);
            const float gain = span->gain_start
                + (span->gain_end - span->gain_start) * ratio;
            values.append(source[sourcePoint * 2].toFloat() * gain);
            values.append(source[sourcePoint * 2 + 1].toFloat() * gain);
        }
        rebuilt.append(QVariant::fromValue(values));
    }
    channel_peaks_ = rebuilt;
}

void AudioEditorController::refreshActions()
{
    const bool selection = document_.snapshot().selection.has_value();
    const bool idle = state_ != EditorSessionState::Saving
        && state_ != EditorSessionState::Exporting
        && state_ != EditorSessionState::Processing
        && state_ != EditorSessionState::Finalizing
        && state_ != EditorSessionState::Recording
        && state_ != EditorSessionState::RecordingPaused;
    actions_.setEnabled(QStringLiteral("editor.open"), idle);
    actions_.setEnabled(QStringLiteral("editor.newRecording"), idle);
    actions_.setEnabled(QStringLiteral("editor.save"), has_document_ && idle);
    actions_.setEnabled(QStringLiteral("editor.export"), has_document_ && idle);
    actions_.setEnabled(QStringLiteral("editor.moreMenu"), has_document_ && idle);
    actions_.setEnabled(QStringLiteral("editor.undo"), document_.canUndo() && idle);
    actions_.setEnabled(QStringLiteral("editor.redo"), document_.canRedo() && idle);
    actions_.setEnabled(QStringLiteral("editor.paste"), document_.hasClipboard() && idle);
    for (const QString& id : {
             QStringLiteral("editor.cut"), QStringLiteral("editor.copy"),
             QStringLiteral("editor.deleteSelection"),
             QStringLiteral("editor.cropToSelection"),
             QStringLiteral("editor.silenceSelection"),
             QStringLiteral("editor.fadeIn"), QStringLiteral("editor.fadeOut")}) {
        actions_.setEnabled(id, has_document_ && selection && idle);
    }
}

void AudioEditorController::setState(const EditorSessionState value)
{
    if (state_ == value) return;
    state_ = value;
    refreshActions();
    emit stateChanged();
}

void AudioEditorController::setError(QString message)
{
    if (error_message_ == message) return;
    error_message_ = std::move(message);
    emit errorMessageChanged();
}

void AudioEditorController::setProgress(const double value)
{
    const double bounded = std::clamp(value, 0.0, 1.0);
    if (qFuzzyCompare(progress_, bounded)) return;
    progress_ = bounded;
    emit progressChanged();
}

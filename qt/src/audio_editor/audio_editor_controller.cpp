#include "audio_editor_controller.hpp"
#include "audio_editor/audio_file_analyzer.hpp"
#include "audio_editor/document_writer.hpp"
#include "bpm_analyzer.hpp"

#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent>

#include "decoder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <list>
#include <optional>
#include <unordered_set>

using agplayer::editor::AudioDocument;
using agplayer::editor::AudioEvent;
using agplayer::editor::AudioFileAnalysis;
using agplayer::editor::AudioFileAnalyzer;
using agplayer::editor::AudioSource;
using agplayer::editor::DocumentWriter;
using agplayer::editor::ProjectDocument;
using agplayer::editor::ProjectExportSettings;
using agplayer::editor::ProjectLoadResult;
using agplayer::editor::ProjectSourceIssue;
using agplayer::editor::ProjectSourceIssueKind;
using agplayer::editor::ProjectSourceRecord;
using agplayer::editor::ProjectSaveRequest;
using agplayer::editor::Selection;
using agplayer::editor::WriteRequest;

namespace {

constexpr std::size_t kMaxProjectSources = 4'096;

qint64 viewportTargetPoints(const qint64 visibleFrames,
                           const qreal viewportWidth)
{
    const qint64 width = std::max<qint64>(1, static_cast<qint64>(std::ceil(
        std::max<qreal>(1.0, viewportWidth))));
    return std::min<qint64>(visibleFrames, width * 2);
}

std::vector<std::vector<float>> peaksAsChannels(const QVariantList& channelPeaks)
{
    std::vector<std::vector<float>> result;
    result.reserve(static_cast<std::size_t>(channelPeaks.size()));
    for (const QVariant& channel_value : channelPeaks) {
        const QVariantList values = channel_value.toList();
        if (values.empty() || values.size() % 2 != 0) {
            return {};
        }
        std::vector<float> channel;
        channel.reserve(static_cast<std::size_t>(values.size()));
        for (qsizetype index = 0; index < values.size(); index += 2) {
            const double minimum = values[index].toDouble();
            const double maximum = values[index + 1].toDouble();
            if (!std::isfinite(minimum) || !std::isfinite(maximum)
                || minimum > maximum) {
                return {};
            }
            channel.push_back(static_cast<float>(minimum));
            channel.push_back(static_cast<float>(maximum));
        }
        result.push_back(std::move(channel));
    }
    return result;
}

QVariantList build_variant_peaks(
    const std::vector<std::vector<float>>& channels)
{
    QVariantList result;
    for (const auto& channel : channels) {
        QVariantList values;
        values.reserve(static_cast<qsizetype>(channel.size()));
        for (const float value : channel) {
            values.append(std::isfinite(value) ? QVariant::fromValue(value)
                                               : QVariant{});
        }
        result.append(QVariant::fromValue(values));
    }
    return result;
}

qint64 clamped_int64_to_qint64(const std::size_t value)
{
    return value > static_cast<std::size_t>(std::numeric_limits<qint64>::max())
        ? std::numeric_limits<qint64>::max() : static_cast<qint64>(value);
}

std::vector<std::vector<float>> buildRecordingPlaceholderPeaks(
    const qreal recPeak, const int channels, const qint64 targetPoints,
    const qint64 startFrame, const qint64 endFrame, const qint64 recStartFrame,
    const qint64 recordedFrames)
{
    if (targetPoints <= 0 || channels <= 0 || recordedFrames <= 0
        || startFrame >= endFrame) {
        return {};
    }
    const qint64 recEndFrame = recStartFrame + recordedFrames;
    const float blank = std::numeric_limits<float>::quiet_NaN();
    std::vector<std::vector<float>> peaks(
        static_cast<std::size_t>(std::max<qint64>(1, channels)),
        std::vector<float>(static_cast<std::size_t>(targetPoints * 2LL), blank));
    const float amplitude = std::clamp(static_cast<float>(recPeak), 0.0F, 1.0F);
    const qint64 visibleFrameWindow = std::max<qint64>(1, endFrame - startFrame);
    for (qint64 index = 0; index < targetPoints; ++index) {
        const qint64 pointStart =
            startFrame + index * visibleFrameWindow / targetPoints;
        const qint64 pointEnd =
            startFrame + (index + 1) * visibleFrameWindow / targetPoints;
        if (pointStart < recEndFrame && pointEnd > recStartFrame) {
            for (auto& channel : peaks) {
                channel[static_cast<std::size_t>(index * 2)] = -amplitude;
                channel[static_cast<std::size_t>(index * 2 + 1)] = amplitude;
            }
        }
    }
    return peaks;
}

qint64 scaledBucket(const qint64 frame, const qint64 totalFrames,
                    const qint64 bucketCount)
{
    if (totalFrames <= 0 || bucketCount <= 0) return 0;
    const long double scaled = static_cast<long double>(frame)
        * static_cast<long double>(bucketCount)
        / static_cast<long double>(totalFrames);
    return std::clamp<qint64>(static_cast<qint64>(std::floor(scaled)), 0,
                              bucketCount - 1);
}

void includePeak(std::vector<float>& output, const qint64 point,
                 float minimum, float maximum, const float gain)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) return;
    minimum *= gain;
    maximum *= gain;
    if (minimum > maximum) std::swap(minimum, maximum);
    const auto index = static_cast<std::size_t>(point * 2);
    if (!std::isfinite(output[index]) || !std::isfinite(output[index + 1U])) {
        output[index] = minimum;
        output[index + 1U] = maximum;
        return;
    }
    output[index] = std::min(output[index], minimum);
    output[index + 1U] = std::max(output[index + 1U], maximum);
}

void mergeEventFromSourcePeaks(
    const AudioEvent& event,
    const std::vector<std::vector<float>>& sourcePeaks,
    const qint64 visibleStart, const qint64 visibleEnd,
    const qint64 targetPoints, std::vector<std::vector<float>>& output)
{
    if (!event.source || sourcePeaks.empty()) return;
    const qint64 eventEnd = event.timelineStart
        + agplayer::editor::audibleFrames(event);
    const qint64 intersectionStart = std::max(visibleStart, event.timelineStart);
    const qint64 intersectionEnd = std::min(visibleEnd, eventEnd);
    if (intersectionEnd <= intersectionStart) return;
    const qint64 visibleFrames = visibleEnd - visibleStart;

    for (qint64 point = 0; point < targetPoints; ++point) {
        const qint64 pointStart = visibleStart
            + static_cast<qint64>(static_cast<long double>(point)
                * visibleFrames / targetPoints);
        const qint64 pointEnd = visibleStart
            + static_cast<qint64>(static_cast<long double>(point + 1)
                * visibleFrames / targetPoints);
        const qint64 overlapStart = std::max(pointStart, intersectionStart);
        const qint64 overlapEnd = std::min(pointEnd, intersectionEnd);
        if (overlapEnd <= overlapStart) continue;
        const qint64 sourceStart = event.sourceStart
            + overlapStart - event.timelineStart;
        const qint64 sourceEnd = event.sourceStart
            + overlapEnd - event.timelineStart;
        for (std::size_t channelIndex = 0;
             channelIndex < output.size(); ++channelIndex) {
            const auto sourceIndex = std::min(channelIndex,
                                              sourcePeaks.size() - 1U);
            const auto& channel = sourcePeaks[sourceIndex];
            const qint64 buckets = static_cast<qint64>(channel.size() / 2U);
            if (buckets <= 0) continue;
            const qint64 first = scaledBucket(sourceStart,
                                               event.source->total_frames,
                                               buckets);
            const qint64 last = scaledBucket(
                std::max(sourceStart, sourceEnd - 1),
                event.source->total_frames, buckets);
            float minimum = std::numeric_limits<float>::infinity();
            float maximum = -std::numeric_limits<float>::infinity();
            for (qint64 bucket = first; bucket <= last; ++bucket) {
                const auto index = static_cast<std::size_t>(bucket * 2);
                if (index + 1U >= channel.size()) break;
                minimum = std::min(minimum, channel[index]);
                maximum = std::max(maximum, channel[index + 1U]);
            }
            includePeak(output[channelIndex], point,
                        event.mute ? 0.0F : minimum,
                        event.mute ? 0.0F : maximum,
                        event.mute ? 1.0F : event.gain);
        }
    }
}

bool mergeEventFromDecodedSlice(
    const AudioEvent& event, const qint64 visibleStart, const qint64 visibleEnd,
    const qint64 targetPoints,
    const std::shared_ptr<std::atomic_bool>& cancelToken,
    std::vector<std::vector<float>>& output)
{
    if (!event.source || event.source->path.empty() || output.empty()
        || targetPoints <= 0) return false;
    const qint64 eventEnd = event.timelineStart
        + agplayer::editor::audibleFrames(event);
    const qint64 intersectionStart = std::max(visibleStart, event.timelineStart);
    const qint64 intersectionEnd = std::min(visibleEnd, eventEnd);
    if (intersectionEnd <= intersectionStart) return false;
    const qint64 sourceStart = event.sourceStart
        + intersectionStart - event.timelineStart;
    const qint64 sourceEnd = event.sourceStart
        + intersectionEnd - event.timelineStart;
    const int sourceChannels = static_cast<int>(event.source->channels);
    const int sampleRate = static_cast<int>(event.source->sample_rate);
    if (sourceChannels <= 0 || sampleRate <= 0 || sourceEnd <= sourceStart) {
        return false;
    }
    const QString path = QString::fromStdWString(event.source->path.wstring());
    agplayer::Decoder decoder;
    if (decoder.open(path.toUtf8().toStdString()) != AG_OK
        || decoder.seek(static_cast<qint64>(std::llround(
            static_cast<long double>(sourceStart) * 1'000.0L / sampleRate)))
            != AG_OK) {
        decoder.close();
        return false;
    }
    const qint64 visibleFrames = visibleEnd - visibleStart;
    qint64 currentFrame = sourceStart;
    bool wrote = false;
    agplayer::DecodedAudioBlock block;
    while (currentFrame < sourceEnd) {
        if (cancelToken->load(std::memory_order_acquire)) break;
        if (decoder.read(block) != AG_OK) break;
        if (cancelToken->load(std::memory_order_acquire)) break;
        if (block.frames == 0U) {
            if (block.end_of_stream) break;
            continue;
        }
        const qint64 blockStart = block.timestamp_ms >= 0
            ? static_cast<qint64>(std::llround(
                static_cast<long double>(block.timestamp_ms) * sampleRate
                / 1'000.0L))
            : currentFrame;
        const qint64 blockEnd = blockStart + static_cast<qint64>(block.frames);
        const qint64 localStart = std::max(blockStart, sourceStart);
        const qint64 localEnd = std::min(blockEnd, sourceEnd);
        for (qint64 sourceFrame = localStart; sourceFrame < localEnd;
             ++sourceFrame) {
            const qint64 timelineFrame = event.timelineStart
                + sourceFrame - event.sourceStart;
            const qint64 point = std::clamp<qint64>(
                static_cast<qint64>(std::floor(
                    static_cast<long double>(timelineFrame - visibleStart)
                    * targetPoints / visibleFrames)), 0, targetPoints - 1);
            const std::size_t sampleBase = static_cast<std::size_t>(
                sourceFrame - blockStart) * static_cast<std::size_t>(sourceChannels);
            if (sampleBase >= block.samples.size()) break;
            for (std::size_t outputChannel = 0;
                 outputChannel < output.size(); ++outputChannel) {
                const std::size_t sourceChannel = std::min<std::size_t>(
                    outputChannel, static_cast<std::size_t>(sourceChannels - 1));
                if (sampleBase + sourceChannel >= block.samples.size()) break;
                const float value = event.mute ? 0.0F
                    : block.samples[sampleBase + sourceChannel];
                includePeak(output[outputChannel], point, value, value,
                            event.mute ? 1.0F : event.gain);
            }
            wrote = true;
        }
        currentFrame = std::max(currentFrame + 1, blockEnd);
        if (block.end_of_stream) break;
    }
    decoder.close();
    return wrote;
}

std::vector<std::vector<float>> composeVisibleTimelinePeaks(
    const agplayer::editor::TimelineSnapshot& snapshot,
    const QString& primarySourcePath,
    const std::vector<std::vector<float>>& primarySourcePeaks,
    const qint64 visibleStart, const qint64 visibleEnd,
    const qint64 targetPoints, const int channels,
    const std::shared_ptr<std::atomic_bool>& cancelToken)
{
    const float blank = std::numeric_limits<float>::quiet_NaN();
    std::vector<std::vector<float>> result(
        static_cast<std::size_t>(std::max(0, channels)),
        std::vector<float>(static_cast<std::size_t>(targetPoints * 2), blank));
    const qint64 visibleFrames = visibleEnd - visibleStart;
    if (visibleFrames <= 0 || targetPoints <= 0 || channels <= 0) return result;
    const bool preciseSlice = visibleFrames <= targetPoints * 2;

    for (const AudioEvent& event : snapshot.events) {
        if (cancelToken->load(std::memory_order_acquire)) return {};
        const qint64 eventEnd = event.timelineStart
            + agplayer::editor::audibleFrames(event);
        if (!event.source || eventEnd <= visibleStart
            || event.timelineStart >= visibleEnd) {
            continue;
        }
        const QString eventPath = QString::fromStdWString(event.source->path.wstring());
        const bool primary = eventPath == primarySourcePath
            && !primarySourcePeaks.empty();
        bool decoded = false;
        if (preciseSlice || !primary) {
            decoded = mergeEventFromDecodedSlice(
                event, visibleStart, visibleEnd, targetPoints,
                cancelToken, result);
        }
        if (!decoded && primary) {
            mergeEventFromSourcePeaks(event, primarySourcePeaks,
                                      visibleStart, visibleEnd,
                                      targetPoints, result);
        }
    }
    return result;
}

QString local_path(const QUrl& url)
{
    return url.isLocalFile() ? url.toLocalFile() : QString{};
}

std::optional<quint64> nextAvailableSourceId(const std::vector<ProjectSourceRecord>& records)
{
    std::unordered_set<quint64> ids;
    ids.reserve(records.size());
    for (const ProjectSourceRecord& record : records) {
        if (record.sourceId > 0 && record.sourceId < std::numeric_limits<quint64>::max()) {
            ids.insert(record.sourceId);
        }
    }
    for (quint64 candidate = 1; candidate < std::numeric_limits<quint64>::max(); ++candidate) {
        if (ids.count(candidate) == 0) return candidate;
    }
    return std::nullopt;
}

ProjectSourceRecord project_source_record(const quint64 sourceId,
                                          const std::shared_ptr<const AudioSource>& source)
{
    const QString path = source ? QString::fromStdWString(source->path.wstring()) : QString{};
    const QFileInfo file(path);
    return {sourceId, source, file.exists() ? file.size() : -1,
            file.exists() ? file.lastModified().toUTC().toMSecsSinceEpoch() : -1};
}

bool same_project_export_settings(const ProjectExportSettings& left,
                                  const ProjectExportSettings& right)
{
    return left.codecName == right.codecName
        && left.sampleRate == right.sampleRate
        && left.channels == right.channels
        && left.bitRate == right.bitRate
        && left.keepMetadata == right.keepMetadata
        && left.variableBitRate == right.variableBitRate
        && left.quality == right.quality
        && left.outputDirectory == right.outputDirectory;
}

bool same_selection(const std::optional<Selection>& left,
                    const std::optional<Selection>& right) noexcept
{
    if (left.has_value() != right.has_value()) return false;
    return !left || (left->start == right->start && left->end == right->end);
}

QVariantList to_project_issues(const std::vector<ProjectSourceIssue>& issues)
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(issues.size()));
    for (const ProjectSourceIssue& issue : issues) {
        const QString kind = issue.kind == ProjectSourceIssueKind::Missing
            ? QStringLiteral("missing")
            : issue.kind == ProjectSourceIssueKind::IdentityMismatch
                ? QStringLiteral("identityMismatch") : QStringLiteral("unavailable");
        result.append(QVariantMap{{QStringLiteral("kind"), kind},
                                  {QStringLiteral("sourceId"), issue.sourceId},
                                  {QStringLiteral("path"), issue.path},
                                  {QStringLiteral("message"), issue.message}});
    }
    return result;
}

qint64 effectiveDocumentFramesForViewport(
    const bool hasDocument, const qint64 documentFrames,
    const bool recording, const qint64 recordingInsertFrame,
    const qint64 recordedFrames, const bool insertAtCursor)
{
    if (!recording) return documentFrames;
    const qint64 projected = insertAtCursor && hasDocument
        ? recordingInsertFrame + std::max<qint64>(0, recordedFrames)
        : std::max<qint64>(0, recordedFrames);
    return std::max(documentFrames, projected);
}

} // namespace

struct AudioEditorController::RecordingFinalizeResult final {
    agplayer::editor::RecordingResult recording;
    AudioFileAnalysis analysis;
};

AudioEditorController::AudioEditorController(
    const ag_audio_backend backend, QObject* parent)
    : QObject(parent), actions_(this), viewport_(this)
{
    Q_UNUSED(backend);
    connect(&viewport_, &EditorViewport::viewportChanged, this,
            &AudioEditorController::requestViewportWaveform);
    connect(&viewport_, &EditorViewport::viewportChanged, this, [this] {
        if (suppress_persisted_state_tracking_
            || (!has_document_ && !recording())) return;
        viewport_persisted_dirty_ = viewport_.visibleStartFrame()
                != saved_visible_start_frame_
            || viewport_.visibleEndFrame() != saved_visible_end_frame_;
        if (syncModifiedFromHistory()) emit documentChanged();
    });
    refreshActions();
}

AudioEditorController::~AudioEditorController()
{
    cancelOperation();
    if (viewport_waveform_cancel_token_) {
        viewport_waveform_cancel_token_->store(true,
                                               std::memory_order_release);
    }
    if (viewport_waveform_watcher_) {
        viewport_waveform_watcher_->disconnect(this);
        viewport_waveform_watcher_->cancel();
    }
    if (write_watcher_) write_watcher_->future().waitForFinished();
    if (time_pitch_watcher_) time_pitch_watcher_->future().waitForFinished();
    if (recording_start_watcher_) recording_start_watcher_->future().waitForFinished();
    if (recording_stop_watcher_) recording_stop_watcher_->future().waitForFinished();
    if (bpm_watcher_) bpm_watcher_->future().waitForFinished();
    if (recording()) (void)recording_session_.stop();
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

QVariantList AudioEditorController::channelPeaks() const
{
    return recording() && !live_recording_peaks_.isEmpty()
        ? live_recording_peaks_ : channel_peaks_;
}

QVariantList AudioEditorController::timelineEventViews() const
{
    QVariantList result;
    const auto snapshot = document_.timelineSnapshot();
    result.reserve(static_cast<qsizetype>(snapshot.events.size()));
    for (const AudioEvent& event : snapshot.events) {
        AudioEvent visible = event;
        if (event_gesture_.kind != EventGestureKind::None
            && event_gesture_.id == event.id && event_gesture_.pending) {
            visible.timelineStart = event_gesture_.timelineStart;
            if (event_gesture_.kind == EventGestureKind::Trim) {
                visible.sourceStart = event_gesture_.sourceStart;
                visible.sourceEnd = event_gesture_.sourceEnd;
            }
        }
        result.append(QVariantMap{
            {QStringLiteral("id"), QString::number(visible.id)},
            {QStringLiteral("timelineStart"), QVariant::fromValue<qint64>(
                visible.timelineStart)},
            {QStringLiteral("timelineEnd"), QVariant::fromValue<qint64>(
                visible.timelineStart + agplayer::editor::audibleFrames(visible))},
            {QStringLiteral("sourceStart"), QVariant::fromValue<qint64>(
                visible.sourceStart)},
            {QStringLiteral("sourceEnd"), QVariant::fromValue<qint64>(
                visible.sourceEnd)}});
    }
    return result;
}

bool AudioEditorController::busy() const noexcept
{
    return state_ == EditorSessionState::Processing
        || state_ == EditorSessionState::Saving
        || state_ == EditorSessionState::Exporting
        || state_ == EditorSessionState::Finalizing;
}

qint64 AudioEditorController::selectionStart() const noexcept
{
    const auto selection = document_.selection();
    return selection ? selection->start : -1;
}

qint64 AudioEditorController::selectionEnd() const noexcept
{
    const auto selection = document_.selection();
    return selection ? selection->end : -1;
}

qint64 AudioEditorController::selectionFrames() const noexcept
{
    const auto selection = document_.selection();
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

QVariantMap AudioEditorController::projectExportSettingsMap() const
{
    return {{QStringLiteral("codecName"), project_export_settings_.codecName},
            {QStringLiteral("sampleRate"), project_export_settings_.sampleRate},
            {QStringLiteral("channels"), project_export_settings_.channels},
            {QStringLiteral("bitRate"), project_export_settings_.bitRate},
            {QStringLiteral("keepMetadata"), project_export_settings_.keepMetadata},
            {QStringLiteral("variableBitRate"), project_export_settings_.variableBitRate},
            {QStringLiteral("quality"), project_export_settings_.quality},
            {QStringLiteral("outputDirectory"),
             project_export_settings_.outputDirectory}};
}

bool AudioEditorController::setProjectExportSettings(
    const ProjectExportSettings& settings)
{
    if (!isValidProjectExportSettings(settings)) {
        setError(tr("导出参数无效"));
        return false;
    }
    if (same_project_export_settings(project_export_settings_, settings)) {
        return true;
    }
    project_export_settings_ = settings;
    (void)syncModifiedFromHistory();
    setError({});
    emit documentChanged();
    emit projectChanged();
    return true;
}

void AudioEditorController::setProjectExportSettingsMap(
    const QVariantMap& settings)
{
    ProjectExportSettings value;
    value.codecName = settings.value(QStringLiteral("codecName")).toString();
    value.sampleRate = settings.value(QStringLiteral("sampleRate")).toInt();
    value.channels = settings.value(QStringLiteral("channels")).toInt();
    value.bitRate = settings.value(QStringLiteral("bitRate")).toLongLong();
    value.keepMetadata = settings.value(
        QStringLiteral("keepMetadata"), true).toBool();
    value.variableBitRate = settings.value(
        QStringLiteral("variableBitRate"), true).toBool();
    value.quality = settings.value(QStringLiteral("quality"), 80).toInt();
    value.outputDirectory = settings.value(
        QStringLiteral("outputDirectory")).toString();
    (void)setProjectExportSettings(value);
}

void AudioEditorController::markProjectClean() noexcept
{
    saved_history_state_ = document_.historyStateId();
    saved_selection_ = document_.selection();
    saved_playhead_frame_ = playhead_frame_;
    playhead_persisted_dirty_ = false;
    saved_visible_start_frame_ = viewport_.visibleStartFrame();
    saved_visible_end_frame_ = viewport_.visibleEndFrame();
    viewport_persisted_dirty_ = false;
    saved_export_settings_ = project_export_settings_;
    forced_project_dirty_ = false;
    modified_ = false;
}

void AudioEditorController::markProjectDirty() noexcept
{
    forced_project_dirty_ = true;
    modified_ = true;
}

bool AudioEditorController::updatePersistedPlayhead(const qint64 frame,
                                                     const qint64 positionMs) noexcept
{
    playhead_frame_ = frame;
    position_ms_ = positionMs;
    playhead_persisted_dirty_ = playhead_frame_ != saved_playhead_frame_;
    return syncModifiedFromHistory();
}

bool AudioEditorController::syncModifiedFromHistory() noexcept
{
    const bool previous = modified_;
    modified_ = forced_project_dirty_ || !saved_history_state_.has_value()
        || document_.historyStateId() != *saved_history_state_
        || !same_selection(document_.selection(), saved_selection_)
        || playhead_persisted_dirty_
        || viewport_persisted_dirty_
        || !same_project_export_settings(project_export_settings_,
                                         saved_export_settings_);
    return previous != modified_;
}

void AudioEditorController::setViewportDocumentFrames(const qint64 frames) noexcept
{
    suppress_persisted_state_tracking_ = true;
    viewport_.setDocumentFrames(frames);
    suppress_persisted_state_tracking_ = false;
    viewport_persisted_dirty_ = viewport_.visibleStartFrame()
            != saved_visible_start_frame_
        || viewport_.visibleEndFrame() != saved_visible_end_frame_;
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
    project_path_.clear();
    project_sources_.clear();
    project_issues_.clear();
    known_project_issues_.clear();
    project_export_settings_ = {};
    format_name_ = QStringLiteral("WAV");
    sample_rate_ = static_cast<int>(sampleRate);
    channels_ = static_cast<int>(channels);
    bits_per_sample_ = 24;
    bit_rate_ = 0;
    QVariantList flat;
    constexpr int silent_points = 2'048;
    flat.reserve(silent_points * 2);
    for (int point = 0; point < silent_points; ++point) {
        flat.append(0.0F);
        flat.append(0.0F);
    }
    channel_peaks_.clear();
    for (quint32 channel = 0; channel < channels; ++channel) {
        channel_peaks_.append(QVariant::fromValue(flat));
    }
    has_document_ = true;
    position_ms_ = 0;
    playhead_frame_ = 0;
    setViewportDocumentFrames(frames);
    markProjectClean();
    setState(EditorSessionState::Ready);
    refreshActions();
    emit waveformChanged();
    emit documentChanged();
    emit projectChanged();
    return true;
}

bool AudioEditorController::openFile(const QUrl& source)
{
    if (modified_ && !allow_document_replace_) {
        pending_open_url_ = source;
        pending_open_is_project_ = false;
        pending_clear_document_ = false;
        emit discardConfirmationRequested();
        return false;
    }
    allow_document_replace_ = false;
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
    project_path_.clear();
    project_sources_ = {project_source_record(1,
        document_.timelineSnapshot().events.front().source)};
    project_issues_.clear();
    known_project_issues_.clear();
    project_export_settings_ = {};
    format_name_ = QString::fromStdString(analysis.format).toUpper();
    sample_rate_ = static_cast<int>(analysis.source.sample_rate);
    channels_ = static_cast<int>(analysis.source.channels);
    bits_per_sample_ = analysis.bits_per_sample;
    bit_rate_ = analysis.bit_rate;
    channel_peaks_ = build_variant_peaks(analysis.channel_peaks);
    clearViewportWaveformState();
    has_document_ = true;
    position_ms_ = 0;
    playhead_frame_ = 0;
    setViewportDocumentFrames(document_.totalFrames());
    markProjectClean();
    setState(EditorSessionState::Ready);
    setProgress(1.0);
    setError({});
    refreshActions();
    emit waveformChanged();
    emit playbackChanged();
    emit documentChanged();
    emit projectChanged();
    return true;
}

bool AudioEditorController::confirmDiscardAndOpen()
{
    if (pending_clear_document_) {
        pending_clear_document_ = false;
        allow_document_replace_ = true;
        return clearDocument();
    }
    if (!pending_open_url_.isValid()) return false;
    const QUrl source = pending_open_url_;
    const bool openProjectFile = pending_open_is_project_;
    pending_open_url_.clear();
    pending_open_is_project_ = false;
    allow_document_replace_ = true;
    return openProjectFile ? openProject(source) : openFile(source);
}

void AudioEditorController::cancelDiscardAndOpen()
{
    pending_open_url_.clear();
    pending_open_is_project_ = false;
    pending_clear_document_ = false;
}

bool AudioEditorController::save()
{
    if (project_path_.isEmpty()) {
        emit saveProjectAsRequested();
        return false;
    }
    return saveProject(QUrl::fromLocalFile(project_path_));
}

bool AudioEditorController::saveProject(const QUrl& target)
{
    const QString path = local_path(target);
    if (!has_document_ || path.isEmpty() || busy()) {
        if (path.isEmpty()) setError(tr("保存工程路径无效"));
        return false;
    }
    syncProjectSourcesAndIssues();
    syncPrimarySourceSummary();
    ProjectSaveRequest request;
    request.document = &document_;
    request.playheadFrame = playhead_frame_;
    request.visibleStartFrame = viewport_.visibleStartFrame();
    request.visibleEndFrame = viewport_.visibleEndFrame();
    request.exportSettings = project_export_settings_;
    request.sourceRecords = &project_sources_;
    const auto result = ProjectDocument::save(path, request);
    if (!result.ok()) {
        setError(result.message);
        return false;
    }
    for (const ProjectSourceRecord& saved : result.sources) {
        const auto existing = std::find_if(project_sources_.begin(), project_sources_.end(),
            [&saved](const ProjectSourceRecord& record) {
                return record.sourceId == saved.sourceId;
            });
        if (existing == project_sources_.end()) project_sources_.push_back(saved);
        else *existing = saved;
    }
    syncProjectSourcesAndIssues();
    project_path_ = QFileInfo(path).absoluteFilePath();
    markProjectClean();
    setError({});
    refreshActions();
    emit documentChanged();
    emit projectChanged();
    return true;
}

bool AudioEditorController::saveProjectAs(const QUrl& target)
{
    return saveProject(target);
}

bool AudioEditorController::openProject(const QUrl& source)
{
    if (modified_ && !allow_document_replace_) {
        pending_open_url_ = source;
        pending_open_is_project_ = true;
        pending_clear_document_ = false;
        emit discardConfirmationRequested();
        return false;
    }
    allow_document_replace_ = false;
    const QString path = local_path(source);
    if (path.isEmpty() || busy()) {
        if (path.isEmpty()) setError(tr("请选择本地工程文件"));
        return false;
    }
    ProjectLoadResult loaded = ProjectDocument::load(path);
    if (!loaded.ok()) {
        setError(loaded.message);
        return false;
    }

    // Every fallible operation completed before the active document is replaced.
    stopPlayback();
    document_ = std::move(*loaded.document);
    project_sources_ = std::move(loaded.sources);
    known_project_issues_ = to_project_issues(loaded.issues);
    syncProjectSourcesAndIssues();
    project_export_settings_ = loaded.exportSettings;
    project_path_ = QFileInfo(path).absoluteFilePath();
    syncPrimarySourceSummary();
    channel_peaks_.clear();
    clearViewportWaveformState();
    has_document_ = true;
    playhead_frame_ = loaded.playheadFrame;
    position_ms_ = sample_rate_ > 0 ? playhead_frame_ * 1'000 / sample_rate_ : 0;
    setViewportDocumentFrames(document_.totalFrames());
    suppress_persisted_state_tracking_ = true;
    (void)viewport_.setVisibleRange(loaded.visibleStartFrame, loaded.visibleEndFrame);
    suppress_persisted_state_tracking_ = false;
    markProjectClean();
    setState(EditorSessionState::Ready);
    setProgress(1.0);
    setError({});
    refreshActions();
    emit waveformChanged();
    emit playbackChanged();
    emit documentChanged();
    emit projectChanged();
    return true;
}

bool AudioEditorController::relinkProjectSource(const quint64 sourceId,
                                                 const QUrl& replacement)
{
    const QString path = local_path(replacement);
    if (!has_document_ || path.isEmpty() || busy()) return false;
    const auto result = ProjectDocument::relink(document_, project_sources_, sourceId, path);
    if (!result.ok()) {
        setError(result.message);
        return false;
    }
    known_project_issues_.erase(std::remove_if(known_project_issues_.begin(), known_project_issues_.end(),
        [sourceId](const QVariant& value) {
            return value.toMap().value(QStringLiteral("sourceId")).toULongLong() == sourceId;
        }), known_project_issues_.end());
    syncProjectSourcesAndIssues();
    markProjectDirty();
    syncPrimarySourceSummary();
    clearViewportWaveformState();
    refreshActions();
    emit waveformChanged();
    emit documentChanged();
    emit projectChanged();
    return true;
}

bool AudioEditorController::undo()
{
    if (!has_document_ || busy() || !document_.undo()) return false;
    finishTimelineMutation();
    return true;
}

bool AudioEditorController::redo()
{
    if (!has_document_ || busy() || !document_.redo()) return false;
    finishTimelineMutation();
    return true;
}

bool AudioEditorController::saveAs(const QUrl& target)
{
    if (!exportSupported()) return false;
    return exportWithSettings(target, false, {}, 0, 0, 0, true, true, 80,
                              false);
}

bool AudioEditorController::exportTo(
    const QUrl& target, const bool selectionOnly, const QString& codecName,
    const int sampleRate, const int channels, const qint64 bitRate,
    const bool keepMetadata, const bool variableBitRate, const int quality)
{
    if (!exportSupported()) return false;
    return exportWithSettings(target, selectionOnly, codecName, sampleRate,
                              channels, bitRate, keepMetadata,
                              variableBitRate, quality, true);
}

bool AudioEditorController::exportWithSettings(
    const QUrl& target, const bool selectionOnly, const QString& codecName,
    const int sampleRate, const int channels, const qint64 bitRate,
    const bool keepMetadata, const bool variableBitRate, const int quality,
    const bool usePersistedDefaults)
{
    const QString path = local_path(target);
    const auto selection = document_.selection();
    if (!requireOnlineProjectSources()) return false;
    if (!has_document_ || path.isEmpty() || (selectionOnly && !selection)) {
        setError(tr("导出范围或路径无效"));
        return false;
    }
    const bool defaultArguments = codecName.isEmpty() && sampleRate == 0
        && channels == 0 && bitRate == 0 && keepMetadata
        && variableBitRate && quality == 80;
    ProjectExportSettings effective{codecName, sampleRate, channels, bitRate,
                                    keepMetadata, variableBitRate, quality,
                                    QFileInfo(path).absolutePath()};
    if (usePersistedDefaults && defaultArguments) {
        effective = project_export_settings_;
        effective.outputDirectory = QFileInfo(path).absolutePath();
    }
    if (!isValidProjectExportSettings(effective)) {
        setError(tr("导出参数无效"));
        return false;
    }
    if (busy()) return false;
    if (usePersistedDefaults
        && !same_project_export_settings(project_export_settings_, effective)) {
        project_export_settings_ = effective;
        markProjectDirty();
        emit documentChanged();
        emit projectChanged();
    }
    setState(usePersistedDefaults ? EditorSessionState::Exporting
                                  : EditorSessionState::Saving);
    setProgress(0.0);
    WriteRequest request;
    request.snapshot = document_.timelineSnapshot();
    request.output_path = std::filesystem::path(path.toStdWString());
    request.codec_name = effective.codecName.toStdString();
    if (!source_path_.isEmpty()) {
        request.metadata_source_path = std::filesystem::path(
            source_path_.toStdWString());
    }
    request.sample_rate = effective.sampleRate;
    request.channels = effective.channels;
    request.bit_rate = effective.bitRate;
    request.keep_metadata = effective.keepMetadata;
    request.variable_bit_rate = effective.variableBitRate;
    request.quality = effective.quality;
    if (selectionOnly) {
        request.range = selection;
    }
    operation_cancelled_.store(false, std::memory_order_release);
    auto* watcher = new QFutureWatcher<agplayer::editor::WriteResult>(this);
    write_watcher_ = watcher;
    connect(watcher, &QFutureWatcher<agplayer::editor::WriteResult>::finished,
            this, [this, watcher] {
        write_watcher_ = nullptr;
        const auto result = watcher->result();
        watcher->deleteLater();
        if (!result.ok()) {
            setState(result.error == agplayer::editor::WriteError::Cancelled
                ? EditorSessionState::Ready : EditorSessionState::Error);
            setError(result.error == agplayer::editor::WriteError::Cancelled
                ? tr("操作已取消") : QString::fromStdString(result.message));
            return;
        }
        setProgress(1.0);
        setState(EditorSessionState::Ready);
        setError({});
    });
    const QPointer<AudioEditorController> guard(this);
    watcher->setFuture(QtConcurrent::run([this, request, guard] {
        return DocumentWriter{}.write(request, &operation_cancelled_,
            [guard](const float value) {
                if (guard) QMetaObject::invokeMethod(
                    guard, [guard, value] { if (guard) guard->setProgress(value); },
                    Qt::QueuedConnection);
            });
    }));
    return true;
}

bool AudioEditorController::setSelection(
    const qint64 startFrame, const qint64 endFrame)
{
    if (!has_document_ || !document_.setSelection({startFrame, endFrame})) {
        return false;
    }
    (void)syncModifiedFromHistory();
    refreshActions();
    emit documentChanged();
    emit projectChanged();
    return true;
}

bool AudioEditorController::clearSelection()
{
    if (!document_.clearSelection()) {
        return false;
    }
    (void)syncModifiedFromHistory();
    refreshActions();
    emit documentChanged();
    emit projectChanged();
    return true;
}

void AudioEditorController::cancelOperation()
{
    operation_cancelled_.store(true, std::memory_order_release);
}

bool AudioEditorController::moveEvent(const quint64 id, const qint64 timelineStart)
{
    if (!has_document_ || busy()
        || !document_.moveEvent(static_cast<agplayer::editor::EventId>(id),
                                timelineStart)) return false;
    finishTimelineMutation();
    return true;
}

std::optional<agplayer::editor::EventId> AudioEditorController::parseEventId(
    const QString& id) noexcept
{
    if (id.isEmpty()) return std::nullopt;
    for (const QChar character : id) {
        if (!character.isDigit()) return std::nullopt;
    }
    bool ok = false;
    const quint64 value = id.toULongLong(&ok, 10);
    if (!ok || value == 0) return std::nullopt;
    return static_cast<agplayer::editor::EventId>(value);
}

bool AudioEditorController::moveEvent(const QString& id,
                                      const qint64 timelineStart)
{
    const auto eventId = parseEventId(id);
    if (!eventId) return false;
    if (event_gesture_.kind != EventGestureKind::None) {
        if (event_gesture_.kind != EventGestureKind::Move
            || event_gesture_.id != *eventId || timelineStart < 0) {
            return false;
        }
        event_gesture_.timelineStart = timelineStart;
        event_gesture_.pending = true;
        emit documentChanged();
        return true;
    }
    return moveEvent(static_cast<quint64>(*eventId), timelineStart);
}

std::optional<agplayer::editor::EventId> eventAtPlayhead(
    const agplayer::editor::TimelineSnapshot& snapshot,
    const qint64 frame)
{
    for (const auto& event : snapshot.events) {
        const qint64 end = event.timelineStart + agplayer::editor::audibleFrames(event);
        if (frame > event.timelineStart && frame < end) return event.id;
    }
    return std::nullopt;
}

std::optional<std::pair<agplayer::editor::EventId, agplayer::editor::EventId>>
mergePairCoveredBySelection(const agplayer::editor::TimelineSnapshot& snapshot,
                            const std::optional<Selection>& selection)
{
    if (!selection) return std::nullopt;
    std::vector<const agplayer::editor::AudioEvent*> covered;
    for (const auto& event : snapshot.events) {
        const qint64 end = event.timelineStart + agplayer::editor::audibleFrames(event);
        if (event.timelineStart >= selection->start && end <= selection->end) {
            covered.push_back(&event);
        }
    }
    if (covered.size() != 2) return std::nullopt;
    const auto& left = *covered[0];
    const auto& right = *covered[1];
    const bool sameEnvelope = left.envelope.size() == right.envelope.size()
        && std::equal(left.envelope.begin(), left.envelope.end(),
                      right.envelope.begin(), [](const auto& first, const auto& second) {
                          return first.offset == second.offset && first.gain == second.gain;
                      });
    if (left.timelineStart + agplayer::editor::audibleFrames(left)
            != right.timelineStart
        || left.sourceEnd != right.sourceStart || left.source != right.source
        || left.gain != right.gain || left.fadeIn != right.fadeIn
        || left.fadeOut != right.fadeOut || left.speedRatio != right.speedRatio
        || left.pitchSemitone != right.pitchSemitone || left.mute != right.mute
        || !sameEnvelope) return std::nullopt;
    return std::make_pair(covered[0]->id, covered[1]->id);
}

bool AudioEditorController::trimEvent(const quint64 id, const qint64 sourceStart,
                                      const qint64 sourceEnd,
                                      const qint64 timelineStart)
{
    if (!has_document_ || busy()
        || !document_.trimEvent(static_cast<agplayer::editor::EventId>(id),
                                sourceStart, sourceEnd, timelineStart)) return false;
    finishTimelineMutation();
    return true;
}

bool AudioEditorController::trimEvent(const QString& id,
                                      const qint64 sourceStart,
                                      const qint64 sourceEnd,
                                      const qint64 timelineStart)
{
    const auto eventId = parseEventId(id);
    if (!eventId) return false;
    if (event_gesture_.kind != EventGestureKind::None) {
        if (event_gesture_.kind != EventGestureKind::Trim
            || event_gesture_.id != *eventId || sourceStart < 0
            || sourceEnd <= sourceStart || timelineStart < 0) {
            return false;
        }
        event_gesture_.sourceStart = sourceStart;
        event_gesture_.sourceEnd = sourceEnd;
        event_gesture_.timelineStart = timelineStart;
        event_gesture_.pending = true;
        emit documentChanged();
        return true;
    }
    return trimEvent(static_cast<quint64>(*eventId), sourceStart, sourceEnd,
                     timelineStart);
}

bool AudioEditorController::splitEvent(const quint64 id, const qint64 frame)
{
    if (!has_document_ || busy()
        || !document_.splitEventAt(static_cast<agplayer::editor::EventId>(id), frame)) {
        return false;
    }
    finishTimelineMutation();
    return true;
}

bool AudioEditorController::splitEvent(const QString& id, const qint64 frame)
{
    const auto eventId = parseEventId(id);
    return eventId && splitEvent(static_cast<quint64>(*eventId), frame);
}

bool AudioEditorController::beginEventGesture(const QString& id,
                                              const QString& operation,
                                              const bool duplicate)
{
    if (busy() || event_gesture_.kind != EventGestureKind::None) return false;
    const auto eventId = parseEventId(id);
    if (!eventId) return false;
    const auto snapshot = document_.timelineSnapshot();
    const auto event = std::find_if(snapshot.events.cbegin(), snapshot.events.cend(),
        [eventId](const AudioEvent& value) { return value.id == *eventId; });
    if (event == snapshot.events.cend()) return false;
    const EventGestureKind kind = operation == QStringLiteral("move")
        ? EventGestureKind::Move
        : operation == QStringLiteral("trim") ? EventGestureKind::Trim
                                               : EventGestureKind::None;
    if (kind == EventGestureKind::None
        || (duplicate && kind != EventGestureKind::Move)) {
        return false;
    }
    event_gesture_ = EventGesture{kind, *eventId, duplicate, false,
        event->timelineStart, event->sourceStart, event->sourceEnd};
    return true;
}

bool AudioEditorController::endEventGesture()
{
    if (event_gesture_.kind == EventGestureKind::None) return false;
    const EventGesture gesture = event_gesture_;
    event_gesture_ = {};
    if (!gesture.pending) {
        emit documentChanged();
        return true;
    }

    bool changed = false;
    if (gesture.duplicate) {
        changed = document_.duplicateEvent(gesture.id, gesture.timelineStart);
    } else if (gesture.kind == EventGestureKind::Move) {
        changed = document_.moveEvent(gesture.id, gesture.timelineStart);
    } else if (gesture.kind == EventGestureKind::Trim) {
        changed = document_.trimEvent(gesture.id, gesture.sourceStart,
                                      gesture.sourceEnd,
                                      gesture.timelineStart);
    }
    if (!changed) return false;
    finishTimelineMutation();
    return true;
}

bool AudioEditorController::cancelEventGesture()
{
    if (event_gesture_.kind == EventGestureKind::None) return false;
    event_gesture_ = {};
    emit documentChanged();
    return true;
}

bool AudioEditorController::setActiveTool(const QString& tool)
{
    if (tool != QStringLiteral("select")
        && tool != QStringLiteral("scissors")) {
        return false;
    }
    if (active_tool_ == tool) return true;
    active_tool_ = tool;
    emit toolChanged();
    return true;
}

bool AudioEditorController::clearTransientState()
{
    bool changed = cancelEventGesture();
    if (active_tool_ != QStringLiteral("select")) {
        active_tool_ = QStringLiteral("select");
        emit toolChanged();
        changed = true;
    }
    if (document_.selection()) {
        changed = clearSelection() || changed;
    }
    return changed;
}

bool AudioEditorController::mergeEvents(const quint64 left, const quint64 right)
{
    if (!has_document_ || busy()
        || !document_.mergeEvents(static_cast<agplayer::editor::EventId>(left),
                                  static_cast<agplayer::editor::EventId>(right))) {
        return false;
    }
    finishTimelineMutation();
    return true;
}

bool AudioEditorController::detectBpm()
{
    return false;
}

void AudioEditorController::setOriginalBpm(const double value)
{
    if (!bpmDetectionSupported()) return;
    time_pitch_.setOriginalBpm(value);
    emit timePitchChanged();
}

bool AudioEditorController::setTargetBpm(const double value)
{
    if (!timePitchSupported()) return false;
    const bool changed = time_pitch_.setTargetBpm(value);
    if (changed) {
        time_pitch_preview_active_ = false;
        emit timePitchChanged();
    }
    return changed;
}

bool AudioEditorController::setSpeedPercent(const double value)
{
    if (!timePitchSupported()) return false;
    const bool changed = time_pitch_.setSpeedPercent(value);
    if (changed) {
        time_pitch_preview_active_ = false;
        emit timePitchChanged();
    }
    return changed;
}

void AudioEditorController::setKeepPitch(const bool value)
{
    if (!timePitchSupported()) return;
    if (time_pitch_.keepPitch() == value) return;
    time_pitch_.setKeepPitch(value);
    time_pitch_preview_active_ = false;
    emit timePitchChanged();
}

bool AudioEditorController::setPitch(const int semitones, const int cents)
{
    if (!timePitchSupported()) return false;
    const bool changed = time_pitch_.setPitch(semitones, cents);
    if (changed) {
        time_pitch_preview_active_ = false;
        emit timePitchChanged();
    }
    return changed;
}

void AudioEditorController::refreshRecordingDevices()
{
    recording_devices_.clear();
    emit recordingDevicesChanged();
}

bool AudioEditorController::startRecording(
    const QUrl& target, const QString& deviceId,
    const int recordingSampleRate, const int recordingChannels,
    const bool monitor, const bool insertAtCursor)
{
    if (!recordingSupported()) return false;
    const int effectiveSampleRate = std::max(1, recordingSampleRate);
    const int effectiveChannels = std::max(1, recordingChannels);
    QString path = local_path(target);
    if (path.isEmpty()) {
        QString directory = recording_directory_;
        if (directory.isEmpty()) {
            directory = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        }
        if (directory.isEmpty()) {
            directory = QDir::currentPath();
        }
        QDir outputDir(directory);
        if (!outputDir.exists()) {
            outputDir.mkpath(QStringLiteral("."));
        }
        const QString timestamp = QDateTime::currentDateTime().toString(
            QStringLiteral("yyyyMMdd_HHmmss"));
        int attempt = 0;
        do {
            path = outputDir.filePath(QStringLiteral("AgPlayer_Recording_%1%2.wav")
                                     .arg(timestamp, (attempt > 0
                                         ? QString("_%1").arg(attempt)
                                         : QString{})));
            ++attempt;
        } while (QFileInfo::exists(path));
        recording_directory_ = outputDir.absolutePath();
    }
    if (recording() || recording_start_watcher_ || recording_stop_watcher_) return false;
    stopPlayback();
    agplayer::editor::RecordingConfig config;
    config.output_path = std::filesystem::path(path.toStdWString());
    config.device_id = deviceId.toStdString();
    config.sample_rate = static_cast<std::uint32_t>(effectiveSampleRate);
    config.channels = static_cast<std::uint32_t>(effectiveChannels);
    config.monitor = monitor;
    insert_recording_at_cursor_ = insertAtCursor && has_document_;
    recording_insert_frame_ = playhead_frame_;
    setError({});
    setState(EditorSessionState::Processing);
    emit recordingChanged();
    auto* watcher = new QFutureWatcher<bool>(this);
    recording_start_watcher_ = watcher;
    connect(watcher, &QFutureWatcher<bool>::finished, this,
            [this, watcher, path, deviceId, recordingSampleRate,
             recordingChannels, monitor] {
        recording_start_watcher_ = nullptr;
        const bool started = watcher->result();
        watcher->deleteLater();
        if (!started) {
            setState(has_document_ ? EditorSessionState::Ready
                                   : EditorSessionState::Empty);
            setError(tr("无法启动录音设备，请检查设备与权限"));
            emit recordingChanged();
            return;
        }
        QSettings settings;
        settings.beginGroup(QStringLiteral("audioEditor"));
        recording_directory_ = QFileInfo(path).absolutePath();
        recording_device_id_ = deviceId;
        recording_sample_rate_ = recordingSampleRate;
        recording_channels_ = recordingChannels;
        recording_monitor_ = monitor;
        settings.setValue(QStringLiteral("recordingDirectory"), recording_directory_);
        settings.setValue(QStringLiteral("recordingDeviceId"), recording_device_id_);
        settings.setValue(QStringLiteral("recordingSampleRate"), recording_sample_rate_);
        settings.setValue(QStringLiteral("recordingChannels"), recording_channels_);
        settings.setValue(QStringLiteral("recordingMonitor"), recording_monitor_);
        settings.endGroup();
        emit recordingPreferencesChanged();
        recording_timer_.start();
        setState(EditorSessionState::Recording);
        emit recordingChanged();
    });
    watcher->setFuture(QtConcurrent::run([this, config] {
        return recording_session_.start(config);
    }));
    return true;
}

bool AudioEditorController::startRecordingToTemporaryFile(
    const QString& deviceId, const int recordingSampleRate,
    const int recordingChannels, const bool monitor,
    const bool insertAtCursor)
{
    Q_UNUSED(deviceId);
    Q_UNUSED(recordingSampleRate);
    Q_UNUSED(recordingChannels);
    Q_UNUSED(monitor);
    Q_UNUSED(insertAtCursor);
    return false;
}

bool AudioEditorController::createRecordingDocument(
    const quint32 sampleRate, const quint32 channels)
{
    return createUntitledDocument(sampleRate, channels, 1);
}

bool AudioEditorController::clearDocument()
{
    if (recording() || busy()) return false;
    if (modified_ && !allow_document_replace_) {
        pending_open_url_.clear();
        pending_open_is_project_ = false;
        pending_clear_document_ = true;
        emit discardConfirmationRequested();
        return false;
    }
    allow_document_replace_ = false;
    pending_clear_document_ = false;
    stopPlayback();
    document_ = AudioDocument{};
    source_path_.clear();
    project_path_.clear();
    project_sources_.clear();
    project_issues_.clear();
    known_project_issues_.clear();
    project_export_settings_ = {};
    format_name_.clear();
    sample_rate_ = 0;
    channels_ = 0;
    bits_per_sample_ = 0;
    bit_rate_ = 0;
    channel_peaks_.clear();
    has_document_ = false;
    position_ms_ = 0;
    playhead_frame_ = 0;
    setViewportDocumentFrames(0);
    markProjectClean();
    setState(EditorSessionState::Empty);
    refreshActions();
    emit waveformChanged();
    emit playbackChanged();
    emit documentChanged();
    emit projectChanged();
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
    if (!recording() || recording_stop_watcher_) return false;
    setState(EditorSessionState::Finalizing);
    recording_timer_.stop();
    auto* watcher = new QFutureWatcher<RecordingFinalizeResult>(this);
    recording_stop_watcher_ = watcher;
    connect(watcher, &QFutureWatcher<RecordingFinalizeResult>::finished, this,
            [this, watcher] {
        recording_stop_watcher_ = nullptr;
        const RecordingFinalizeResult outcome = watcher->result();
        watcher->deleteLater();
        const auto& result = outcome.recording;
        const auto& analysis = outcome.analysis;
        if (!result.success || !analysis.success) {
            setState(EditorSessionState::Error);
            setError(QString::fromStdString(result.success
                ? analysis.message : result.message));
            emit recordingChanged();
            return;
        }
        if (insert_recording_at_cursor_) {
            syncProjectSourcesAndIssues();
            const auto sourceId = nextProjectSourceId();
            if (!sourceId) {
                setState(EditorSessionState::Error);
                setError(tr("工程音频源数量已达上限，无法插入录音"));
                emit recordingChanged();
                return;
            }
            if (!document_.insertSource(analysis.source, recording_insert_frame_)) {
                setState(EditorSessionState::Error);
                setError(tr("录音完成，但无法插入当前文档"));
                emit recordingChanged();
                return;
            }
            const auto insertedSnapshot = document_.timelineSnapshot();
            const auto inserted = std::find_if(insertedSnapshot.events.begin(),
                insertedSnapshot.events.end(), [&analysis](const AudioEvent& event) {
                    return event.source && event.source->path == analysis.source.path;
                });
            if (inserted == insertedSnapshot.events.end()) {
                setState(EditorSessionState::Error);
                setError(tr("录音完成，但无法登记当前工程源"));
                emit recordingChanged();
                return;
            }
            project_sources_.push_back(project_source_record(*sourceId, inserted->source));
            finishTimelineMutation();
        } else {
            document_ = AudioDocument::fromSource(analysis.source);
            source_path_ = QString::fromStdWString(result.path.wstring());
            format_name_ = QStringLiteral("WAV");
            sample_rate_ = static_cast<int>(analysis.source.sample_rate);
            channels_ = static_cast<int>(analysis.source.channels);
            bits_per_sample_ = 24;
            bit_rate_ = sample_rate_ * channels_ * bits_per_sample_;
            channel_peaks_ = build_variant_peaks(analysis.channel_peaks);
            has_document_ = true;
            project_path_.clear();
            project_sources_ = {project_source_record(1,
                document_.timelineSnapshot().events.front().source)};
            project_issues_.clear();
            known_project_issues_.clear();
            project_export_settings_ = {};
            setViewportDocumentFrames(document_.totalFrames());
            markProjectClean();
        }
        setState(EditorSessionState::Ready);
        refreshActions();
        emit waveformChanged();
        emit documentChanged();
        emit recordingChanged();
    });
    watcher->setFuture(QtConcurrent::run([this] {
        RecordingFinalizeResult outcome;
        outcome.recording = recording_session_.stop();
        if (outcome.recording.success) {
            outcome.analysis = AudioFileAnalyzer::analyze(
                outcome.recording.path, 2'048);
        }
        return outcome;
    }));
    return true;
}

bool AudioEditorController::cancelRecording()
{
    if (!recording()) return false;
    recording_timer_.stop();
    const bool cancelled = recording_session_.cancel();
    position_ms_ = 0;
    clearViewportWaveformState();
    setState(has_document_ ? EditorSessionState::Ready
                           : EditorSessionState::Empty);
    if (cancelled) setError(tr("录音已取消"));
    emit recordingChanged();
    emit playbackChanged();
    return cancelled;
}

bool AudioEditorController::actionEnabled(const QString& id) const noexcept
{
    const EditorAction* const item = actions_.action(id);
    return item != nullptr && item->enabled;
}

bool AudioEditorController::triggerAction(const QString& id)
{
    EditorAction* const item = action(id);
    if (!item) {
        return false;
    }
    if (!item->enabled) return false;
    if (id == QStringLiteral("editor.export") && !requireOnlineProjectSources()) return false;
    if (id == QStringLiteral("editor.open")) {
        emit openRequested();
        return true;
    }
    if (id == QStringLiteral("editor.newRecording")) {
        emit newRecordingRequested();
        return true;
    }
    if (id == QStringLiteral("editor.save")) return save();
    if (id == QStringLiteral("editor.undo")) return undo();
    if (id == QStringLiteral("editor.redo")) return redo();
    if (id == QStringLiteral("editor.export")) {
        emit exportRequested();
        return true;
    }
    if (id == QStringLiteral("editor.split")) {
        const qint64 frame = playhead_frame_;
        const auto event = eventAtPlayhead(document_.timelineSnapshot(), frame);
        return event && splitEvent(*event, frame);
    }
    if (id == QStringLiteral("editor.merge")) {
        const auto pair = mergePairCoveredBySelection(
            document_.timelineSnapshot(), document_.selection());
        return pair && mergeEvents(pair->first, pair->second);
    }
    bool changed = false;
    if (id == QStringLiteral("editor.cut")) changed = document_.cutSelection();
    else if (id == QStringLiteral("editor.copy")) changed = document_.copySelection();
    else if (id == QStringLiteral("editor.paste")) {
        const qint64 frame = playhead_frame_;
        changed = document_.pasteAt(frame);
    }
    else if (id == QStringLiteral("editor.deleteSelection")) changed = document_.deleteSelection();
    if (!changed) return false;
    if (id != QStringLiteral("editor.copy")) {
        finishTimelineMutation();
        return true;
    }
    refreshActions();
    emit waveformChanged();
    emit documentChanged();
    return true;
}

bool AudioEditorController::playPause()
{
    return false;
}

bool AudioEditorController::stopPlayback()
{
    const bool wasActive = playing_ || position_ms_ != 0;
    playing_ = false;
    const bool modifiedChanged = updatePersistedPlayhead(0, 0);
    if (has_document_ && state_ == EditorSessionState::Playing) {
        setState(EditorSessionState::Ready);
    }
    if (wasActive) emit playbackChanged();
    if (modifiedChanged) emit documentChanged();
    return true;
}

bool AudioEditorController::seekMs(const qint64 value)
{
    if (!has_document_ || value < 0 || value > durationMs()) return false;
    const qint64 playhead = sample_rate_ > 0 ? value * sample_rate_ / 1'000 : 0;
    const bool modifiedChanged = updatePersistedPlayhead(playhead, value);
    refreshActions();
    emit playbackChanged();
    if (modifiedChanged) emit documentChanged();
    return true;
}

bool AudioEditorController::seekFrame(const qint64 frame)
{
    if (!has_document_ || frame < 0 || frame > document_.totalFrames()) return false;
    const qint64 positionMs = sample_rate_ > 0 ? frame * 1'000 / sample_rate_ : 0;
    const bool modifiedChanged = updatePersistedPlayhead(frame, positionMs);
    refreshActions();
    emit playbackChanged();
    if (modifiedChanged) emit documentChanged();
    return true;
}

void AudioEditorController::setVolume(const double value)
{
    const double bounded = std::clamp(value, 0.0, 1.0);
    if (qFuzzyCompare(volume_, bounded)) return;
    volume_ = bounded;
    emit playbackChanged();
}

void AudioEditorController::setLoopEnabled(const bool enabled)
{
    if (loop_enabled_ == enabled) return;
    loop_enabled_ = enabled;
    emit playbackChanged();
}

void AudioEditorController::clearViewportWaveformState()
{
    ++viewport_waveform_generation_;
    if (viewport_waveform_cancel_token_) {
        viewport_waveform_cancel_token_->store(true,
                                               std::memory_order_release);
    }
    viewport_channel_peaks_.clear();
}

void AudioEditorController::requestViewportWaveform()
{
    const quint64 generation = ++viewport_waveform_generation_;
    if (viewport_waveform_cancel_token_) {
        viewport_waveform_cancel_token_->store(true,
                                               std::memory_order_release);
        viewport_waveform_cancel_token_.reset();
    }
    if (viewport_waveform_watcher_) {
        viewport_waveform_watcher_->disconnect(this);
        viewport_waveform_watcher_->cancel();
        viewport_waveform_watcher_->deleteLater();
        viewport_waveform_watcher_ = nullptr;
    }
    const qint64 recordedFrames = recording()
        ? static_cast<qint64>(recording_session_.framesCaptured())
        : 0;
    const qint64 totalFrames = effectiveDocumentFramesForViewport(
        has_document_, document_.totalFrames(), recording(), recording_insert_frame_,
        recordedFrames, insert_recording_at_cursor_);
    const qreal viewportWidth = viewport_.viewportWidth();
    const qint64 clampedTotal = std::max<qint64>(0, totalFrames);
    qint64 startFrame = std::clamp<qint64>(
        viewport_.visibleStartFrame(), 0, clampedTotal);
    qint64 endFrame = std::clamp<qint64>(
        viewport_.visibleEndFrame(), 0, clampedTotal);
    if (endFrame <= startFrame) {
        startFrame = 0;
        endFrame = std::max<qint64>(1, clampedTotal);
    }
    const qint64 visibleFrames = std::max<qint64>(0, endFrame - startFrame);
    const qint64 targetPoints = visibleFrames <= 0 || viewportWidth <= 0.0
        ? 0 : viewportTargetPoints(visibleFrames, viewportWidth);
    if ((!has_document_ && !recording())
        || clampedTotal <= 0 || channels_ <= 0 || visibleFrames <= 0
        || targetPoints <= 0) {
        viewport_channel_peaks_.clear();
        emit waveformChanged();
        return;
    }

    const auto snapshot = document_.timelineSnapshot();
    const auto primaryPeaks = peaksAsChannels(channel_peaks_);
    const bool recordingActive = recording();
    const qint64 recordingStartFrame = insert_recording_at_cursor_ && has_document_
        ? recording_insert_frame_ : 0;
    const qreal recordingPeak = static_cast<qreal>(recording_session_.peak());
    const auto cancelToken = std::make_shared<std::atomic_bool>(false);
    viewport_waveform_cancel_token_ = cancelToken;

    auto* watcher = new QFutureWatcher<std::vector<std::vector<float>>>(this);
    viewport_waveform_watcher_ = watcher;
    connect(watcher,
            &QFutureWatcher<std::vector<std::vector<float>>>::finished,
            this,
            [this, watcher, generation, cancelToken] {
        if (viewport_waveform_watcher_ == watcher) {
            viewport_waveform_watcher_ = nullptr;
        }
        if (cancelToken->load(std::memory_order_acquire)
            || generation != viewport_waveform_generation_) {
            watcher->deleteLater();
            return;
        }
        viewport_channel_peaks_ = build_variant_peaks(watcher->result());
        emit waveformChanged();
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(
        [snapshot, primaryPath = source_path_, primaryPeaks,
         startFrame, endFrame, targetPoints, channels = channels_,
         recordingActive, recordedFrames, recordingStartFrame,
         recordingPeak, cancelToken]() mutable {
            auto peaks = composeVisibleTimelinePeaks(
                snapshot, primaryPath, primaryPeaks, startFrame, endFrame,
                targetPoints, channels, cancelToken);
            if (cancelToken->load(std::memory_order_acquire)) return peaks;
            if (recordingActive && recordedFrames > 0) {
                auto recordingPeaks = buildRecordingPlaceholderPeaks(
                    recordingPeak, channels, targetPoints, startFrame,
                    endFrame, recordingStartFrame, recordedFrames);
                if (peaks.empty()) peaks = recordingPeaks;
                const std::size_t channelCount = std::min(
                    peaks.size(), recordingPeaks.size());
                for (std::size_t channel = 0; channel < channelCount; ++channel) {
                    for (qint64 point = 0; point < targetPoints; ++point) {
                        const auto index = static_cast<std::size_t>(point * 2);
                        if (index + 1U >= peaks[channel].size()
                            || index + 1U >= recordingPeaks[channel].size()
                            || !std::isfinite(recordingPeaks[channel][index])
                            || !std::isfinite(recordingPeaks[channel][index + 1U])) {
                            continue;
                        }
                        includePeak(peaks[channel], point,
                                    recordingPeaks[channel][index],
                                    recordingPeaks[channel][index + 1U], 1.0F);
                    }
                }
            }
            return peaks;
        }));
    return;

}

void AudioEditorController::refreshActions()
{
    const bool selection = document_.selection().has_value();
    const bool idle = state_ != EditorSessionState::Saving
        && state_ != EditorSessionState::Exporting
        && state_ != EditorSessionState::Processing
        && state_ != EditorSessionState::Finalizing
        && state_ != EditorSessionState::Recording
        && state_ != EditorSessionState::RecordingPaused;
    actions_.setEnabled(QStringLiteral("editor.open"), idle);
    actions_.setEnabled(QStringLiteral("editor.newRecording"),
                        recordingSupported() && idle);
    actions_.setEnabled(QStringLiteral("editor.save"), has_document_ && idle);
    actions_.setEnabled(QStringLiteral("editor.export"), exportSupported()
                        && has_document_ && idle && projectSourcesOnline());
    actions_.setEnabled(QStringLiteral("editor.undo"), has_document_ && idle
                        && document_.canUndo());
    actions_.setEnabled(QStringLiteral("editor.redo"), has_document_ && idle
                        && document_.canRedo());
    actions_.setEnabled(QStringLiteral("editor.paste"), document_.hasClipboard() && idle);
    const qint64 playhead = playhead_frame_;
    actions_.setEnabled(QStringLiteral("editor.split"), has_document_ && idle
        && eventAtPlayhead(document_.timelineSnapshot(), playhead).has_value());
    actions_.setEnabled(QStringLiteral("editor.merge"), has_document_ && idle
        && mergePairCoveredBySelection(document_.timelineSnapshot(),
                                       document_.selection()).has_value());
    for (const QString& id : {
             QStringLiteral("editor.cut"), QStringLiteral("editor.copy"),
             QStringLiteral("editor.deleteSelection")}) {
        actions_.setEnabled(id, has_document_ && selection && idle);
    }
    for (const QString& id : {QStringLiteral("editor.cropToSelection"),
             QStringLiteral("editor.silenceSelection"), QStringLiteral("editor.fadeIn"),
             QStringLiteral("editor.fadeOut")}) {
        actions_.setEnabled(id, false);
    }
}

bool AudioEditorController::projectSourcesOnline() const noexcept
{
    return project_issues_.isEmpty();
}

bool AudioEditorController::requireOnlineProjectSources()
{
    if (projectSourcesOnline()) return true;
    setError(tr("工程音频源离线或已变更，请重新链接后再继续"));
    return false;
}

void AudioEditorController::syncProjectSourcesAndIssues()
{
    const auto snapshot = document_.timelineSnapshot();
    std::unordered_set<const AudioSource*> referencedSources;
    std::unordered_set<quint64> referencedIds;
    for (const auto& source : document_.retainedSources()) {
        if (source) referencedSources.insert(source.get());
    }
    for (const auto& event : snapshot.events) {
        if (event.source) referencedSources.insert(event.source.get());
    }
    project_sources_.erase(std::remove_if(project_sources_.begin(), project_sources_.end(),
        [&referencedSources](const ProjectSourceRecord& record) {
            return !record.source || referencedSources.count(record.source.get()) == 0;
        }), project_sources_.end());

    std::unordered_set<quint64> retainedIds;
    for (const ProjectSourceRecord& record : project_sources_) retainedIds.insert(record.sourceId);
    QVariantList compactIssues;
    std::unordered_set<quint64> issueIds;
    for (const QVariant& issue : std::as_const(known_project_issues_)) {
        const quint64 sourceId = issue.toMap().value(QStringLiteral("sourceId")).toULongLong();
        if (retainedIds.count(sourceId) != 0 && issueIds.insert(sourceId).second) {
            compactIssues.append(issue);
        }
    }
    known_project_issues_ = std::move(compactIssues);

    std::unordered_set<const AudioSource*> currentSources;
    for (const auto& event : snapshot.events) {
        if (!event.source || !currentSources.insert(event.source.get()).second) continue;
        auto record = std::find_if(project_sources_.begin(), project_sources_.end(),
            [&event](const ProjectSourceRecord& value) {
                return value.source.get() == event.source.get();
            });
        if (record == project_sources_.end()) {
            const auto sourceId = nextAvailableSourceId(project_sources_);
            if (!sourceId) continue;
            project_sources_.push_back(project_source_record(*sourceId, event.source));
            record = std::prev(project_sources_.end());
            const QString path = QString::fromStdWString(event.source->path.wstring());
            if (!path.isEmpty() && !QFileInfo::exists(path)) {
                known_project_issues_.append(QVariantMap{
                    {QStringLiteral("kind"), QStringLiteral("missing")},
                    {QStringLiteral("sourceId"), record->sourceId},
                    {QStringLiteral("path"), path},
                    {QStringLiteral("message"), QStringLiteral("source file is missing")}});
            }
        }
        referencedIds.insert(record->sourceId);
    }

    project_issues_.clear();
    for (const QVariant& issue : std::as_const(known_project_issues_)) {
        const quint64 sourceId = issue.toMap().value(
            QStringLiteral("sourceId")).toULongLong();
        if (referencedIds.count(sourceId) != 0) project_issues_.append(issue);
    }
}

std::optional<quint64> AudioEditorController::nextProjectSourceId() const
{
    const auto snapshot = document_.timelineSnapshot();
    std::unordered_set<const AudioSource*> sources;
    for (const AudioEvent& event : snapshot.events) {
        if (event.source) sources.insert(event.source.get());
    }
    if (sources.size() >= kMaxProjectSources) return std::nullopt;
    return nextAvailableSourceId(project_sources_);
}

void AudioEditorController::finishTimelineMutation()
{
    const qint64 requestedPlayhead = playhead_frame_;
    stopPlayback();
    syncProjectSourcesAndIssues();
    syncPrimarySourceSummary();
    const qint64 frames = std::max<qint64>(0, document_.totalFrames());
    setViewportDocumentFrames(frames);
    playhead_frame_ = std::clamp<qint64>(requestedPlayhead, 0, frames);
    position_ms_ = sample_rate_ > 0
        ? playhead_frame_ * 1'000 / sample_rate_ : 0;
    playhead_persisted_dirty_ = playhead_frame_ != saved_playhead_frame_;
    (void)syncModifiedFromHistory();
    clearViewportWaveformState();
    refreshActions();
    emit waveformChanged();
    emit playbackChanged();
    emit documentChanged();
    emit projectChanged();
}

void AudioEditorController::syncPrimarySourceSummary()
{
    const auto snapshot = document_.timelineSnapshot();
    const auto first = snapshot.events.empty() ? std::shared_ptr<const AudioSource>{}
                                               : snapshot.events.front().source;
    source_path_ = first ? QString::fromStdWString(first->path.wstring()) : QString{};
    format_name_ = source_path_.isEmpty() ? QString{}
                                          : QFileInfo(source_path_).suffix().toUpper();
    sample_rate_ = first ? static_cast<int>(first->sample_rate) : 0;
    channels_ = first ? static_cast<int>(first->channels) : 0;
    bits_per_sample_ = 0;
    bit_rate_ = 0;
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

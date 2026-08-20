#include "audio_editor_controller.hpp"
#include "playback_controller.hpp"

#include "audio_editor/audio_file_analyzer.hpp"
#include "audio_editor/document_renderer.hpp"
#include "audio_editor/document_writer.hpp"
#include "bpm_analyzer.hpp"
#include "transcode_capability.hpp"

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

using agplayer::editor::AudioDocument;
using agplayer::editor::AudioFileAnalysis;
using agplayer::editor::AudioFileAnalyzer;
using agplayer::editor::AudioSource;
using agplayer::editor::DocumentRenderer;
using agplayer::editor::DocumentWriter;
using agplayer::editor::Selection;
using agplayer::editor::WriteRequest;

namespace {

constexpr qint64 kViewportWaveformCacheDefaultLimit = 16LL * 1024LL * 1024LL;
constexpr qint64 kViewportTileFrameLimit = 4'096LL;
constexpr qint64 kViewportTileFrameLimitMode1 = 8'192LL;
constexpr qint64 kViewportMode2PointCap = 4'096LL;

constexpr qreal kViewportModeAThreshold = 32.0;
constexpr qreal kViewportModeBThreshold = 1.2;

int viewportRenderMode(const qreal samplesPerPixel)
{
    if (!std::isfinite(samplesPerPixel) || samplesPerPixel <= 0.0) {
        return 0;
    }
    if (samplesPerPixel > kViewportModeAThreshold) {
        return 0;
    }
    if (samplesPerPixel > kViewportModeBThreshold) {
        return 1;
    }
    return 2;
}

qint64 viewportTargetPoints(const int renderMode, const qint64 visibleFrames,
                           const qreal viewportWidth)
{
    const qint64 width = std::max<qint64>(1, static_cast<qint64>(std::llround(
        std::max<qreal>(1.0, viewportWidth))));
    if (renderMode == 0) {
        return std::max<qint64>(48, std::min(visibleFrames, width / 2));
    }
    if (renderMode == 1) {
        return std::max<qint64>(width,
            std::min<qint64>(visibleFrames, width * 2));
    }
    return std::min<qint64>(visibleFrames, std::min(width * 2, kViewportMode2PointCap));
}

qint64 viewportCacheBytes(const std::vector<std::vector<float>>& channels)
{
    qint64 bytes = 0;
    for (const auto& channel : channels) {
        bytes += static_cast<qint64>(channel.size())
            * static_cast<qint64>(sizeof(float));
    }
    return bytes;
}

QString waveformCacheKey(const QString& path, const qint64 version,
                        const qint64 startFrame, const qint64 endFrame,
                        const qint64 pointsPerSample, const int mode)
{
    return QStringLiteral("%1:%2:%3:%4:%5:%6")
        .arg(QString::number(version))
        .arg(path.isEmpty() ? QStringLiteral("noval") : path)
        .arg(startFrame)
        .arg(endFrame)
        .arg(pointsPerSample)
        .arg(mode);
}

qint64 tileFrameSpan(const int renderMode)
{
    return renderMode == 2 ? kViewportTileFrameLimit : kViewportTileFrameLimitMode1;
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
            values.append(value);
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

qint64 spanFromBucket(const qint64 frame, const qint64 totalFrames,
                      const qint64 peakBuckets)
{
    if (totalFrames <= 0 || peakBuckets <= 0) {
        return 0;
    }
    return std::clamp(
        (frame * peakBuckets) / totalFrames, 0LL,
        peakBuckets - 1);
}

qint64 mapFrameToBucket(const qint64 frame, const qint64 frameStart,
                        const qint64 frameEnd, const qint64 totalBuckets)
{
    const qint64 frameCount = std::max<qint64>(1, frameEnd - frameStart);
    const qint64 offset = std::max<qint64>(0, frame - frameStart);
    return std::clamp(
        (offset * totalBuckets) / frameCount, 0LL, std::max<qint64>(0, totalBuckets - 1));
}

void assignBucketRange(const std::vector<float>& sourceValues,
                      const qint64 sourceBuckets,
                      const qint64 totalFrames,
                      const qint64 requestStart,
                      const qint64 requestEnd,
                      const qint64 targetPoints,
                      std::vector<float>& outputMin,
                      std::vector<float>& outputMax)
{
    const qint64 requestFrames = requestEnd - requestStart;
    for (qint64 point = 0; point < targetPoints; ++point) {
        const qint64 pointStart = requestStart + point * requestFrames
            / targetPoints;
        const qint64 pointEnd = requestStart + (point + 1) * requestFrames
            / targetPoints;
        const qint64 bucketStart = spanFromBucket(
            pointStart, totalFrames, sourceBuckets);
        const qint64 bucketEnd = spanFromBucket(
            pointEnd, totalFrames, sourceBuckets);
        const qint64 first = std::min(bucketStart, bucketEnd);
        const qint64 last = std::max(bucketStart, bucketEnd);
        float minimum = 0.0F;
        float maximum = 0.0F;
        bool hasValue = false;
        for (qint64 bucket = first; bucket <= last
             && bucket < sourceBuckets; ++bucket) {
            const qint64 minIndex = bucket * 2;
            const qint64 maxIndex = minIndex + 1;
            if (minIndex + 1 >= static_cast<qint64>(sourceValues.size())
                || maxIndex >= static_cast<qint64>(sourceValues.size())) {
                continue;
            }
            const float minimumCandidate = sourceValues[minIndex];
            const float maximumCandidate = sourceValues[maxIndex];
            if (!hasValue) {
                minimum = minimumCandidate;
                maximum = maximumCandidate;
                hasValue = true;
            } else {
                minimum = std::min(minimum, minimumCandidate);
                maximum = std::max(maximum, maximumCandidate);
            }
        }
        if (!hasValue) {
            outputMin[static_cast<size_t>(point)] = 0.0F;
            outputMax[static_cast<size_t>(point)] = 0.0F;
        } else {
            outputMin[static_cast<size_t>(point)] = minimum;
            outputMax[static_cast<size_t>(point)] = maximum;
        }
    }
}

std::vector<std::vector<float>> viewportPeaksFromSnapshot(
    const std::vector<std::vector<float>>& channelSourcePeaks,
    const std::vector<std::vector<float>>& fallbackPeaks,
    const qint64 totalFrames,
    const qint64 requestStart,
    const qint64 requestEnd,
    const qint64 targetPoints,
    const int channels)
{
    std::vector<std::vector<float>> result;
    result.resize(static_cast<std::size_t>(std::max<qint64>(0, channels)));
    const std::vector<std::vector<float>>* source = nullptr;
    const auto& sourceVector = channelSourcePeaks.empty() ? fallbackPeaks
                                                         : channelSourcePeaks;
    if (!sourceVector.empty()) {
        source = &sourceVector;
    } else {
        result.assign(static_cast<std::size_t>(std::max<qint64>(0, channels)),
                      std::vector<float>(static_cast<std::size_t>(targetPoints * 2U),
                                        0.0F));
        return result;
    }
    const qint64 requestFrames = std::max<qint64>(1, requestEnd - requestStart);
    const qint64 availableChannels = static_cast<qint64>(source->size());
    for (qint64 channelIndex = 0; channelIndex < channels; ++channelIndex) {
        const qint64 safeChannel = std::clamp(
            channelIndex, 0LL, availableChannels - 1 >= 0 ? availableChannels - 1 : 0);
        const auto& sourceChannelRaw = (*source)[static_cast<std::size_t>(safeChannel)];
        if (sourceChannelRaw.empty()) {
            result[static_cast<std::size_t>(channelIndex)].assign(
                static_cast<std::size_t>(targetPoints * 2LL), 0.0F);
            continue;
        }
        const qint64 bucketCount = static_cast<qint64>(sourceChannelRaw.size() / 2);
        auto& minOut = result[static_cast<std::size_t>(channelIndex)];
        auto& maxOut = minOut;
        result[static_cast<std::size_t>(channelIndex)].assign(
            static_cast<std::size_t>(targetPoints * 2LL), 0.0F);
        (void)maxOut;
        std::vector<float> localMin(static_cast<std::size_t>(targetPoints), 0.0F);
        std::vector<float> localMax(static_cast<std::size_t>(targetPoints), 0.0F);
        assignBucketRange(sourceChannelRaw, bucketCount, totalFrames,
                         requestStart, requestEnd, targetPoints, localMin, localMax);
        for (qint64 point = 0; point < targetPoints; ++point) {
            const float minimum = localMin[static_cast<std::size_t>(point)];
            const float maximum = localMax[static_cast<std::size_t>(point)];
            const qint64 sampleIndex = point * 2LL;
            result[static_cast<std::size_t>(channelIndex)]
                  [static_cast<std::size_t>(sampleIndex)] = minimum;
            result[static_cast<std::size_t>(channelIndex)]
                  [static_cast<std::size_t>(sampleIndex + 1)] = maximum;
        }
    }
    (void)requestFrames;
    return result;
}

struct TileRangeData final {
    qint64 start_frame{};
    qint64 end_frame{};
    std::vector<std::vector<float>> channels;
};

struct ViewportWaveformJobResult final {
    std::vector<std::vector<float>> peaks;
    std::vector<TileRangeData> decoded_tiles;
};

std::vector<std::vector<float>> mergeResampledFromTileData(
    const std::vector<std::vector<float>>& sourceData,
    const qint64 sourceStartFrame,
    const qint64 visibleStartFrame,
    const qint64 visibleEndFrame,
    const qint64 visibleFrames,
    const qint64 targetPoints,
    const int channels,
    std::vector<std::vector<float>> output)
{
    if (sourceData.empty() || visibleFrames <= 0 || targetPoints <= 0
        || visibleEndFrame <= visibleStartFrame) {
        return output;
    }
    if (output.empty()) {
        output = std::vector<std::vector<float>>(
            static_cast<std::size_t>(std::max<qint64>(0, channels)),
            std::vector<float>(static_cast<std::size_t>(targetPoints * 2LL), 0.0F));
    }

    for (std::size_t channelIndex = 0; channelIndex < output.size();
         ++channelIndex) {
        auto& channel = output[channelIndex];
        if (channel.size() != static_cast<std::size_t>(targetPoints * 2LL)) {
            channel.assign(static_cast<std::size_t>(targetPoints * 2LL), 0.0F);
        }
        for (qint64 point = 0; point < targetPoints; ++point) {
            const auto minIndex = static_cast<std::size_t>(point * 2LL);
            channel[minIndex] = std::numeric_limits<float>::infinity();
            channel[minIndex + 1U] = -std::numeric_limits<float>::infinity();
        }
    }

    for (std::size_t channelIndex = 0;
         channelIndex < output.size() && channelIndex < sourceData.size();
         ++channelIndex) {
        const std::vector<float>& source = sourceData[channelIndex];
        if (source.empty()) continue;
        for (qint64 sampleIndex = 0;
             sampleIndex < static_cast<qint64>(source.size());
             ++sampleIndex) {
            const qint64 frame = sourceStartFrame + sampleIndex;
            if (frame < visibleStartFrame || frame >= visibleEndFrame) {
                continue;
            }
            const qint64 local = frame - visibleStartFrame;
            const qint64 point = std::clamp<qint64>(
                (local * targetPoints) / visibleFrames, 0, targetPoints - 1);
            const float value = source[static_cast<std::size_t>(sampleIndex)];
            auto& target = output[static_cast<std::size_t>(channelIndex)];
            const auto minIndex = static_cast<std::size_t>(point * 2);
            if (!std::isfinite(value)) continue;
            target[minIndex] = std::min(target[minIndex], value);
            target[minIndex + 1U] = std::max(target[minIndex + 1U], value);
        }
    }
    for (qint64 point = 0; point < targetPoints; ++point) {
        const auto minIndex = static_cast<std::size_t>(point * 2);
        for (std::size_t channelIndex = 0; channelIndex < output.size();
             ++channelIndex) {
            const auto minIdx = minIndex;
            const auto maxIdx = minIdx + 1U;
            auto& channelResult = output[channelIndex];
            if (!std::isfinite(channelResult[minIdx])
                || !std::isfinite(channelResult[maxIdx])
                || channelResult[minIdx] > channelResult[maxIdx]) {
                channelResult[minIdx] = 0.0F;
                channelResult[maxIdx] = 0.0F;
            }
        }
    }
    return output;
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
    std::vector<std::vector<float>> peaks(
        static_cast<std::size_t>(std::max<qint64>(1, channels)),
        std::vector<float>(static_cast<std::size_t>(targetPoints * 2LL), 0.0F));
    const float amplitude = std::clamp(static_cast<float>(recPeak), 0.0F, 1.0F);
    if (amplitude <= 0.0F) return peaks;
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

std::vector<std::vector<float>> decodeViewportTileSamples(
    const QString& path,
    const qint64 startFrame,
    const qint64 endFrame,
    const int channels,
    const int sampleRate,
    const std::shared_ptr<std::atomic_bool> cancelToken)
{
    std::vector<std::vector<float>> empty;
    if (path.isEmpty() || startFrame >= endFrame || channels <= 0
        || sampleRate <= 0 || startFrame < 0) {
        return empty;
    }
    const qint64 frameCount = endFrame - startFrame;
    if (frameCount <= 0) return empty;

    agplayer::Decoder decoder;
    if (decoder.open(path.toStdString()) != AG_OK) {
        return empty;
    }
    if (decoder.seek(std::llround(static_cast<long double>(startFrame)
                                 * 1000.0L / static_cast<long double>(sampleRate)))
        != AG_OK) {
        decoder.close();
        return empty;
    }
    if (cancelToken && cancelToken->load(std::memory_order_acquire)) {
        decoder.close();
        return empty;
    }

    std::vector<std::vector<float>> result(
        static_cast<std::size_t>(channels),
        std::vector<float>());
    for (auto& channel : result) {
        channel.reserve(static_cast<std::size_t>(frameCount));
    }

    qint64 currentFrame = startFrame;
    qint64 consumed = 0;
    agplayer::DecodedAudioBlock block;
    while (consumed < frameCount
           && !cancelToken->load(std::memory_order_acquire)) {
        if (decoder.read(block) != AG_OK) break;
        if (block.frames == 0U) {
            if (block.end_of_stream) break;
            continue;
        }

        qint64 blockFrameStart = block.timestamp_ms >= 0
            ? static_cast<qint64>(std::llround(static_cast<long double>(block.timestamp_ms)
                                               * sampleRate / 1000.0L))
            : currentFrame;
        const qint64 blockFrameEnd = blockFrameStart + static_cast<qint64>(block.frames);
        if (blockFrameEnd <= startFrame) {
            if (block.end_of_stream) break;
            currentFrame = blockFrameEnd;
            continue;
        }

        const qint64 localStart = std::max(blockFrameStart, startFrame);
        const qint64 localEnd = std::min(blockFrameEnd, endFrame);
        if (localEnd <= localStart) {
            if (block.end_of_stream) break;
            currentFrame = blockFrameEnd;
            continue;
        }

        const qint64 dropFrames = localStart - blockFrameStart;
        const qint64 takeFrames = std::min(localEnd - localStart, frameCount - consumed);
        const qint64 takeStart = dropFrames * static_cast<qint64>(channels);
        const qint64 takeCount = takeFrames * static_cast<qint64>(channels);
        if (takeStart < 0 || static_cast<std::size_t>(takeStart) >= block.samples.size()) {
            if (block.end_of_stream) break;
            currentFrame = blockFrameEnd;
            continue;
        }

        const std::size_t localStartIndex = static_cast<std::size_t>(takeStart);
        const std::size_t localEndIndex = static_cast<std::size_t>(
            std::min<std::size_t>(block.samples.size(),
                                  static_cast<std::size_t>(takeStart + takeCount)));
        for (std::size_t sampleBase = localStartIndex;
             sampleBase < localEndIndex;
             sampleBase += static_cast<std::size_t>(channels)) {
            const qint64 globalFrame = currentFrame
                + static_cast<qint64>(
                    (sampleBase - localStartIndex) / static_cast<std::size_t>(channels));
            if (globalFrame < startFrame || globalFrame >= endFrame) {
                continue;
            }
            for (int channel = 0; channel < channels; ++channel) {
                const std::size_t sampleIndex = sampleBase + static_cast<std::size_t>(channel);
                if (sampleIndex >= localEndIndex) break;
                result[static_cast<std::size_t>(channel)].push_back(block.samples[sampleIndex]);
            }
            ++consumed;
            if (consumed >= frameCount) {
                break;
            }
        }
        currentFrame += static_cast<qint64>(block.frames);
        if (block.end_of_stream || consumed >= frameCount) {
            break;
        }
    }
    for (auto& channel : result) {
        if (channel.size() > static_cast<std::size_t>(frameCount)) {
            channel.resize(static_cast<std::size_t>(frameCount));
        }
    }
    decoder.close();
    return result;
}

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

struct AudioEditorController::PreviewRenderResult final {
    bool success{};
    bool processed{};
    std::uint64_t generation{};
    QString path;
    QString error;
};

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
    viewport_cache_size_limit_ = kViewportWaveformCacheDefaultLimit;
    connect(&viewport_, &EditorViewport::viewportChanged, this,
            &AudioEditorController::requestViewportWaveform);
    recording_timer_.setInterval(33);
    connect(&recording_timer_, &QTimer::timeout, this, [this] {
        QVariantList points;
        const std::vector<float> peaks = recording_session_.recentPeaks(640);
        points.reserve(static_cast<qsizetype>(peaks.size() * 2));
        for (const float peak : peaks) {
            points.append(-peak);
            points.append(peak);
        }
        live_recording_peaks_.clear();
        for (int channel = 0; channel < (std::max)(1, recording_channels_); ++channel) {
            live_recording_peaks_.append(QVariant::fromValue(points));
        }
        position_ms_ = recording_sample_rate_ > 0
            ? recordingFrames() * 1'000 / recording_sample_rate_ : 0;
        viewport_.setDocumentFrames((std::max<qint64>)(1, recordingFrames()));
        emit waveformChanged();
        emit playbackChanged();
        emit recordingChanged();
        emit playbackChanged();
    });
    QSettings settings;
    settings.beginGroup(QStringLiteral("audioEditor"));
    recording_directory_ = settings.value(
        QStringLiteral("recordingDirectory")).toString();
    recording_device_id_ = settings.value(
        QStringLiteral("recordingDeviceId")).toString();
    recording_sample_rate_ = settings.value(
        QStringLiteral("recordingSampleRate"), 48'000).toInt();
    recording_channels_ = settings.value(
        QStringLiteral("recordingChannels"), 2).toInt();
    recording_monitor_ = settings.value(
        QStringLiteral("recordingMonitor"), false).toBool();
    settings.endGroup();
    if (!recording_directory_.isEmpty()) {
        (void)agplayer::editor::RecordingSession::recoverIncomplete(
            std::filesystem::path(recording_directory_.toStdWString()));
    }
    export_formats_ = buildExportFormats();
    refreshRecordingDevices();
    refreshActions();
}

AudioEditorController::~AudioEditorController()
{
    cancelOperation();
    if (write_watcher_) write_watcher_->future().waitForFinished();
    if (time_pitch_watcher_) time_pitch_watcher_->future().waitForFinished();
    if (recording_start_watcher_) recording_start_watcher_->future().waitForFinished();
    if (recording_stop_watcher_) recording_stop_watcher_->future().waitForFinished();
    if (bpm_watcher_) bpm_watcher_->future().waitForFinished();
    if (preview_watcher_) preview_watcher_->future().waitForFinished();
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

QVariantList AudioEditorController::channelPeaks() const
{
    return recording() && !live_recording_peaks_.isEmpty()
        ? live_recording_peaks_ : channel_peaks_;
}

QVariantList AudioEditorController::buildExportFormats() const
{
    struct Candidate final {
        const char* key;
        const char* label;
        const char* extension;
        const char* codec;
        const char* filter;
    };
    static constexpr Candidate candidates[]{
        {"wav", QT_TRANSLATE_NOOP("AudioEditorController", "WAV PCM 24-bit"),
         "wav", "pcm_s24le", QT_TRANSLATE_NOOP("AudioEditorController", "WAV 音频 (*.wav)")},
        {"flac", QT_TRANSLATE_NOOP("AudioEditorController", "FLAC 无损"),
         "flac", "flac", QT_TRANSLATE_NOOP("AudioEditorController", "FLAC 音频 (*.flac)")},
        {"mp3", "MP3", "mp3", "libmp3lame",
         QT_TRANSLATE_NOOP("AudioEditorController", "MP3 音频 (*.mp3)")},
        {"m4a", "AAC", "m4a", "aac",
         QT_TRANSLATE_NOOP("AudioEditorController", "AAC 音频 (*.m4a)")},
        {"ogg", "Ogg Vorbis", "ogg", "libvorbis", "Ogg Vorbis (*.ogg)"},
        {"opus", "Opus", "opus", "libopus",
         QT_TRANSLATE_NOOP("AudioEditorController", "Opus 音频 (*.opus)")},
    };
    const auto capabilities = agplayer::transcode_capabilities();
    QVariantList result;
    for (const Candidate& candidate : candidates) {
        const auto* capability = agplayer::find_transcode_capability(
            capabilities, candidate.key);
        if (capability == nullptr || !capability->available
            || ag_encoder_available(candidate.codec) == 0) {
            continue;
        }
        QVariantList rates;
        for (const int rate : capability->sample_rates) rates.append(rate);
        result.append(QVariantMap{
            {QStringLiteral("text"), tr(candidate.label)},
            {QStringLiteral("extension"), QString::fromLatin1(candidate.extension)},
            {QStringLiteral("codec"), QString::fromLatin1(candidate.codec)},
            {QStringLiteral("filter"), tr(candidate.filter)},
            {QStringLiteral("lossy"), capability->lossy},
            {QStringLiteral("supportsMetadata"), capability->supports_metadata
                && std::string_view(candidate.key) != "wav"},
            {QStringLiteral("sampleRates"), rates},
        });
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
    if (modified_ && !allow_document_replace_) {
        pending_open_url_ = source;
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
    playback_path_ = path;
    format_name_ = QString::fromStdString(analysis.format).toUpper();
    sample_rate_ = static_cast<int>(analysis.source.sample_rate);
    channels_ = static_cast<int>(analysis.source.channels);
    bits_per_sample_ = analysis.bits_per_sample;
    bit_rate_ = analysis.bit_rate;
    source_channel_peaks_ = to_variant_peaks(analysis.channel_peaks);
    channel_peaks_ = source_channel_peaks_;
    clearViewportWaveformCache();
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

bool AudioEditorController::confirmDiscardAndOpen()
{
    if (!pending_open_url_.isValid()) return false;
    const QUrl source = pending_open_url_;
    pending_open_url_.clear();
    allow_document_replace_ = true;
    return openFile(source);
}

void AudioEditorController::cancelDiscardAndOpen()
{
    pending_open_url_.clear();
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
    if (busy()) return false;
    stopPlayback();
    setState(EditorSessionState::Saving);
    setProgress(0.0);
    WriteRequest request;
    request.snapshot = document_.timelineSnapshot();
    request.output_path = std::filesystem::path(path.toStdWString());
    operation_cancelled_.store(false, std::memory_order_release);
    auto* watcher = new QFutureWatcher<agplayer::editor::WriteResult>(this);
    write_watcher_ = watcher;
    connect(watcher, &QFutureWatcher<agplayer::editor::WriteResult>::finished,
            this, [this, watcher, path] {
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
        source_path_ = path;
        playback_path_ = path;
        modified_ = false;
        setProgress(1.0);
        setState(EditorSessionState::Ready);
        setError({});
        refreshActions();
        emit documentChanged();
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

bool AudioEditorController::exportTo(
    const QUrl& target, const bool selectionOnly, const QString& codecName,
    const int sampleRate, const int channels, const qint64 bitRate,
    const bool keepMetadata, const bool variableBitRate, const int quality)
{
    const QString path = local_path(target);
    const auto selection = document_.selection();
    if (!has_document_ || path.isEmpty() || (selectionOnly && !selection)) {
        setError(tr("导出范围或路径无效"));
        return false;
    }
    const bool validSampleRate = sampleRate == 0
        || (sampleRate >= 8'000 && sampleRate <= 384'000);
    const bool validChannels = channels >= 0 && channels <= 2;
    const bool validBitRate = bitRate >= 0 && bitRate <= 1'536'000;
    if (!validSampleRate || !validChannels || !validBitRate
        || quality < 0 || quality > 100) {
        setError(tr("导出参数无效"));
        return false;
    }
    if (busy()) return false;
    setState(EditorSessionState::Exporting);
    setProgress(0.0);
    WriteRequest request;
    request.snapshot = document_.timelineSnapshot();
    request.output_path = std::filesystem::path(path.toStdWString());
    request.codec_name = codecName.toStdString();
    if (!source_path_.isEmpty()) {
        request.metadata_source_path = std::filesystem::path(
            source_path_.toStdWString());
    }
    request.sample_rate = sampleRate;
    request.channels = channels;
    request.bit_rate = bitRate;
    request.keep_metadata = keepMetadata;
    request.variable_bit_rate = variableBitRate;
    request.quality = quality;
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

void AudioEditorController::cancelOperation()
{
    operation_cancelled_.store(true, std::memory_order_release);
    preview_generation_.fetch_add(1, std::memory_order_acq_rel);
}

bool AudioEditorController::moveEvent(const quint64 id, const qint64 timelineStart)
{
    if (!has_document_ || busy()
        || !document_.moveEvent(static_cast<agplayer::editor::EventId>(id),
                                timelineStart)) return false;
    modified_ = true;
    playback_path_.clear();
    viewport_.setDocumentFrames(document_.totalFrames());
    clearViewportWaveformCache();
    refreshActions();
    emit waveformChanged();
    emit documentChanged();
    return true;
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
    modified_ = true;
    playback_path_.clear();
    viewport_.setDocumentFrames(document_.totalFrames());
    clearViewportWaveformCache();
    refreshActions();
    emit waveformChanged();
    emit documentChanged();
    return true;
}

bool AudioEditorController::splitEvent(const quint64 id, const qint64 frame)
{
    if (!has_document_ || busy()
        || !document_.splitEventAt(static_cast<agplayer::editor::EventId>(id), frame)) {
        return false;
    }
    modified_ = true;
    playback_path_.clear();
    clearViewportWaveformCache();
    refreshActions();
    emit waveformChanged();
    emit documentChanged();
    return true;
}

bool AudioEditorController::mergeEvents(const quint64 left, const quint64 right)
{
    if (!has_document_ || busy()
        || !document_.mergeEvents(static_cast<agplayer::editor::EventId>(left),
                                  static_cast<agplayer::editor::EventId>(right))) {
        return false;
    }
    modified_ = true;
    playback_path_.clear();
    clearViewportWaveformCache();
    refreshActions();
    emit waveformChanged();
    emit documentChanged();
    return true;
}

bool AudioEditorController::detectBpm()
{
    if (!has_document_ || busy() || bpm_watcher_) return false;
    if (!preview_directory_.isValid()) return false;
    const auto snapshot = document_.timelineSnapshot();
    const QString path = preview_directory_.filePath(QStringLiteral("bpm-analysis.wav"));
    setState(EditorSessionState::Processing);
    setError({});
    auto* watcher = new QFutureWatcher<BpmAnalyzeResult>(this);
    bpm_watcher_ = watcher;
    connect(watcher, &QFutureWatcher<BpmAnalyzeResult>::finished,
            this, [this, watcher] {
        bpm_watcher_ = nullptr;
        const BpmAnalyzeResult result = watcher->result();
        watcher->deleteLater();
        setState(EditorSessionState::Ready);
        if (result.bpm <= 0.0) {
            setError(tr("BPM 检测失败"));
            return;
        }
        time_pitch_.setOriginalBpm(result.bpm);
        setError({});
        emit timePitchChanged();
    });
    watcher->setFuture(QtConcurrent::run([snapshot, path] {
        const auto rendered = DocumentRenderer{}.renderFloatWav(
            snapshot, std::nullopt,
            std::filesystem::path(path.toStdWString()));
        return rendered.success ? analyze_bpm(path) : BpmAnalyzeResult{};
    }));
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
    recording_insert_frame_ = position_ms_ * sample_rate_ / 1'000;
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
    if (!preview_directory_.isValid()) {
        setError(tr("无法创建录音临时目录"));
        return false;
    }
    const QString path = preview_directory_.filePath(
        QStringLiteral("recording-%1.wav").arg(
            QDateTime::currentMSecsSinceEpoch()));
    return startRecording(QUrl::fromLocalFile(path), deviceId,
                          recordingSampleRate, recordingChannels,
                          monitor, insertAtCursor);
}

bool AudioEditorController::createRecordingDocument(
    const quint32 sampleRate, const quint32 channels)
{
    return createUntitledDocument(sampleRate, channels, 1);
}

bool AudioEditorController::clearDocument()
{
    if (recording() || busy()) return false;
    stopPlayback();
    document_ = AudioDocument{};
    source_path_.clear();
    playback_path_.clear();
    format_name_.clear();
    sample_rate_ = 0;
    channels_ = 0;
    bits_per_sample_ = 0;
    bit_rate_ = 0;
    source_channel_peaks_.clear();
    channel_peaks_.clear();
    has_document_ = false;
    modified_ = false;
    position_ms_ = 0;
    viewport_.setDocumentFrames(0);
    setState(EditorSessionState::Empty);
    refreshActions();
    emit waveformChanged();
    emit playbackChanged();
    emit documentChanged();
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
            if (!document_.insertSource(analysis.source, recording_insert_frame_)) {
                setState(EditorSessionState::Error);
                setError(tr("录音完成，但无法插入当前文档"));
                emit recordingChanged();
                return;
            }
            modified_ = true;
            playback_path_.clear();
            viewport_.setDocumentFrames(document_.totalFrames());
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
    clearViewportWaveformCache();
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
    if (id == QStringLiteral("editor.split")) {
        const qint64 frame = position_ms_ * sample_rate_ / 1'000;
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
        const qint64 frame = position_ms_ * sample_rate_ / 1'000;
        changed = document_.pasteAt(frame);
    }
    else if (id == QStringLiteral("editor.deleteSelection")) changed = document_.deleteSelection();
    if (!changed) return false;
    if (id != QStringLiteral("editor.copy")) {
        stopPlayback();
        modified_ = true;
        playback_path_.clear();
        clearViewportWaveformCache();
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
        if (preview_watcher_) return true;
        if (!preview_directory_.isValid()) {
            setError(tr("无法创建预览目录"));
            return false;
        }
        const bool processed = std::abs(time_pitch_.speedPercent() - 100.0) > 0.001
            || time_pitch_.pitchCents() != 0;
        const QString path = preview_directory_.filePath(
            processed ? QStringLiteral("time-pitch-preview.wav")
                      : QStringLiteral("preview.wav"));
        const std::uint64_t generation = preview_generation_.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        const auto snapshot = document_.timelineSnapshot();
        const auto parameters = time_pitch_;
        setState(EditorSessionState::Processing);
        auto* watcher = new QFutureWatcher<PreviewRenderResult>(this);
        preview_watcher_ = watcher;
        connect(watcher, &QFutureWatcher<PreviewRenderResult>::finished,
                this, [this, watcher] {
            preview_watcher_ = nullptr;
            const PreviewRenderResult result = watcher->result();
            watcher->deleteLater();
            if (result.generation != preview_generation_.load(
                    std::memory_order_acquire)) {
                setState(EditorSessionState::Ready);
                return;
            }
            if (!result.success) {
                setState(EditorSessionState::Error);
                setError(result.error);
                return;
            }
            playback_path_ = result.path;
            time_pitch_preview_active_ = result.processed;
            emit timePitchChanged();
            startPreparedPlayback();
        });
        watcher->setFuture(QtConcurrent::run(
            [snapshot, parameters, path, processed, generation] {
            PreviewRenderResult result;
            result.generation = generation;
            result.processed = processed;
            result.path = path;
            if (processed) {
                const auto outcome = parameters.process(
                    snapshot, std::filesystem::path(path.toStdWString()));
                result.success = outcome.success;
                result.error = QString::fromStdString(outcome.message);
            } else {
                const auto outcome = DocumentRenderer{}.renderFloatWav(
                    snapshot, std::nullopt,
                    std::filesystem::path(path.toStdWString()));
                result.success = outcome.success;
                result.error = QString::fromStdString(outcome.message);
            }
            return result;
        }));
        return true;
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
    if (main_playback_ != nullptr
        && main_playback_->state() != PlaybackController::Stopped) {
        main_playback_->stop();
    }
    const auto selection = document_.selection();
    if (selection && sample_rate_ > 0) {
        const qint64 start = selection->start * 1'000 / sample_rate_;
        const qint64 end = selection->end * 1'000 / sample_rate_;
        if (position_ms_ < start || position_ms_ >= end) {
            position_ms_ = start;
        }
    }
    ag_playback_snapshot snapshot{};
    if (ag_player_snapshot(player_, &snapshot) != AG_OK
        || snapshot.state == AG_STOPPED || snapshot.state == AG_ERROR) {
        if (!preparePlayback()) return false;
        if (preview_watcher_) return true;
    }
    if (ag_player_play(player_) != AG_OK) return false;
    playing_ = true;
    playback_timer_.start();
    setState(EditorSessionState::Playing);
    emit playbackChanged();
    return true;
}

void AudioEditorController::startPreparedPlayback()
{
    if (!player_ || playback_path_.isEmpty()) return;
    if (ag_player_load(player_, playback_path_.toUtf8().constData()) != AG_OK) {
        setState(EditorSessionState::Error);
        setError(tr("无法载入编辑预览"));
        return;
    }
    ag_player_set_volume(player_, static_cast<float>(volume_));
    const qint64 preview_position = time_pitch_preview_active_
        ? static_cast<qint64>(std::llround(
            static_cast<double>(position_ms_) * 100.0
            / time_pitch_.speedPercent()))
        : position_ms_;
    if (preview_position > 0) ag_player_seek(player_, preview_position);
    if (ag_player_play(player_) != AG_OK) {
        setState(EditorSessionState::Error);
        setError(tr("无法开始编辑预览"));
        return;
    }
    playing_ = true;
    playback_timer_.start();
    setState(EditorSessionState::Playing);
    emit playbackChanged();
}

void AudioEditorController::setMainPlaybackController(
    PlaybackController* playback) noexcept
{
    main_playback_ = playback;
}

bool AudioEditorController::stopPlayback()
{
    if (!player_) return false;
    if (preview_watcher_) {
        preview_generation_.fetch_add(1, std::memory_order_acq_rel);
    }
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
    refreshActions();
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
    const auto selection = document_.selection();
    if (selection && sample_rate_ > 0) {
        const qint64 start = selection->start * 1'000 / sample_rate_;
        const qint64 end = selection->end * 1'000 / sample_rate_;
        if (position_ms_ >= end) {
            {
                const qint64 previewStart = time_pitch_preview_active_
                    ? static_cast<qint64>(std::llround(
                        static_cast<double>(start) * 100.0
                        / time_pitch_.speedPercent()))
                    : start;
                ag_player_seek(player_, previewStart);
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

QVariantList AudioEditorController::toVariantPeaks(
    const std::vector<std::vector<float>>& channels) const
{
    return build_variant_peaks(channels);
}

QString AudioEditorController::activeWaveformCacheKey(
    const qint64 startFrame, const qint64 endFrame,
    const qint64 targetPointCount, const int mode) const
{
    return QStringLiteral("%1-%2-%3-%4-%5")
        .arg(startFrame)
        .arg(endFrame)
        .arg(targetPointCount)
        .arg(mode)
        .arg(viewport_cache_version_);
}

void AudioEditorController::clearViewportWaveformCache()
{
    viewport_cache_size_bytes_ = 0;
    viewport_waveform_cache_.clear();
    viewport_waveform_lru_.clear();
    ++viewport_cache_version_;
    viewport_channel_peaks_.clear();
}

void AudioEditorController::requestViewportWaveform()
{
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
        ? 0 : viewportTargetPoints(
            viewportRenderMode(std::max<qreal>(1.0, static_cast<qreal>(visibleFrames))
                              / std::max<qreal>(1.0, viewportWidth)),
            visibleFrames, viewportWidth);
    if ((!has_document_ && !recording())
        || clampedTotal <= 0 || channels_ <= 0 || visibleFrames <= 0
        || targetPoints <= 0) {
        viewport_channel_peaks_.clear();
        emit waveformChanged();
        return;
    }

    const QVariantList sourcePeaksData = !channel_peaks_.isEmpty()
        ? channel_peaks_ : source_channel_peaks_;
    const auto sourcePeaks = peaksAsChannels(sourcePeaksData);
    const auto fallbackPeaks = peaksAsChannels(source_channel_peaks_);
    const bool hasSourcePeaks = !sourcePeaks.empty();
    const bool hasFallbackPeaks = !fallbackPeaks.empty();
    if (!hasSourcePeaks && !hasFallbackPeaks && !recording()) {
        viewport_channel_peaks_.clear();
        emit waveformChanged();
        return;
    }
    const bool recordingActive = recording();
    const qint64 recordingStartFrame = insert_recording_at_cursor_ && has_document_
        ? recording_insert_frame_ : 0;
    const qreal recordingPeak = static_cast<qreal>(recording_session_.peak());

    const int renderMode = viewportRenderMode(
        std::max<qreal>(1.0, static_cast<qreal>(visibleFrames))
        / std::max<qreal>(1.0, viewportWidth));
    if (renderMode < 2) {
        const quint64 generation = ++viewport_waveform_generation_;
        const QString key = waveformCacheKey(
            source_path_, viewport_cache_version_, startFrame, endFrame,
            targetPoints, renderMode);
        if (const auto cache_it = viewport_waveform_cache_.find(key);
            cache_it != viewport_waveform_cache_.end()) {
            const auto cached = cache_it->second;
            if (cached && !cached->channels.empty()) {
                viewport_waveform_lru_.splice(viewport_waveform_lru_.begin(),
                                              viewport_waveform_lru_,
                                              cached->lru_iterator);
                viewport_channel_peaks_ = toVariantPeaks(cached->channels);
                emit waveformChanged();
                return;
            }
        }

        if (viewport_waveform_cancel_token_) {
            viewport_waveform_cancel_token_->store(true,
                                                  std::memory_order_release);
        }
        const auto cancel_token = std::make_shared<std::atomic_bool>(false);
        viewport_waveform_cancel_token_ = cancel_token;

        if (viewport_waveform_watcher_) {
            viewport_waveform_watcher_->disconnect(this);
            viewport_waveform_watcher_->deleteLater();
            viewport_waveform_watcher_ = nullptr;
        }

        auto* watcher = new QFutureWatcher<QVariantList>(this);
        viewport_waveform_watcher_ = watcher;
        connect(watcher, &QFutureWatcher<QVariantList>::finished, this,
                [this, watcher, generation, cancel_token, key, startFrame, endFrame,
                 targetPoints, renderMode] {
            viewport_waveform_watcher_ = nullptr;
            if (cancel_token->load(std::memory_order_acquire)
                || generation != viewport_waveform_generation_) {
                watcher->deleteLater();
                return;
            }
            const auto peaks = watcher->result();
            const auto channels = peaksAsChannels(peaks);
            if (!channels.empty()) {
                const qint64 bytes = viewportCacheBytes(channels);
                auto entry = std::make_shared<ViewportWaveformCacheEntry>();
                entry->start_frame = startFrame;
                entry->end_frame = endFrame;
                entry->source_start_frame = 0;
                entry->target_frames_per_point = targetPoints;
                entry->mode = renderMode;
                entry->sample_rate = sample_rate_;
                entry->bytes = bytes;
                entry->channels = channels;
                if (const auto existing_it = viewport_waveform_cache_.find(key);
                    existing_it != viewport_waveform_cache_.end()) {
                    viewport_cache_size_bytes_ -= existing_it->second->bytes;
                    viewport_waveform_lru_.erase(existing_it->second->lru_iterator);
                    viewport_waveform_cache_.erase(existing_it);
                }
                viewport_waveform_cache_.insert_or_assign(key, entry);
                viewport_waveform_lru_.push_front(key);
                entry->lru_iterator = viewport_waveform_lru_.begin();
                viewport_cache_size_bytes_ += bytes;
                while (viewport_cache_size_bytes_ > viewport_cache_size_limit_
                       && !viewport_waveform_lru_.empty()) {
                    const QString stale_key = viewport_waveform_lru_.back();
                    viewport_waveform_lru_.pop_back();
                    if (const auto stale_it =
                            viewport_waveform_cache_.find(stale_key);
                        stale_it != viewport_waveform_cache_.end()) {
                        viewport_cache_size_bytes_ -= stale_it->second->bytes;
                        viewport_waveform_cache_.erase(stale_it);
                    }
                }
            }
            watcher->deleteLater();
            viewport_channel_peaks_ = peaks;
            emit waveformChanged();
        });

        watcher->setFuture(QtConcurrent::run(
            [this, startFrame, endFrame, targetPoints, renderMode, sourcePeaks,
             fallbackPeaks, channels = channels_, totalFrames, recordingActive,
             recordedFrames, recStart = recording_insert_frame_,
             recAtCursor = insert_recording_at_cursor_,
             recPeak = static_cast<qreal>(recording_session_.peak()),
             cancel_token] {
                if (cancel_token->load(std::memory_order_acquire)) {
                    return QVariantList{};
                }
                if (!recordingActive && sourcePeaks.empty() && fallbackPeaks.empty()) {
                    return QVariantList{};
                }
                if (sourcePeaks.empty() && fallbackPeaks.empty() && recordingActive) {
                    const qint64 recStartFrame = recAtCursor ? recStart : 0;
                    return build_variant_peaks(buildRecordingPlaceholderPeaks(
                        recPeak, channels, targetPoints, startFrame, endFrame,
                        recStartFrame, recordedFrames));
                }
                const auto peaks = viewportPeaksFromSnapshot(
                    sourcePeaks, fallbackPeaks, totalFrames, startFrame, endFrame,
                    targetPoints, channels);
                return build_variant_peaks(peaks);
            }));
        return;
    }

    const qint64 tileSpan = tileFrameSpan(renderMode);
    const qint64 prefetchStart = std::max<qint64>(
        0, startFrame - tileSpan);
    const qint64 prefetchEnd = std::min<qint64>(endFrame + tileSpan, clampedTotal);
    const qint64 cacheFramesPerPoint = std::max<qint64>(1, tileSpan);
    struct CachedTileView final {
        qint64 start_frame{};
        qint64 end_frame{};
        std::vector<std::vector<float>> channels;
    };
    std::vector<CachedTileView> cached_tiles;
    std::vector<std::pair<qint64, qint64>> missing_tiles;

    const qint64 alignedStart = (prefetchStart / tileSpan) * tileSpan;
    for (qint64 tileStart = alignedStart; tileStart < prefetchEnd; tileStart += tileSpan) {
        const qint64 tileEnd = std::min<qint64>(tileStart + tileSpan, clampedTotal);
        if (tileEnd <= tileStart) continue;
        const QString tile_key = waveformCacheKey(
            source_path_, viewport_cache_version_, tileStart, tileEnd,
            cacheFramesPerPoint, renderMode);
        if (const auto cache_it = viewport_waveform_cache_.find(tile_key);
            cache_it != viewport_waveform_cache_.end()) {
            const auto cached = cache_it->second;
            if (cached && cached->mode == 2 && cached->sample_rate == sample_rate_
                && cached->source_start_frame == tileStart && !cached->channels.empty()) {
                viewport_waveform_lru_.splice(viewport_waveform_lru_.begin(),
                                              viewport_waveform_lru_,
                                              cached->lru_iterator);
                cached_tiles.push_back(
                    {cached->source_start_frame, cached->end_frame,
                     cached->channels});
                continue;
            }
        }
        missing_tiles.push_back(std::make_pair(tileStart, tileEnd));
    }

    const quint64 generation = ++viewport_waveform_generation_;
    if (source_path_.isEmpty() && !recordingActive) {
        viewport_channel_peaks_.clear();
        emit waveformChanged();
        return;
    }

    if (viewport_waveform_cancel_token_) {
        viewport_waveform_cancel_token_->store(true,
                                              std::memory_order_release);
    }
    const auto cancel_token = std::make_shared<std::atomic_bool>(false);
    viewport_waveform_cancel_token_ = cancel_token;

    if (viewport_waveform_watcher_) {
        viewport_waveform_watcher_->disconnect(this);
        viewport_waveform_watcher_->deleteLater();
        viewport_waveform_watcher_ = nullptr;
    }

    auto* watcher = new QFutureWatcher<ViewportWaveformJobResult>(this);
    viewport_waveform_watcher_ = watcher;
    connect(watcher, &QFutureWatcher<ViewportWaveformJobResult>::finished, this,
            [this, watcher, generation, cancel_token, startFrame, endFrame,
             targetPoints, renderMode, cacheFramesPerPoint, sourcePath = source_path_,
             recActive = recordingActive, recPeak = recordingPeak,
             recFrames = recordedFrames, recStart = recordingStartFrame] {
        viewport_waveform_watcher_ = nullptr;
        if (cancel_token->load(std::memory_order_acquire)
            || generation != viewport_waveform_generation_) {
            watcher->deleteLater();
            return;
        }

        const ViewportWaveformJobResult jobResult = watcher->result();
        if (!jobResult.decoded_tiles.empty()) {
            for (const auto& tile : jobResult.decoded_tiles) {
                const QString tileKey = waveformCacheKey(
                    sourcePath, viewport_cache_version_, tile.start_frame,
                    tile.end_frame, cacheFramesPerPoint, renderMode);
                const qint64 bytes = viewportCacheBytes(tile.channels);
                auto entry = std::make_shared<ViewportWaveformCacheEntry>();
                entry->start_frame = tile.start_frame;
                entry->end_frame = tile.end_frame;
                entry->source_start_frame = tile.start_frame;
                entry->target_frames_per_point = cacheFramesPerPoint;
                entry->mode = renderMode;
                entry->sample_rate = sample_rate_;
                entry->bytes = bytes;
                entry->channels = tile.channels;
                if (const auto existing_it = viewport_waveform_cache_.find(tileKey);
                    existing_it != viewport_waveform_cache_.end()) {
                    viewport_cache_size_bytes_ -= existing_it->second->bytes;
                    viewport_waveform_lru_.erase(existing_it->second->lru_iterator);
                    viewport_waveform_cache_.erase(existing_it);
                }
                viewport_waveform_cache_.insert_or_assign(tileKey, entry);
                viewport_waveform_lru_.push_front(tileKey);
                entry->lru_iterator = viewport_waveform_lru_.begin();
                viewport_cache_size_bytes_ += bytes;
                while (viewport_cache_size_bytes_ > viewport_cache_size_limit_
                       && !viewport_waveform_lru_.empty()) {
                    const QString stale_key = viewport_waveform_lru_.back();
                    viewport_waveform_lru_.pop_back();
                    if (const auto stale_it = viewport_waveform_cache_.find(stale_key);
                        stale_it != viewport_waveform_cache_.end()) {
                        viewport_cache_size_bytes_ -= stale_it->second->bytes;
                        viewport_waveform_cache_.erase(stale_it);
                    }
                }
            }
        }

        std::vector<std::vector<float>> peaks = jobResult.peaks;
        if (recActive && !peaks.empty()) {
            auto recordingPeaks = buildRecordingPlaceholderPeaks(
                recPeak, channels_, targetPoints, startFrame, endFrame,
                recStart, recFrames);
            const std::size_t channelCount = std::min(peaks.size(),
                                                     recordingPeaks.size());
            for (std::size_t channel = 0; channel < channelCount; ++channel) {
                auto& channelPeak = peaks[channel];
                const auto& recordingChannel = recordingPeaks[channel];
                for (qint64 point = 0; point < targetPoints; ++point) {
                    const auto minIndex = static_cast<std::size_t>(point * 2LL);
                    if (minIndex + 1ULL >= channelPeak.size()
                        || minIndex + 1ULL >= recordingChannel.size()) {
                        continue;
                    }
                    channelPeak[minIndex] = std::min(
                        channelPeak[minIndex], recordingChannel[minIndex]);
                    channelPeak[minIndex + 1ULL] = std::max(
                        channelPeak[minIndex + 1ULL], recordingChannel[minIndex + 1ULL]);
                }
            }
        }

        if (!peaks.empty()) {
            const auto peaksVariant = toVariantPeaks(peaks);
            viewport_channel_peaks_ = peaksVariant;
        } else {
            viewport_channel_peaks_.clear();
        }
        emit waveformChanged();
        watcher->deleteLater();
    });

        watcher->setFuture(QtConcurrent::run(
        [this, cached_tiles = std::move(cached_tiles),
         missing_tiles = std::move(missing_tiles), startFrame, endFrame, targetPoints,
         renderMode, cacheFramesPerPoint, channels = channels_,
         sourcePath = source_path_, sampleRate = sample_rate_, cancel_token,
         recActive = recordingActive, recFrames = recordedFrames,
         recStart = recordingStartFrame, recPeak = recordingPeak]() mutable
            -> ViewportWaveformJobResult {
            ViewportWaveformJobResult jobResult;
            jobResult.peaks = std::vector<std::vector<float>>();
            const qint64 visibleFrames = std::max<qint64>(1, endFrame - startFrame);
            if (sourcePath.isEmpty()) {
                if (!recActive || recFrames <= 0) {
                    return jobResult;
                }
                auto recordingPeaks = buildRecordingPlaceholderPeaks(
                    recPeak, channels, targetPoints, startFrame, endFrame,
                    recStart, recFrames);
                if (!recordingPeaks.empty()) {
                    jobResult.peaks = std::move(recordingPeaks);
                }
                return jobResult;
            }
            std::vector<std::vector<float>> merged;
            for (const auto& cached : cached_tiles) {
                merged = mergeResampledFromTileData(
                    cached.channels, cached.start_frame, startFrame, endFrame,
                    visibleFrames, targetPoints, channels, std::move(merged));
            }
            for (const auto& missing : missing_tiles) {
                if (cancel_token->load(std::memory_order_acquire)) break;
                auto decoded = decodeViewportTileSamples(sourcePath, missing.first,
                                                        missing.second, channels,
                                                        sampleRate, cancel_token);
                if (decoded.empty()) {
                    continue;
                }
                TileRangeData data{missing.first, missing.second,
                                   std::move(decoded)};
                merged = mergeResampledFromTileData(
                    data.channels, data.start_frame, startFrame, endFrame,
                    visibleFrames, targetPoints, channels, std::move(merged));
                jobResult.decoded_tiles.push_back(std::move(data));
            }
            if (merged.empty()) {
                return jobResult;
            }
            if (recActive && recFrames > 0) {
                auto recordingPeaks = buildRecordingPlaceholderPeaks(
                    recPeak, channels, targetPoints, startFrame, endFrame,
                    recStart, recFrames);
                if (!recordingPeaks.empty() && recordingPeaks.size() == merged.size()) {
                    for (std::size_t channel = 0;
                         channel < merged.size(); ++channel) {
                        for (qint64 point = 0; point < targetPoints; ++point) {
                            const auto minIndex = static_cast<std::size_t>(point * 2LL);
                            if (minIndex + 1ULL >= merged[channel].size()
                                || minIndex + 1ULL >= recordingPeaks[channel].size()) {
                                continue;
                            }
                            merged[channel][minIndex] = std::min(
                                merged[channel][minIndex], recordingPeaks[channel][minIndex]);
                            merged[channel][minIndex + 1ULL] = std::max(
                                merged[channel][minIndex + 1ULL],
                                recordingPeaks[channel][minIndex + 1ULL]);
                        }
                    }
                }
            }
            jobResult.peaks = std::move(merged);
            return jobResult;
        }));
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
    actions_.setEnabled(QStringLiteral("editor.newRecording"), idle);
    actions_.setEnabled(QStringLiteral("editor.save"), has_document_ && idle);
    actions_.setEnabled(QStringLiteral("editor.export"), has_document_ && idle);
    actions_.setEnabled(QStringLiteral("editor.undo"), false);
    actions_.setEnabled(QStringLiteral("editor.redo"), false);
    actions_.setEnabled(QStringLiteral("editor.paste"), document_.hasClipboard() && idle);
    const qint64 playhead = position_ms_ * sample_rate_ / 1'000;
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

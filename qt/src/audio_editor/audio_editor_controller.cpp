#include "audio_editor_controller.hpp"
#include "audio_editor/audio_file_analyzer.hpp"
#include "audio_editor/document_render_pipeline.hpp"
#include "audio_editor/waveform_render_limits.hpp"
#include "bpm_analyzer.hpp"
#include "../playback_controller.hpp"

#include <QFileInfo>
#include <QFile>
#include <QDateTime>
#include <QDir>
#include <QPointer>
#include <QStandardPaths>
#include <QThread>
#include <QtConcurrent>

#include "decoder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <list>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

using agplayer::editor::AudioDocument;
using agplayer::editor::AudioEvent;
using agplayer::editor::AudioFileAnalysis;
using agplayer::editor::AudioFileAnalyzer;
using agplayer::editor::AudioSource;
using agplayer::editor::DocumentRenderPipeline;
using agplayer::editor::NoiseReducer;
using agplayer::editor::ProjectDocument;
using agplayer::editor::ProjectEditorSettings;
using agplayer::editor::ProjectExportSettings;
using agplayer::editor::ProjectLoadResult;
using agplayer::editor::ProjectSourceIssue;
using agplayer::editor::ProjectSourceIssueKind;
using agplayer::editor::ProjectSourceRecord;
using agplayer::editor::ProjectSaveRequest;
using agplayer::editor::Selection;
using agplayer::editor::TimePitchSession;
using agplayer::editor::WriteRequest;

namespace {

constexpr std::size_t kMaxProjectSources = 4'096;
enum class PeakReadState {
    Complete,
    Unavailable,
    Cancelled,
};

QString handoffTimeStamp(const qint64 milliseconds)
{
    const qint64 bounded = std::max<qint64>(0, milliseconds);
    return QStringLiteral("%1m%2.%3")
        .arg(bounded / 60'000, 2, 10, QLatin1Char('0'))
        .arg((bounded / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(bounded % 1000, 3, 10, QLatin1Char('0'));
}

qint64 viewportTargetPoints(const qint64 visibleFrames,
                           const qreal viewportWidth,
                           const qreal devicePixelRatio)
{
    const qreal boundedDpr = std::clamp(devicePixelRatio, 1.0,
                                        kMaxWaveformDevicePixelRatio);
    const qreal scaledBuckets = 2.0 * std::max<qreal>(0.0, viewportWidth)
        * boundedDpr;
    const qint64 buckets = std::max<qint64>(1, static_cast<qint64>(
        std::floor(scaledBuckets)));
    return std::min<qint64>(visibleFrames, buckets);
}

std::vector<float> centeredAmplitudeMix(
    const std::vector<std::vector<float>>& channels)
{
    if (channels.empty() || channels.front().empty()
        || channels.front().size() % 2U != 0U) {
        return {};
    }
    const std::size_t pairCount = channels.front().size() / 2U;
    for (const auto& channel : channels) {
        if (channel.size() / 2U != pairCount) return {};
    }
    std::vector<float> result(pairCount * 2U, 0.0F);
    for (std::size_t pair = 0; pair < pairCount; ++pair) {
        float amplitude = 0.0F;
        bool valid = true;
        for (const auto& channel : channels) {
            const float minimum = channel[pair * 2U];
            const float maximum = channel[pair * 2U + 1U];
            if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
                valid = false;
                break;
            }
            amplitude = std::max(amplitude, std::abs(minimum));
            amplitude = std::max(amplitude, std::abs(maximum));
        }
        if (valid) {
            result[pair * 2U] = -amplitude;
            result[pair * 2U + 1U] = amplitude;
        } else {
            result[pair * 2U] = std::numeric_limits<float>::quiet_NaN();
            result[pair * 2U + 1U] = std::numeric_limits<float>::quiet_NaN();
        }
    }
    return result;
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

void includePeakWithGainRange(std::vector<float>& output,
                              const qint64 point,
                              const float minimum,
                              const float maximum,
                              const float minimumGain,
                              const float maximumGain)
{
    const std::array<float, 4> products{
        minimum * minimumGain,
        minimum * maximumGain,
        maximum * minimumGain,
        maximum * maximumGain};
    const auto extrema = std::minmax_element(products.cbegin(), products.cend());
    includePeak(output, point, *extrema.first, *extrema.second, 1.0F);
}

std::pair<float, float> eventGainExtrema(
    const AudioEvent& event, const qint64 localStart, const qint64 localEnd)
{
    if (event.mute) return {0.0F, 0.0F};
    std::vector<qint64> boundaries{localStart, localEnd};
    const auto includeBoundary = [&](const qint64 value) {
        if (value >= localStart && value <= localEnd) {
            boundaries.push_back(value);
        }
    };
    for (const auto& point : event.envelope) includeBoundary(point.offset);
    if (event.fadeIn > 0) {
        includeBoundary(event.fadeIn - 1);
        includeBoundary(event.fadeIn);
    }
    if (event.fadeOut > 0) {
        includeBoundary(agplayer::editor::audibleFrames(event) - event.fadeOut);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                     boundaries.end());

    float minimum = std::numeric_limits<float>::infinity();
    float maximum = -std::numeric_limits<float>::infinity();
    const auto include = [&](const qint64 offset) {
        const float gain = agplayer::editor::eventAmplitudeGainAt(
            event, offset);
        minimum = std::min(minimum, gain);
        maximum = std::max(maximum, gain);
    };
    for (const qint64 boundary : boundaries) include(boundary);
    constexpr qint64 kMaximumInteriorSamples = 128;
    for (std::size_t index = 1; index < boundaries.size(); ++index) {
        const qint64 begin = boundaries[index - 1];
        const qint64 end = boundaries[index];
        if (end - begin < 2) continue;
        const qint64 span = end - begin;
        const qint64 samples = std::min(
            span - 1, kMaximumInteriorSamples);
        for (qint64 sample = 1; sample <= samples; ++sample) {
            const qint64 candidate = begin
                + static_cast<qint64>(static_cast<long double>(span)
                    * sample / (samples + 1));
            if (candidate > begin && candidate < end) include(candidate);
        }
    }
    return {minimum, maximum};
}

PeakReadState mergeEventFromSourcePeaks(
    const AudioEvent& event,
    const std::vector<std::vector<float>>& sourcePeaks,
    const qint64 sourcePeakStart, const qint64 sourcePeakFrames,
    const qint64 visibleStart, const qint64 visibleEnd,
    const qint64 targetPoints, std::vector<std::vector<float>>& output)
{
    if (!event.source || sourcePeaks.empty() || sourcePeakFrames <= 0
        || output.empty() || targetPoints <= 0) {
        return PeakReadState::Unavailable;
    }
    const qint64 eventEnd = event.timelineStart
        + agplayer::editor::audibleFrames(event);
    const qint64 intersectionStart = std::max(visibleStart, event.timelineStart);
    const qint64 intersectionEnd = std::min(visibleEnd, eventEnd);
    if (intersectionEnd <= intersectionStart) return PeakReadState::Unavailable;
    if (std::max<qint64>(event.sourceStart, event.sourceStart
            + agplayer::editor::sourceOffsetAt(event, intersectionStart - event.timelineStart))
            < sourcePeakStart
        || std::min<qint64>(event.sourceEnd, event.sourceStart
            + agplayer::editor::sourceOffsetAt(event, intersectionEnd - event.timelineStart))
            > sourcePeakStart + sourcePeakFrames) {
        return PeakReadState::Unavailable;
    }
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
        const qint64 sourceStart = std::clamp<qint64>(event.sourceStart
            + agplayer::editor::sourceOffsetAt(event, overlapStart - event.timelineStart),
            event.sourceStart, event.sourceEnd - 1);
        const qint64 sourceEnd = std::min<qint64>(event.sourceEnd, std::max<qint64>(sourceStart + 1,
            event.sourceStart + agplayer::editor::sourceOffsetAt(event, overlapEnd - event.timelineStart)));
        for (std::size_t channelIndex = 0;
             channelIndex < output.size(); ++channelIndex) {
            const auto sourceIndex = std::min(channelIndex,
                                              sourcePeaks.size() - 1U);
            const auto& channel = sourcePeaks[sourceIndex];
            if (channel.empty() || channel.size() % 2U != 0U) {
                return PeakReadState::Unavailable;
            }
            const qint64 buckets = static_cast<qint64>(channel.size() / 2U);
            if (buckets <= 0) return PeakReadState::Unavailable;
            const qint64 first = scaledBucket(
                std::max<qint64>(0, sourceStart - sourcePeakStart),
                sourcePeakFrames, buckets);
            const qint64 last = scaledBucket(
                std::max<qint64>(0, std::max(sourceStart, sourceEnd - 1)
                    - sourcePeakStart), sourcePeakFrames, buckets);
            float minimum = std::numeric_limits<float>::infinity();
            float maximum = -std::numeric_limits<float>::infinity();
            for (qint64 bucket = first; bucket <= last; ++bucket) {
                const auto index = static_cast<std::size_t>(bucket * 2);
                if (index + 1U >= channel.size()) {
                    return PeakReadState::Unavailable;
                }
                const qint64 bucketSourceStart = sourcePeakStart
                    + static_cast<qint64>(static_cast<long double>(bucket)
                        * sourcePeakFrames / buckets);
                const qint64 bucketSourceEnd = static_cast<qint64>(std::ceil(
                    static_cast<long double>(sourcePeakStart)
                    + static_cast<long double>(bucket + 1)
                    * sourcePeakFrames / buckets));
                const qint64 affectedStart = std::max(sourceStart,
                    std::max(event.sourceStart, bucketSourceStart));
                const qint64 affectedEnd = std::min(sourceEnd,
                    std::min(event.sourceEnd, bucketSourceEnd));
                if (affectedEnd <= affectedStart) continue;
                const qint64 localStart = std::clamp<qint64>(
                    agplayer::editor::projectOffsetAt(event, affectedStart),
                    overlapStart - event.timelineStart, overlapEnd - 1 - event.timelineStart);
                const qint64 localEnd = std::clamp<qint64>(
                    agplayer::editor::projectOffsetAt(event, affectedEnd - 1),
                    localStart, overlapEnd - 1 - event.timelineStart);
                const auto gainExtrema = eventGainExtrema(
                    event, localStart, localEnd);
                const std::array<float, 4> products{
                    channel[index] * gainExtrema.first,
                    channel[index] * gainExtrema.second,
                    channel[index + 1U] * gainExtrema.first,
                    channel[index + 1U] * gainExtrema.second};
                const auto productExtrema = std::minmax_element(
                    products.cbegin(), products.cend());
                minimum = std::min(minimum, *productExtrema.first);
                maximum = std::max(maximum, *productExtrema.second);
            }
            if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
                return PeakReadState::Unavailable;
            }
            includePeakWithGainRange(output[channelIndex], point,
                minimum, maximum, 1.0F, 1.0F);
        }
    }
    return PeakReadState::Complete;
}

PeakReadState mergeEventFromDecodedSlice(
    const AudioEvent& event, const qint64 visibleStart, const qint64 visibleEnd,
    const qint64 targetPoints,
    const std::shared_ptr<std::atomic_bool>& cancelToken,
    std::vector<std::vector<float>>& output)
{
    if (!event.source || event.source->path.empty() || output.empty()
        || targetPoints <= 0) return PeakReadState::Unavailable;
    const qint64 eventEnd = event.timelineStart + agplayer::editor::audibleFrames(event);
    const qint64 intersectionStart = std::max(visibleStart, event.timelineStart);
    const qint64 intersectionEnd = std::min(visibleEnd, eventEnd);
    if (intersectionEnd <= intersectionStart) return PeakReadState::Unavailable;
    const auto sourceFrameAt = [&](qint64 timelineFrame) {
        return std::clamp<qint64>(event.sourceStart
            + agplayer::editor::sourceOffsetAt(event, timelineFrame - event.timelineStart),
            event.sourceStart, event.sourceEnd);
    };
    const int sourceChannels = static_cast<int>(event.source->channels);
    if (sourceChannels <= 0 || event.source->sample_rate == 0)
        return PeakReadState::Unavailable;
    agplayer::Decoder decoder;
    agplayer::DecoderOpenOptions options;
    options.interrupt_context = cancelToken.get();
    options.interrupt_callback = [](void* context) noexcept {
        return static_cast<std::atomic_bool*>(context)->load(std::memory_order_acquire);
    };
    if (decoder.open(event.source->path.u8string(), options) != AG_OK
        || decoder.seekFrame(sourceFrameAt(intersectionStart)) != AG_OK)
        return cancelToken->load() ? PeakReadState::Cancelled : PeakReadState::Unavailable;

    const qint64 visibleFrames = visibleEnd - visibleStart;
    agplayer::DecodedAudioBlock block;
    qint64 blockStart = sourceFrameAt(intersectionStart);
    qint64 blockEnd = blockStart;
    // Map project bucket boundaries back to absolute source frames. Iterating
    // project buckets avoids source-rate rounding shifting transients by one
    // bucket, and permits a source sample to cover multiple upsampled buckets.
    for (qint64 point = 0; point < targetPoints; ++point) {
        if (cancelToken->load(std::memory_order_acquire)) return PeakReadState::Cancelled;
        const qint64 timelineStart = std::max(intersectionStart, visibleStart
            + static_cast<qint64>(static_cast<long double>(point) * visibleFrames / targetPoints));
        const qint64 timelineEnd = std::min(intersectionEnd, visibleStart
            + static_cast<qint64>(static_cast<long double>(point + 1) * visibleFrames / targetPoints));
        if (timelineEnd <= timelineStart) continue;
        const qint64 sourceStart = std::min(sourceFrameAt(timelineStart), event.sourceEnd - 1);
        const qint64 sourceEnd = std::min(event.sourceEnd,
            std::max(sourceStart + 1, sourceFrameAt(timelineEnd)));
        float amplitude = 0;
        for (qint64 frame = sourceStart; frame < sourceEnd; ++frame) {
            while (frame >= blockEnd) {
                if (decoder.read(block) != AG_OK || cancelToken->load(std::memory_order_acquire))
                    return cancelToken->load() ? PeakReadState::Cancelled : PeakReadState::Unavailable;
                if (!block.frames) {
                    if (block.end_of_stream) return PeakReadState::Unavailable;
                    continue;
                }
                blockStart = block.timestamp_frame >= 0 ? block.timestamp_frame : blockEnd;
                blockEnd = blockStart + static_cast<qint64>(block.frames);
                if (blockStart > frame) return PeakReadState::Unavailable;
            }
            if (frame < blockStart) return PeakReadState::Unavailable;
            const auto base = static_cast<std::size_t>(frame - blockStart)
                * static_cast<std::size_t>(sourceChannels);
            if (base + static_cast<std::size_t>(sourceChannels) > block.samples.size())
                return PeakReadState::Unavailable;
            for (int channel = 0; channel < sourceChannels; ++channel) {
                const float sample = block.samples[base + static_cast<std::size_t>(channel)];
                if (std::isfinite(sample)) amplitude = std::max(amplitude, std::abs(sample));
            }
        }
        const auto gain = eventGainExtrema(event, timelineStart - event.timelineStart,
            timelineEnd - 1 - event.timelineStart);
        for (auto& channel : output)
            includePeakWithGainRange(channel, point, -amplitude, amplitude, gain.first, gain.second);
    }
    return PeakReadState::Complete;
}

PeakReadState mergeEventFromPyramid(const AudioEvent& event,
    const agplayer::editor::PeakPyramid& pyramid,
    const qint64 visibleStart, const qint64 visibleEnd, const qint64 targetPoints,
    std::vector<std::vector<float>>& output, const int contour = 0)
{
    const qint64 sourceStart = std::clamp<qint64>(event.sourceStart
        + agplayer::editor::sourceOffsetAt(event, visibleStart - event.timelineStart),
        event.sourceStart, event.sourceEnd - 1);
    const qint64 sourceEnd = std::clamp<qint64>(event.sourceStart
        + agplayer::editor::sourceOffsetAt(event, visibleEnd - event.timelineStart),
        sourceStart + 1, event.sourceEnd);
    std::vector<std::vector<float>> selected;
    qint64 peakStart = sourceStart;
    qint64 peakFrames = sourceEnd - sourceStart;
    for (std::size_t channel = 0; channel < output.size(); ++channel) {
        const auto window = pyramid.readWindow(std::min(channel, pyramid.channelCount() - 1),
            sourceStart, sourceEnd - sourceStart, static_cast<std::size_t>(targetPoints));
        if (window.buckets.empty()) return PeakReadState::Unavailable;
        if (!channel) {
            peakStart = window.start;
            peakFrames = std::min<qint64>(event.source->total_frames - window.start,
                window.bucketFrames * static_cast<qint64>(window.buckets.size()));
        }
        std::vector<float> flattened;
        flattened.reserve(window.buckets.size() * 2);
        for (const auto bucket : window.buckets) {
            if (contour && (bucket.meanSquare < 0 || bucket.meanAbsolute < 0)) return PeakReadState::Unavailable;
            const float amplitude = contour == 2 ? std::sqrt(bucket.meanSquare) : bucket.meanAbsolute;
            flattened.push_back(contour ? -amplitude : bucket.minimum);
            flattened.push_back(contour ? amplitude : bucket.maximum);
        }
        selected.push_back(std::move(flattened));
    }
    return mergeEventFromSourcePeaks(event, selected, peakStart, peakFrames,
        visibleStart, visibleEnd, targetPoints, output);
}

std::string sourcePeakKey(const AudioSource& source)
{
    return source.path.u8string() + '|' + std::to_string(source.sample_rate)
        + '|' + std::to_string(source.channels) + '|'
        + std::to_string(source.total_frames);
}

AudioEditorController::ViewportWaveformResult composeVisibleTimelinePeaks(
    const agplayer::editor::TimelineSnapshot& snapshot,
    const QString& primarySourcePath,
    const std::vector<std::vector<float>>& primarySourcePeaks,
    const std::shared_ptr<const agplayer::editor::PeakPyramid>& primaryPyramid,
    const std::unordered_map<std::string,
        std::shared_ptr<const agplayer::editor::PeakPyramid>>& sourcePyramids,
    const qint64 visibleStart, const qint64 visibleEnd,
    const qint64 targetPoints, const int channels,
    const std::shared_ptr<std::atomic_bool>& cancelToken)
{
    const float blank = std::numeric_limits<float>::quiet_NaN();
    AudioEditorController::ViewportWaveformResult composition;
    composition.revision = snapshot.revision;
    auto& result = composition.peaks;
    result.assign(static_cast<std::size_t>(std::max(0, channels)),
        std::vector<float>(static_cast<std::size_t>(targetPoints * 2), blank));
    const qint64 visibleFrames = visibleEnd - visibleStart;
    if (visibleFrames <= 0 || targetPoints <= 0 || channels <= 0) return composition;
    const auto projectRate = snapshot.sampleRate ? snapshot.sampleRate
        : (snapshot.events.empty() || !snapshot.events.front().source
            ? 0U : snapshot.events.front().source->sample_rate);
    const bool preciseSlice = visibleFrames <= targetPoints * 1'024
        && visibleFrames <= static_cast<qint64>(projectRate) * 300;

    for (const AudioEvent& event : snapshot.events) {
        if (cancelToken->load(std::memory_order_acquire)) return {};
        const qint64 eventEnd = event.timelineStart + agplayer::editor::audibleFrames(event);
        if (!event.source || eventEnd <= visibleStart || event.timelineStart >= visibleEnd) continue;
        composition.hasVisibleEvent = true;
        const qint64 start = std::max(visibleStart, event.timelineStart);
        const qint64 end = std::min(visibleEnd, eventEnd);
        const qint64 points = std::min<qint64>(end - start, std::max<qint64>(1,
            static_cast<qint64>(std::ceil(static_cast<long double>(end - start)
                * targetPoints / visibleFrames))));
        AudioEditorController::ClipWaveformSlice clip;
        clip.startFrame = start;
        clip.endFrame = end;
        clip.peaks.assign(static_cast<std::size_t>(channels),
            std::vector<float>(static_cast<std::size_t>(points * 2), blank));
        const QString eventPath = QString::fromStdWString(event.source->path.wstring());
        const bool primary = eventPath == primarySourcePath && !primarySourcePeaks.empty();
        const auto cached = sourcePyramids.find(sourcePeakKey(*event.source));
        const auto pyramid = cached != sourcePyramids.end() ? cached->second
            : (primary ? primaryPyramid : std::shared_ptr<const agplayer::editor::PeakPyramid>{});
        PeakReadState state = PeakReadState::Unavailable;
        if (preciseSlice)
            state = mergeEventFromDecodedSlice(event, start, end, points, cancelToken, clip.peaks);
        clip.decodedDetail = preciseSlice && state == PeakReadState::Complete;
        if (state == PeakReadState::Cancelled) return {};
        if (state != PeakReadState::Complete && pyramid && pyramid->channelCount())
            state = mergeEventFromPyramid(event, *pyramid, start, end, points, clip.peaks);
        if (state != PeakReadState::Complete && primary)
            state = mergeEventFromSourcePeaks(event, primarySourcePeaks, 0,
                event.source->total_frames, start, end, points, clip.peaks);
        composition.hasUnavailableVisibleEvent |= state != PeakReadState::Complete;
        if (state != PeakReadState::Complete) continue;

        // Reuse this clip's decoded/pyramid buckets for the legacy composite,
        // while exposing its own local window to each six-track native item.
        for (qint64 point = 0; point < points; ++point) {
            const qint64 pointStart = start + static_cast<qint64>(
                static_cast<long double>(point) * (end - start) / points);
            const qint64 pointEnd = start + static_cast<qint64>(
                static_cast<long double>(point + 1) * (end - start) / points);
            const auto first = scaledBucket(pointStart - visibleStart, visibleFrames, targetPoints);
            const auto last = scaledBucket(std::max(pointStart, pointEnd - 1)
                - visibleStart, visibleFrames, targetPoints);
            for (auto bucket = first; bucket <= last; ++bucket) {
                for (std::size_t channel = 0; channel < result.size(); ++channel)
                    includePeak(result[channel], bucket,
                        clip.peaks[channel][static_cast<std::size_t>(point * 2)],
                        clip.peaks[channel][static_cast<std::size_t>(point * 2 + 1)], 1);
            }
        }
        composition.clipPeaks.emplace(event.id, std::move(clip));
    }
    return composition;
}

std::shared_ptr<const agplayer::editor::PeakPyramid> buildPeakPyramid(
    const std::vector<std::vector<float>>& channelPeaks,
    const qint64 totalFrames, const std::vector<float>& meanSquare = {},
    const std::vector<float>& meanAbsolute = {})
{
    if (channelPeaks.empty() || totalFrames <= 0) return {};
    // Match WaveformBucketizer's source-wide normalization once, never
    // normalize the current viewport (which would pump while panning).
    const float squareMaximum = meanSquare.empty() ? 0 : *std::max_element(meanSquare.begin(), meanSquare.end());
    const float absoluteMaximum = meanAbsolute.empty() ? 0 : *std::max_element(meanAbsolute.begin(), meanAbsolute.end());
    std::vector<std::vector<agplayer::editor::PeakBucket>> channels;
    channels.reserve(channelPeaks.size());
    for (const auto& values : channelPeaks) {
        if (values.empty() || values.size() % 2U != 0U) return {};
        std::vector<agplayer::editor::PeakBucket> buckets;
        buckets.reserve(values.size() / 2U);
        for (std::size_t index = 0; index < values.size(); index += 2U) {
            const auto point = index / 2U;
            const auto count = values.size() / 2U;
            const auto first = static_cast<qint64>((static_cast<long double>(point) * totalFrames + count - 1) / count);
            const auto last = static_cast<qint64>((static_cast<long double>(point + 1) * totalFrames + count - 1) / count);
            buckets.push_back({values[index], values[index + 1U],
                point < meanSquare.size() ? (squareMaximum > 0 ? meanSquare[point] / squareMaximum : 0) : -1.0F, last - first,
                point < meanAbsolute.size() ? (absoluteMaximum > 0 ? meanAbsolute[point] / absoluteMaximum : 0) : -1.0F});
        }
        channels.push_back(std::move(buckets));
    }
    const qint64 bucketCount = static_cast<qint64>(
        channelPeaks.front().size() / 2U);
    if (bucketCount <= 0) return {};
    const qint64 baseBucketFrames = std::max<qint64>(
        1, (totalFrames + bucketCount - 1) / bucketCount);
    auto pyramid = agplayer::editor::PeakPyramid::fromBaseBuckets(
        std::move(channels), baseBucketFrames, totalFrames);
    if (pyramid.channelCount() == 0U) return {};
    return std::make_shared<const agplayer::editor::PeakPyramid>(
        std::move(pyramid));
}

struct PeakPyramidRegistry final {
    std::mutex mutex;
    std::unordered_map<std::string,
        std::weak_ptr<const agplayer::editor::PeakPyramid>> entries;
};

PeakPyramidRegistry& peakPyramidRegistry()
{
    static PeakPyramidRegistry registry;
    return registry;
}

std::string peakPyramidIdentity(const AudioSource& source)
{
    const QFileInfo file(QString::fromStdWString(source.path.wstring()));
    return sourcePeakKey(source) + '|'
        + std::to_string(file.exists() ? file.size() : -1) + '|'
        + std::to_string(file.exists()
            ? file.lastModified().toUTC().toMSecsSinceEpoch() : -1);
}

std::shared_ptr<const agplayer::editor::PeakPyramid> lookupPeakPyramid(
    const AudioSource& source)
{
    auto& registry = peakPyramidRegistry();
    const std::lock_guard<std::mutex> lock(registry.mutex);
    const auto found = registry.entries.find(peakPyramidIdentity(source));
    if (found == registry.entries.end()) return {};
    auto cached = found->second.lock();
    if (!cached) registry.entries.erase(found);
    return cached;
}

std::shared_ptr<const agplayer::editor::PeakPyramid> cachedPeakPyramid(
    const AudioFileAnalysis& analysis)
{
    if (auto cached = lookupPeakPyramid(analysis.source)) return cached;
    const std::string identity = peakPyramidIdentity(analysis.source);
    const std::vector<float> amplitudeMix = centeredAmplitudeMix(
        analysis.channel_peaks);
    const std::vector<std::vector<float>> mix = amplitudeMix.empty()
        ? (analysis.visual_mix_peaks.empty()
            ? analysis.channel_peaks
            : std::vector<std::vector<float>>{analysis.visual_mix_peaks})
        : std::vector<std::vector<float>>{amplitudeMix};
    auto pyramid = buildPeakPyramid(mix, analysis.source.total_frames, analysis.mean_square, analysis.mean_absolute);
    if (!pyramid) return {};
    auto& registry = peakPyramidRegistry();
    {
        const std::lock_guard<std::mutex> lock(registry.mutex);
        for (auto entry = registry.entries.begin();
             entry != registry.entries.end();) {
            entry = entry->second.expired() ? registry.entries.erase(entry)
                                            : std::next(entry);
        }
        registry.entries.insert_or_assign(identity, pyramid);
    }
    return pyramid;
}

QString local_path(const QUrl& url)
{
    return url.isLocalFile() ? url.toLocalFile() : QString{};
}

double embedded_bpm(const QString& path)
{
    ag_metadata* metadata = nullptr;
    const QByteArray utf8 = path.toUtf8();
    if (path.isEmpty()
        || ag_metadata_open(utf8.constData(), &metadata) != AG_OK
        || metadata == nullptr) {
        return 0.0;
    }
    const QString text = QString::fromUtf8(
        ag_metadata_bpm_tag(metadata)).trimmed();
    ag_metadata_destroy(metadata);
    bool ok = false;
    const double value = text.toDouble(&ok);
    return ok && std::isfinite(value) && value >= 20.0 && value <= 400.0
        ? value : 0.0;
}

QString fade_curve_name(const agplayer::editor::FadeCurve curve)
{
    using agplayer::editor::FadeCurve;
    switch (curve) {
    case FadeCurve::Linear: return QStringLiteral("linear");
    case FadeCurve::Smooth: return QStringLiteral("smooth");
    case FadeCurve::Exponential: return QStringLiteral("exponential");
    }
    return {};
}

std::optional<agplayer::editor::FadeCurve> fade_curve(
    const QString& name)
{
    using agplayer::editor::FadeCurve;
    const QString key = name.trimmed().toLower();
    if (key == QStringLiteral("linear")) return FadeCurve::Linear;
    if (key == QStringLiteral("smooth")) return FadeCurve::Smooth;
    if (key == QStringLiteral("exponential")) return FadeCurve::Exponential;
    return std::nullopt;
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
        && left.bitDepth == right.bitDepth
        && left.channels == right.channels
        && left.bitRate == right.bitRate
        && left.keepMetadata == right.keepMetadata
        && left.variableBitRate == right.variableBitRate
        && left.quality == right.quality
        && left.outputDirectory == right.outputDirectory;
}

ProjectExportSettings defaultProjectExportSettings(
    const int sourceSampleRate = 44'100, const int sourceChannels = 2)
{
    ProjectExportSettings settings;
    settings.codecName = QStringLiteral("WAV");
    settings.sampleRate = std::clamp(sourceSampleRate, 8'000, 384'000);
    settings.bitDepth = 24;
    settings.channels = std::clamp(sourceChannels, 1, 8);
    settings.bitRate = 320'000;
    settings.keepMetadata = true;
    settings.variableBitRate = false;
    settings.quality = 100;
    settings.outputDirectory = QStandardPaths::writableLocation(
        QStandardPaths::DesktopLocation);
    if (settings.outputDirectory.isEmpty()) {
        settings.outputDirectory = QDir::home().filePath(QStringLiteral("Desktop"));
    }
    return settings;
}

bool same_selection(const std::optional<Selection>& left,
                    const std::optional<Selection>& right) noexcept
{
    if (left.has_value() != right.has_value()) return false;
    return !left || *left == *right;
}

QString handoffSourceIdentity(
    const agplayer::editor::TimelineSnapshot& snapshot)
{
    QString identity;
    for (const AudioEvent& event : snapshot.events) {
        if (!event.source) continue;
        const QString path = QString::fromStdWString(
            event.source->path.wstring());
        const QFileInfo info(path);
        identity += path;
        identity += QLatin1Char('|');
        identity += QString::number(info.exists() ? info.size() : -1);
        identity += QLatin1Char('|');
        identity += QString::number(info.exists()
            ? info.lastModified().toUTC().toMSecsSinceEpoch() : -1);
        identity += QLatin1Char(';');
    }
    return identity;
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
                                  {QStringLiteral("sourceId"),
                                   QString::number(issue.sourceId)},
                                  {QStringLiteral("path"), issue.path},
                                  {QStringLiteral("message"), issue.message}});
    }
    return result;
}

} // namespace

struct AudioEditorController::NoiseReductionFinalizeResult final {
    agplayer::editor::NoiseReductionResult reduction;
    AudioFileAnalysis analysis;
};

AudioEditorController::AudioEditorController(QObject* parent)
    : AudioEditorController(std::nullopt, parent)
{
}

AudioEditorController::AudioEditorController(
    const ag_audio_backend backend, QObject* parent)
    : AudioEditorController(std::optional<ag_audio_backend>{backend}, parent)
{
}

AudioEditorController::AudioEditorController(
    const std::optional<ag_audio_backend> backend, QObject* parent)
    : QObject(parent), actions_(this), viewport_(this)
{
    project_export_settings_ = defaultProjectExportSettings();
    if (backend.has_value()) {
        ag_player_config config{};
        config.backend = *backend;
        if (ag_player_create_with_config(&config, &player_) == AG_OK) {
            owns_player_ = true;
        } else {
            player_ = nullptr;
        }
    }
    playback_adapter_ = std::make_unique<EditorPlaybackAdapter>(player_);
    connect(&recorder_, &agplayer::editor::EditorRecordingService::stateChanged,
            this, [this] { refreshActions(); emit stateChanged(); });
    connect(&recorder_, &agplayer::editor::EditorRecordingService::recordedFramesChanged,
            this, [this] {
        if (recording_track_ < 0) return;
        playhead_frame_ = recording_start_frame_ + recorder_.recordedFrames();
        position_ms_ = sample_rate_ > 0 ? playhead_frame_ * 1000 / sample_rate_ : 0;
        const auto span = std::max<qint64>(viewport_.visibleFrameCount(), sample_rate_ * 10LL);
        const auto start = viewport_.visibleStartFrame();
        viewport_.setDocumentFrames(std::max(document_.totalFrames(), playhead_frame_ + span));
        if (playhead_frame_ >= start + span)
            viewport_.setVisibleRange(playhead_frame_ - span / 4, playhead_frame_ + span * 3 / 4);
        else viewport_.setVisibleRange(start, start + span);
        emit playbackChanged();
    });
    connect(&recorder_, &agplayer::editor::EditorRecordingService::failed,
            this, [this](const QString& message, const QString& path, qint64) {
        recording_track_ = -1;
        playhead_frame_ = std::min(playhead_frame_, document_.totalFrames());
        position_ms_ = sample_rate_ > 0 ? playhead_frame_ * 1000 / sample_rate_ : 0;
        setViewportDocumentFrames(document_.totalFrames());
        emit playbackChanged();
        setError(path.isEmpty() ? message : tr("%1；部分录音保留于：%2").arg(message, path));
        refreshActions();
        emit stateChanged();
    });
    connect(&recorder_, &agplayer::editor::EditorRecordingService::finished,
            this, [this](const QString& path, qint64) {
        if (recording_track_ < 0
            || document_.timelineSnapshot().revision != recording_revision_) {
            recording_track_ = -1;
            refreshActions();
            emit stateChanged();
            setError(tr("工程已改变，录音保留于：%1").arg(path));
            return;
        }
        DocumentLoadJob job;
        job.kind = DocumentLoadKind::Append;
        job.appendUrls = {QUrl::fromLocalFile(path)};
        job.appendFrame = recording_start_frame_;
        job.expectedRevision = recording_revision_;
        job.recordingTrack = recording_track_;
        recording_track_ = -1;
        enqueueDocumentLoad(std::move(job));
    });
    playback_timer_.setInterval(17);
    connect(&playback_timer_, &QTimer::timeout,
            this, &AudioEditorController::pollPlayback);
    viewport_waveform_debounce_timer_.setSingleShot(true);
    viewport_waveform_debounce_timer_.setInterval(75);
    connect(&viewport_waveform_debounce_timer_, &QTimer::timeout, this, [this] {
        if (viewport_waveform_watcher_ || !pending_viewport_waveform_job_) return;
        auto job = std::move(*pending_viewport_waveform_job_);
        pending_viewport_waveform_job_.reset();
        if (job.generation == viewport_waveform_generation_)
            startViewportWaveformJob(std::move(job));
    });
    connect(&viewport_, &EditorViewport::viewportChanged, this,
            &AudioEditorController::requestViewportWaveform);
    connect(&viewport_, &EditorViewport::viewportChanged, this, [this] {
        if (suppress_persisted_state_tracking_
            || !has_document_) return;
        viewport_persisted_dirty_ = viewport_.visibleStartFrame()
                != saved_visible_start_frame_
            || viewport_.visibleEndFrame() != saved_visible_end_frame_;
        if (syncModifiedFromHistory()) emit documentChanged();
    });
    refreshActions();
}

ProjectEditorSettings project_editor_settings(
    const TimePitchSession& timePitch, const bool trackMuted,
    const bool trackSolo, const double trackGainDb)
{
    ProjectEditorSettings settings;
    settings.originalBpm = timePitch.originalBpm();
    settings.targetBpm = timePitch.targetBpm();
    settings.speedPercent = timePitch.speedPercent();
    settings.keepPitch = timePitch.keepPitch();
    settings.formantPreservation = timePitch.formantPreservation();
    settings.pitchCents = timePitch.pitchCents();
    settings.trackMuted = trackMuted;
    settings.trackSolo = trackSolo;
    settings.trackGainDb = trackGainDb;
    return settings;
}

AudioEditorController::~AudioEditorController()
{
    recorder_.cancel();
    cancelOperation();
    cancelSourcePeakCacheJob();
    pending_viewport_waveform_job_.reset();
    if (viewport_waveform_cancel_token_) {
        viewport_waveform_cancel_token_->store(true,
                                               std::memory_order_release);
    }
    if (viewport_waveform_watcher_) {
        viewport_waveform_watcher_->disconnect(this);
        viewport_waveform_watcher_->cancel();
        viewport_waveform_watcher_->waitForFinished();
    }
    if (write_watcher_) write_watcher_->future().waitForFinished();
    if (time_pitch_watcher_) time_pitch_watcher_->future().waitForFinished();
    if (bpm_watcher_) bpm_watcher_->future().waitForFinished();
    if (source_peak_cache_watcher_) {
        source_peak_cache_watcher_->future().waitForFinished();
    }
    if (noise_reduction_watcher_) {
        noise_reduction_watcher_->future().waitForFinished();
    }
    if (playback_adapter_) {
        (void)playback_adapter_->stop();
        playback_adapter_->release();
    }
    if (player_ && owns_player_) {
        (void)ag_player_stop(player_);
        ag_player_destroy(player_);
    }
}

void AudioEditorController::setPlaybackController(
    PlaybackController* const controller)
{
    if (controller == playback_controller_) return;
    if (playback_adapter_) {
        (void)playback_adapter_->stop();
        releaseEditorPlaybackOutput();
    }
    playback_controller_ = controller;
    if (controller == nullptr) {
        // A borrowed core belongs to the application. Revoke both copies of
        // its handle before the owner destroys it; standalone cores stay owned.
        if (!owns_player_) player_ = nullptr;
        playback_adapter_ = std::make_unique<EditorPlaybackAdapter>(player_);
        playback_timer_.stop();
        playing_ = false;
        if (has_document_ && state_ == EditorSessionState::Playing) {
            setState(EditorSessionState::Ready);
        }
        emit playbackChanged();
        emit documentChanged();
        return;
    }
    if (controller->playerHandle() == player_) {
        playback_adapter_ = std::make_unique<EditorPlaybackAdapter>(
            player_, controller);
        return;
    }
    if (player_ != nullptr && owns_player_) ag_player_destroy(player_);
    player_ = controller->playerHandle();
    owns_player_ = false;
    playback_adapter_ = std::make_unique<EditorPlaybackAdapter>(player_, controller);
    playback_prepared_ = false;
    updatePlaybackMix();
}

QVariantList AudioEditorController::channelPeaks() const
{
    return channel_peaks_;
}

QVariantList AudioEditorController::timelineEventViews() const
{
    QVariantList result;
    const auto snapshot = timelineSnapshotForView();
    result.reserve(static_cast<qsizetype>(snapshot.events.size()));
    for (const AudioEvent& event : snapshot.events) {
        const AudioEvent& visible = event;
        QVariantList envelope;
        envelope.reserve(static_cast<qsizetype>(visible.envelope.size()));
        for (const auto& point : visible.envelope) {
            envelope.append(QVariantMap{
                {QStringLiteral("offset"), QVariant::fromValue<qint64>(point.offset)},
                {QStringLiteral("gain"), point.gain}});
        }
        result.append(QVariantMap{
            {QStringLiteral("id"), QString::number(visible.id)},
            {QStringLiteral("trackIndex"), visible.trackIndex},
            {QStringLiteral("sourceSampleRate"), visible.source ? visible.source->sample_rate : 0},
            {QStringLiteral("sourceTotalFrames"), QVariant::fromValue<qint64>(visible.source ? visible.source->total_frames : 0)},
            {QStringLiteral("projectSampleRate"), visible.timelineSampleRate ? visible.timelineSampleRate
                : (visible.source ? visible.source->sample_rate : 0)},
            {QStringLiteral("name"), visible.source
                ? QFileInfo(QString::fromStdWString(visible.source->path.wstring())).fileName() : QString{}},
            {QStringLiteral("timelineStart"), QVariant::fromValue<qint64>(
                visible.timelineStart)},
            {QStringLiteral("timelineEnd"), QVariant::fromValue<qint64>(
                visible.timelineStart + agplayer::editor::audibleFrames(visible))},
            {QStringLiteral("sourceStart"), QVariant::fromValue<qint64>(
                visible.sourceStart)},
            {QStringLiteral("sourceEnd"), QVariant::fromValue<qint64>(
                visible.sourceEnd)},
            {QStringLiteral("fadeIn"), QVariant::fromValue<qint64>(visible.fadeIn)},
            {QStringLiteral("fadeOut"), QVariant::fromValue<qint64>(visible.fadeOut)},
            {QStringLiteral("fadeInCurve"), fade_curve_name(visible.fadeInCurve)},
            {QStringLiteral("fadeOutCurve"), fade_curve_name(visible.fadeOutCurve)},
            {QStringLiteral("gain"), visible.gain},
            {QStringLiteral("mute"), visible.mute},
            {QStringLiteral("envelope"), envelope}});
    }
    return result;
}

agplayer::editor::TimelineSnapshot
AudioEditorController::timelineSnapshotForView() const
{
    auto snapshot = document_.timelineSnapshot();
    if (event_gesture_.kind == EventGestureKind::None
        || !event_gesture_.pending) {
        return snapshot;
    }
    if (event_gesture_.kind == EventGestureKind::SharedBoundary) {
        const auto left = std::find_if(snapshot.events.begin(),
            snapshot.events.end(), [this](const AudioEvent& item) {
                return item.id == event_gesture_.id;
            });
        const auto right = std::find_if(snapshot.events.begin(),
            snapshot.events.end(), [this](const AudioEvent& item) {
                return item.id == event_gesture_.secondaryId;
            });
        if (left == snapshot.events.end() || right == snapshot.events.end()) {
            return snapshot;
        }
        if (!agplayer::editor::reframeSharedBoundary(
                *left, *right, event_gesture_.sourceEnd)) {
            return document_.timelineSnapshot();
        }
        return snapshot;
    }
    const auto event = std::find_if(
        snapshot.events.begin(), snapshot.events.end(),
        [this](const AudioEvent& item) { return item.id == event_gesture_.id; });
    if (event == snapshot.events.end()) return snapshot;
    event->timelineStart = event_gesture_.timelineStart;
    if (event_gesture_.kind == EventGestureKind::Move)
        event->trackIndex = event_gesture_.trackIndex;
    if (event_gesture_.kind == EventGestureKind::Trim) {
        event->sourceStart = event_gesture_.sourceStart;
        event->sourceEnd = event_gesture_.sourceEnd;
    } else if (event_gesture_.kind == EventGestureKind::FadeOut) {
        event->fadeOut = event_gesture_.fadeOut;
    } else if (event_gesture_.kind == EventGestureKind::Gain) {
        event->gain = static_cast<float>(event_gesture_.gain);
    } else if (event_gesture_.kind == EventGestureKind::EnvelopePoint) {
        const auto point = std::find_if(
            event->envelope.begin(), event->envelope.end(),
            [this](const agplayer::editor::EnvelopePoint& item) {
                return item.offset == event_gesture_.originalEnvelopeOffset;
            });
        if (point != event->envelope.end()) {
            point->offset = event_gesture_.envelopeOffset;
            point->gain = static_cast<float>(event_gesture_.envelopeGain);
            std::sort(event->envelope.begin(), event->envelope.end(),
                [](const auto& left, const auto& right) {
                    return left.offset < right.offset;
                });
        }
    }
    return snapshot;
}

bool AudioEditorController::busy() const noexcept
{
    return document_loading_ || recording_track_ >= 0
        || state_ == EditorSessionState::Processing
        || state_ == EditorSessionState::Saving
        || state_ == EditorSessionState::Exporting;
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
            {QStringLiteral("bitDepth"), project_export_settings_.bitDepth},
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
    value.bitDepth = settings.value(QStringLiteral("bitDepth"), 24).toInt();
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

void AudioEditorController::markEditorSettingsDirty()
{
    if (!has_document_) return;
    markProjectDirty();
    refreshActions();
    emit projectChanged();
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

QVariantList AudioEditorController::tracks() const
{
    static const QStringList colors{"#BD91FF", "#5CDDE2", "#FF9EBC", "#F7CF73", "#78D7A3", "#8FACFF"};
    const auto snapshot = document_.timelineSnapshot();
    QVariantList result;
    for (int index = 0; index < 6; ++index) {
        QStringList names;
        for (const auto& clip : snapshot.events) {
            if (clip.trackIndex == index && clip.source)
                names.append(QFileInfo(QString::fromStdWString(clip.source->path.wstring())).fileName());
        }
        result.append(QVariantMap{{"index", index}, {"id", QStringLiteral("track-%1").arg(index + 1)},
            {"name", names.isEmpty() ? tr("空轨道") : names.join(QStringLiteral(" / "))},
            {"color", colors[index]}, {"muted", snapshot.tracks[index].muted},
            {"gain", gain_gesture_track_ == index ? gain_gesture_value_ : snapshot.tracks[index].gain}});
    }
    return result;
}

void AudioEditorController::setSelectedTrack(int index)
{
    if (index < 0 || index >= 6 || selected_track_ == index) return;
    selected_track_ = index;
    emit selectedTrackChanged();
}

bool AudioEditorController::addFiles(const QList<QUrl>& sources, int targetTrack, qint64 frame)
{
    if (sources.isEmpty() || busy()) return false;
    if (sources.size() > 4096) { setError(tr("一次导入不能超过 4096 个文件")); return false; }
    DocumentLoadJob job;
    job.kind = DocumentLoadKind::Append;
    job.appendUrls = sources;
    job.appendFrame = frame >= 0 ? frame : playhead_frame_;
    job.dropTrack = targetTrack >= 0 && targetTrack < 6 ? targetTrack : -1;
    job.expectedRevision = document_.timelineSnapshot().revision;
    if (playing_) playPause();
    return enqueueDocumentLoad(std::move(job));
}

bool AudioEditorController::setTrackMute(int index, bool muted)
{
    if (busy() || !document_.setTrackMuted(index, muted)) return false;
    finishTimelineMutation();
    return true;
}

bool AudioEditorController::setTrackGain(int index, double gain)
{
    if (busy() || !std::isfinite(gain)
        || !document_.setTrackGain(index, static_cast<float>(gain))) return false;
    finishTimelineMutation();
    return true;
}

bool AudioEditorController::beginTrackGainGesture(int index)
{
    if (busy() || index < 0 || index >= 6 || gain_gesture_track_ >= 0) return false;
    gain_gesture_track_ = index;
    gain_gesture_value_ = document_.timelineSnapshot().tracks[index].gain;
    return true;
}

bool AudioEditorController::updateTrackGainGesture(double gain)
{
    if (gain_gesture_track_ < 0 || !std::isfinite(gain)) return false;
    gain_gesture_value_ = std::clamp(gain, 0.0, 2.0);
    emit documentChanged();
    return true;
}

bool AudioEditorController::endTrackGainGesture()
{
    if (gain_gesture_track_ < 0) return false;
    const int track = gain_gesture_track_;
    gain_gesture_track_ = -1;
    const bool result = setTrackGain(track, gain_gesture_value_);
    emit documentChanged();
    return result;
}

bool AudioEditorController::cancelTrackGainGesture()
{
    if (gain_gesture_track_ < 0) return false;
    gain_gesture_track_ = -1;
    emit documentChanged();
    return true;
}

bool AudioEditorController::moveEventToTrack(const QString& id, qint64 frame, int track)
{
    if (busy() || track < 0 || track >= 6) return false;
    const auto eventId = parseEventId(id);
    const auto snapshot = document_.timelineSnapshot();
    const auto clip = std::find_if(snapshot.events.begin(), snapshot.events.end(),
        [eventId](const AudioEvent& event) { return eventId && event.id == *eventId; });
    if (clip == snapshot.events.end()) return false;
    const qint64 duration = agplayer::editor::audibleFrames(*clip);
    frame = std::max<qint64>(0, frame);
    const qint64 threshold = viewport_.viewportWidth() > 0
        ? static_cast<qint64>(8.0 * viewport_.visibleFrameCount() / viewport_.viewportWidth()) : 0;
    std::vector<qint64> snap{0};
    std::vector<qint64> legalCandidates{0, frame};
    for (const auto& other : snapshot.events) {
        if (other.id == *eventId) continue;
        const qint64 end = other.timelineStart + agplayer::editor::audibleFrames(other);
        snap.insert(snap.end(), {other.timelineStart, end, other.timelineStart - duration, end - duration});
        if (other.trackIndex == track) {
            legalCandidates.push_back(end);
            legalCandidates.push_back(other.timelineStart - duration);
        }
    }
    auto nearer = [frame](qint64 a, qint64 b) {
        const auto da = std::abs(a - frame), db = std::abs(b - frame);
        return da < db || (da == db && a < b);
    };
    std::sort(snap.begin(), snap.end(), nearer);
    for (qint64 point : snap) if (point >= 0 && std::abs(point - frame) <= threshold) { frame = point; break; }
    legalCandidates.push_back(frame);
    auto legal = [&](qint64 candidate) {
        if (candidate < 0 || candidate > std::numeric_limits<qint64>::max() - duration) return false;
        return std::none_of(snapshot.events.begin(), snapshot.events.end(), [&](const AudioEvent& other) {
            return other.id != *eventId && other.trackIndex == track
                && candidate < other.timelineStart + agplayer::editor::audibleFrames(other)
                && candidate + duration > other.timelineStart;
        });
    };
    if (!legal(frame)) {
        std::sort(legalCandidates.begin(), legalCandidates.end(), [frame](qint64 a, qint64 b) {
            return std::abs(a - frame) == std::abs(b - frame) ? a < b : std::abs(a - frame) < std::abs(b - frame);
        });
        const auto position = std::find_if(legalCandidates.begin(), legalCandidates.end(), legal);
        if (position == legalCandidates.end()) { setError(tr("目标轨道没有可用位置，片段保持原状")); return false; }
        frame = *position;
    }
    if (event_gesture_.kind == EventGestureKind::Move && event_gesture_.id == *eventId) {
        event_gesture_.timelineStart = frame;
        event_gesture_.trackIndex = track;
        event_gesture_.pending = true;
        emit documentChanged();
        return true;
    }
    if (!document_.moveEvent(*eventId, frame, track)) return false;
    setSelectedTrack(track);
    finishTimelineMutation();
    return true;
}

QVariantList AudioEditorController::eventPeaks(const QString& id, int pixels, const int contour) const
{
    const auto eventId = parseEventId(id);
    const auto snapshot = timelineSnapshotForView();
    const auto clip = std::find_if(snapshot.events.begin(), snapshot.events.end(),
        [eventId](const AudioEvent& event) { return eventId && event.id == *eventId; });
    if (clip == snapshot.events.end() || !clip->source) return {};
    const qint64 start = std::max<qint64>(clip->timelineStart, viewport_.visibleStartFrame());
    const qint64 end = std::min<qint64>(clip->timelineStart
        + agplayer::editor::audibleFrames(*clip), viewport_.visibleEndFrame());
    if (start >= end) return {};
    const auto displayPeaks = [&](const std::vector<std::vector<float>>& values) {
        if (!contour) return build_variant_peaks(values);
        // Stable source-wide visual scaling: quiet recordings stay readable
        // without zoom-dependent pumping or modifying PCM/event/track gain.
        float peak = 0;
        const auto sourceCache = source_peak_pyramids_.find(sourcePeakKey(*clip->source));
        if (sourceCache != source_peak_pyramids_.end() && sourceCache->second) {
            for (std::size_t channel = 0; channel < sourceCache->second->channelCount(); ++channel)
                for (const auto& bucket : sourceCache->second->read(channel, 0, sourceCache->second->documentFrames(), 1))
                    peak = std::max(peak, std::max(std::abs(bucket.minimum), std::abs(bucket.maximum)));
        }
        const float scale = peak > 0.001F ? std::clamp(0.85F / peak, 1.0F, 8.0F) : 1.0F;
        auto displayed = values;
        for (auto& channel : displayed)
            for (float& value : channel) value *= scale;
        return build_variant_peaks(displayed);
    };
    const auto detail = event_waveform_peaks_.find(*eventId);
    if (event_waveform_generation_ == viewport_waveform_generation_
        && detail != event_waveform_peaks_.end()
        && (!contour || detail->second.decodedDetail)
        && detail->second.startFrame == start && detail->second.endFrame == end)
        return displayPeaks(detail->second.peaks);

    // While a narrower view refines, reuse the already decoded local detail
    // rather than reverting to the whole-file coarse envelope. Never reuse
    // a different document revision or an in-progress edit gesture.
    if (detail != event_waveform_peaks_.end() && detail->second.decodedDetail
        && event_waveform_revision_ == snapshot.revision
        && event_gesture_.kind == EventGestureKind::None
        && start >= detail->second.startFrame && end <= detail->second.endFrame
        && detail->second.endFrame > detail->second.startFrame) {
        auto cropped = detail->second.peaks;
        for (auto& channel : cropped) {
            const auto buckets = static_cast<qint64>(channel.size() / 2);
            if (!buckets) return {};
            const auto span = detail->second.endFrame - detail->second.startFrame;
            const auto first = std::clamp<qint64>((start - detail->second.startFrame) * buckets / span, 0, buckets - 1);
            const auto last = std::clamp<qint64>(((end - detail->second.startFrame) * buckets + span - 1) / span, first + 1, buckets);
            channel = std::vector<float>(channel.begin() + first * 2, channel.begin() + last * 2);
        }
        return displayPeaks(cropped);
    }

    const auto cache = source_peak_pyramids_.find(sourcePeakKey(*clip->source));
    if (cache == source_peak_pyramids_.end() || !cache->second
        || !cache->second->channelCount()) return {};
    const auto points = viewportTargetPoints(end - start,
        static_cast<qreal>(std::clamp(pixels, 1, 8'192)), viewport_waveform_device_pixel_ratio_);
    std::vector<std::vector<float>> peaks(1, std::vector<float>(
        static_cast<std::size_t>(points * 2), std::numeric_limits<float>::quiet_NaN()));
    if (mergeEventFromPyramid(*clip, *cache->second, start, end, points, peaks, contour)
        != PeakReadState::Complete) return {};
    return build_variant_peaks(peaks);
}

bool AudioEditorController::beginScrub()
{
    if (!has_document_ || busy() || scrub_active_) return false;
    scrub_original_frame_ = currentPlaybackTimelineFrame();
    scrub_was_playing_ = playing_;
    if (playing_) playPause();
    scrub_active_ = true;
    return true;
}

void AudioEditorController::previewScrub(qint64 frame)
{
    if (!scrub_active_) return;
    playhead_frame_ = std::clamp<qint64>(frame, 0, document_.totalFrames());
    position_ms_ = sample_rate_ > 0 ? playhead_frame_ * 1000 / sample_rate_ : 0;
    emit playbackChanged();
}

bool AudioEditorController::endScrub()
{
    if (!scrub_active_) return false;
    scrub_active_ = false;
    const bool result = seekFrame(playhead_frame_);
    if (scrub_was_playing_ && result) playPause();
    return result;
}

void AudioEditorController::cancelScrub()
{
    if (!scrub_active_) return;
    playhead_frame_ = scrub_original_frame_;
    endScrub();
}

bool AudioEditorController::startRecording()
{
    if (busy()) return false;
    if (playing_) playPause();
    if (!has_document_) {
        document_.setProjectFormat(48000, 2);
        sample_rate_ = 48000;
        channels_ = 2;
        has_document_ = true;
        setState(EditorSessionState::Ready);
        emit documentChanged();
    }
    const QString path = uniqueGeneratedMediaPath(QStringLiteral("recording"));
    if (path.isEmpty()) { setError(tr("无法创建录音文件")); return false; }
    recording_track_ = selected_track_;
    recording_start_frame_ = playhead_frame_;
    // Short captures need a readable recording window, not a whole-song view.
    const auto recordingSpan = static_cast<qint64>(sample_rate_) * 10;
    const auto recordingViewStart = std::max<qint64>(0, recording_start_frame_ - sample_rate_ * 2LL);
    viewport_.setDocumentFrames(std::max(document_.totalFrames(), recordingViewStart + recordingSpan));
    viewport_.setVisibleRange(recordingViewStart, recordingViewStart + recordingSpan);
    recording_revision_ = document_.timelineSnapshot().revision;
    if (channels_ < 1 || channels_ > 2) {
        recording_track_ = -1;
        setError(tr("录音支持单声道或立体声工程"));
        return false;
    }
    if (!recorder_.start(path, sample_rate_, channels_)) { recording_track_ = -1; return false; }
    return true;
}

void AudioEditorController::pauseResumeRecording()
{
    if (recorder_.state() == agplayer::editor::EditorRecordingService::Paused) recorder_.resume();
    else recorder_.pause();
}

void AudioEditorController::stopRecording() { recorder_.stop(); }

bool AudioEditorController::createUntitledDocument(
    const quint32 sampleRate, const quint32 channels, const qint64 frames)
{
    auto candidate = AudioDocument::fromSource(
        AudioSource{std::filesystem::path{}, sampleRate, channels, frames});
    if (candidate.totalFrames() <= 0) {
        return false;
    }
    cancelDocumentLoad();
    cancelSourcePeakCacheJob();
    cancelBpmDetection(false);
    stopPlayback();
    clearViewportWaveformState();
    event_gesture_ = {};
    clearEventSelection();
    document_ = std::move(candidate);
    source_path_.clear();
    project_path_.clear();
    project_sources_.clear();
    project_issues_.clear();
    known_project_issues_.clear();
    time_pitch_ = {};
    time_pitch_preview_active_ = false;
    track_muted_ = false;
    track_solo_ = false;
    track_gain_db_ = 0.0;
    project_export_settings_ = defaultProjectExportSettings(
        static_cast<int>(sampleRate), static_cast<int>(channels));
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
    channel_peaks_.append(QVariant::fromValue(flat));
    primary_peak_pyramid_ = buildPeakPyramid(
        peaksAsChannels(channel_peaks_), frames);
    source_peak_pyramids_.clear();
    const auto untitledSnapshot = document_.timelineSnapshot();
    if (!untitledSnapshot.events.empty() && untitledSnapshot.events.front().source
        && primary_peak_pyramid_) {
        source_peak_pyramids_.emplace(
            sourcePeakKey(*untitledSnapshot.events.front().source),
            primary_peak_pyramid_);
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
    emit trackMixChanged();
    emit timePitchChanged();
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
    if (path.isEmpty() || (busy() && !loading())) {
        setError(tr("请选择本地音频文件"));
        return false;
    }
    DocumentLoadJob job;
    job.kind = DocumentLoadKind::OpenFile;
    job.path = path;
    return enqueueDocumentLoad(std::move(job));
}

bool AudioEditorController::openDroppedUrls(const QList<QUrl>& urls)
{
    if (urls.size() == 1 && urls.front().toLocalFile().endsWith(QStringLiteral(".agproj"), Qt::CaseInsensitive))
        return openProject(urls.front());
    return addFiles(urls);
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
    request.editorSettings = project_editor_settings(
        time_pitch_, track_muted_, track_solo_, track_gain_db_);
    request.sourceRecords = &project_sources_;
    for (const QString& media : owned_media_)
        request.generatedMediaPaths.emplace_back(media.toStdWString());
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
    if (path.isEmpty() || (busy() && !loading())) {
        if (path.isEmpty()) setError(tr("请选择本地工程文件"));
        return false;
    }
    DocumentLoadJob job;
    job.kind = DocumentLoadKind::OpenProject;
    job.path = path;
    return enqueueDocumentLoad(std::move(job));
}

bool AudioEditorController::relinkProjectSource(const QString& sourceId,
                                                 const QUrl& replacement)
{
    bool ok = false;
    const quint64 parsed = sourceId.toULongLong(&ok, 10);
    if (!ok || parsed == 0
        || parsed == std::numeric_limits<quint64>::max()) {
        return false;
    }
    return relinkProjectSource(parsed, replacement);
}

bool AudioEditorController::relinkProjectSource(const quint64 sourceId,
                                                 const QUrl& replacement)
{
    const QString path = local_path(replacement);
    if (!has_document_ || path.isEmpty() || (busy() && !loading())) {
        return false;
    }
    DocumentLoadJob job;
    job.kind = DocumentLoadKind::Relink;
    job.path = path;
    job.sourceId = sourceId;
    job.relinkSnapshot = document_.timelineSnapshot();
    job.relinkMarkers = document_.markers();
    job.relinkSelection = document_.selection();
    job.relinkSources = project_sources_;
    return enqueueDocumentLoad(std::move(job));
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
                              false, agplayer::editor::OutputCommitMode::Overwrite);
}

bool AudioEditorController::exportTo(
    const QUrl& target, const bool selectionOnly, const QString& codecName,
    const int sampleRate, const int channels, const qint64 bitRate,
    const bool keepMetadata, const bool variableBitRate, const int quality)
{
    if (!exportSupported()) return false;
    return exportWithSettings(target, selectionOnly, codecName, sampleRate,
                              channels, bitRate, keepMetadata,
                              variableBitRate, quality, true,
                              agplayer::editor::OutputCommitMode::Overwrite);
}

bool AudioEditorController::exportToConfiguredDirectory(
    const bool selectionOnly)
{
    if (!has_document_ || busy()) return false;
    if (selectionOnly && !document_.selection()) {
        setError(tr("导出范围或路径无效"));
        return false;
    }
    if (project_export_settings_.outputDirectory.trimmed().isEmpty()) {
        setError({});
        emit exportDirectoryRequested();
        return false;
    }
    const QString key = project_export_settings_.codecName.trimmed().toLower();
    QString extension;
    QString encoder = key;
    if (key.isEmpty() || key == QStringLiteral("wav")
        || key.startsWith(QStringLiteral("pcm_"))) {
        extension = QStringLiteral("wav");
        switch (project_export_settings_.bitDepth) {
        case 16:
            encoder = QStringLiteral("pcm_s16le");
            break;
        case 24:
            encoder = QStringLiteral("pcm_s24le");
            break;
        case 32:
            encoder = QStringLiteral("pcm_s32le");
            break;
        default:
            setError(tr("WAV 位深仅支持 16、24 或 32-bit"));
            return false;
        }
    } else if (key == QStringLiteral("flac")) {
        extension = QStringLiteral("flac");
    } else if (key == QStringLiteral("mp3")
               || key == QStringLiteral("libmp3lame")) {
        extension = QStringLiteral("mp3");
        encoder = QStringLiteral("libmp3lame");
    } else if (key == QStringLiteral("m4a") || key == QStringLiteral("aac")) {
        extension = QStringLiteral("m4a");
        encoder = QStringLiteral("aac");
    } else if (key == QStringLiteral("ogg")
               || key == QStringLiteral("libvorbis")) {
        extension = QStringLiteral("ogg");
        encoder = QStringLiteral("libvorbis");
    } else if (key == QStringLiteral("opus")
               || key == QStringLiteral("libopus")) {
        extension = QStringLiteral("opus");
        encoder = QStringLiteral("libopus");
    } else {
        setError(tr("不支持的导出格式"));
        return false;
    }
    QDir directory(project_export_settings_.outputDirectory);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        setError(tr("无法创建输出目录"));
        return false;
    }
    QString stem = QFileInfo(source_path_).completeBaseName();
    if (stem.isEmpty()) stem = QStringLiteral("AgPlayer");
    QString target = directory.filePath(
        QStringLiteral("%1_edited.%2").arg(stem, extension));
    for (int suffix = 2; QFileInfo::exists(target); ++suffix) {
        target = directory.filePath(QStringLiteral("%1_edited_%2.%3")
            .arg(stem).arg(suffix).arg(extension));
    }
    return exportWithSettings(
        QUrl::fromLocalFile(target), selectionOnly, encoder,
        project_export_settings_.sampleRate,
        project_export_settings_.channels,
        project_export_settings_.bitRate,
        project_export_settings_.keepMetadata,
        project_export_settings_.variableBitRate,
        project_export_settings_.quality, false,
        agplayer::editor::OutputCommitMode::CreateNoReplace);
}

bool AudioEditorController::exportWithSettings(
    const QUrl& target, const bool selectionOnly, const QString& codecName,
    const int sampleRate, const int channels, const qint64 bitRate,
    const bool keepMetadata, const bool variableBitRate, const int quality,
    const bool usePersistedDefaults,
    const agplayer::editor::OutputCommitMode commitMode)
{
    const QString path = local_path(target);
    const auto selection = document_.selection();
    if (!requireOnlineProjectSources()) return false;
    if (!has_document_ || path.isEmpty() || (selectionOnly && !selection)) {
        setError(tr("导出范围或路径无效"));
        return false;
    }
    const QFileInfo targetInfo(path);
    const QString targetIdentity = targetInfo.exists() ? targetInfo.canonicalFilePath() : targetInfo.absoluteFilePath();
    for (const auto& source : document_.retainedSources()) {
        if (!source || source->path.empty()) continue;
        const QFileInfo info(QString::fromStdWString(source->path.wstring()));
        const QString identity = info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
#ifdef Q_OS_WIN
        const bool same = targetIdentity.compare(identity, Qt::CaseInsensitive) == 0;
#else
        const bool same = targetIdentity == identity;
#endif
        std::error_code ec;
        if (same || std::filesystem::equivalent(source->path, std::filesystem::path(path.toStdWString()), ec)) {
            setError(tr("导出不能覆盖工程源文件，请选择新文件名"));
            return false;
        }
    }
    const bool defaultArguments = codecName.isEmpty() && sampleRate == 0
        && channels == 0 && bitRate == 0 && keepMetadata
        && variableBitRate && quality == 80;
    ProjectExportSettings effective{codecName, sampleRate, 24, channels, bitRate,
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
    if (!last_export_path_.isEmpty()) {
        last_export_path_.clear();
        emit exportResultChanged();
    }
    setProgress(0.0);
    WriteRequest request;
    request.snapshot = document_.timelineSnapshot();
    applyTrackMix(request.snapshot);
    request.output_path = std::filesystem::path(path.toStdWString());
    request.commit_mode = commitMode;
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
        applySelectionTrack(request.snapshot);
    }
    const auto timePitch = time_pitch_;
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
        setProgress(1.0);
        setState(EditorSessionState::Ready);
        setError({});
        last_export_path_ = QFileInfo(path).absoluteFilePath();
        emit exportResultChanged();
        emit exportSucceeded(last_export_path_);
    });
    const QPointer<AudioEditorController> guard(this);
    watcher->setFuture(QtConcurrent::run(
        [this, request, guard, timePitch] {
        const auto publishProgress = [guard](const float value) {
            if (guard) QMetaObject::invokeMethod(
                guard, [guard, value] {
                    if (guard) guard->setProgress(value);
                }, Qt::QueuedConnection);
        };
        return DocumentRenderPipeline{}.write(
            request, timePitch, &operation_cancelled_, publishProgress);
    }));
    return true;
}

bool AudioEditorController::setSelection(
    const qint64 startFrame, const qint64 endFrame, const int trackIndex)
{
    if (trackIndex < -1 || trackIndex >= 6) return false;
    const bool wasPlaying = playing_;
    const qint64 playbackFrame = currentPlaybackTimelineFrame();
    if (!has_document_ || !document_.setSelection({startFrame, endFrame, trackIndex})) {
        return false;
    }
    setLoopEnabled(true);
    finishTimePitchChange(wasPlaying, playbackFrame);
    (void)syncModifiedFromHistory();
    refreshActions();
    emit documentChanged();
    emit projectChanged();
    return true;
}

bool AudioEditorController::clearSelection()
{
    const bool wasPlaying = playing_;
    const qint64 playbackFrame = currentPlaybackTimelineFrame();
    const bool selectionChanged = document_.clearSelection();
    const bool loopChanged = loop_enabled_;
    setLoopEnabled(false);
    if (!selectionChanged && !loopChanged) {
        return false;
    }
    finishTimePitchChange(wasPlaying, playbackFrame);
    (void)syncModifiedFromHistory();
    refreshActions();
    emit documentChanged();
    emit projectChanged();
    return true;
}

void AudioEditorController::selectEvent(const QString& id)
{
    const auto eventId = parseEventId(id);
    const auto snapshot = document_.timelineSnapshot();
    const bool exists = eventId && std::any_of(snapshot.events.cbegin(),
        snapshot.events.cend(), [eventId](const AudioEvent& event) {
            return event.id == *eventId;
        });
    if (!exists) {
        clearEventSelection();
        return;
    }
    const QString normalized = QString::number(*eventId);
    for (const auto& event : snapshot.events)
        if (event.id == *eventId) setSelectedTrack(event.trackIndex);
    if (selected_event_id_ == normalized) return;
    selected_event_id_ = normalized;
    refreshActions();
    emit selectedEventChanged();
}

void AudioEditorController::clearEventSelection()
{
    if (selected_event_id_.isEmpty()) return;
    selected_event_id_.clear();
    refreshActions();
    emit selectedEventChanged();
}

bool AudioEditorController::clearTimeline()
{
    if (!has_document_ || busy() || !document_.clearTimeline()) return false;
    cancelBpmDetection(false);
    setLoopEnabled(false);
    finishTimelineMutation();
    return true;
}

void AudioEditorController::cancelOperation()
{
    operation_cancelled_.store(true, std::memory_order_release);
    if (bpm_busy_) cancelBpmDetection(true);
    cancelDocumentLoad();
}

void AudioEditorController::deactivate()
{
    if (recording_track_ >= 0) recorder_.stop();
    cancelOperation();
    cancelSourcePeakCacheJob();
    cancelSelectionHandoff();
    selection_drag_controller_.reset();
    handoff_assets_.reset();
    pending_viewport_waveform_job_.reset();
    if (viewport_waveform_cancel_token_) {
        viewport_waveform_cancel_token_->store(true, std::memory_order_release);
    }
    if (playback_adapter_ && (playback_prepared_ || editor_playback_owns_player_)) {
        (void)playback_adapter_->stop();
        releaseEditorPlaybackOutput();
    }
    playback_timer_.stop();
    playing_ = false;
    if (has_document_ && state_ == EditorSessionState::Playing) {
        setState(EditorSessionState::Ready);
    }
    emit playbackChanged();
    emit deactivated();
}

void AudioEditorController::activate()
{
    if (playback_controller_ != nullptr) {
        const bool ownsPlayer = playback_controller_->acquireEditorOutput();
        if (editor_playback_owns_player_ != ownsPlayer) {
            editor_playback_owns_player_ = ownsPlayer;
            emit playbackOwnershipChanged();
        }
    }
    if (has_document_) refreshSourcePeakCachesAsync();
    emit activated();
}

void AudioEditorController::releaseEditorPlaybackOutput() noexcept
{
    if (playback_adapter_) playback_adapter_->release();
    playback_prepared_ = false;
    if (!editor_playback_owns_player_) return;
    editor_playback_owns_player_ = false;
    emit playbackOwnershipChanged();
}

bool AudioEditorController::reduceNoise()
{
    if (!has_document_ || busy() || noise_reduction_watcher_
        || !requireOnlineProjectSources()) {
        return false;
    }
    stopPlayback();
    auto snapshot = document_.timelineSnapshot();
    const auto selected = parseEventId(selected_event_id_);
    const auto clip = std::find_if(snapshot.events.begin(), snapshot.events.end(),
        [selected](const AudioEvent& event) { return selected && event.id == *selected; });
    if (clip == snapshot.events.end()) { setError(tr("请先选择要降噪的片段")); return false; }
    const auto range = document_.selection();
    const Selection replacement{
        std::max(clip->timelineStart, range ? range->start : clip->timelineStart),
        std::min(clip->timelineStart + agplayer::editor::audibleFrames(*clip),
                 range ? range->end : document_.totalFrames())};
    if (!replacement.valid()) { setError(tr("选区未与所选片段相交")); return false; }
    AudioEvent raw = *clip;
    raw.gain = 1.0F;
    raw.mute = false;
    raw.fadeIn = raw.fadeOut = 0;
    raw.envelope.clear();
    snapshot.events = {raw};
    snapshot.tracks = {};
    snapshot.legacyMasterGain = 1.0F;
    const QString output = uniqueGeneratedMediaPath(
        QStringLiteral("noise-reduced"));
    if (output.isEmpty()) {
        setError(tr("无法创建工程媒体目录"));
        return false;
    }
    const std::uint64_t expectedRevision = snapshot.revision;
    operation_cancelled_.store(false, std::memory_order_release);
    setProgress(0.0);
    setError({});
    setState(EditorSessionState::Processing);
    auto* watcher = new QFutureWatcher<NoiseReductionFinalizeResult>(this);
    noise_reduction_watcher_ = watcher;
    connect(watcher,
            &QFutureWatcher<NoiseReductionFinalizeResult>::finished,
            this, [this, watcher, replacement, range, expectedRevision, selected] {
        noise_reduction_watcher_ = nullptr;
        const NoiseReductionFinalizeResult outcome = watcher->result();
        const auto& result = outcome.reduction;
        watcher->deleteLater();
        if (!result.success || !outcome.analysis.success) {
            const bool cancelled = operation_cancelled_.load(
                std::memory_order_acquire);
            if (result.success) {
                std::error_code ignored;
                std::filesystem::remove(result.output_path, ignored);
            }
            setState(EditorSessionState::Ready);
            setError(cancelled ? tr("操作已取消")
                : QString::fromStdString(result.success
                    ? outcome.analysis.message : result.message));
            return;
        }
        AudioSource source{result.output_path, result.sample_rate,
                           result.channels, result.frames};
        const auto currentSelection = document_.selection();
        const bool selectionUnchanged = range
            ? currentSelection == range : !currentSelection.has_value();
        const bool canRegisterSource = nextProjectSourceId().has_value();
        if (document_.timelineSnapshot().revision != expectedRevision
            || !selectionUnchanged || !canRegisterSource) {
            std::error_code ignored;
            std::filesystem::remove(result.output_path, ignored);
            setState(EditorSessionState::Error);
            setError(tr("无法提交降噪结果"));
            return;
        }
        if (!document_.replaceEventWithSource(*selected, std::move(source), replacement.start, replacement.end)) {
            std::error_code ignored;
            std::filesystem::remove(result.output_path, ignored);
            setState(EditorSessionState::Error);
            setError(tr("无法提交降噪结果"));
            return;
        }
        auto generatedPyramid = cachedPeakPyramid(outcome.analysis);
        owned_media_.append(QString::fromStdWString(result.output_path.wstring()));
        if (generatedPyramid) {
            source_peak_pyramids_.insert_or_assign(
                sourcePeakKey(outcome.analysis.source),
                std::move(generatedPyramid));
        }
        finishTimelineMutation();
        setProgress(1.0);
        setState(EditorSessionState::Ready);
        setError({});
    });
    const QPointer<AudioEditorController> guard(this);
    watcher->setFuture(QtConcurrent::run(
        [this, snapshot, replacement, output, guard] {
        NoiseReductionFinalizeResult outcome;
        outcome.reduction = NoiseReducer::reduce(
            snapshot, replacement, std::filesystem::path(output.toStdWString()),
            &operation_cancelled_, [guard](const float value) {
                if (guard) QMetaObject::invokeMethod(
                    guard, [guard, value] {
                        if (guard) guard->setProgress(value);
                    }, Qt::QueuedConnection);
            });
        if (outcome.reduction.success) {
            outcome.analysis = AudioFileAnalyzer::analyze(
                outcome.reduction.output_path, 2'048, &operation_cancelled_);
        }
        return outcome;
    }));
    return true;
}

bool AudioEditorController::beginSelectionHandoff(
    const double sceneX, const double sceneY)
{
    const auto selection = document_.selection();
    if (!has_document_ || busy() || !selection) {
        return false;
    }
    HandoffRequest request;
    request.snapshot = document_.timelineSnapshot();
    request.sourceIdentity = handoffSourceIdentity(request.snapshot);
    if (request.sourceIdentity.isEmpty()) return false;
    const qint64 selectionStartMs = sample_rate_ > 0
        ? selection->start * 1000 / sample_rate_ : 0;
    const qint64 selectionEndMs = sample_rate_ > 0
        ? selection->end * 1000 / sample_rate_ : 0;
    const QString sourceStem = source_path_.isEmpty()
        ? tr("未命名音频") : QFileInfo(source_path_).completeBaseName();
    request.outputFileStem = QStringLiteral("%1_片段_%2-%3")
        .arg(sourceStem, handoffTimeStamp(selectionStartMs),
             handoffTimeStamp(selectionEndMs));
    ensureSelectionHandoffServices();
    applyTrackMix(request.snapshot);
    applySelectionTrack(request.snapshot);
    request.selection = *selection;
    request.timelineRevision = request.snapshot.revision;
    request.renderState = {
        sample_rate_, channels_,
        static_cast<float>(std::pow(10.0, track_gain_db_ / 20.0)),
        track_muted_, time_pitch_.speedPercent(), time_pitch_.pitchCents(),
        time_pitch_.keepPitch(), time_pitch_.formantPreservation()};
    selection_drag_controller_->begin(QPointF(sceneX, sceneY),
                                      std::move(request));
    return true;
}

void AudioEditorController::updateSelectionHandoff(
    const double sceneX, const double sceneY)
{
    if (selection_drag_controller_) {
        selection_drag_controller_->update(QPointF(sceneX, sceneY));
    }
}

void AudioEditorController::releaseSelectionHandoff()
{
    if (selection_drag_controller_) selection_drag_controller_->release();
}

void AudioEditorController::cancelSelectionHandoff()
{
    if (selection_drag_controller_) selection_drag_controller_->cancel();
}

void AudioEditorController::ensureSelectionHandoffServices()
{
    if (handoff_assets_ && selection_drag_controller_) return;
    handoff_assets_ = std::make_unique<HandoffAssetManager>();
    selection_drag_controller_ =
        std::make_unique<SelectionDragController>(handoff_assets_.get());
    connect(selection_drag_controller_.get(),
            &SelectionDragController::errorOccurred,
            this, [this](const QString& message) { setError(message); });
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
        const qint64 frames = event_gesture_.sourceEnd
            - event_gesture_.sourceStart;
        if (timelineStart > std::numeric_limits<qint64>::max() - frames) {
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
        const qint64 frames = sourceEnd - sourceStart;
        if (timelineStart > std::numeric_limits<qint64>::max() - frames) {
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

bool AudioEditorController::trimSharedBoundary(const QString& leftId,
                                               const QString& rightId,
                                               const qint64 sourceBoundary)
{
    const auto left = parseEventId(leftId);
    const auto right = parseEventId(rightId);
    if (!left || !right || sourceBoundary < 0) return false;
    if (event_gesture_.kind == EventGestureKind::SharedBoundary) {
        if (event_gesture_.id != *left || event_gesture_.secondaryId != *right) {
            return false;
        }
        const auto snapshot = document_.timelineSnapshot();
        const auto leftEvent = std::find_if(snapshot.events.cbegin(),
            snapshot.events.cend(), [left](const AudioEvent& event) {
                return event.id == *left;
            });
        const auto rightEvent = std::find_if(snapshot.events.cbegin(),
            snapshot.events.cend(), [right](const AudioEvent& event) {
                return event.id == *right;
            });
        if (leftEvent == snapshot.events.cend()
            || rightEvent == snapshot.events.cend()) {
            return false;
        }
        AudioEvent previewLeft = *leftEvent;
        AudioEvent previewRight = *rightEvent;
        if (!agplayer::editor::reframeSharedBoundary(
                previewLeft, previewRight, sourceBoundary)) return false;
        event_gesture_.sourceEnd = sourceBoundary;
        event_gesture_.pending = sourceBoundary != leftEvent->sourceEnd;
        emit documentChanged();
        return true;
    }
    if (!has_document_ || busy()
        || !document_.trimSharedBoundary(*left, *right, sourceBoundary)) {
        return false;
    }
    finishTimelineMutation();
    return true;
}

bool AudioEditorController::splitEvent(const quint64 id, const qint64 frame)
{
    if (!has_document_ || busy()
        || !document_.splitEventAt(static_cast<agplayer::editor::EventId>(id), frame)) {
        return false;
    }
    finishTimelineMutation();
    const auto after = document_.timelineSnapshot();
    for (const auto& event : after.events)
        if (event.timelineStart == frame && event.trackIndex == selected_track_ && event.id != id) {
            selectEvent(QString::number(event.id));
            break;
        }
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
        : operation == QStringLiteral("fadeOut") ? EventGestureKind::FadeOut
                                               : EventGestureKind::None;
    if (kind == EventGestureKind::None
        || (duplicate && kind != EventGestureKind::Move)) {
        return false;
    }
    event_gesture_ = EventGesture{kind, *eventId, 0, duplicate, false,
        event->timelineStart, event->sourceStart, event->sourceEnd,
        event->fadeOut};
    event_gesture_.trackIndex = event->trackIndex;
    return true;
}

bool AudioEditorController::beginSharedBoundaryGesture(const QString& leftId,
                                                        const QString& rightId)
{
    if (!has_document_ || busy()
        || event_gesture_.kind != EventGestureKind::None) {
        return false;
    }
    const auto leftIdValue = parseEventId(leftId);
    const auto rightIdValue = parseEventId(rightId);
    if (!leftIdValue || !rightIdValue || *leftIdValue == *rightIdValue) {
        return false;
    }
    const auto snapshot = document_.timelineSnapshot();
    const auto left = std::find_if(snapshot.events.cbegin(), snapshot.events.cend(),
        [leftIdValue](const AudioEvent& event) { return event.id == *leftIdValue; });
    const auto right = std::find_if(snapshot.events.cbegin(), snapshot.events.cend(),
        [rightIdValue](const AudioEvent& event) { return event.id == *rightIdValue; });
    if (left == snapshot.events.cend() || right == snapshot.events.cend()) {
        return false;
    }
    AudioEvent previewLeft = *left;
    AudioEvent previewRight = *right;
    if (!agplayer::editor::reframeSharedBoundary(
            previewLeft, previewRight, left->sourceEnd)) return false;
    event_gesture_ = {};
    event_gesture_.kind = EventGestureKind::SharedBoundary;
    event_gesture_.id = *leftIdValue;
    event_gesture_.secondaryId = *rightIdValue;
    event_gesture_.timelineStart = left->timelineStart;
    event_gesture_.sourceStart = left->sourceStart;
    event_gesture_.sourceEnd = left->sourceEnd;
    return true;
}

bool AudioEditorController::endEventGesture()
{
    if (event_gesture_.kind == EventGestureKind::None) return false;
    const EventGesture gesture = event_gesture_;
    const bool waveformPreview = gesture.kind == EventGestureKind::Gain
        || gesture.kind == EventGestureKind::EnvelopePoint;
    event_gesture_ = {};
    if (!gesture.pending) {
        return true;
    }

    bool changed = false;
    std::optional<agplayer::editor::EventId> duplicatedId;
    if (gesture.duplicate) {
        const auto before = document_.timelineSnapshot();
        changed = document_.duplicateEvent(gesture.id, gesture.timelineStart);
        if (changed) {
            const auto after = document_.timelineSnapshot();
            const auto duplicate = std::find_if(after.events.cbegin(),
                after.events.cend(), [&before](const AudioEvent& event) {
                    return std::none_of(before.events.cbegin(),
                        before.events.cend(), [&event](const AudioEvent& item) {
                            return item.id == event.id;
                        });
                });
            if (duplicate != after.events.cend()) duplicatedId = duplicate->id;
        }
    } else if (gesture.kind == EventGestureKind::Move) {
        changed = document_.moveEvent(gesture.id, gesture.timelineStart, gesture.trackIndex);
    } else if (gesture.kind == EventGestureKind::Trim) {
        changed = document_.trimEvent(gesture.id, gesture.sourceStart,
                                      gesture.sourceEnd,
                                      gesture.timelineStart);
    } else if (gesture.kind == EventGestureKind::SharedBoundary) {
        changed = document_.trimSharedBoundary(gesture.id, gesture.secondaryId,
                                               gesture.sourceEnd);
    } else if (gesture.kind == EventGestureKind::FadeOut) {
        changed = document_.setEventFadeOut(gesture.id, gesture.fadeOut);
    } else if (gesture.kind == EventGestureKind::Gain) {
        changed = document_.setEventGain(
            gesture.id, static_cast<float>(gesture.gain));
    } else if (gesture.kind == EventGestureKind::EnvelopePoint) {
        changed = document_.moveEnvelopePoint(
            gesture.id, gesture.originalEnvelopeOffset,
            gesture.envelopeOffset,
            static_cast<float>(gesture.envelopeGain));
    }
    if (!changed) {
        if (waveformPreview) requestViewportWaveform();
        emit documentChanged();
        return false;
    }
    finishTimelineMutation();
    if (gesture.kind == EventGestureKind::Move && !gesture.duplicate)
        setSelectedTrack(gesture.trackIndex);
    if (duplicatedId) selectEvent(QString::number(*duplicatedId));
    return true;
}

bool AudioEditorController::cancelEventGesture()
{
    if (event_gesture_.kind == EventGestureKind::None) return false;
    const bool waveformPreview = event_gesture_.kind == EventGestureKind::Gain
        || event_gesture_.kind == EventGestureKind::EnvelopePoint;
    event_gesture_ = {};
    if (waveformPreview) requestViewportWaveform();
    emit documentChanged();
    return true;
}

bool AudioEditorController::setEventFadeOut(const QString& id,
                                            const qint64 frames)
{
    if (!has_document_ || busy()) return false;
    const auto eventId = parseEventId(id);
    if (!eventId) return false;
    if (event_gesture_.kind != EventGestureKind::None) {
        if (event_gesture_.kind != EventGestureKind::FadeOut
            || event_gesture_.id != *eventId || frames < 0) {
            return false;
        }
        const auto snapshot = document_.timelineSnapshot();
        const auto event = std::find_if(snapshot.events.cbegin(),
            snapshot.events.cend(), [eventId](const AudioEvent& value) {
                return value.id == *eventId;
            });
        if (event == snapshot.events.cend()
            || frames + event->fadeIn > agplayer::editor::audibleFrames(*event)) {
            return false;
        }
        event_gesture_.fadeOut = frames;
        event_gesture_.pending = true;
        emit documentChanged();
        return true;
    }
    if (!document_.setEventFadeOut(*eventId, frames)) return false;
    finishTimelineMutation();
    return true;
}

bool AudioEditorController::setEventFadeIn(const QString& id,
                                           const qint64 frames)
{
    if (!has_document_ || busy()) return false;
    const auto eventId = parseEventId(id);
    if (!eventId || !document_.setEventFadeIn(*eventId, frames)) return false;
    finishTimelineMutation();
    return true;
}

bool AudioEditorController::setEventGain(const QString& id,
                                         const double gain)
{
    if (!has_document_ || busy() || !std::isfinite(gain)) return false;
    const auto eventId = parseEventId(id);
    if (!eventId || !document_.setEventGain(
            *eventId, static_cast<float>(gain))) {
        return false;
    }
    finishTimelineMutation();
    return true;
}

bool AudioEditorController::addEnvelopePoint(const QString& id,
                                             const qint64 offset,
                                             const double gain)
{
    if (!has_document_ || busy() || !std::isfinite(gain)) return false;
    const auto eventId = parseEventId(id);
    if (!eventId || !document_.addEnvelopePoint(
            *eventId, offset, static_cast<float>(gain))) {
        return false;
    }
    finishTimelineMutation();
    return true;
}

bool AudioEditorController::moveEnvelopePoint(
    const QString& id, const qint64 originalOffset,
    const qint64 offset, const double gain)
{
    if (!has_document_ || busy() || !std::isfinite(gain)) return false;
    const auto eventId = parseEventId(id);
    if (!eventId || !document_.moveEnvelopePoint(
            *eventId, originalOffset, offset, static_cast<float>(gain))) {
        return false;
    }
    finishTimelineMutation();
    return true;
}

bool AudioEditorController::removeEnvelopePoint(const QString& id,
                                                const qint64 offset)
{
    if (!has_document_ || busy()) return false;
    const auto eventId = parseEventId(id);
    if (!eventId || !document_.removeEnvelopePoint(*eventId, offset)) {
        return false;
    }
    finishTimelineMutation();
    return true;
}

bool AudioEditorController::beginEventGainGesture(const QString& id)
{
    if (!has_document_ || busy()
        || event_gesture_.kind != EventGestureKind::None) {
        return false;
    }
    const auto eventId = parseEventId(id);
    if (!eventId) return false;
    const auto snapshot = document_.timelineSnapshot();
    const auto event = std::find_if(snapshot.events.cbegin(), snapshot.events.cend(),
        [eventId](const AudioEvent& value) { return value.id == *eventId; });
    if (event == snapshot.events.cend()) return false;
    event_gesture_ = {};
    event_gesture_.kind = EventGestureKind::Gain;
    event_gesture_.id = *eventId;
    event_gesture_.originalGain = event->gain;
    event_gesture_.gain = event->gain;
    return true;
}

bool AudioEditorController::updateEventGainGesture(const double gain)
{
    if (event_gesture_.kind != EventGestureKind::Gain
        || !std::isfinite(gain)) {
        return false;
    }
    event_gesture_.gain = std::clamp(gain, 0.0, 2.0);
    event_gesture_.pending = event_gesture_.gain
        != event_gesture_.originalGain;
    requestViewportWaveform();
    emit documentChanged();
    return true;
}

bool AudioEditorController::endEventGainGesture()
{
    return event_gesture_.kind == EventGestureKind::Gain
        && endEventGesture();
}

bool AudioEditorController::cancelEventGainGesture()
{
    return event_gesture_.kind == EventGestureKind::Gain
        && cancelEventGesture();
}

bool AudioEditorController::beginEnvelopePointGesture(
    const QString& id, const qint64 offset)
{
    if (!has_document_ || busy()
        || event_gesture_.kind != EventGestureKind::None) {
        return false;
    }
    const auto eventId = parseEventId(id);
    if (!eventId) return false;
    const auto snapshot = document_.timelineSnapshot();
    const auto event = std::find_if(snapshot.events.cbegin(), snapshot.events.cend(),
        [eventId](const AudioEvent& value) { return value.id == *eventId; });
    if (event == snapshot.events.cend()) return false;
    const auto point = std::find_if(event->envelope.cbegin(), event->envelope.cend(),
        [offset](const agplayer::editor::EnvelopePoint& value) {
            return value.offset == offset;
        });
    if (point == event->envelope.cend()) return false;
    event_gesture_ = {};
    event_gesture_.kind = EventGestureKind::EnvelopePoint;
    event_gesture_.id = *eventId;
    event_gesture_.originalEnvelopeOffset = offset;
    event_gesture_.envelopeOffset = offset;
    event_gesture_.originalEnvelopeGain = point->gain;
    event_gesture_.envelopeGain = point->gain;
    return true;
}

bool AudioEditorController::updateEnvelopePointGesture(
    const qint64 offset, const double gain)
{
    if (event_gesture_.kind != EventGestureKind::EnvelopePoint
        || !std::isfinite(gain)) {
        return false;
    }
    const auto snapshot = document_.timelineSnapshot();
    const auto event = std::find_if(snapshot.events.cbegin(), snapshot.events.cend(),
        [this](const AudioEvent& value) {
            return value.id == event_gesture_.id;
        });
    if (event == snapshot.events.cend()) return false;
    const qint64 boundedOffset = std::clamp<qint64>(
        offset, 0, agplayer::editor::audibleFrames(*event) - 1);
    const bool collision = std::any_of(
        event->envelope.cbegin(), event->envelope.cend(),
        [this, boundedOffset](const agplayer::editor::EnvelopePoint& point) {
            return point.offset != event_gesture_.originalEnvelopeOffset
                && point.offset == boundedOffset;
        });
    if (collision) return false;
    event_gesture_.envelopeOffset = boundedOffset;
    event_gesture_.envelopeGain = std::clamp(gain, 0.0, 2.0);
    event_gesture_.pending = boundedOffset
            != event_gesture_.originalEnvelopeOffset
        || event_gesture_.envelopeGain
            != event_gesture_.originalEnvelopeGain;
    requestViewportWaveform();
    emit documentChanged();
    return true;
}

bool AudioEditorController::setEventFadeCurve(
    const QString& id, const bool fadeIn, const QString& curveName)
{
    if (!has_document_ || busy()) return false;
    const auto eventId = parseEventId(id);
    const auto curve = fade_curve(curveName);
    if (!eventId || !curve
        || !document_.setEventFadeCurve(*eventId, fadeIn, *curve)) {
        return false;
    }
    finishTimelineMutation();
    return true;
}

bool AudioEditorController::commitEnvelopePointGesture(
    const qint64 offset, const double gain)
{
    if (event_gesture_.kind != EventGestureKind::EnvelopePoint
        || !std::isfinite(gain)) {
        return false;
    }
    const auto snapshot = document_.timelineSnapshot();
    const auto event = std::find_if(snapshot.events.cbegin(), snapshot.events.cend(),
        [this](const AudioEvent& value) {
            return value.id == event_gesture_.id;
        });
    if (event == snapshot.events.cend()) {
        (void)cancelEventGesture();
        setError(tr("音量控制点已不存在"));
        return false;
    }
    const qint64 boundedOffset = std::clamp<qint64>(
        offset, 0, agplayer::editor::audibleFrames(*event) - 1);
    const bool collision = std::any_of(
        event->envelope.cbegin(), event->envelope.cend(),
        [this, boundedOffset](const agplayer::editor::EnvelopePoint& point) {
            return point.offset != event_gesture_.originalEnvelopeOffset
                && point.offset == boundedOffset;
        });
    if (collision) {
        (void)cancelEventGesture();
        setError(tr("音量控制点不能与现有控制点重合"));
        return false;
    }
    event_gesture_.envelopeOffset = boundedOffset;
    event_gesture_.envelopeGain = std::clamp(gain, 0.0, 2.0);
    event_gesture_.pending = boundedOffset
            != event_gesture_.originalEnvelopeOffset
        || event_gesture_.envelopeGain
            != event_gesture_.originalEnvelopeGain;
    return endEventGesture();
}

bool AudioEditorController::endEnvelopePointGesture()
{
    return event_gesture_.kind == EventGestureKind::EnvelopePoint
        && endEventGesture();
}

bool AudioEditorController::cancelEnvelopePointGesture()
{
    return event_gesture_.kind == EventGestureKind::EnvelopePoint
        && cancelEventGesture();
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
    if (!requireOnlineProjectSources()) return false;
    if (!has_document_) return false;
    if (!bpmDetectionSupported()) {
        bpm_busy_ = false;
        bpm_result_ = 0.0;
        bpm_error_ = tr("BPM 检测仅支持单声道或立体声");
        emit bpmChanged();
        return false;
    }

    const quint64 generation = ++bpm_generation_;
    if (bpm_cancel_token_) {
        bpm_cancel_token_->store(true, std::memory_order_release);
    }
    auto cancelToken = std::make_shared<std::atomic_bool>(false);
    bpm_cancel_token_ = cancelToken;
    pending_bpm_job_.reset();
    bpm_busy_ = true;
    bpm_result_ = 0.0;
    bpm_error_.clear();
    emit bpmChanged();

    BpmJob job{generation, std::move(cancelToken),
               document_.timelineSnapshot()};
    if (bpm_watcher_) {
        pending_bpm_job_ = std::move(job);
    } else {
        startBpmJob(std::move(job));
    }
    return true;
}

void AudioEditorController::startBpmJob(BpmJob job)
{
    const quint64 generation = job.generation;
    const auto cancelToken = job.cancelToken;
    const auto observer = bpm_task_observer_;
    auto* watcher = new QFutureWatcher<BpmAnalyzeResult>(this);
    bpm_watcher_ = watcher;
    connect(watcher, &QFutureWatcher<BpmAnalyzeResult>::finished,
            this, [this, watcher, generation, cancelToken] {
        if (bpm_watcher_ == watcher) bpm_watcher_ = nullptr;
        const BpmAnalyzeResult result = watcher->result();
        watcher->deleteLater();
        const bool publish = generation == bpm_generation_
            && !cancelToken->load(std::memory_order_acquire);
        if (publish) {
            bpm_busy_ = false;
            if (bpm_cancel_token_ == cancelToken) {
                bpm_cancel_token_.reset();
            }
            if (result.status == AG_CANCELLED) {
                bpm_result_ = 0.0;
                bpm_error_ = tr("操作已取消");
                emit bpmChanged();
            } else if (result.status != AG_OK || result.bpm <= 0.0) {
                bpm_result_ = 0.0;
                bpm_error_ = result.error.isEmpty() ? tr("BPM 检测失败")
                                                    : result.error;
                emit bpmChanged();
            } else {
                adoptBpm(result.bpm);
            }
        }
        if (!bpm_watcher_ && pending_bpm_job_) {
            BpmJob next = std::move(*pending_bpm_job_);
            pending_bpm_job_.reset();
            if (next.generation == bpm_generation_
                && !next.cancelToken->load(std::memory_order_acquire)) {
                startBpmJob(std::move(next));
            }
        }
    });
    watcher->setFuture(QtConcurrent::run(
        [snapshot = std::move(job.snapshot), cancelToken, observer] {
        if (observer) observer(true);
        QThread* const thread = QThread::currentThread();
        const QThread::Priority previous = thread->priority();
        thread->setPriority(QThread::LowPriority);
        BpmAnalyzeResult result = analyze_bpm(
            std::move(snapshot), cancelToken.get());
        thread->setPriority(previous == QThread::InheritPriority
                                ? QThread::NormalPriority : previous);
        if (observer) observer(false);
        return result;
    }));
}

void AudioEditorController::cancelBpmDetection(const bool publishCancelled)
{
    ++bpm_generation_;
    pending_bpm_job_.reset();
    if (bpm_cancel_token_) {
        bpm_cancel_token_->store(true, std::memory_order_release);
        bpm_cancel_token_.reset();
    }
    const bool changed = bpm_busy_ || bpm_result_ != 0.0 || !bpm_error_.isEmpty();
    bpm_busy_ = false;
    bpm_result_ = 0.0;
    if (publishCancelled) bpm_error_ = tr("操作已取消");
    else bpm_error_.clear();
    if (changed) emit bpmChanged();
}

void AudioEditorController::cancelAndWaitForBpmTaskForTesting()
{
    if (bpm_busy_) cancelBpmDetection(false);
    if (bpm_watcher_) bpm_watcher_->future().waitForFinished();
}

void AudioEditorController::adoptBpm(const double bpm)
{
    const double requestedSpeed = time_pitch_.speedPercent();
    bpm_busy_ = false;
    bpm_result_ = bpm;
    bpm_error_.clear();
    time_pitch_.setOriginalBpm(bpm);
    if (requestedSpeed != 100.0) {
        (void)time_pitch_.setSpeedPercent(requestedSpeed);
    }
    emit bpmChanged();
    emit timePitchChanged();
}

void AudioEditorController::setOriginalBpm(const double value)
{
    if (!bpmDetectionSupported()) return;
    const bool wasPlaying = playing_;
    const qint64 timelineFrame = currentPlaybackTimelineFrame();
    const double previousOriginal = time_pitch_.originalBpm();
    const double previousTarget = time_pitch_.targetBpm();
    const double previousSpeed = time_pitch_.speedPercent();
    time_pitch_.setOriginalBpm(value);
    if (previousOriginal == time_pitch_.originalBpm()
        && previousTarget == time_pitch_.targetBpm()
        && previousSpeed == time_pitch_.speedPercent()) {
        return;
    }
    if (bpm_busy_) cancelBpmDetection(false);
    markEditorSettingsDirty();
    finishTimePitchChange(wasPlaying, timelineFrame);
    emit timePitchChanged();
}

bool AudioEditorController::setTargetBpm(const double value)
{
    if (!timePitchSupported()) return false;
    if (std::abs(time_pitch_.targetBpm() - value) < 0.000001) return true;
    const bool wasPlaying = playing_;
    const qint64 timelineFrame = currentPlaybackTimelineFrame();
    const bool changed = time_pitch_.setTargetBpm(value);
    if (changed) {
        if (bpm_busy_) cancelBpmDetection(false);
        markEditorSettingsDirty();
        finishTimePitchChange(wasPlaying, timelineFrame);
        emit timePitchChanged();
    }
    return changed;
}

bool AudioEditorController::setSpeedPercent(const double value)
{
    if (!timePitchSupported()) return false;
    if (std::abs(time_pitch_.speedPercent() - value) < 0.000001) return true;
    const bool wasPlaying = playing_;
    const qint64 timelineFrame = currentPlaybackTimelineFrame();
    const bool changed = time_pitch_.setSpeedPercent(value);
    if (changed) {
        markEditorSettingsDirty();
        finishTimePitchChange(wasPlaying, timelineFrame);
        emit timePitchChanged();
    }
    return changed;
}

bool AudioEditorController::resetTimePitch()
{
    const double original = time_pitch_.originalBpm();
    if (std::abs(time_pitch_.speedPercent() - 100.0) < 0.000001
        && time_pitch_.keepPitch()
        && !time_pitch_.formantPreservation()
        && time_pitch_.pitchCents() == 0) {
        return true;
    }
    if (bpm_busy_) cancelBpmDetection(false);
    const bool wasPlaying = playing_;
    const qint64 timelineFrame = currentPlaybackTimelineFrame();
    agplayer::editor::TimePitchSession reset;
    if (original > 0.0) reset.setOriginalBpm(original);
    time_pitch_ = reset;
    markEditorSettingsDirty();
    finishTimePitchChange(wasPlaying, timelineFrame);
    emit timePitchChanged();
    return true;
}

void AudioEditorController::setKeepPitch(const bool value)
{
    if (!timePitchSupported()) return;
    if (time_pitch_.keepPitch() == value) return;
    const bool wasPlaying = playing_;
    const qint64 timelineFrame = currentPlaybackTimelineFrame();
    time_pitch_.setKeepPitch(value);
    markEditorSettingsDirty();
    finishTimePitchChange(wasPlaying, timelineFrame);
    emit timePitchChanged();
}

void AudioEditorController::setFormantPreservation(const bool value)
{
    if (!formantPreservationSupported()) return;
    if (time_pitch_.formantPreservation() == value) return;
    const bool wasPlaying = playing_;
    const qint64 timelineFrame = currentPlaybackTimelineFrame();
    time_pitch_.setFormantPreservation(value);
    markEditorSettingsDirty();
    finishTimePitchChange(wasPlaying, timelineFrame);
    emit timePitchChanged();
}

bool AudioEditorController::setPitch(const int semitones, const int cents)
{
    if (!timePitchSupported()) return false;
    if (time_pitch_.pitchCents() == semitones * 100 + cents) return true;
    const bool wasPlaying = playing_;
    const qint64 timelineFrame = currentPlaybackTimelineFrame();
    const bool changed = time_pitch_.setPitch(semitones, cents);
    if (changed) {
        markEditorSettingsDirty();
        finishTimePitchChange(wasPlaying, timelineFrame);
        emit timePitchChanged();
    }
    return changed;
}

QString AudioEditorController::generatedMediaDirectory() const
{
    if (!project_path_.isEmpty()) {
        const QFileInfo project(project_path_);
        return project.dir().filePath(
            project.completeBaseName() + QStringLiteral(".media"));
    }
    QString root = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty()) root = QDir::currentPath();
    return QDir(root).filePath(QStringLiteral("audio-editor-media"));
}

QString AudioEditorController::uniqueGeneratedMediaPath(
    const QString& prefix) const
{
    QDir directory(generatedMediaDirectory());
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        return {};
    }
    const QString timestamp = QString::number(
        QDateTime::currentMSecsSinceEpoch());
    for (int suffix = 0; suffix < 10'000; ++suffix) {
        const QString name = suffix == 0
            ? QStringLiteral("%1-%2.wav").arg(prefix, timestamp)
            : QStringLiteral("%1-%2-%3.wav").arg(prefix, timestamp)
                  .arg(suffix);
        const QString candidate = directory.filePath(name);
        const QString partial = candidate.left(candidate.size() - 4)
            + QStringLiteral(".partial.wav");
        if (!QFileInfo::exists(candidate)
            && !QFileInfo::exists(partial)) {
            return candidate;
        }
    }
    return {};
}

bool AudioEditorController::clearDocument()
{
    if (busy() && !loading()) return false;
    if (loading()) cancelDocumentLoad();
    if (modified_ && !allow_document_replace_) {
        pending_open_url_.clear();
        pending_open_is_project_ = false;
        pending_clear_document_ = true;
        emit discardConfirmationRequested();
        return false;
    }
    allow_document_replace_ = false;
    pending_clear_document_ = false;
    cancelDocumentLoad();
    cancelBpmDetection(false);
    cancelSourcePeakCacheJob();
    stopPlayback();
    cancelSelectionHandoff();
    selection_drag_controller_.reset();
    handoff_assets_.reset();
    event_gesture_ = {};
    if (active_tool_ != QStringLiteral("select")) {
        active_tool_ = QStringLiteral("select");
        emit toolChanged();
    }
    clearEventSelection();
    document_ = AudioDocument{};
    cleanupUnreferencedSessionMedia();
    source_path_.clear();
    project_path_.clear();
    project_sources_.clear();
    project_issues_.clear();
    known_project_issues_.clear();
    time_pitch_ = {};
    time_pitch_preview_active_ = false;
    track_muted_ = false;
    track_solo_ = false;
    track_gain_db_ = 0.0;
    project_export_settings_ = defaultProjectExportSettings();
    format_name_.clear();
    sample_rate_ = 0;
    channels_ = 0;
    bits_per_sample_ = 0;
    bit_rate_ = 0;
    channel_peaks_.clear();
    primary_peak_pyramid_.reset();
    source_peak_pyramids_.clear();
    has_document_ = false;
    position_ms_ = 0;
    playhead_frame_ = 0;
    setViewportDocumentFrames(0);
    markProjectClean();
    setState(EditorSessionState::Empty);
    setError({});
    refreshActions();
    emit waveformChanged();
    emit playbackChanged();
    emit documentChanged();
    emit projectChanged();
    emit trackMixChanged();
    emit timePitchChanged();
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
    if (!item) {
        return false;
    }
    if (!item->enabled) return false;
    if (id == QStringLiteral("editor.export") && !requireOnlineProjectSources()) return false;
    if (id == QStringLiteral("editor.open")) {
        emit openRequested();
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
        const auto event = parseEventId(selected_event_id_);
        return event && splitEvent(*event, frame);
    }
    if (id == QStringLiteral("editor.merge")) {
        const auto pair = mergePairCoveredBySelection(
            document_.timelineSnapshot(), document_.selection());
        return pair && mergeEvents(pair->first, pair->second);
    }
    const auto selectedEvent = parseEventId(selected_event_id_);
    const auto snapshot = document_.timelineSnapshot();
    const bool hasSelectedEvent = selectedEvent && std::any_of(
        snapshot.events.cbegin(), snapshot.events.cend(),
        [selectedEvent](const AudioEvent& event) {
            return event.id == *selectedEvent;
        });
    bool changed = false;
    std::optional<agplayer::editor::EventId> pastedEvent;
    if (id == QStringLiteral("editor.cut")) {
        changed = hasSelectedEvent && document_.cutEvent(*selectedEvent);
    }
    else if (id == QStringLiteral("editor.copy")) {
        changed = hasSelectedEvent && document_.copyEvent(*selectedEvent);
    }
    else if (id == QStringLiteral("editor.paste")) {
        const qint64 frame = playhead_frame_;
        changed = document_.pasteAt(frame, selected_track_);
        if (changed) {
            const auto pasted = document_.timelineSnapshot();
            for (const AudioEvent& event : pasted.events) {
                const bool existed = std::any_of(
                    snapshot.events.cbegin(), snapshot.events.cend(),
                    [&event](const AudioEvent& prior) {
                        return prior.id == event.id;
                    });
                if (!existed && (!pastedEvent || event.id < *pastedEvent)) {
                    pastedEvent = event.id;
                }
            }
        }
    }
    else if (id == QStringLiteral("editor.deleteSelection")) {
        changed = hasSelectedEvent && document_.deleteEvent(*selectedEvent);
    }
    else if (id == QStringLiteral("editor.cropToSelection"))
        changed = hasSelectedEvent && document_.cropEventToSelection(*selectedEvent);
    else if (id == QStringLiteral("editor.silenceSelection")) {
        changed = hasSelectedEvent ? document_.silenceEvent(*selectedEvent)
                                   : document_.silenceSelection();
    }
    else if (id == QStringLiteral("editor.fadeIn")) {
        changed = hasSelectedEvent ? document_.fadeEvent(*selectedEvent, true)
                                   : document_.fadeIn();
    }
    else if (id == QStringLiteral("editor.fadeOut")) {
        changed = hasSelectedEvent ? document_.fadeEvent(*selectedEvent, false)
                                   : document_.fadeOut();
    }
    if (!changed) return false;
    if (id != QStringLiteral("editor.copy")) {
        finishTimelineMutation();
        if (id == QStringLiteral("editor.cropToSelection")) {
            viewport_.setVisibleRange(0, document_.totalFrames());
            setLoopEnabled(true);
            if (const auto croppedSelection = document_.selection()) {
                (void)seekFrame(croppedSelection->start);
            }
        }
        if (pastedEvent) selectEvent(QString::number(*pastedEvent));
        return true;
    }
    refreshActions();
    emit waveformChanged();
    emit documentChanged();
    return true;
}

bool AudioEditorController::playPause()
{
    // A keyboard shortcut must not bypass the disabled transport during capture.
    if (recording_track_ >= 0) return false;
    if (!has_document_ || !playbackSupported()
        || !playback_adapter_ || !playback_adapter_->available()) {
        setError(has_document_ && channels_ > 2
            ? tr("编辑预览仅支持单声道或立体声")
            : tr("播放核心不可用"));
        return false;
    }
    if (playing_) {
        if (playback_adapter_->pause() != AG_OK) {
            setError(tr("无法暂停编辑预览"));
            return false;
        }
        playing_ = false;
        playback_timer_.stop();
        setState(EditorSessionState::Ready);
        emit playbackChanged();
        return true;
    }
    bool accepted = true;
    emit playbackRequested(&accepted);
    if (!accepted) return false;
    const auto selection = document_.selection();
    if (selection && sample_rate_ > 0) {
        const qint64 start = selection->start * 1'000 / sample_rate_;
        const qint64 end = selection->end * 1'000 / sample_rate_;
        if (position_ms_ < start || position_ms_ >= end) {
            if (updatePersistedPlayhead(selection->start, start)) {
                emit documentChanged();
            }
        }
    }
    ag_playback_snapshot snapshot{};
    if (!playback_prepared_
        || playback_adapter_->snapshot(snapshot) != AG_OK
        || snapshot.state == AG_STOPPED || snapshot.state == AG_ERROR) {
        if (!preparePlayback()) {
            setState(EditorSessionState::Error);
            return false;
        }
    }
    if (playback_adapter_->play() != AG_OK) {
        releaseEditorPlaybackOutput();
        setState(EditorSessionState::Error);
        setError(tr("无法开始编辑预览"));
        return false;
    }
    playing_ = true;
    playback_timer_.start();
    setState(EditorSessionState::Playing);
    emit playbackChanged();
    return true;
}

bool AudioEditorController::pauseForPlaybackHandoff()
{
    if (playing_) {
        const qint64 frame = currentPlaybackTimelineFrame();
        if (!playPause()) return false;
        const qint64 position = sample_rate_ > 0 ? frame * 1000 / sample_rate_ : position_ms_;
        const bool modifiedChanged = updatePersistedPlayhead(frame, position);
        emit playbackChanged();
        if (modifiedChanged) emit documentChanged();
    }
    if (playback_controller_ && playback_controller_->editorOutputOwned()) {
        playback_controller_->releaseEditorOutput(false);
        if (playback_controller_->editorOutputOwned()) return false;
        releaseEditorPlaybackOutput();
    }
    return true;
}

bool AudioEditorController::stopPlayback()
{
    if (state_ == EditorSessionState::Error || !playback_adapter_
        || !playback_adapter_->available()) return false;
    const bool wasActive = playing_ || position_ms_ != 0;
    const bool playheadChanged = playhead_frame_ != 0 || position_ms_ != 0;
    if (playback_prepared_ && playback_adapter_->stop() != AG_OK) {
        setError(tr("无法停止编辑预览"));
        return false;
    }
    playback_timer_.stop();
    playing_ = false;
    const bool modifiedChanged = updatePersistedPlayhead(0, 0);
    if (has_document_ && state_ == EditorSessionState::Playing) {
        setState(EditorSessionState::Ready);
    }
    if (wasActive) emit playbackChanged();
    if (modifiedChanged && playheadChanged) emit documentChanged();
    return true;
}

bool AudioEditorController::seekMs(const qint64 value)
{
    if (!has_document_ || state_ == EditorSessionState::Error
        || value < 0 || value > durationMs()) return false;
    if (playback_prepared_ && playback_adapter_
        && playback_adapter_->available()) {
        const qint64 previewPosition = time_pitch_preview_active_
            ? static_cast<qint64>(std::llround(
                static_cast<double>(value) * 100.0
                / time_pitch_.speedPercent()))
            : value;
        if (playback_adapter_->seek(previewPosition) != AG_OK) {
            releaseEditorPlaybackOutput();
            setError(tr("无法定位编辑预览"));
            return false;
        }
    }
    const qint64 playhead = sample_rate_ > 0 ? value * sample_rate_ / 1'000 : 0;
    const bool modifiedChanged = updatePersistedPlayhead(playhead, value);
    refreshActions();
    emit playbackChanged();
    if (modifiedChanged) emit documentChanged();
    return true;
}

bool AudioEditorController::seekFrame(const qint64 frame)
{
    if (!has_document_ || state_ == EditorSessionState::Error
        || frame < 0 || frame > document_.totalFrames()) return false;
    const qint64 positionMs = sample_rate_ > 0 ? frame * 1'000 / sample_rate_ : 0;
    if (playback_prepared_ && playback_adapter_
        && playback_adapter_->available()) {
        const qint64 previewPosition = time_pitch_preview_active_
            ? static_cast<qint64>(std::llround(
                static_cast<double>(positionMs) * 100.0
                / time_pitch_.speedPercent()))
            : positionMs;
        if (playback_adapter_->seek(previewPosition) != AG_OK) {
            releaseEditorPlaybackOutput();
            setError(tr("无法定位编辑预览"));
            return false;
        }
    }
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
    updatePlaybackMix();
    emit playbackChanged();
}

double AudioEditorController::effectivePlaybackVolume() const noexcept
{
    return volume_;
}

void AudioEditorController::updatePlaybackMix() noexcept
{
    if (!player_) return;
    (void)ag_player_set_volume(
        player_, static_cast<float>(effectivePlaybackVolume()));
    (void)ag_player_set_replay_gain(
        player_, 0.0F, 0.0F, 0);
}

void AudioEditorController::applyTrackMix(
    agplayer::editor::TimelineSnapshot& snapshot) const noexcept
{
    const float gain = static_cast<float>(std::pow(10.0, track_gain_db_ / 20.0));
    for (AudioEvent& event : snapshot.events) {
        if (track_muted_) {
            event.mute = true;
        } else {
            event.gain *= gain;
        }
    }
}

void AudioEditorController::setTrackMuted(const bool value)
{
    const bool solo = value ? false : track_solo_;
    if (track_muted_ == value && track_solo_ == solo) return;
    const bool wasPlaying = playing_;
    const qint64 frame = currentPlaybackTimelineFrame();
    track_muted_ = value;
    track_solo_ = solo;
    markEditorSettingsDirty();
    updatePlaybackMix();
    finishTimePitchChange(wasPlaying, frame);
    emit trackMixChanged();
}

void AudioEditorController::setTrackSolo(const bool value)
{
    const bool muted = value ? false : track_muted_;
    if (track_solo_ == value && track_muted_ == muted) return;
    const bool wasPlaying = playing_;
    const qint64 frame = currentPlaybackTimelineFrame();
    track_solo_ = value;
    track_muted_ = muted;
    markEditorSettingsDirty();
    updatePlaybackMix();
    finishTimePitchChange(wasPlaying, frame);
    emit trackMixChanged();
}

void AudioEditorController::setTrackGainDb(const double value)
{
    if (!std::isfinite(value)) return;
    const double bounded = std::clamp(value, -60.0, 12.0);
    if (qFuzzyCompare(track_gain_db_, bounded)) return;
    const bool wasPlaying = playing_;
    const qint64 frame = currentPlaybackTimelineFrame();
    track_gain_db_ = bounded;
    markEditorSettingsDirty();
    updatePlaybackMix();
    finishTimePitchChange(wasPlaying, frame);
    emit trackMixChanged();
}

void AudioEditorController::setLoopEnabled(const bool enabled)
{
    if (loop_enabled_ == enabled) return;
    loop_enabled_ = enabled;
    emit playbackChanged();
}

qint64 AudioEditorController::currentPlaybackTimelineFrame() const noexcept
{
    qint64 frame = playhead_frame_;
    if (!playing_ || !playback_prepared_ || !playback_adapter_
        || sample_rate_ <= 0) {
        return frame;
    }
    ag_playback_snapshot snapshot{};
    if (playback_adapter_->snapshot(snapshot) != AG_OK) return frame;
    const qint64 timelinePosition = time_pitch_preview_active_
        ? static_cast<qint64>(std::llround(
            static_cast<double>(snapshot.position_ms)
            * time_pitch_.speedPercent() / 100.0))
        : snapshot.position_ms;
    frame = timelinePosition * sample_rate_ / 1'000;
    return std::clamp<qint64>(frame, 0, document_.totalFrames());
}

void AudioEditorController::finishTimePitchChange(
    const bool wasPlaying, const qint64 timelineFrame)
{
    playback_prepared_ = false;
    time_pitch_preview_active_ = false;
    if (!wasPlaying) return;

    const qint64 frame = std::clamp<qint64>(
        timelineFrame, 0, document_.totalFrames());
    const qint64 position = sample_rate_ > 0
        ? frame * 1'000 / sample_rate_ : 0;
    const bool modifiedChanged = updatePersistedPlayhead(frame, position);
    if (!preparePlayback()
        || playback_adapter_->play() != AG_OK) {
        playing_ = false;
        playback_timer_.stop();
        setState(EditorSessionState::Error);
        if (error_message_.isEmpty()) setError(tr("无法继续编辑预览"));
    } else {
        playing_ = true;
        playback_timer_.start();
        setState(EditorSessionState::Playing);
    }
    emit playbackChanged();
    if (modifiedChanged) emit documentChanged();
}

bool AudioEditorController::preparePlayback()
{
    if (!has_document_ || !playback_adapter_
        || !playback_adapter_->available() || !requireOnlineProjectSources()) {
        return false;
    }
    auto snapshot = document_.timelineSnapshot();
    agplayer::editor::EditorPlaybackParameters parameters;
    applyTrackMix(snapshot);
    applySelectionTrack(snapshot);
    parameters.speed_ratio = time_pitch_.speedPercent() / 100.0;
    parameters.keep_pitch = time_pitch_.keepPitch();
    parameters.pitch_cents = time_pitch_.pitchCents();
    parameters.formant_preservation = time_pitch_.formantPreservation();
    QString error;
    if (!playback_adapter_->prepare(std::move(snapshot), parameters, error)) {
        releaseEditorPlaybackOutput();
        setError(error.isEmpty() ? tr("无法载入编辑预览") : error);
        return false;
    }
    playback_prepared_ = true;
    const bool processed = std::abs(parameters.speed_ratio - 1.0) > 0.000001
        || parameters.pitch_cents != 0 || parameters.formant_preservation;
    if (time_pitch_preview_active_ != processed) {
        time_pitch_preview_active_ = processed;
        emit timePitchChanged();
    }
    updatePlaybackMix();
    const qint64 previewPosition = time_pitch_preview_active_
        ? static_cast<qint64>(std::llround(
            static_cast<double>(position_ms_) * 100.0
            / time_pitch_.speedPercent()))
        : position_ms_;
    if (previewPosition > 0 && playback_adapter_->seek(previewPosition) != AG_OK) {
        releaseEditorPlaybackOutput();
        setError(tr("无法定位编辑预览"));
        return false;
    }
    setError({});
    return true;
}

void AudioEditorController::pollPlayback()
{
    if (!playback_adapter_ || !playback_prepared_) return;
    ag_playback_snapshot snapshot{};
    if (playback_adapter_->snapshot(snapshot) != AG_OK) {
        releaseEditorPlaybackOutput();
        playing_ = false;
        playback_timer_.stop();
        setState(EditorSessionState::Error);
        setError(tr("无法读取编辑预览状态"));
        emit playbackChanged();
        return;
    }
    const qint64 positionMs = time_pitch_preview_active_
        ? static_cast<qint64>(std::llround(
            static_cast<double>(snapshot.position_ms)
            * time_pitch_.speedPercent() / 100.0))
        : snapshot.position_ms;
    const qint64 playhead = sample_rate_ > 0
        ? positionMs * sample_rate_ / 1'000 : 0;
    bool modifiedChanged = updatePersistedPlayhead(playhead, positionMs);
    const auto selection = document_.selection();
    if (selection && sample_rate_ > 0) {
        const qint64 start = selection->start * 1'000 / sample_rate_;
        const qint64 end = selection->end * 1'000 / sample_rate_;
        if (position_ms_ >= end) {
            if (loop_enabled_) {
                const qint64 previewStart = time_pitch_preview_active_
                    ? static_cast<qint64>(std::llround(
                        static_cast<double>(start) * 100.0
                        / time_pitch_.speedPercent()))
                    : start;
                if (playback_adapter_->seek(previewStart) != AG_OK) {
                    releaseEditorPlaybackOutput();
                    playing_ = false;
                    playback_timer_.stop();
                    setState(EditorSessionState::Error);
                    setError(tr("无法循环编辑预览"));
                    emit playbackChanged();
                    if (modifiedChanged) emit documentChanged();
                    return;
                }
                if (snapshot.state != AG_PLAYING
                    && playback_adapter_->play() != AG_OK) {
                    releaseEditorPlaybackOutput();
                    playing_ = false;
                    playback_timer_.stop();
                    setState(EditorSessionState::Error);
                    setError(tr("无法恢复循环编辑预览"));
                    emit playbackChanged();
                    if (modifiedChanged) emit documentChanged();
                    return;
                }
                snapshot.state = AG_PLAYING;
                modifiedChanged = updatePersistedPlayhead(selection->start,
                                                          start)
                    || modifiedChanged;
            } else {
                if (playback_adapter_->pause() != AG_OK) {
                    playing_ = false;
                    playback_timer_.stop();
                    setState(EditorSessionState::Error);
                    setError(tr("无法暂停编辑预览"));
                    emit playbackChanged();
                    if (modifiedChanged) emit documentChanged();
                    return;
                }
                playing_ = false;
                playback_timer_.stop();
                setState(EditorSessionState::Ready);
            }
        }
    }
    if (snapshot.state == AG_ERROR) {
        releaseEditorPlaybackOutput();
        playing_ = false;
        playback_timer_.stop();
        setState(EditorSessionState::Error);
        setError(tr("编辑预览解码失败"));
    } else if (snapshot.state != AG_PLAYING && playing_) {
        playing_ = false;
        playback_timer_.stop();
        setState(EditorSessionState::Ready);
    }
    emit playbackChanged();
    if (modifiedChanged) emit documentChanged();
}

bool AudioEditorController::enqueueDocumentLoad(DocumentLoadJob job)
{
    ++document_load_generation_;
    pending_document_load_job_.reset();
    if (document_load_cancel_token_) {
        document_load_cancel_token_->store(true, std::memory_order_release);
    }
    auto cancelToken = std::make_shared<std::atomic_bool>(false);
    document_load_cancel_token_ = cancelToken;
    job.generation = document_load_generation_;
    job.cancelToken = cancelToken;
    setError({});
    setProgress(0.05);
    setDocumentLoading(true);
    setState(EditorSessionState::Processing);
    if (document_load_watcher_) {
        pending_document_load_job_ = std::move(job);
    } else {
        startDocumentLoadJob(std::move(job));
    }
    return true;
}

bool AudioEditorController::openFileWhenReady(
    const QUrl& source, QObject* const context,
    std::function<void()> onLoaded)
{
    if (context == nullptr || !onLoaded || !openFile(source)) return false;
    const quint64 generation = document_load_generation_;
    connect(this, &AudioEditorController::loadingChanged, context,
            [this, generation, onLoaded = std::move(onLoaded)]() mutable {
                if (loading()) return;
                if (generation == document_load_generation_
                    && has_document_
                    && state_ != EditorSessionState::Error
                    && error_message_.isEmpty()) {
                    onLoaded();
                }
            }, Qt::SingleShotConnection);
    return true;
}

void AudioEditorController::startDocumentLoadJob(DocumentLoadJob job)
{
    if (job.kind == DocumentLoadKind::Append) {
        if (job.appendIndex == 0) {
            import_results_.clear();
            emit importResultsChanged();
        }
        if (job.generation != document_load_generation_
            || job.cancelToken->load(std::memory_order_acquire)) return;
        const auto snapshot = document_.timelineSnapshot();
        bool possibleSlot = job.recordingTrack >= 0;
        for (int track = 0; track < 6; ++track) {
            if (job.appendUsedTracks[track] || (job.recordingTrack >= 0 && track != job.recordingTrack)) continue;
            if (std::none_of(snapshot.events.begin(), snapshot.events.end(), [&](const AudioEvent& event) {
                    return event.trackIndex == track && job.appendFrame >= event.timelineStart
                        && job.appendFrame < event.timelineStart + agplayer::editor::audibleFrames(event);
                })) {
                possibleSlot = true;
                break;
            }
        }
        job.appendSkipMessage = possibleSlot ? QString{} : tr("冻结导入位置没有可用轨道，未覆盖原素材");
        if (snapshot.events.size() >= agplayer::editor::kMaxTimelineEvents)
            job.appendSkipMessage = tr("工程片段容量不足，未覆盖原素材");
    }
    const quint64 generation = job.generation;
    const auto cancelToken = job.cancelToken;
    const auto observer = document_load_task_observer_;
    auto* watcher = new QFutureWatcher<DocumentLoadOutcome>(this);
    document_load_watcher_ = watcher;
    connect(watcher, &QFutureWatcher<DocumentLoadOutcome>::finished,
            this, [this, watcher, generation, cancelToken] {
        if (document_load_watcher_ == watcher) document_load_watcher_ = nullptr;
        if (!cancelToken->load(std::memory_order_acquire)
            && generation == document_load_generation_) {
            DocumentLoadOutcome outcome = watcher->result();
            if (outcome.kind == DocumentLoadKind::Append) {
                applyDocumentLoadOutcome(std::move(outcome));
                if (generation == document_load_generation_
                    && !cancelToken->load(std::memory_order_acquire)
                    && !pending_document_load_job_) {
                    if (document_load_cancel_token_ == cancelToken) document_load_cancel_token_.reset();
                    setDocumentLoading(false);
                }
            } else if (outcome.success) {
                if (document_load_cancel_token_ == cancelToken) document_load_cancel_token_.reset();
                applyDocumentLoadOutcome(std::move(outcome));
                setDocumentLoading(false);
            } else {
                if (document_load_cancel_token_ == cancelToken) document_load_cancel_token_.reset();
                setProgress(0.0);
                setError(std::move(outcome.message));
                setState(EditorSessionState::Error);
                setDocumentLoading(false);
            }
        }
        watcher->deleteLater();
        if (!document_load_watcher_ && pending_document_load_job_) {
            DocumentLoadJob next = std::move(*pending_document_load_job_);
            pending_document_load_job_.reset();
            if (next.generation == document_load_generation_
                && !next.cancelToken->load(std::memory_order_acquire)) {
                startDocumentLoadJob(std::move(next));
            }
        }
    });
    watcher->setFuture(QtConcurrent::run(
        [job = std::move(job), observer]() mutable {
            if (observer) observer(true);
            DocumentLoadOutcome outcome;
            outcome.kind = job.kind;
            outcome.path = job.path;
            outcome.sourceId = job.sourceId;
            if (job.kind == DocumentLoadKind::Append)
                outcome.appendJob = std::make_shared<DocumentLoadJob>(job);
            try {
                if (job.cancelToken->load(std::memory_order_acquire)) {
                    outcome.message = QStringLiteral("document load cancelled");
                } else if (job.kind == DocumentLoadKind::Append) {
                    const auto& url = job.appendUrls.at(job.appendIndex);
                    const auto path = local_path(url);
                    outcome.path = path.isEmpty() ? url.toDisplayString() : path;
                    if (!job.appendSkipMessage.isEmpty()) {
                        outcome.analysis.message = job.appendSkipMessage.toStdString();
                    } else if (!url.isLocalFile() || path.isEmpty()) {
                        outcome.analysis.message = "请选择本地音频文件";
                    } else {
                        outcome.analysis = AudioFileAnalyzer::analyze(
                            std::filesystem::path(path.toStdWString()), 2048, job.cancelToken.get());
                    }
                    outcome.success = !job.cancelToken->load(std::memory_order_acquire);
                } else if (job.kind == DocumentLoadKind::OpenFile) {
                    outcome.analysis = AudioFileAnalyzer::analyze(
                        std::filesystem::path(job.path.toStdWString()), 2'048,
                        job.cancelToken.get());
                    outcome.success = outcome.analysis.success
                        && !job.cancelToken->load(std::memory_order_acquire);
                    outcome.message = QString::fromStdString(
                        outcome.analysis.message);
                } else if (job.kind == DocumentLoadKind::OpenProject) {
                    outcome.project = std::make_shared<ProjectLoadResult>(
                        ProjectDocument::load(job.path,
                                              job.cancelToken.get()));
                    outcome.success = outcome.project->ok()
                        && !job.cancelToken->load(std::memory_order_acquire);
                    outcome.message = outcome.project->message;
                } else {
                    outcome.analysis = AudioFileAnalyzer::analyze(
                        std::filesystem::path(job.path.toStdWString()), 2'048,
                        job.cancelToken.get());
                    if (!outcome.analysis.success
                        || job.cancelToken->load(std::memory_order_acquire)) {
                        outcome.message = QString::fromStdString(
                            outcome.analysis.message);
                    } else {
                        AudioDocument candidate = AudioDocument::fromSnapshot(
                            std::move(job.relinkSnapshot));
                        bool restored = !candidate.timelineSnapshot().events.empty();
                        for (const auto& marker : job.relinkMarkers) {
                            restored = restored && candidate.addMarker(marker);
                        }
                        if (restored && job.relinkSelection) {
                            restored = candidate.setSelection(
                                *job.relinkSelection);
                        }
                        if (!restored) {
                            outcome.message = QStringLiteral(
                                "could not copy project for relink");
                        } else {
                            const agplayer::editor::ProjectRelinkResult relink =
                                ProjectDocument::relink(
                                    candidate, job.relinkSources,
                                    job.sourceId, job.path,
                                    job.cancelToken.get());
                            outcome.success = relink.ok()
                                && !job.cancelToken->load(
                                    std::memory_order_acquire);
                            outcome.message = relink.message;
                            if (outcome.success) {
                                outcome.relinkDocument =
                                    std::make_shared<AudioDocument>(
                                        std::move(candidate));
                                outcome.relinkSources =
                                    std::move(job.relinkSources);
                            }
                        }
                    }
                }
            } catch (...) {
                outcome.success = false;
                outcome.message = QStringLiteral("document load failed");
            }
            if (observer) observer(false);
            return outcome;
        }));
}

void AudioEditorController::cancelDocumentLoad()
{
    ++document_load_generation_;
    pending_document_load_job_.reset();
    if (document_load_cancel_token_) {
        document_load_cancel_token_->store(true, std::memory_order_release);
        document_load_cancel_token_.reset();
    }
    if (document_loading_) {
        setDocumentLoading(false);
        setProgress(0.0);
        setState(has_document_ ? EditorSessionState::Ready
                               : EditorSessionState::Empty);
    }
}

void AudioEditorController::applyDocumentLoadOutcome(
    DocumentLoadOutcome outcome)
{
    if (outcome.kind == DocumentLoadKind::Append) {
        if (!outcome.appendJob) return;
        auto job = std::move(*outcome.appendJob);
        const auto current = [&] {
            return job.generation == document_load_generation_
                && !job.cancelToken->load(std::memory_order_acquire);
        };
        if (!current()) return;
        const auto before = document_.timelineSnapshot();
        if (before.revision != job.expectedRevision) {
            setState(has_document_ ? EditorSessionState::Ready : EditorSessionState::Empty);
            setError(tr("工程已改变，已丢弃过期导入结果"));
            return;
        }
        auto& analysis = outcome.analysis;
        if (analysis.success && before.sampleRate == 0 && job.dropTrack >= 0)
            job.appendFrame = agplayer::editor::sourceToProjectFrames(job.appendFrame, 48000, analysis.source.sample_rate);
        QString error = analysis.success ? QString{} : QString::fromStdString(analysis.message);
        if (!analysis.success && error.isEmpty())
            error = outcome.message.isEmpty() ? tr("音频导入失败") : outcome.message;
        int target = -1;
        bool imported = false;
        if (error.isEmpty()) {
            const auto rate = before.sampleRate == 0 ? analysis.source.sample_rate : before.sampleRate;
            const auto duration = agplayer::editor::sourceToProjectFrames(
                analysis.source.total_frames, analysis.source.sample_rate, rate);
            if (duration <= 0 || job.appendFrame < 0
                || job.appendFrame > std::numeric_limits<qint64>::max() - duration) {
                error = tr("音频时长超出工程范围");
            } else {
                const auto fits = [&](const int track) {
                    return !job.appendUsedTracks[track]
                        && std::none_of(before.events.begin(), before.events.end(), [&](const AudioEvent& clip) {
                            return clip.trackIndex == track
                                && job.appendFrame < clip.timelineStart + agplayer::editor::audibleFrames(clip)
                                && clip.timelineStart < job.appendFrame + duration;
                        });
                };
                if (job.recordingTrack >= 0) {
                    if (job.recordingTrack < 6) target = job.recordingTrack;
                } else if (job.dropTrack >= 0) {
                    const int dropTarget = static_cast<int>((job.dropTrack + job.appendIndex) % 6);
                    for (int offset = 0; offset < 6; ++offset) {
                        const int candidate = (dropTarget + offset) % 6;
                        if (fits(candidate)) { target = candidate; break; }
                    }
                } else {
                    // Prefer empty tracks, then unused tracks with a gap at the frozen cursor.
                    for (int track = 0; track < 6; ++track) {
                        if (fits(track) && std::none_of(before.events.begin(), before.events.end(),
                                [track](const AudioEvent& clip) { return clip.trackIndex == track; })) {
                            target = track;
                            break;
                        }
                    }
                    if (target < 0) {
                        for (int track = 0; track < 6; ++track) {
                            if (fits(track)) { target = track; break; }
                        }
                    }
                }
                if (target < 0) error = tr("冻结导入位置没有可用轨道，未覆盖原素材");
            }
        }
        if (error.isEmpty()) {
            if (!has_document_)
                document_.setProjectFormat(analysis.source.sample_rate, 2);
            const bool inserted = job.recordingTrack >= 0
                ? document_.overwriteSource(analysis.source, job.appendFrame, target)
                : document_.insertSource(analysis.source, job.appendFrame, target);
            if (!inserted) {
                error = tr("片段冲突或工程容量不足，未覆盖原素材");
            } else {
                if (!has_document_) {
                    has_document_ = true;
                    project_export_settings_ = defaultProjectExportSettings(
                        static_cast<int>(analysis.source.sample_rate), 2);
                }
                job.appendUsedTracks[target] = true;
                imported = true;
                if (job.recordingTrack >= 0) owned_media_.append(outcome.path);
                setSelectedTrack(target);
                source_peak_pyramids_.insert_or_assign(sourcePeakKey(analysis.source), cachedPeakPyramid(analysis));
            }
        }
        job.expectedRevision = document_.timelineSnapshot().revision;
        if (!error.isEmpty()) job.appendFailures.append(QFileInfo(outcome.path).fileName() + QStringLiteral(": ") + error);
        import_results_.append(QVariantMap{{"path", outcome.path}, {"success", error.isEmpty()},
                                           {"message", error}, {"trackIndex", target}});
        ++job.appendIndex;
        const bool more = job.appendIndex < job.appendUrls.size();
        if (more) pending_document_load_job_ = job;
        if (imported) finishTimelineMutation();
        if (!current()) return;
        if (document_.timelineSnapshot().revision != job.expectedRevision) {
            pending_document_load_job_.reset();
            setState(has_document_ ? EditorSessionState::Ready : EditorSessionState::Empty);
            setError(tr("工程已改变，已停止后续导入"));
            emit importResultsChanged();
            return;
        }
        setState(more ? EditorSessionState::Processing
                     : (has_document_ ? EditorSessionState::Ready : EditorSessionState::Empty));
        setProgress(static_cast<double>(job.appendIndex) / job.appendUrls.size());
        setError(job.appendFailures.join(QLatin1Char('\n')));
        // A direct slot can cancel or replace the document here. Do not write state afterwards.
        emit importResultsChanged();
        return;
    }
    stopPlayback();
    const bool replaceDocument = outcome.kind != DocumentLoadKind::Relink;
    if (replaceDocument) {
        clearEventSelection();
        cancelBpmDetection(false);
        time_pitch_ = {};
        time_pitch_preview_active_ = false;
        track_muted_ = false;
        track_solo_ = false;
        track_gain_db_ = 0.0;
    }
    cancelSourcePeakCacheJob();
    event_gesture_ = {};
    if (outcome.kind == DocumentLoadKind::OpenFile) {
        AudioDocument candidate = AudioDocument::fromSource(
            outcome.analysis.source);
        document_ = std::move(candidate);
        source_path_ = outcome.path;
        project_path_.clear();
        project_sources_ = {project_source_record(1,
            document_.timelineSnapshot().events.front().source)};
        project_issues_.clear();
        known_project_issues_.clear();
        project_export_settings_ = defaultProjectExportSettings(
            static_cast<int>(outcome.analysis.source.sample_rate),
            static_cast<int>(outcome.analysis.source.channels));
        format_name_ = QString::fromStdString(outcome.analysis.format).toUpper();
        sample_rate_ = static_cast<int>(outcome.analysis.source.sample_rate);
        channels_ = static_cast<int>(document_.timelineSnapshot().channels);
        bits_per_sample_ = outcome.analysis.bits_per_sample;
        bit_rate_ = outcome.analysis.bit_rate;
        const std::vector<float> amplitudeMix = centeredAmplitudeMix(
            outcome.analysis.channel_peaks);
        channel_peaks_ = build_variant_peaks(amplitudeMix.empty()
            ? std::vector<std::vector<float>>{
                outcome.analysis.visual_mix_peaks}
            : std::vector<std::vector<float>>{amplitudeMix});
        primary_peak_pyramid_ = cachedPeakPyramid(outcome.analysis);
        source_peak_pyramids_.clear();
        if (primary_peak_pyramid_) {
            source_peak_pyramids_.emplace(
                sourcePeakKey(outcome.analysis.source), primary_peak_pyramid_);
        }
        clearViewportWaveformState();
        has_document_ = true;
        position_ms_ = 0;
        playhead_frame_ = 0;
        setViewportDocumentFrames(document_.totalFrames());
        markProjectClean();
    } else if (outcome.kind == DocumentLoadKind::OpenProject) {
        ProjectLoadResult& loaded = *outcome.project;
        SourcePeakPyramids loadedPyramids;
        for (const ProjectSourceRecord& record : loaded.sources) {
            if (!record.source || record.source->path.empty()) continue;
            const auto key = sourcePeakKey(*record.source);
            if (loadedPyramids.count(key) != 0U) continue;
            if (auto pyramid = lookupPeakPyramid(*record.source)) {
                loadedPyramids.emplace(key, std::move(pyramid));
            }
        }
        document_ = std::move(*loaded.document);
        project_sources_ = std::move(loaded.sources);
        known_project_issues_ = to_project_issues(loaded.issues);
        syncProjectSourcesAndIssues();
        project_export_settings_ = loaded.exportSettings;
        const ProjectEditorSettings& editor = loaded.editorSettings;
        if (editor.originalBpm > 0.0) {
            time_pitch_.setOriginalBpm(editor.originalBpm);
        }
        (void)time_pitch_.setSpeedPercent(editor.speedPercent);
        time_pitch_.setKeepPitch(editor.keepPitch);
        time_pitch_.setFormantPreservation(editor.formantPreservation);
        (void)time_pitch_.setPitch(editor.pitchCents / 100,
                                  editor.pitchCents % 100);
        track_muted_ = editor.trackMuted;
        track_solo_ = editor.trackSolo;
        track_gain_db_ = editor.trackGainDb;
        bpm_result_ = editor.originalBpm;
        bpm_error_.clear();
        project_path_ = QFileInfo(outcome.path).absoluteFilePath();
        syncPrimarySourceSummary();
        channel_peaks_.clear();
        primary_peak_pyramid_.reset();
        source_peak_pyramids_ = std::move(loadedPyramids);
        clearViewportWaveformState();
        has_document_ = true;
        playhead_frame_ = loaded.playheadFrame;
        position_ms_ = sample_rate_ > 0
            ? playhead_frame_ * 1'000 / sample_rate_ : 0;
        refreshSourcePeakCachesAsync();
        setViewportDocumentFrames(document_.totalFrames());
        suppress_persisted_state_tracking_ = true;
        (void)viewport_.setVisibleRange(loaded.visibleStartFrame,
                                        loaded.visibleEndFrame);
        suppress_persisted_state_tracking_ = false;
        markProjectClean();
    } else {
        document_ = std::move(*outcome.relinkDocument);
        project_sources_ = std::move(outcome.relinkSources);
        known_project_issues_.erase(std::remove_if(
            known_project_issues_.begin(), known_project_issues_.end(),
            [&outcome](const QVariant& value) {
                return value.toMap().value(QStringLiteral("sourceId"))
                    .toULongLong() == outcome.sourceId;
            }), known_project_issues_.end());
        syncProjectSourcesAndIssues();
        markProjectDirty();
        syncPrimarySourceSummary();
        primary_peak_pyramid_.reset();
        source_peak_pyramids_.clear();
        if (auto pyramid = cachedPeakPyramid(outcome.analysis)) {
            source_peak_pyramids_.insert_or_assign(
                sourcePeakKey(outcome.analysis.source), std::move(pyramid));
        }
        clearViewportWaveformState();
        refreshSourcePeakCachesAsync();
        setViewportDocumentFrames(document_.totalFrames());
    }
    setLoopEnabled(document_.selection().has_value());
    setState(EditorSessionState::Ready);
    setProgress(1.0);
    setError({});
    refreshActions();
    emit waveformChanged();
    emit playbackChanged();
    emit documentChanged();
    emit projectChanged();
    if (replaceDocument) {
        cleanupUnreferencedSessionMedia();
        emit timePitchChanged();
        emit trackMixChanged();
    }
    if (outcome.kind == DocumentLoadKind::OpenFile) {
        const double taggedBpm = embedded_bpm(source_path_);
        if (taggedBpm > 0.0) adoptBpm(taggedBpm);
        else (void)detectBpm();
    }
}

void AudioEditorController::setDocumentLoading(const bool loading)
{
    if (document_loading_ == loading) return;
    document_loading_ = loading;
    refreshActions();
    emit loadingChanged();
    emit stateChanged();
}

void AudioEditorController::applySelectionTrack(
    agplayer::editor::TimelineSnapshot& snapshot) const noexcept
{
    const int track = selectionTrack();
    if (track < 0) return;
    for (auto& event : snapshot.events) {
        if (event.trackIndex != track) event.mute = true;
    }
}

void AudioEditorController::clearViewportWaveformState(
    const bool clearPublished)
{
    viewport_waveform_debounce_timer_.stop();
    ++viewport_waveform_generation_;
    pending_viewport_waveform_job_.reset();
    if (viewport_waveform_cancel_token_) {
        viewport_waveform_cancel_token_->store(true,
                                               std::memory_order_release);
    }
    if (clearPublished) {
        viewport_channel_peaks_.clear();
        event_waveform_peaks_.clear();
    }
}

void AudioEditorController::setViewportWaveformDevicePixelRatio(
    const double devicePixelRatio)
{
    const qreal bounded = std::clamp<qreal>(
        static_cast<qreal>(devicePixelRatio), 1.0,
        kMaxWaveformDevicePixelRatio);
    if (qFuzzyCompare(viewport_waveform_device_pixel_ratio_, bounded)) return;
    viewport_waveform_device_pixel_ratio_ = bounded;
    requestViewportWaveform();
}

void AudioEditorController::clearViewportSourcePeaksForTesting()
{
    cancelSourcePeakCacheJob();
    if (source_peak_cache_watcher_) {
        auto* const watcher = source_peak_cache_watcher_;
        watcher->future().waitForFinished();
        if (source_peak_cache_watcher_ == watcher) {
            watcher->disconnect(this);
            source_peak_cache_watcher_ = nullptr;
            delete watcher;
        }
    }
    channel_peaks_.clear();
    primary_peak_pyramid_.reset();
    source_peak_pyramids_.clear();
}

void AudioEditorController::cancelSourcePeakCacheJob()
{
    ++source_peak_cache_generation_;
    pending_source_peak_cache_job_.reset();
    if (source_peak_cache_cancel_token_) {
        source_peak_cache_cancel_token_->store(true,
                                               std::memory_order_release);
        source_peak_cache_cancel_token_.reset();
    }
}

void AudioEditorController::refreshSourcePeakCachesAsync()
{
    cancelSourcePeakCacheJob();
    if (!has_document_) return;
    std::vector<AudioSource> missing;
    std::unordered_set<std::string> seen;
    for (const AudioEvent& event : document_.timelineSnapshot().events) {
        if (!event.source || event.source->path.empty()) continue;
        const std::string key = sourcePeakKey(*event.source);
        if (!seen.insert(key).second
            || source_peak_pyramids_.count(key) != 0U) {
            continue;
        }
        if (auto cached = lookupPeakPyramid(*event.source)) {
            source_peak_pyramids_.emplace(key, std::move(cached));
            continue;
        }
        const QString path = QString::fromStdWString(
            event.source->path.wstring());
        if (QFileInfo::exists(path)) missing.push_back(*event.source);
    }
    if (missing.empty()) {
        requestViewportWaveform();
        return;
    }
    auto cancelToken = std::make_shared<std::atomic_bool>(false);
    source_peak_cache_cancel_token_ = cancelToken;
    SourcePeakCacheJob job{source_peak_cache_generation_, cancelToken,
                           std::move(missing)};
    if (source_peak_cache_watcher_) {
        pending_source_peak_cache_job_ = std::move(job);
        return;
    }
    startSourcePeakCacheJob(std::move(job));
}

void AudioEditorController::startSourcePeakCacheJob(SourcePeakCacheJob job)
{
    const quint64 generation = job.generation;
    const auto cancelToken = job.cancelToken;
    const auto observer = source_peak_cache_task_observer_;
    auto* watcher = new QFutureWatcher<SourcePeakCacheResult>(this);
    source_peak_cache_watcher_ = watcher;
    connect(watcher, &QFutureWatcher<SourcePeakCacheResult>::finished,
            this, [this, watcher, generation, cancelToken] {
        if (source_peak_cache_watcher_ == watcher) {
            source_peak_cache_watcher_ = nullptr;
        }
        if (!cancelToken->load(std::memory_order_acquire)
            && generation == source_peak_cache_generation_) {
            SourcePeakCacheResult result = watcher->result();
            for (auto& entry : result.pyramids) {
                source_peak_pyramids_.insert_or_assign(
                    std::move(entry.first), std::move(entry.second));
            }
            if (source_peak_cache_cancel_token_ == cancelToken) {
                source_peak_cache_cancel_token_.reset();
            }
            requestViewportWaveform();
        }
        watcher->deleteLater();
        if (!source_peak_cache_watcher_
            && pending_source_peak_cache_job_) {
            SourcePeakCacheJob next = std::move(
                *pending_source_peak_cache_job_);
            pending_source_peak_cache_job_.reset();
            if (next.generation == source_peak_cache_generation_
                && !next.cancelToken->load(std::memory_order_acquire)) {
                startSourcePeakCacheJob(std::move(next));
            }
        }
    });
    watcher->setFuture(QtConcurrent::run(
        [job = std::move(job), observer]() mutable {
            if (observer) observer(true);
            SourcePeakCacheResult result;
            try {
                for (const AudioSource& source : job.sources) {
                    if (job.cancelToken->load(std::memory_order_acquire)) break;
                    const AudioFileAnalysis analysis = AudioFileAnalyzer::analyze(
                        source.path, 2'048, job.cancelToken.get());
                    if (!analysis.success
                        || job.cancelToken->load(std::memory_order_acquire)) {
                        continue;
                    }
                    auto pyramid = cachedPeakPyramid(analysis);
                    if (pyramid) {
                        result.pyramids.insert_or_assign(
                            sourcePeakKey(source), std::move(pyramid));
                    }
                }
            } catch (...) {
                result.pyramids.clear();
            }
            if (observer) observer(false);
            return result;
        }));
}

void AudioEditorController::requestViewportWaveform()
{
    viewport_waveform_debounce_timer_.stop();
    const quint64 generation = ++viewport_waveform_generation_;
    if (viewport_waveform_cancel_token_) {
        viewport_waveform_cancel_token_->store(true,
                                               std::memory_order_release);
    }
    if (sourcePeakCacheActiveForTesting()) {
        pending_viewport_waveform_job_.reset();
        return;
    }
    const qint64 totalFrames = has_document_ ? document_.totalFrames() : 0;
    const int renderChannels = 1;
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
        ? 0 : viewportTargetPoints(visibleFrames, viewportWidth,
                                   viewport_waveform_device_pixel_ratio_);
    if (!has_document_
        || clampedTotal <= 0 || renderChannels <= 0 || visibleFrames <= 0
        || targetPoints <= 0) {
        pending_viewport_waveform_job_.reset();
        if (!has_document_
            || clampedTotal <= 0 || renderChannels <= 0) {
            viewport_channel_peaks_.clear();
            event_waveform_peaks_.clear();
            emit waveformChanged();
        }
        return;
    }

    const auto snapshot = timelineSnapshotForView();
    const auto primaryPeaks = peaksAsChannels(channel_peaks_);
    const auto primaryPyramid = primary_peak_pyramid_;
    const auto sourcePyramids = source_peak_pyramids_;
    const auto cancelToken = std::make_shared<std::atomic_bool>(false);
    ViewportWaveformJob job{
        generation,
        cancelToken,
        [snapshot, primaryPath = source_path_, primaryPeaks, primaryPyramid,
          sourcePyramids, startFrame, endFrame, targetPoints,
          channels = renderChannels, cancelToken]() mutable {
             return composeVisibleTimelinePeaks(
                 snapshot, primaryPath, primaryPeaks, primaryPyramid,
                 sourcePyramids,
                 startFrame, endFrame,
                 targetPoints, channels, cancelToken);
         }};
    pending_viewport_waveform_job_ = std::move(job);
    viewport_waveform_debounce_timer_.start();
}

void AudioEditorController::startViewportWaveformJob(ViewportWaveformJob job)
{
    const quint64 generation = job.generation;
    const auto cancelToken = job.cancelToken;
    const auto observer = viewport_waveform_task_observer_;
    viewport_waveform_cancel_token_ = cancelToken;
    auto* watcher = new QFutureWatcher<ViewportWaveformResult>(this);
    viewport_waveform_watcher_ = watcher;
    connect(watcher, &QFutureWatcher<ViewportWaveformResult>::finished,
            this, [this, watcher, generation, cancelToken] {
        if (viewport_waveform_watcher_ == watcher) {
            viewport_waveform_watcher_ = nullptr;
            viewport_waveform_cancel_token_.reset();
        }
        if (!cancelToken->load(std::memory_order_acquire)
            && generation == viewport_waveform_generation_) {
            auto result = watcher->result();
            event_waveform_peaks_ = std::move(result.clipPeaks);
            event_waveform_revision_ = result.revision;
            event_waveform_generation_ = generation;
            if (!result.hasVisibleEvent || !result.hasUnavailableVisibleEvent) {
                viewport_channel_peaks_ = build_variant_peaks(result.peaks);
            }
            emit waveformChanged();
        }
        watcher->deleteLater();
        if (!viewport_waveform_watcher_ && pending_viewport_waveform_job_
            && !viewport_waveform_debounce_timer_.isActive()) {
            ViewportWaveformJob next = std::move(*pending_viewport_waveform_job_);
            pending_viewport_waveform_job_.reset();
            if (next.generation == viewport_waveform_generation_) {
                startViewportWaveformJob(std::move(next));
            }
        }
    });
    watcher->setFuture(QtConcurrent::run(
        [work = std::move(job.work), observer]() mutable {
            if (observer) observer(true);
            ViewportWaveformResult result = work();
            if (observer) observer(false);
            return result;
        }));
}

void AudioEditorController::refreshActions()
{
    const bool selection = document_.selection().has_value();
    const auto selectedEvent = parseEventId(selected_event_id_);
    const auto snapshot = document_.timelineSnapshot();
    const bool eventSelection = selectedEvent && std::any_of(
        snapshot.events.cbegin(), snapshot.events.cend(),
        [selectedEvent](const AudioEvent& event) {
            return event.id == *selectedEvent;
        });
    const bool idle = !busy();
    actions_.setEnabled(QStringLiteral("editor.open"), idle);
    actions_.setEnabled(QStringLiteral("editor.save"), has_document_ && idle);
    actions_.setEnabled(QStringLiteral("editor.export"), exportSupported()
                        && idle && projectSourcesOnline());
    actions_.setEnabled(QStringLiteral("editor.undo"), has_document_ && idle
                        && document_.canUndo());
    actions_.setEnabled(QStringLiteral("editor.redo"), has_document_ && idle
                        && document_.canRedo());
    actions_.setEnabled(QStringLiteral("editor.paste"), document_.hasClipboard() && idle);
    const qint64 playhead = playhead_frame_;
    actions_.setEnabled(QStringLiteral("editor.split"), has_document_ && idle
        && eventSelection && std::any_of(snapshot.events.begin(), snapshot.events.end(),
            [selectedEvent, playhead](const AudioEvent& event) {
                return selectedEvent && event.id == *selectedEvent && playhead > event.timelineStart
                    && playhead < event.timelineStart + agplayer::editor::audibleFrames(event);
            }));
    actions_.setEnabled(QStringLiteral("editor.merge"), has_document_ && idle
        && mergePairCoveredBySelection(document_.timelineSnapshot(),
                                       document_.selection()).has_value());
    for (const QString& id : {
             QStringLiteral("editor.cut"), QStringLiteral("editor.copy"),
             QStringLiteral("editor.deleteSelection")}) {
        actions_.setEnabled(id, has_document_ && eventSelection && idle);
    }
    actions_.setEnabled(QStringLiteral("editor.cropToSelection"),
                        has_document_ && selection && eventSelection && idle);
    for (const QString& id : {QStringLiteral("editor.silenceSelection"),
             QStringLiteral("editor.fadeIn"), QStringLiteral("editor.fadeOut")}) {
        actions_.setEnabled(id, has_document_ && (selection || eventSelection) && idle);
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
                    {QStringLiteral("sourceId"),
                     QString::number(record->sourceId)},
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
    std::unordered_set<std::string> retainedPeakKeys;
    retainedPeakKeys.reserve(referencedSources.size());
    for (const AudioSource* source : referencedSources) {
        if (source) retainedPeakKeys.insert(sourcePeakKey(*source));
    }
    for (auto cache = source_peak_pyramids_.begin();
         cache != source_peak_pyramids_.end();) {
        cache = retainedPeakKeys.count(cache->first) == 0U
            ? source_peak_pyramids_.erase(cache) : std::next(cache);
    }
    cleanupUnreferencedSessionMedia();
}

void AudioEditorController::cleanupUnreferencedSessionMedia()
{
    // Only paths produced and adopted by this controller are eligible. Archived
    // project media and user sources never enter owned_media_. Undo and clipboard
    // retention are authoritative; a failed/partial take is left for recovery.
    auto retained = document_.retainedSources();
    for (const auto& event : document_.timelineSnapshot().events) {
        if (event.source) retained.push_back(event.source);
    }
    for (auto path = owned_media_.begin(); path != owned_media_.end();) {
        const auto media = std::filesystem::path(path->toStdWString());
        const bool referenced = std::any_of(retained.begin(), retained.end(),
            [&media](const auto& source) {
                if (!source) return false;
                if (source->path.lexically_normal() == media.lexically_normal()) return true;
                std::error_code error;
                return std::filesystem::equivalent(source->path, media, error);
            });
        if (!referenced && (!QFileInfo::exists(*path) || QFile::remove(*path)))
            path = owned_media_.erase(path);
        else ++path;
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
    const bool restartPendingBpm = bpm_busy_;
    if (restartPendingBpm) cancelBpmDetection(false);
    const qint64 requestedPlayhead = playhead_frame_;
    stopPlayback();
    if (!document_.selection()) setLoopEnabled(false);
    clearMissingEventSelection();
    syncProjectSourcesAndIssues();
    syncPrimarySourceSummary();
    const qint64 frames = std::max<qint64>(0, document_.totalFrames());
    setViewportDocumentFrames(frames);
    playhead_frame_ = std::clamp<qint64>(requestedPlayhead, 0, frames);
    position_ms_ = sample_rate_ > 0
        ? playhead_frame_ * 1'000 / sample_rate_ : 0;
    playhead_persisted_dirty_ = playhead_frame_ != saved_playhead_frame_;
    (void)syncModifiedFromHistory();
    requestViewportWaveform();
    refreshActions();
    emit playbackChanged();
    emit documentChanged();
    emit projectChanged();
    if (restartPendingBpm && frames > 0) (void)detectBpm();
}

void AudioEditorController::clearMissingEventSelection()
{
    if (selected_event_id_.isEmpty()) return;
    const auto eventId = parseEventId(selected_event_id_);
    const auto snapshot = document_.timelineSnapshot();
    const bool exists = eventId && std::any_of(snapshot.events.cbegin(),
        snapshot.events.cend(), [eventId](const AudioEvent& event) {
            return event.id == *eventId;
        });
    if (!exists) clearEventSelection();
}

void AudioEditorController::syncPrimarySourceSummary()
{
    const auto snapshot = document_.timelineSnapshot();
    const auto first = snapshot.events.empty() ? std::shared_ptr<const AudioSource>{}
                                               : snapshot.events.front().source;
    source_path_ = first ? QString::fromStdWString(first->path.wstring()) : QString{};
    format_name_ = source_path_.isEmpty() ? QString{}
                                          : QFileInfo(source_path_).suffix().toUpper();
    sample_rate_ = static_cast<int>(snapshot.sampleRate);
    channels_ = static_cast<int>(snapshot.channels);
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

#include "editor_playback_stream.hpp"

#include "../time_pitch_engine.hpp"
#include "automation_time_mapper.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace agplayer::editor {
namespace {

constexpr std::size_t kReadFrames = 4'096U;

bool frameToMilliseconds(const SampleFrame frame, const std::uint32_t sampleRate,
                         std::int64_t& value) noexcept
{
    if (frame < 0 || sampleRate == 0) return false;
    const SampleFrame seconds = frame / sampleRate;
    const SampleFrame remainder = frame % sampleRate;
    if (seconds > std::numeric_limits<std::int64_t>::max() / 1'000) return false;
    value = seconds * 1'000
        + remainder * 1'000 / static_cast<SampleFrame>(sampleRate);
    return true;
}

SampleFrame millisecondsToFrame(const std::int64_t milliseconds,
                                const std::uint32_t sampleRate) noexcept
{
    if (milliseconds <= 0 || sampleRate == 0) return 0;
    const long double frames = static_cast<long double>(milliseconds)
        * static_cast<long double>(sampleRate) / 1'000.0L;
    return frames >= static_cast<long double>(
               std::numeric_limits<SampleFrame>::max())
        ? std::numeric_limits<SampleFrame>::max()
        : static_cast<SampleFrame>(std::ceil(frames));
}

bool validateSnapshot(const TimelineSnapshot& snapshot,
                      const EditorPlaybackParameters& parameters,
                      std::string& error)
{
    if (snapshot.events.empty() || snapshot.totalFrames <= 0
        || !std::isfinite(parameters.speed_ratio)
        || parameters.speed_ratio < 0.5 || parameters.speed_ratio > 2.0
        || parameters.pitch_cents < -1'200 || parameters.pitch_cents > 1'200) {
        error = "invalid editor playback request";
        return false;
    }
    const AudioEvent& first = snapshot.events.front();
    if (!isValid(first) || first.source->sample_rate == 0
        || first.source->channels == 0 || first.source->channels > 2) {
        error = "editor playback format is unsupported";
        return false;
    }
    SampleFrame previousEnd = 0;
    for (const AudioEvent& event : snapshot.events) {
        if (!isValid(event)
            || event.source->sample_rate != first.source->sample_rate
            || event.source->channels != first.source->channels
            || event.speedRatio != 1.0 || event.pitchSemitone != 0
            || event.timelineStart < previousEnd
            || audibleFrames(event) > snapshot.totalFrames - event.timelineStart) {
            error = "editor timeline contains an unsupported event";
            return false;
        }
        previousEnd = event.timelineStart + audibleFrames(event);
    }
    return true;
}

} // namespace

class EditorPlaybackStream::Impl final {
public:
    Impl(TimelineSnapshot value, EditorPlaybackParameters settings,
         agplayer::TimePitchEngineFactory factory)
        : snapshot(std::move(value)), parameters(settings),
          automation_time(0, snapshot.totalFrames, parameters.speed_ratio),
          engine_factory(factory)
    {
        metadata_value.sample_rate = static_cast<int>(
            snapshot.events.front().source->sample_rate);
        metadata_value.channels = static_cast<int>(
            snapshot.events.front().source->channels);
        metadata_value.duration_ms = static_cast<std::int64_t>(std::llround(
            static_cast<long double>(snapshot.totalFrames) * 1'000.0L
            / metadata_value.sample_rate / parameters.speed_ratio));
        configureProcessor();
        resetRaw(0);
        resetAutomationCursor(0);
    }

    void configureProcessor()
    {
        const int speedPitch = parameters.keep_pitch ? 0
            : static_cast<int>(std::lround(
                1'200.0 * std::log2(parameters.speed_ratio)));
        effective_pitch = std::clamp(parameters.pitch_cents + speedPitch,
                                     -1'200, 1'200);
        const double effectivePitchRatio = std::pow(
            2.0, effective_pitch / 1'200.0);
        processing = std::abs(parameters.speed_ratio - 1.0) > 0.000001
            || effective_pitch != 0;
        if (processing) {
            processor = engine_factory != nullptr ? engine_factory() : nullptr;
            const double tempo = parameters.keep_pitch
                ? parameters.speed_ratio
                : parameters.speed_ratio / effectivePitchRatio;
            processor_ready = processor != nullptr
                && processor->configure(metadata_value.sample_rate,
                                        metadata_value.channels)
                && processor->setTempoRatio(tempo)
                && (parameters.keep_pitch
                    ? processor->setPitchCents(effective_pitch)
                    : processor->setRateRatio(effectivePitchRatio))
                && processor->setFormantPreservation(
                    parameters.formant_preservation);
        } else {
            processor.reset();
            processor_ready = true;
        }
    }

    void resetRaw(const SampleFrame frame)
    {
        cursor = std::clamp<SampleFrame>(frame, 0, snapshot.totalFrames);
        event_index = 0;
        while (event_index < snapshot.events.size()) {
            const AudioEvent& event = snapshot.events[event_index];
            if (event.timelineStart + audibleFrames(event) > cursor) break;
            ++event_index;
        }
        decoder.close();
        decoder_ready = false;
        decoded = {};
        decoded_offset = 0;
        decoded_discard = 0;
        raw_eof = cursor >= snapshot.totalFrames;
        flushed = false;
    }

    bool openDecoder(const AudioEvent& event)
    {
        if (decoder_ready) return true;
        if (decoder.open(event.source->path.u8string(), metadata_value.sample_rate,
                         metadata_value.channels) != AG_OK) {
            return false;
        }
        const SampleFrame sourceFrame = event.sourceStart
            + (cursor - event.timelineStart);
        std::int64_t seekMs = 0;
        if (!frameToMilliseconds(sourceFrame, event.source->sample_rate, seekMs)
            || decoder.seek(seekMs) != AG_OK) {
            return false;
        }
        const SampleFrame decoderStart = millisecondsToFrame(
            seekMs, event.source->sample_rate);
        if (decoderStart > sourceFrame) return false;
        decoded_discard = sourceFrame - decoderStart;
        decoder_ready = true;
        decoded = {};
        decoded_offset = 0;
        return true;
    }

    ag_result readRaw(std::vector<float>& output, const std::size_t maximumFrames)
    {
        output.assign(maximumFrames * static_cast<std::size_t>(
            metadata_value.channels), 0.0F);
        std::size_t produced = 0;
        while (produced < maximumFrames && cursor < snapshot.totalFrames) {
            if (event_index >= snapshot.events.size()) {
                const auto silence = static_cast<std::size_t>(
                    std::min<SampleFrame>(snapshot.totalFrames - cursor,
                        static_cast<SampleFrame>(maximumFrames - produced)));
                cursor += static_cast<SampleFrame>(silence);
                produced += silence;
                continue;
            }
            const AudioEvent& event = snapshot.events[event_index];
            const SampleFrame eventEnd = event.timelineStart + audibleFrames(event);
            if (cursor < event.timelineStart) {
                const auto silence = static_cast<std::size_t>(
                    std::min<SampleFrame>(event.timelineStart - cursor,
                        static_cast<SampleFrame>(maximumFrames - produced)));
                cursor += static_cast<SampleFrame>(silence);
                produced += silence;
                continue;
            }
            if (cursor >= eventEnd) {
                ++event_index;
                decoder.close();
                decoder_ready = false;
                continue;
            }
            const std::size_t eventFrames = static_cast<std::size_t>(
                std::min<SampleFrame>(eventEnd - cursor,
                    static_cast<SampleFrame>(maximumFrames - produced)));
            if (event.mute) {
                cursor += static_cast<SampleFrame>(eventFrames);
                produced += eventFrames;
                continue;
            }
            if (!openDecoder(event)) return AG_DECODE_ERROR;
            std::size_t copied = 0;
            while (copied < eventFrames) {
                if (decoded_offset >= decoded.frames) {
                    if (decoder.read(decoded) != AG_OK) return AG_DECODE_ERROR;
                    decoded_offset = 0;
                    if (decoded.frames == 0) {
                        if (decoded.end_of_stream) return AG_DECODE_ERROR;
                        continue;
                    }
                    const auto discard = static_cast<std::size_t>(
                        std::min<SampleFrame>(decoded_discard,
                            static_cast<SampleFrame>(decoded.frames)));
                    decoded_offset = discard;
                    decoded_discard -= static_cast<SampleFrame>(discard);
                }
                const std::size_t available = decoded.frames - decoded_offset;
                const std::size_t take = std::min(eventFrames - copied, available);
                for (std::size_t frame = 0; frame < take; ++frame) {
                    for (int channel = 0; channel < metadata_value.channels;
                         ++channel) {
                        const std::size_t channelIndex = static_cast<std::size_t>(channel);
                        output[(produced + copied + frame)
                                   * static_cast<std::size_t>(metadata_value.channels)
                               + channelIndex]
                            = decoded.samples[(decoded_offset + frame)
                                  * static_cast<std::size_t>(metadata_value.channels)
                              + channelIndex];
                    }
                }
                decoded_offset += take;
                copied += take;
            }
            cursor += static_cast<SampleFrame>(copied);
            produced += copied;
        }
        output.resize(produced * static_cast<std::size_t>(metadata_value.channels));
        raw_eof = cursor >= snapshot.totalFrames;
        return AG_OK;
    }

    void resetAutomationCursor(const SampleFrame outputFrame)
    {
        const SampleFrame timelineFrame = automation_time.map(outputFrame);
        const auto event = std::lower_bound(
            snapshot.events.cbegin(), snapshot.events.cend(), timelineFrame,
            [](const AudioEvent& value, const SampleFrame frame) {
                return value.timelineStart + audibleFrames(value) <= frame;
            });
        automation_event_index = static_cast<std::size_t>(
            std::distance(snapshot.events.cbegin(), event));
    }

    void applyAutomation(std::vector<float>& samples, const std::size_t frames)
    {
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const SampleFrame outputFrame = emitted_frames
                + static_cast<SampleFrame>(frame);
            const SampleFrame timelineFrame = automation_time.map(outputFrame);
            while (automation_event_index < snapshot.events.size()
                   && snapshot.events[automation_event_index].timelineStart
                        + audibleFrames(snapshot.events[automation_event_index])
                        <= timelineFrame) {
                ++automation_event_index;
            }
            float gain = 1.0F;
            if (automation_event_index < snapshot.events.size()) {
                const AudioEvent& event = snapshot.events[automation_event_index];
                if (timelineFrame >= event.timelineStart
                    && timelineFrame < event.timelineStart
                        + audibleFrames(event)) {
                    gain = eventAmplitudeGainAt(
                        event, timelineFrame - event.timelineStart);
                }
            }
            if (gain == 1.0F) continue;
            for (int channel = 0; channel < metadata_value.channels; ++channel) {
                samples[frame * static_cast<std::size_t>(
                    metadata_value.channels) + static_cast<std::size_t>(channel)]
                    *= gain;
            }
        }
    }

    ag_result read(agplayer::DecodedAudioBlock& block) noexcept
    {
        try {
            block = {};
            if (!processor_ready) return AG_INVALID_ARGUMENT;
            if (!processing) {
                const ag_result result = readRaw(block.samples, kReadFrames);
                if (result != AG_OK) return result;
                block.frames = block.samples.size()
                    / static_cast<std::size_t>(metadata_value.channels);
                applyAutomation(block.samples, block.frames);
                block.timestamp_frame = emitted_frames;
                block.timestamp_ms = emitted_frames * 1'000
                    / metadata_value.sample_rate;
                emitted_frames += static_cast<SampleFrame>(block.frames);
                block.end_of_stream = raw_eof && block.frames == 0;
                return AG_OK;
            }

            block.samples.resize(kReadFrames * static_cast<std::size_t>(
                metadata_value.channels));
            std::size_t received = processor->receive(block.samples.data(),
                                                       kReadFrames);
            if (processor->failed()) return AG_INTERNAL_ERROR;
            while (received == 0 && !flushed) {
                std::vector<float> raw;
                const ag_result result = readRaw(raw, kReadFrames);
                if (result != AG_OK) return result;
                const std::size_t frames = raw.size()
                    / static_cast<std::size_t>(metadata_value.channels);
                if (frames > 0) {
                    processor->put(raw.data(), frames);
                    if (processor->failed()) return AG_INTERNAL_ERROR;
                }
                if (raw_eof) {
                    processor->flush();
                    if (processor->failed()) return AG_INTERNAL_ERROR;
                    flushed = true;
                }
                received = processor->receive(block.samples.data(), kReadFrames);
                if (processor->failed()) return AG_INTERNAL_ERROR;
            }
            block.samples.resize(received * static_cast<std::size_t>(
                metadata_value.channels));
            applyAutomation(block.samples, received);
            block.frames = received;
            block.timestamp_frame = emitted_frames;
            block.timestamp_ms = emitted_frames * 1'000 / metadata_value.sample_rate;
            emitted_frames += static_cast<SampleFrame>(received);
            block.end_of_stream = flushed && received == 0;
            return AG_OK;
        } catch (...) {
            block = {};
            return AG_INTERNAL_ERROR;
        }
    }

    ag_result seek(const std::int64_t positionMs) noexcept
    {
        if (positionMs < 0 || positionMs > metadata_value.duration_ms) {
            return AG_INVALID_ARGUMENT;
        }
        try {
            const long double originalFrames = static_cast<long double>(positionMs)
                * metadata_value.sample_rate * parameters.speed_ratio / 1'000.0L;
            const SampleFrame rawFrame = static_cast<SampleFrame>(
                std::ceil(originalFrames));
            resetRaw(rawFrame);
            const long double outputFrames = static_cast<long double>(positionMs)
                * metadata_value.sample_rate / 1'000.0L;
            emitted_frames = static_cast<SampleFrame>(std::ceil(outputFrames));
            automation_time.resetAnchor(emitted_frames, rawFrame);
            resetAutomationCursor(emitted_frames);
            if (processor) {
                processor->reset();
                configureProcessor();
            }
            return processor_ready ? AG_OK : AG_INVALID_ARGUMENT;
        } catch (...) {
            return AG_INTERNAL_ERROR;
        }
    }

    TimelineSnapshot snapshot;
    EditorPlaybackParameters parameters;
    AutomationTimeMapper automation_time;
    agplayer::MediaMetadata metadata_value;
    agplayer::Decoder decoder;
    agplayer::DecodedAudioBlock decoded;
    std::size_t decoded_offset{};
    SampleFrame decoded_discard{};
    std::size_t event_index{};
    std::size_t automation_event_index{};
    SampleFrame cursor{};
    SampleFrame emitted_frames{};
    bool decoder_ready{};
    bool raw_eof{};
    bool processing{};
    bool processor_ready{};
    bool flushed{};
    int effective_pitch{};
    std::unique_ptr<agplayer::ITimePitchEngine> processor;
    agplayer::TimePitchEngineFactory engine_factory{};
};

EditorPlaybackStream::EditorPlaybackStream(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

EditorPlaybackStream::~EditorPlaybackStream() = default;

std::shared_ptr<EditorPlaybackStream> EditorPlaybackStream::create(
    TimelineSnapshot snapshot, const EditorPlaybackParameters& parameters,
    std::string& error, agplayer::TimePitchEngineFactory engine_factory)
{
    if (!validateSnapshot(snapshot, parameters, error)) return {};
    try {
        auto impl = std::make_unique<Impl>(
            std::move(snapshot), parameters, engine_factory);
        if (!impl->processor_ready) {
            error = "cannot configure editor time/pitch processing";
            return {};
        }
        error.clear();
        return std::shared_ptr<EditorPlaybackStream>(
            new EditorPlaybackStream(std::move(impl)));
    } catch (...) {
        error = "cannot allocate editor playback stream";
        return {};
    }
}

const agplayer::MediaMetadata& EditorPlaybackStream::metadata() const noexcept
{
    return impl_->metadata_value;
}

ag_result EditorPlaybackStream::read(agplayer::DecodedAudioBlock& block) noexcept
{
    return impl_->read(block);
}

ag_result EditorPlaybackStream::seek(const std::int64_t positionMs) noexcept
{
    return impl_->seek(positionMs);
}

} // namespace agplayer::editor

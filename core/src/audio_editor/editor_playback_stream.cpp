#include "editor_playback_stream.hpp"
#include "timeline_mixer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace agplayer::editor {
namespace {
constexpr std::size_t kReadFrames = TimelineMixer::kBlockFrames;
SampleFrame boundedCeil(const long double frames) noexcept
{
    return frames >= static_cast<long double>(std::numeric_limits<SampleFrame>::max())
        ? std::numeric_limits<SampleFrame>::max() : static_cast<SampleFrame>(std::ceil(frames));
}
} // namespace

class EditorPlaybackStream::Impl final {
public:
    Impl(TimelineSnapshot snapshot, EditorPlaybackParameters settings,
         agplayer::TimePitchEngineFactory factory, const std::optional<Selection>& range,
         const std::atomic_bool* cancelled)
        : mixer(std::move(snapshot), cancelled), parameters(settings),
          range_start(range ? range->start : 0),
          range_end(range ? range->end : mixer.snapshot().totalFrames),
          engine_factory(factory), cancelled_(cancelled)
    {
        metadata_value.sample_rate = static_cast<int>(mixer.snapshot().sampleRate);
        metadata_value.channels = static_cast<int>(mixer.snapshot().channels);
        metadata_value.has_audio = true;
        metadata_value.duration_ms = boundedCeil(
            static_cast<long double>(range_end - range_start) * 1'000.0L
            / metadata_value.sample_rate / parameters.speed_ratio);
        configureProcessor();
        resetRaw(range_start);
    }

    void configureProcessor()
    {
        const int speedPitch = parameters.keep_pitch ? 0
            : static_cast<int>(std::lround(1'200.0 * std::log2(parameters.speed_ratio)));
        const int effectivePitch = std::clamp(parameters.pitch_cents + speedPitch, -1'200, 1'200);
        const double pitchRatio = std::pow(2.0, effectivePitch / 1'200.0);
        processing = std::abs(parameters.speed_ratio - 1.0) > 0.000001
            || effectivePitch != 0 || parameters.formant_preservation;
        if (processing) {
            processor = engine_factory ? engine_factory() : nullptr;
            const double tempo = parameters.keep_pitch ? parameters.speed_ratio
                : parameters.speed_ratio / pitchRatio;
            processor_ready = processor
                && processor->configure(metadata_value.sample_rate, metadata_value.channels)
                && processor->setTempoRatio(tempo)
                && (parameters.keep_pitch ? processor->setPitchCents(effectivePitch)
                                          : processor->setRateRatio(pitchRatio))
                && processor->setFormantPreservation(parameters.formant_preservation);
        } else {
            processor.reset();
            processor_ready = true;
        }
    }

    void resetRaw(const SampleFrame frame)
    {
        mixer.seek(std::clamp(frame, range_start, range_end));
        raw_eof = mixer.cursor() >= range_end;
        flushed = false;
    }

    ag_result readRaw(std::vector<float>& samples)
    {
        const auto status = mixer.read(samples, range_end);
        raw_eof = mixer.cursor() >= range_end;
        return status;
    }

    ag_result read(agplayer::DecodedAudioBlock& block) noexcept
    {
        try {
            block.samples.clear();
            block.frames = 0;
            block.timestamp_frame = emitted_frames;
            block.timestamp_ms = boundedCeil(static_cast<long double>(emitted_frames)
                * 1'000.0L / metadata_value.sample_rate);
            block.end_of_stream = false;
            if (cancelled_ && cancelled_->load(std::memory_order_relaxed)) return AG_CANCELLED;
            if (!processor_ready) return AG_INVALID_ARGUMENT;
            if (!processing) {
                const auto status = readRaw(block.samples);
                if (status != AG_OK) return status;
                block.frames = block.samples.size() / static_cast<std::size_t>(metadata_value.channels);
                block.end_of_stream = raw_eof && block.frames == 0;
            } else {
                block.samples.resize(kReadFrames * static_cast<std::size_t>(metadata_value.channels));
                std::size_t received = processor->receive(block.samples.data(), kReadFrames);
                if (processor->failed()) return AG_INTERNAL_ERROR;
                while (received == 0 && !flushed) {
                    const auto status = readRaw(raw_samples);
                    if (status != AG_OK) return status;
                    const auto frames = raw_samples.size() / static_cast<std::size_t>(metadata_value.channels);
                    if (frames) {
                        processor->put(raw_samples.data(), frames);
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
                block.samples.resize(received * static_cast<std::size_t>(metadata_value.channels));
                block.frames = received;
                block.end_of_stream = flushed && received == 0;
            }
            // Clip automation, fades and track state are applied in project
            // frames by the mixer, before the single session DSP processor.
            finalizeTimelineSamples(block.samples);
            emitted_frames += static_cast<SampleFrame>(block.frames);
            return AG_OK;
        } catch (...) {
            block = {};
            return AG_INTERNAL_ERROR;
        }
    }

    ag_result seek(const std::int64_t positionMs) noexcept
    {
        if (positionMs < 0 || positionMs > metadata_value.duration_ms) return AG_INVALID_ARGUMENT;
        try {
            const SampleFrame offset = boundedCeil(static_cast<long double>(positionMs)
                * metadata_value.sample_rate * parameters.speed_ratio / 1'000.0L);
            resetRaw(range_start + std::min(offset, range_end - range_start));
            emitted_frames = boundedCeil(static_cast<long double>(positionMs)
                * metadata_value.sample_rate / 1'000.0L);
            if (processor) configureProcessor();
            return processor_ready ? AG_OK : AG_INVALID_ARGUMENT;
        } catch (...) { return AG_INTERNAL_ERROR; }
    }

    TimelineMixer mixer;
    EditorPlaybackParameters parameters;
    agplayer::MediaMetadata metadata_value;
    std::vector<float> raw_samples;
    SampleFrame range_start{};
    SampleFrame range_end{};
    SampleFrame emitted_frames{};
    bool raw_eof{};
    bool processing{};
    bool processor_ready{};
    bool flushed{};
    std::unique_ptr<agplayer::ITimePitchEngine> processor;
    agplayer::TimePitchEngineFactory engine_factory{};
    const std::atomic_bool* cancelled_{};
};

EditorPlaybackStream::EditorPlaybackStream(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
EditorPlaybackStream::~EditorPlaybackStream() = default;

std::shared_ptr<EditorPlaybackStream> EditorPlaybackStream::create(
    TimelineSnapshot snapshot, const EditorPlaybackParameters& parameters,
    std::string& error, agplayer::TimePitchEngineFactory engine_factory,
    const std::optional<Selection>& range, const std::atomic_bool* cancelled)
{
    try {
        if (!std::isfinite(parameters.speed_ratio) || parameters.speed_ratio < 0.5
            || parameters.speed_ratio > 2 || parameters.pitch_cents < -1'200
            || parameters.pitch_cents > 1'200
            || (range && (!range->valid() || range->end > snapshot.totalFrames))) {
            error = "invalid editor playback request";
            return {};
        }
        if (!TimelineMixer::prepare(snapshot, error)) return {};
        auto impl = std::make_unique<Impl>(std::move(snapshot), parameters,
            engine_factory, range, cancelled);
        if (!impl->processor_ready) {
            error = "cannot configure editor time/pitch processing";
            return {};
        }
        error.clear();
        return std::shared_ptr<EditorPlaybackStream>(new EditorPlaybackStream(std::move(impl)));
    } catch (...) {
        error = "cannot allocate editor playback stream";
        return {};
    }
}

const agplayer::MediaMetadata& EditorPlaybackStream::metadata() const noexcept
{ return impl_->metadata_value; }
ag_result EditorPlaybackStream::read(agplayer::DecodedAudioBlock& block) noexcept
{ return impl_->read(block); }
ag_result EditorPlaybackStream::seek(const std::int64_t positionMs) noexcept
{ return impl_->seek(positionMs); }

} // namespace agplayer::editor

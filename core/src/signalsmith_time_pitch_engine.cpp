#include "time_pitch_engine.hpp"

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

namespace agplayer {
namespace {

constexpr std::size_t kMaxChannels = 2U;
constexpr std::size_t kWorkFrames = 8'192U;
constexpr std::size_t kBufferedFrames = 131'072U;

bool validRatio(const double ratio) noexcept
{
    return std::isfinite(ratio) && ratio >= 0.5 && ratio <= 2.0;
}

class SignalsmithTimePitchEngine final : public ITimePitchEngine {
public:
    bool configure(const int sampleRate, const int channels) override
    {
        if (sampleRate <= 0 || channels <= 0
            || channels > static_cast<int>(kMaxChannels)) {
            return false;
        }
        sample_rate_ = sampleRate;
        channels_ = static_cast<std::size_t>(channels);
        stretch_.presetDefault(channels, static_cast<float>(sampleRate), true);
        input_latency_ = static_cast<std::size_t>(stretch_.inputLatency());
        output_latency_ = static_cast<std::size_t>(stretch_.outputLatency());
        if (input_latency_ > kWorkFrames || output_latency_ > kWorkFrames) {
            return false;
        }
        for (auto& channel : input_planar_) channel.assign(kWorkFrames, 0.0F);
        for (auto& channel : output_planar_) channel.assign(kWorkFrames, 0.0F);
        startup_interleaved_.assign(input_latency_ * channels_, 0.0F);
        output_ring_.assign(kBufferedFrames * channels_, 0.0F);
        configured_ = true;
        reset();
        applySettings();
        return true;
    }

    bool setTempoRatio(const double ratio) override
    {
        if (!configured_ || !validRatio(ratio)) return false;
        tempo_ratio_ = ratio;
        applySettings();
        return true;
    }

    bool setPitchCents(const double cents) override
    {
        if (!configured_ || !std::isfinite(cents)
            || cents < -1'200.0 || cents > 1'200.0) {
            return false;
        }
        pitch_cents_ = cents;
        applySettings();
        return true;
    }

    bool setRateRatio(const double ratio) override
    {
        if (!configured_ || !validRatio(ratio)) return false;
        rate_ratio_ = ratio;
        applySettings();
        return true;
    }

    bool setFormantPreservation(const bool enabled) override
    {
        if (!configured_) return false;
        formant_preservation_ = enabled;
        applySettings();
        return true;
    }

    void put(const float* const samples, const std::size_t frames) override
    {
        if (!configured_ || failed_ || samples == nullptr || frames == 0U
            || flushed_) {
            return;
        }
        input_frames_ += frames;
        if (isPassthrough()) {
            pushInterleaved(samples, frames);
            return;
        }

        std::size_t offset = 0U;
        if (!primed_) {
            const std::size_t required = input_latency_ - startup_frames_;
            const std::size_t copied = std::min(required, frames);
            std::copy_n(samples, copied * channels_,
                        startup_interleaved_.data()
                            + startup_frames_ * channels_);
            startup_frames_ += copied;
            offset += copied;
            if (startup_frames_ == input_latency_) prime();
        }
        while (!failed_ && primed_ && offset < frames) {
            const std::size_t count = std::min(maxInputFrames(), frames - offset);
            processInterleaved(samples + offset * channels_, count,
                               outputFramesFor(count));
            offset += count;
        }
    }

    [[nodiscard]] std::size_t receive(float* const samples,
                                      const std::size_t frames) override
    {
        if (!configured_ || samples == nullptr || frames == 0U) {
            return 0U;
        }
        const std::size_t count = std::min(frames, ring_frames_);
        for (std::size_t frame = 0U; frame < count; ++frame) {
            const std::size_t index = (ring_read_ + frame) % kBufferedFrames;
            std::copy_n(output_ring_.data() + index * channels_, channels_,
                        samples + frame * channels_);
        }
        ring_read_ = (ring_read_ + count) % kBufferedFrames;
        ring_frames_ -= count;
        return count;
    }

    void flush() override
    {
        if (!configured_ || failed_ || flushed_) return;
        flushed_ = true;
        if (isPassthrough()) return;

        final_output_frames_ = static_cast<std::size_t>(std::llround(
            static_cast<long double>(input_frames_) / playbackRate()));
        if (!primed_) {
            // startup_interleaved_ is zero-filled at configure/reset; its first
            // startup_frames_ samples contain the short input supplied so far.
            startup_frames_ = input_latency_;
            prime();
            processSilence(input_latency_);
        } else {
            processSilence(input_latency_);
        }
        if (!failed_) {
            std::size_t remaining = output_latency_;
            while (!failed_ && remaining > 0U) {
                const std::size_t count = std::min(kWorkFrames, remaining);
                for (std::size_t channel = 0U; channel < channels_; ++channel) {
                    output_ptrs_[channel] = output_planar_[channel].data();
                }
                stretch_.flush(output_ptrs_, static_cast<int>(count));
                pushPlanar(count);
                remaining -= count;
            }
        }
    }

    void reset() override
    {
        if (!configured_) return;
        stretch_.reset();
        applySettings();
        ring_read_ = 0U;
        ring_write_ = 0U;
        ring_frames_ = 0U;
        std::fill(startup_interleaved_.begin(), startup_interleaved_.end(), 0.0F);
        startup_frames_ = 0U;
        input_frames_ = 0U;
        generated_frames_ = 0U;
        emitted_frames_ = 0U;
        discard_frames_ = 0U;
        output_fraction_ = 0.0L;
        final_output_frames_ = std::numeric_limits<std::size_t>::max();
        primed_ = false;
        flushed_ = false;
        failed_ = false;
    }

    [[nodiscard]] bool failed() const noexcept override { return failed_; }

    [[nodiscard]] TimePitchEngineKind kind() const noexcept override
    {
        return TimePitchEngineKind::Signalsmith;
    }

private:
    [[nodiscard]] bool isPassthrough() const noexcept
    {
        return std::abs(tempo_ratio_ - 1.0) < 0.000001
            && std::abs(rate_ratio_ - 1.0) < 0.000001
            && std::abs(pitch_cents_) < 0.000001;
    }

    [[nodiscard]] long double playbackRate() const noexcept
    {
        return static_cast<long double>(tempo_ratio_)
            * static_cast<long double>(rate_ratio_);
    }

    void applySettings()
    {
        const double transpose = std::pow(2.0, pitch_cents_ / 1'200.0)
            * rate_ratio_;
        stretch_.setTransposeFactor(static_cast<float>(transpose));
        stretch_.setFormantFactor(1.0F, formant_preservation_);
    }

    void prime()
    {
        deinterleave(startup_interleaved_.data(), input_latency_);
        for (std::size_t channel = 0U; channel < channels_; ++channel) {
            input_ptrs_[channel] = input_planar_[channel].data();
        }
        stretch_.seek(input_ptrs_, static_cast<int>(input_latency_),
                      static_cast<double>(playbackRate()));
        discard_frames_ = output_latency_;
        primed_ = true;
    }

    [[nodiscard]] std::size_t outputFramesFor(const std::size_t inputFrames)
    {
        output_fraction_ += static_cast<long double>(inputFrames) / playbackRate();
        const std::size_t target = static_cast<std::size_t>(
            std::floor(output_fraction_ + 0.0000001L));
        const std::size_t result = target - generated_frames_;
        generated_frames_ = target;
        return result;
    }

    [[nodiscard]] std::size_t maxInputFrames() const noexcept
    {
        const long double limit = std::floor(
            static_cast<long double>(kWorkFrames) * playbackRate());
        return std::max<std::size_t>(1U, std::min(
            kWorkFrames, static_cast<std::size_t>(limit)));
    }

    void processInterleaved(const float* const samples,
                            const std::size_t inputFrames,
                            const std::size_t outputFrames)
    {
        if (inputFrames > kWorkFrames || outputFrames > kWorkFrames) {
            markFailed("Signalsmith processing block exceeded fixed capacity");
            return;
        }
        deinterleave(samples, inputFrames);
        for (std::size_t channel = 0U; channel < channels_; ++channel) {
            input_ptrs_[channel] = input_planar_[channel].data();
            output_ptrs_[channel] = output_planar_[channel].data();
        }
        stretch_.process(input_ptrs_, static_cast<int>(inputFrames), output_ptrs_,
                         static_cast<int>(outputFrames));
        pushPlanar(outputFrames);
    }

    void processSilence(std::size_t inputFrames)
    {
        while (!failed_ && inputFrames > 0U) {
            const std::size_t count = std::min(maxInputFrames(), inputFrames);
            for (std::size_t channel = 0U; channel < channels_; ++channel) {
                std::fill_n(input_planar_[channel].data(), count, 0.0F);
                input_ptrs_[channel] = input_planar_[channel].data();
                output_ptrs_[channel] = output_planar_[channel].data();
            }
            const std::size_t outputFrames = outputFramesFor(count);
            stretch_.process(input_ptrs_, static_cast<int>(count), output_ptrs_,
                             static_cast<int>(outputFrames));
            pushPlanar(outputFrames);
            inputFrames -= count;
        }
    }

    void deinterleave(const float* const samples, const std::size_t frames)
    {
        for (std::size_t frame = 0U; frame < frames; ++frame) {
            for (std::size_t channel = 0U; channel < channels_; ++channel) {
                input_planar_[channel][frame]
                    = samples[frame * channels_ + channel];
            }
        }
    }

    void pushPlanar(std::size_t frames)
    {
        std::size_t sourceOffset = 0U;
        if (discard_frames_ > 0U) {
            const std::size_t skipped = std::min(discard_frames_, frames);
            discard_frames_ -= skipped;
            frames -= skipped;
            if (frames == 0U) return;
            sourceOffset = skipped;
        }
        if (emitted_frames_ >= final_output_frames_) return;
        frames = std::min(frames, final_output_frames_ - emitted_frames_);
        if (frames > kBufferedFrames - ring_frames_) {
            markFailed("Signalsmith output ring is full; caller must receive sooner");
            return;
        }
        for (std::size_t frame = 0U; frame < frames; ++frame) {
            const std::size_t index = (ring_write_ + frame) % kBufferedFrames;
            for (std::size_t channel = 0U; channel < channels_; ++channel) {
                output_ring_[index * channels_ + channel]
                    = output_planar_[channel][sourceOffset + frame];
            }
        }
        ring_write_ = (ring_write_ + frames) % kBufferedFrames;
        ring_frames_ += frames;
        emitted_frames_ += frames;
    }

    void pushInterleaved(const float* const samples, const std::size_t frames)
    {
        if (frames > kBufferedFrames - ring_frames_) {
            markFailed(
                "Signalsmith passthrough ring is full; caller must receive sooner");
            return;
        }
        for (std::size_t frame = 0U; frame < frames; ++frame) {
            const std::size_t index = (ring_write_ + frame) % kBufferedFrames;
            std::copy_n(samples + frame * channels_, channels_,
                        output_ring_.data() + index * channels_);
        }
        ring_write_ = (ring_write_ + frames) % kBufferedFrames;
        ring_frames_ += frames;
    }

    void markFailed(const char* const reason) noexcept
    {
        if (!failed_) std::fprintf(stderr, "%s\n", reason);
        failed_ = true;
    }

    // Keep independent realtime and offline sessions sample-reproducible.
    signalsmith::stretch::SignalsmithStretch<float> stretch_{0x4147506cL};
    std::array<std::vector<float>, kMaxChannels> input_planar_;
    std::array<std::vector<float>, kMaxChannels> output_planar_;
    std::array<float*, kMaxChannels> input_ptrs_{};
    std::array<float*, kMaxChannels> output_ptrs_{};
    std::vector<float> startup_interleaved_;
    std::vector<float> output_ring_;
    int sample_rate_{};
    std::size_t channels_{};
    std::size_t input_latency_{};
    std::size_t output_latency_{};
    std::size_t startup_frames_{};
    std::size_t input_frames_{};
    std::size_t generated_frames_{};
    std::size_t emitted_frames_{};
    std::size_t discard_frames_{};
    std::size_t final_output_frames_{std::numeric_limits<std::size_t>::max()};
    std::size_t ring_read_{};
    std::size_t ring_write_{};
    std::size_t ring_frames_{};
    long double output_fraction_{};
    double tempo_ratio_{1.0};
    double pitch_cents_{};
    double rate_ratio_{1.0};
    bool formant_preservation_{};
    bool configured_{};
    bool primed_{};
    bool flushed_{};
    bool failed_{};
};

} // namespace

std::unique_ptr<ITimePitchEngine> create_signalsmith_time_pitch_engine()
{
    return std::make_unique<SignalsmithTimePitchEngine>();
}

} // namespace agplayer

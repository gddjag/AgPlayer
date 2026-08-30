#include "time_pitch_engine.hpp"

#include <cmath>
#include <atomic>
#include <cstdio>
#include <limits>
#include <utility>

#include <soundtouch/SoundTouch.h>

namespace agplayer {
namespace {

class SoundTouchTimePitchEngine final : public ITimePitchEngine {
public:
    bool configure(const int sampleRate, const int channels) override
    {
        if (sampleRate <= 0 || channels <= 0 || channels > 2) {
            return false;
        }
        processor_.clear();
        processor_.setSampleRate(static_cast<unsigned int>(sampleRate));
        processor_.setChannels(static_cast<unsigned int>(channels));
        configured_ = true;
        return true;
    }

    bool setTempoRatio(const double ratio) override
    {
        if (!configured_ || !validRatio(ratio)) {
            return false;
        }
        processor_.setTempo(static_cast<float>(ratio));
        return true;
    }

    bool setPitchCents(const double cents) override
    {
        if (!configured_ || !std::isfinite(cents)
            || cents < -1200.0 || cents > 1200.0) {
            return false;
        }
        processor_.setPitchSemiTones(static_cast<float>(cents / 100.0));
        return true;
    }

    bool setRateRatio(const double ratio) override
    {
        if (!configured_ || !validRatio(ratio)) {
            return false;
        }
        processor_.setRate(static_cast<float>(ratio));
        return true;
    }

    void put(const float* const samples, const std::size_t frames) override
    {
        if (configured_ && samples != nullptr && frames > 0U
            && frames <= std::numeric_limits<unsigned int>::max()) {
            processor_.putSamples(samples, static_cast<unsigned int>(frames));
        }
    }

    [[nodiscard]] std::size_t receive(float* const samples,
                                      const std::size_t frames) override
    {
        if (!configured_ || samples == nullptr || frames == 0U) {
            return 0U;
        }
        const std::size_t bounded = std::min(
            frames, static_cast<std::size_t>(std::numeric_limits<unsigned int>::max()));
        return static_cast<std::size_t>(processor_.receiveSamples(
            samples, static_cast<unsigned int>(bounded)));
    }

    void flush() override
    {
        if (configured_) {
            processor_.flush();
        }
    }

    void reset() override
    {
        if (configured_) {
            processor_.clear();
        }
    }

private:
    static bool validRatio(const double ratio)
    {
        return std::isfinite(ratio) && ratio >= 0.5 && ratio <= 2.0;
    }

    soundtouch::SoundTouch processor_;
    bool configured_ = false;
};

} // namespace

class PreferredTimePitchEngine final : public ITimePitchEngine {
public:
    bool configure(const int sampleRate, const int channels) override
    {
        if (primary_ != nullptr && primary_->configure(sampleRate, channels)) {
            active_ = primary_.get();
            return true;
        }
        fallback_ = create_soundtouch_time_pitch_engine();
        if (fallback_ == nullptr || !fallback_->configure(sampleRate, channels)) {
            return false;
        }
        static std::atomic_bool logged{false};
        if (!logged.exchange(true, std::memory_order_relaxed)) {
            std::fputs("Signalsmith time/pitch configure failed; using SoundTouch fallback\n",
                       stderr);
        }
        active_ = fallback_.get();
        return true;
    }

    bool setTempoRatio(const double ratio) override
    {
        return active_ != nullptr && active_->setTempoRatio(ratio);
    }

    bool setPitchCents(const double cents) override
    {
        return active_ != nullptr && active_->setPitchCents(cents);
    }

    bool setRateRatio(const double ratio) override
    {
        return active_ != nullptr && active_->setRateRatio(ratio);
    }

    void put(const float* const samples, const std::size_t frames) override
    {
        if (active_ != nullptr) active_->put(samples, frames);
    }

    [[nodiscard]] std::size_t receive(float* const samples,
                                      const std::size_t frames) override
    {
        return active_ != nullptr ? active_->receive(samples, frames) : 0U;
    }

    void flush() override
    {
        if (active_ != nullptr) active_->flush();
    }

    void reset() override
    {
        if (active_ != nullptr) active_->reset();
    }

private:
    std::unique_ptr<ITimePitchEngine> primary_{create_signalsmith_time_pitch_engine()};
    std::unique_ptr<ITimePitchEngine> fallback_;
    ITimePitchEngine* active_{};
};

std::unique_ptr<ITimePitchEngine> create_soundtouch_time_pitch_engine()
{
    return std::make_unique<SoundTouchTimePitchEngine>();
}

std::unique_ptr<ITimePitchEngine> create_time_pitch_engine()
{
    return std::make_unique<PreferredTimePitchEngine>();
}

} // namespace agplayer

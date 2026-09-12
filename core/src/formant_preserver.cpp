#include "formant_preserver.hpp"

#include <algorithm>
#include <cmath>

namespace agplayer {
namespace {

constexpr std::array<double, 14U> kCenters{
    200.0, 280.0, 400.0, 560.0, 700.0, 850.0, 1'000.0,
    1'200.0, 1'500.0, 2'000.0, 2'600.0, 3'000.0, 4'000.0, 5'500.0};

double smoothingCoefficient(const double seconds, const int sampleRate) noexcept
{
    return std::exp(-1.0 / (seconds * sampleRate));
}

} // namespace

float FormantPreserver::BandState::filter(const float input) noexcept
{
    const double output = b0 * input + b1 * x1 + b2 * x2
        - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = input;
    y2 = y1;
    y1 = output;
    return static_cast<float>(output);
}

void FormantPreserver::BandState::clear() noexcept
{
    x1 = 0.0;
    x2 = 0.0;
    y1 = 0.0;
    y2 = 0.0;
    envelope = 0.0;
    gain = 1.0;
}

FormantPreserver::FormantPreserver(const int sampleRate, const int channels,
                                   const double pitchRatio)
    : channels_(std::max(0, channels)),
      pitch_ratio_(std::isfinite(pitchRatio) && pitchRatio > 0.0
                       ? pitchRatio : 1.0),
      attack_(sampleRate > 0 ? smoothingCoefficient(0.004, sampleRate) : 0.0),
      release_(sampleRate > 0 ? smoothingCoefficient(0.050, sampleRate) : 0.0),
      gain_smoothing_(sampleRate > 0
                          ? smoothingCoefficient(0.012, sampleRate) : 0.0),
      centers_(kCenters),
      states_(static_cast<std::size_t>(channels_) * kBandCount)
{
    if (sampleRate <= 0) {
        channels_ = 0;
        states_.clear();
        return;
    }
    const double pi = std::acos(-1.0);
    constexpr double q = 5.0;
    for (std::size_t band = 0; band < kBandCount; ++band) {
        const double frequency = std::min(
            centers_[band], static_cast<double>(sampleRate) * 0.45);
        const double omega = 2.0 * pi * frequency / sampleRate;
        const double alpha = std::sin(omega) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        for (int channel = 0; channel < channels_; ++channel) {
            BandState& state = states_[static_cast<std::size_t>(channel)
                                      * kBandCount + band];
            state.b0 = alpha / a0;
            state.b1 = 0.0;
            state.b2 = -alpha / a0;
            state.a1 = -2.0 * std::cos(omega) / a0;
            state.a2 = (1.0 - alpha) / a0;
        }
        const double sourceFrequency = centers_[band] * pitch_ratio_;
        if (sourceFrequency <= centers_.front()) {
            source_low_[band] = 0U;
            source_high_[band] = 0U;
        } else if (sourceFrequency >= centers_.back()) {
            source_low_[band] = kBandCount - 1U;
            source_high_[band] = kBandCount - 1U;
        } else {
            const auto upper = std::upper_bound(
                centers_.begin(), centers_.end(), sourceFrequency);
            source_high_[band] = static_cast<std::size_t>(
                std::distance(centers_.begin(), upper));
            source_low_[band] = source_high_[band] - 1U;
            source_fraction_[band] =
                (std::log(sourceFrequency)
                    - std::log(centers_[source_low_[band]]))
                / (std::log(centers_[source_high_[band]])
                    - std::log(centers_[source_low_[band]]));
        }
    }
}

void FormantPreserver::reset() noexcept
{
    for (BandState& state : states_) state.clear();
}

double FormantPreserver::interpolatedEnvelope(
    const std::size_t channel, const std::size_t band) const noexcept
{
    const std::size_t base = channel * kBandCount;
    const std::size_t low = source_low_[band];
    const std::size_t high = source_high_[band];
    return states_[base + low].envelope
        + source_fraction_[band] * (states_[base + high].envelope
                                   - states_[base + low].envelope);
}

void FormantPreserver::process(float* const interleaved,
                               const std::size_t frames) noexcept
{
    if (interleaved == nullptr || channels_ <= 0
        || std::abs(pitch_ratio_ - 1.0) < 0.000001) {
        return;
    }
    std::array<float, kBandCount> bands{};
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t channel = 0;
             channel < static_cast<std::size_t>(channels_); ++channel) {
            const std::size_t sampleIndex = frame
                * static_cast<std::size_t>(channels_) + channel;
            const float dry = interleaved[sampleIndex];
            const std::size_t base = channel * kBandCount;
            for (std::size_t band = 0; band < kBandCount; ++band) {
                BandState& state = states_[base + band];
                bands[band] = state.filter(dry);
                const double magnitude = std::abs(bands[band]);
                const double coefficient = magnitude > state.envelope
                    ? attack_ : release_;
                state.envelope = coefficient * state.envelope
                    + (1.0 - coefficient) * magnitude;
            }

            double correction = 0.0;
            for (std::size_t band = 0; band < kBandCount; ++band) {
                BandState& state = states_[base + band];
                const double desired = interpolatedEnvelope(
                    channel, band);
                const double targetGain = std::clamp(
                    desired / std::max(state.envelope, 0.00001), 0.1, 12.0);
                state.gain = gain_smoothing_ * state.gain
                    + (1.0 - gain_smoothing_) * targetGain;
                correction += bands[band] * (state.gain - 1.0);
            }
            interleaved[sampleIndex] = std::clamp(
                static_cast<float>(dry + correction), -1.0F, 1.0F);
        }
    }
}

} // namespace agplayer

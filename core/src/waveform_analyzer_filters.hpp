#pragma once

#include <cmath>

namespace agplayer {
namespace detail {

inline constexpr float kPi = 3.14159265358979323846F;

// Lightweight 2nd-order IIR (biquad) filter used to split the decoded signal
// into bass/mid/high bands for the layered waveform. Coefficients follow the
// standard audio EQ cookbook (Robert Bristow-Johnson).
class BiquadFilter final {
public:
    enum class Type { Lowpass, Highpass };

    BiquadFilter(Type type, float sample_rate, float frequency, float q)
    {
        const float w0 = 2.0F * kPi * frequency / sample_rate;
        const float cos_w0 = std::cos(w0);
        const float sin_w0 = std::sin(w0);
        const float alpha = sin_w0 / (2.0F * q);

        if (type == Type::Lowpass) {
            b0_ = (1.0F - cos_w0) / 2.0F;
            b1_ = 1.0F - cos_w0;
            b2_ = (1.0F - cos_w0) / 2.0F;
        } else {
            b0_ = (1.0F + cos_w0) / 2.0F;
            b1_ = -(1.0F + cos_w0);
            b2_ = (1.0F + cos_w0) / 2.0F;
        }

        const float a0 = 1.0F + alpha;
        a1_ = -2.0F * cos_w0;
        a2_ = 1.0F - alpha;

        b0_ /= a0;
        b1_ /= a0;
        b2_ /= a0;
        a1_ /= a0;
        a2_ /= a0;
    }

    float process(float input) noexcept
    {
        const float output = b0_ * input + b1_ * x1_ + b2_ * x2_
                             - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = input;
        y2_ = y1_;
        y1_ = output;
        return output;
    }

private:
    float b0_ = 0.0F;
    float b1_ = 0.0F;
    float b2_ = 0.0F;
    float a1_ = 0.0F;
    float a2_ = 0.0F;
    float x1_ = 0.0F;
    float x2_ = 0.0F;
    float y1_ = 0.0F;
    float y2_ = 0.0F;
};

// Two-stage bandpass: highpass(low_cut) -> lowpass(high_cut).
class BandpassFilter final {
public:
    BandpassFilter(float sample_rate, float low_cut, float high_cut)
        : highpass_(BiquadFilter::Type::Highpass, sample_rate, low_cut, 0.707F),
          lowpass_(BiquadFilter::Type::Lowpass, sample_rate, high_cut, 0.707F)
    {
    }

    float process(float input) noexcept
    {
        return lowpass_.process(highpass_.process(input));
    }

private:
    BiquadFilter highpass_;
    BiquadFilter lowpass_;
};

} // namespace detail
} // namespace agplayer

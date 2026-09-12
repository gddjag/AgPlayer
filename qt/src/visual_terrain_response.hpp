#pragma once
#include "visual_spectrum_features.hpp"
namespace agplayer {
class VisualTerrainResponse {
public:
    using EqBands = std::array<double,8>;
    using EnabledBands = std::array<bool,8>;
    using Features = VisualSpectrumFeatures::Features;
    void reset() noexcept { state_ = {}; }

    // EQ is normalized 0..1; motion speed retains the UI's 0..100 domain.
    // The input already contains analyzer smoothing. Only terrain bands get
    // this second, frame-time-dependent response; descriptors stay immediate.
    const Features& update(const Features& input, double kickEnvelope,
                           const EqBands& eq, const EnabledBands& enabled,
                           double dt, double motionSpeed = 50) noexcept {
        const double speed = bounded(motionSpeed, 100, 50);
        const double seconds = std::isfinite(dt) ? (std::max)(0.0, dt) : 0;
        const double blend = 1 - std::exp(-(2.2 + .578 * speed) * seconds);
        const double kick = bounded(kickEnvelope, .75) / .75;
        double eqSum = 0;
        for (std::size_t i = 0; i < state_.bands.size(); ++i) {
            const double setting = bounded(eq[i], 1, .5);
            eqSum += setting;
            double value = bounded(input.bands[i], 1);
            if (i == 0) value = value * .22 + kick * 1.28;
            if (i == 1) value = value * .2 + kick * 1.15;
            // Settings are quantized to whole percentages by the reference UI.
            const double adjustment = std::floor(setting * 100 + .5) / 50 - 1;
            if (adjustment >= 0) value *= 1 + adjustment * 1.8;
            else {
                const double cut = -adjustment * .35;
                value = (std::max)(0.0, value - cut) * (1 - cut);
            }
            const double ceiling = i == 0 ? 1.2 : (i == 1 ? 1.15 : 1.0);
            const double target = enabled[i] ? bounded(value, ceiling) : 0;
            state_.bands[i] += (target - state_.bands[i]) * blend;
        }
        state_.energy = bounded(input.energy * (.25 + eqSum / 8 * 1.5), 1);
        const auto& b = state_.bands;
        const double low = b[0] + b[1] + b[2] + b[3];
        const double high = b[5] + b[6] + b[7];
        // highMid does not participate in the original scene's palette ratio.
        const double total = (std::max)(.001, low + high);
        state_.warmth = bounded(low / total, 1);
        state_.brightness = bounded(high / total, 1);
        state_.sharpness = input.sharpness;
        state_.smoothness = input.smoothness;
        state_.density = input.density;
        state_.spectralCentroid = input.spectralCentroid;
        return state_;
    }

    const Features& features() const noexcept { return state_; }

private:
    static double bounded(double value, double maximum, double fallback = 0) noexcept {
        return std::isfinite(value) ? std::clamp(value, 0.0, maximum) : fallback;
    }
    Features state_{};
};
}

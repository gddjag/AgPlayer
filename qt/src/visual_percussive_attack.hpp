#pragma once
#include <array>
#include <algorithm>
#include <cmath>

namespace agplayer::visual {
// Recovery requires sparse low-band amplitude attack evidence. Reference
// candidates use only the short PCM RMS rise, and only for isolated low tones.
// Full-mix RMS cannot reject drums underneath a concurrent sustained layer.
class PercussiveAttack {
public:
    using Magnitudes = std::array<double, 8>;
    void reset() noexcept { *this = {}; }
    bool allowsReferenceAttack() const noexcept { return referenceRemaining_ > 0; }
    bool process(const Magnitudes& magnitudes, double dt, double rms) noexcept {
        dt = std::isfinite(dt) ? std::clamp(dt, 0.0, .25) : 0;
        double total = 0;
        for (std::size_t i = 1; i < magnitudes.size(); ++i) {
            const double value = std::isfinite(magnitudes[i]) ? (std::max)(0.0, magnitudes[i]) : 0;
            total += value;
        }
        auto ordered = history_;
        std::sort(ordered.begin(), ordered.end());
        const double background = ordered[9];
        const double rise = total - previous_;
        rms = std::isfinite(rms) ? (std::max)(0.0, rms) : 0.0;
        const double smoothed = smoothed_ + (rms - smoothed_) * .5;
        risingFor_ = smoothed > smoothed_ ? risingFor_ + dt : 0;
        smoothed_ = smoothed;
        history_[cursor_] = total;
        cursor_ = (cursor_ + 1) % history_.size();
        previous_ = total;
        remaining_ = (std::max)(0.0, remaining_ - dt);
        referenceRemaining_ = (std::max)(0.0, referenceRemaining_ - dt);
        // Reference detection already supplies spectral confidence. Do not
        // require the recovery path's large amplitude ratio for ordinary beats.
        // Keep evidence through the FFT/flux peak-confirmation delay.
        if (risingFor_ > 0 && risingFor_ <= .05 && rms > .002)
            referenceRemaining_ = .08;
        if (risingFor_ <= .05 && rise > .003 && total > background * 1.8 && rise > total * .3)
            remaining_ = .05; // Flux peak confirmation follows the attack.
        return remaining_ > 0;
    }
private:
    std::array<double, 12> history_{};
    std::size_t cursor_ = 0;
    double previous_ = 0;
    double smoothed_ = 0, risingFor_ = 0;
    double remaining_ = 0;
    double referenceRemaining_ = 0;
};
}

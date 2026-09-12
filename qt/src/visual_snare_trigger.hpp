#pragma once
#include "visual_spectrum_analyzer.hpp"
#include <cmath>

namespace agplayer {
class VisualSnareTrigger {
public:
    // Default reference Snare detector; consume the existing byte spectrum
    // exactly once per analyzed render frame. Presentation spacing is separate.
    struct Output { bool triggered = false; double strength = 0; };
    VisualSnareTrigger(std::size_t first = 47, std::size_t last = 120,
                       double sensitivity = .6, int cooldown = 30, double strength = .3)
        : first_((std::min)(first, std::size_t{511})),
          count_(std::clamp(last, first_, (std::min)(first_ + 73, std::size_t{511})) - first_ + 1),
          sensitivity_(std::isfinite(sensitivity) ? std::clamp(sensitivity, 0.0, 1.0) : .6),
          cooldown_(std::clamp(cooldown, 0, 300)),
          strength_(std::isfinite(strength) ? std::clamp(strength, 0.0, 5.0) : .3) {}
    void reset() noexcept {
        previous_.fill(0); history_.fill(0); next_ = 0;
        smoothed_ = previousSmoothed_ = 0; hold_ = 0;
    }
    Output suspend() noexcept { previous_.fill(0); return {}; }
    Output process(const VisualSpectrumAnalyzer::Spectrum& spectrum, bool valid = true) noexcept {
        if (!valid) { reset(); return {}; }
        double flux = 0;
        for (std::size_t i = 0; i < count_; ++i) {
            const double value = spectrum[i + first_] / 255.0;
            const double difference = value - previous_[i];
            if (difference > .01) flux += difference;
            previous_[i] = value;
        }
        smoothed_ += (flux / count_ - smoothed_) * .4;
        history_[next_] = smoothed_;
        next_ = (next_ + 1) % history_.size();
        double mean = 0;
        for (const double value : history_) mean += value;
        mean /= history_.size();
        double variance = 0;
        for (const double value : history_) variance += (value - mean) * (value - mean);
        variance /= history_.size();
        const double threshold = (std::max)(.01, mean + std::sqrt(variance) * (1 + (1 - sensitivity_) * 4));
        Output result;
        if (hold_ > 0) --hold_;
        else if (previousSmoothed_ > threshold && previousSmoothed_ >= smoothed_
                 && previousSmoothed_ - smoothed_ > .0001) {
            result = {true, previousSmoothed_ * 30.0 * strength_};
            hold_ = cooldown_;
        }
        previousSmoothed_ = smoothed_;
        return result;
    }
private:
    std::array<double,74> previous_{};
    std::array<double,40> history_{};
    std::size_t next_ = 0;
    double smoothed_ = 0, previousSmoothed_ = 0;
    int hold_ = 0;
    std::size_t first_, count_;
    double sensitivity_;
    int cooldown_;
    double strength_;
};
}

#pragma once
#include "visual_spectrum_analyzer.hpp"
#include <cmath>

namespace agplayer {
class VisualSnareTrigger {
public:
    // Default reference Snare detector; consume the existing byte spectrum
    // exactly once per analyzed render frame. Presentation spacing is separate.
    struct Output { bool triggered = false; double strength = 0; };
    void reset() noexcept { *this = {}; }
    Output process(const VisualSpectrumAnalyzer::Spectrum& spectrum, bool valid = true) noexcept {
        if (!valid) { reset(); return {}; }
        double flux = 0;
        for (std::size_t i = 0; i < previous_.size(); ++i) {
            const double value = spectrum[i + 47] / 255.0;
            const double difference = value - previous_[i];
            if (difference > .01) flux += difference;
            previous_[i] = value;
        }
        smoothed_ += (flux / previous_.size() - smoothed_) * .4;
        history_[next_] = smoothed_;
        next_ = (next_ + 1) % history_.size();
        double mean = 0;
        for (const double value : history_) mean += value;
        mean /= history_.size();
        double variance = 0;
        for (const double value : history_) variance += (value - mean) * (value - mean);
        variance /= history_.size();
        const double threshold = (std::max)(.01, mean + std::sqrt(variance) * 2.6);
        Output result;
        if (hold_ > 0) --hold_;
        else if (previousSmoothed_ > threshold && previousSmoothed_ >= smoothed_
                 && previousSmoothed_ - smoothed_ > .0001) {
            result = {true, previousSmoothed_ * 30.0 * .3};
            hold_ = 30;
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
};
}

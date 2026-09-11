#pragma once

#include "visual_spectrum_analyzer.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace agplayer {

// Literal port of Sonic Topography's default Auto Beat "Pulse" trigger.
// It is intentionally separate from KickResponse: the reference scene uses
// KickResponse for terrain/floating blocks and this trigger for colored waves.
class VisualPulseTrigger final {
public:
    struct Output {
        bool triggered = false;
        double strength = 0.0;
        std::size_t bandStart = 1;
        std::size_t bandEnd = 2;
    };

    void reset() noexcept { *this = {}; }

    Output suspend() noexcept
    {
        // AudioEngine clears prevData while playback is stopped, but does not
        // advance or reset the Auto Beat trigger's history and hold counters.
        previous_.fill(0.0);
        return {false, 0.0, bandStart_, bandEnd_};
    }

    Output process(const VisualSpectrumAnalyzer::Spectrum& spectrum,
                   double dt, bool valid = true) noexcept
    {
        if (!valid) {
            reset();
            return {};
        }
        dt = std::isfinite(dt) ? std::clamp(dt, 0.0, .25) : 0.0;
        elapsed_ += dt;
        appendTrackerFrame(spectrum);
        if (elapsed_ - lastTrackEvaluation_ > 1.0) {
            lastTrackEvaluation_ = elapsed_;
            evaluateTrackedBand();
        }

        double flux = 0.0;
        for (std::size_t bin = 0; bin < previous_.size(); ++bin) {
            const double value = spectrum[bin] / 255.0;
            if (bin >= bandStart_ && bin <= bandEnd_) {
                const double difference = value - previous_[bin];
                if (difference > .01) flux += difference;
            }
            previous_[bin] = value;
        }
        const double bins = double(bandEnd_ - bandStart_ + 1);
        smoothed_ += (flux / bins - smoothed_) * .4;
        history_[historyCursor_] = smoothed_;
        historyCursor_ = (historyCursor_ + 1) % history_.size();

        double mean = 0.0;
        for (double value : history_) mean += value;
        mean /= history_.size();
        double variance = 0.0;
        for (double value : history_) variance += (value - mean) * (value - mean);
        variance /= history_.size();
        const double threshold = (std::max)(.01, mean + std::sqrt(variance) * 1.6);

        Output result{false, 0.0, bandStart_, bandEnd_};
        holdRemaining_ = (std::max)(0.0, holdRemaining_ - dt);
        if (holdRemaining_ <= 0.0
            && previousSmoothed_ > threshold && previousSmoothed_ >= smoothed_
            && previousSmoothed_ - smoothed_ > .0001) {
            result.triggered = true;
            result.strength = previousSmoothed_ * 30.0 * .2;
            // The browser reference expresses this as 15 animation frames.
            // Normalize its high-refresh (~120 Hz) behavior to time so the
            // native fixed-rate analyzer does not turn it into a 250 ms gate.
            holdRemaining_ = 15.0 / 120.0;
        }
        previousSmoothed_ = smoothed_;
        return result;
    }

private:
    struct TrackerFrame {
        std::array<std::uint8_t, 30> bins{};
        double time = 0.0;
    };

    void appendTrackerFrame(const VisualSpectrumAnalyzer::Spectrum& spectrum) noexcept
    {
        tracker_[trackerWrite_] = {};
        std::copy_n(spectrum.begin(), tracker_[trackerWrite_].bins.size(),
                    tracker_[trackerWrite_].bins.begin());
        tracker_[trackerWrite_].time = elapsed_;
        trackerWrite_ = (trackerWrite_ + 1) % tracker_.size();
        trackerCount_ = (std::min)(trackerCount_ + 1, tracker_.size());
        while (trackerCount_ > 0) {
            const std::size_t oldest = (trackerWrite_ + tracker_.size()
                                      - trackerCount_) % tracker_.size();
            if (elapsed_ - tracker_[oldest].time <= 3.0) break;
            --trackerCount_;
        }
    }

    void evaluateTrackedBand() noexcept
    {
        if (trackerCount_ < 30) return;
        std::array<double, 30> maximum{};
        const std::size_t oldest = (trackerWrite_ + tracker_.size()
                                  - trackerCount_) % tracker_.size();
        for (std::size_t frame = 1; frame < trackerCount_; ++frame) {
            const auto& previous = tracker_[(oldest + frame - 1) % tracker_.size()];
            const auto& current = tracker_[(oldest + frame) % tracker_.size()];
            for (std::size_t bin = 0; bin < maximum.size(); ++bin) {
                const double difference = (double(current.bins[bin])
                    - double(previous.bins[bin])) / 255.0;
                if (difference > .01) maximum[bin] = (std::max)(maximum[bin], difference);
            }
        }
        // WebAudio's smoothed analyser normally spreads one kick across two
        // neighboring bins. The native FFT can also expose a stronger remote
        // transient; choosing two unrelated maxima would turn them into one
        // wide normalized band and dilute the later kick. Score adjacent
        // pairs so auto-track keeps the reference's intended narrow punch
        // window while retaining its 0.15 transient guard.
        std::size_t pairStart = 0;
        double pairScore = maximum[0] + maximum[1];
        for (std::size_t bin = 1; bin + 1 < maximum.size(); ++bin) {
            const double score = maximum[bin] + maximum[bin + 1];
            if (score > pairScore) {
                pairStart = bin;
                pairScore = score;
            }
        }
        if ((std::max)(maximum[pairStart], maximum[pairStart + 1]) < .15)
            return;
        bandStart_ = pairStart;
        bandEnd_ = pairStart + 1;
    }

    std::array<double, VisualSpectrumAnalyzer::binCount> previous_{};
    std::array<double, 40> history_{};
    std::array<TrackerFrame, 192> tracker_{};
    std::size_t historyCursor_ = 0;
    std::size_t trackerWrite_ = 0;
    std::size_t trackerCount_ = 0;
    std::size_t bandStart_ = 1;
    std::size_t bandEnd_ = 2;
    double smoothed_ = 0.0;
    double previousSmoothed_ = 0.0;
    double elapsed_ = 0.0;
    double lastTrackEvaluation_ = 0.0;
    double holdRemaining_ = 0.0;
};

} // namespace agplayer

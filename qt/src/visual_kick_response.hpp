#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cmath>
namespace agplayer::visual {
// Production onset/envelope detector shared by the PCM renderer and studies.
// Fixed storage keeps processing allocation-free and independent of Qt.
class KickResponse {
public:
    enum class Mode { TransientRecovery, Reference };
    explicit KickResponse(Mode mode = Mode::TransientRecovery) noexcept : mode_(mode) {}
    using Spectrum = std::array<std::uint8_t, 512>;
    struct Output { double level{}, flux{}, threshold{}, onset{}, envelope{}, confidence{}; std::size_t windowIndex{}; };
    Output process(const Spectrum& spectrum, double dt, double sensitivity = 100,
                   bool percussiveAttack = true, bool recoveredOnset = false) noexcept {
        dt = std::isfinite(dt) ? (std::max)(0.0, dt) : 0.0;
        const double setting = std::isfinite(sensitivity)
            ? std::floor(std::clamp(sensitivity, 0.0, 100.0) + .5) : 100.0;
        const double lower = (std::min)(setting / 50.0, 1.0);
        const double upper = (std::max)((setting - 50.0) / 50.0, 0.0);
        const auto parameter = [lower, upper](double a, double b, double c) {
            const double middle = a + (b - a) * lower;
            return middle + (c - middle) * upper;
        };
        const double gain = parameter(2.6, 1.8, 1.1);
        const double minimum = parameter(.07, .045, .025);
        double baseline = 0, deviation = 0;
        if (mode_ == Mode::Reference) {
            // sonic-topography beatDetector.ts: mean/std of smoothed flux.
            for (const double value : history_) baseline += value;
            baseline /= history_.size();
            for (const double value : history_) deviation += (value - baseline) * (value - baseline);
            deviation = std::sqrt(deviation / history_.size());
        } else {
            // Robust recovery floor: preceding drum peaks must not become
            // the background gate. Only this supplementary path uses MAD.
            auto sorted = history_;
            std::sort(sorted.begin(), sorted.end());
            baseline = (sorted[44] + sorted[45]) * .5;
            for (double& value : sorted) value = std::abs(value - baseline);
            std::sort(sorted.begin(), sorted.end());
            deviation = (sorted[44] + sorted[45]) * .5 * 1.4826;
        }
        const double threshold = (std::max)(parameter(.05, .028, .016),
            baseline + gain * deviation);

        std::array<double, 4> levels{}, changes{};
        constexpr std::array<std::size_t, 4> starts{0, 1, 2, 0}, ends{2, 4, 6, 7};
        for (std::size_t w = 0; w < levels.size(); ++w) {
            const double width = static_cast<double>(ends[w] - starts[w] + 1);
            const double center = (starts[w] + ends[w]) / 2.0;
            double total = 0;
            for (std::size_t b = starts[w]; b <= ends[w]; ++b) {
                const double weight = .35 + .65 * (1.0 - (std::min)(1.0, std::abs(b - center) / (std::max)(1.0, width / 2.0)));
                levels[w] += spectrum[b] / 255.0 * weight;
                total += weight;
            }
            levels[w] /= total;
            changes[w] = (std::max)(0.0, levels[w] - previous_[w]);
            scores_[w] = scores_[w] * .965 + changes[w] * (1.0 / std::sqrt(width));
        }
        for (std::size_t w = 0; w < levels.size(); ++w)
            if (scores_[w] > scores_[window_] * 1.03) window_ = w;
        const double nextFlux = flux_ + (changes[window_] - flux_) * .35;
        refractory_ = (std::max)(0.0, refractory_ - dt);
        // Keep the source peak rule literal. Only supplementary recovery
        // requires a fresh rise-to-fall transition to reject a decaying tail.
        const bool peak = (mode_ == Mode::Reference || rising_) && flux_ > threshold
            && flux_ >= nextFlux && flux_ >= minimum;
        const bool referenceOnset = refractory_ <= 0 && peak
            && (mode_ == Mode::Reference || percussiveAttack);
        const bool triggered = (referenceOnset && percussiveAttack) || recoveredOnset;
        const double displayFlux = triggered ? flux_ : nextFlux;
        // A recovered transient must never reset the original detector's
        // cooldown and veto the next source-confirmed beat.
        if (referenceOnset) refractory_ = .12;
        // Track attack increments, not their smoothed decay. At fast tempos
        // decays occupy most frames and would otherwise be learned as noise,
        // raising the threshold above the next quieter but identical kick.
        history_[cursor_] = mode_ == Mode::Reference ? nextFlux : changes[window_];
        cursor_ = (cursor_ + 1) % history_.size();
        rising_ = nextFlux > flux_;
        flux_ = nextFlux;
        previous_ = levels;

        // Envelope evolution uses a nominal frame when detector time is paused.
        const double envelopeDt = dt > 0 ? dt : 1.0 / 60.0;
        const double raw = levels[window_];
        floor_ += (raw - floor_) * (1.0 - std::exp(-(raw > floor_ ? 1.15 : .35) * envelopeDt));
        const double level = std::clamp(raw - floor_ - .025, 0.0, 1.0);
        const double breath = (std::min)(.11, level * .18);
        const double target = (std::max)(breath, triggered ? (std::max)(.48, level * .95) : 0.0);
        envelope_ = std::clamp((std::max)(breath, envelope_ + (target - envelope_) *
            (1.0 - std::exp(-(target > envelope_ ? 42.0 : 11.5) * envelopeDt))), 0.0, 1.0);
        return {level, displayFlux, threshold, triggered ? 1.0 : 0.0, envelope_,
            std::clamp(displayFlux / (std::max)(.001, threshold * 2.2), 0.0, 1.0), window_};
    }
    void reset() noexcept { *this = KickResponse{mode_}; }
private:
    Mode mode_;
    std::array<double, 4> scores_{}, previous_{};
    std::array<double, 90> history_{};
    std::size_t window_{1}, cursor_{};
    double flux_{}, refractory_{}, floor_{}, envelope_{};
    bool rising_ = false;
};
}

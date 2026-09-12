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
    using Spectrum = std::array<std::uint8_t, 512>;
    struct Output { double level{}, flux{}, threshold{}, onset{}, envelope{}, confidence{}; std::size_t windowIndex{}; };
    Output process(const Spectrum& spectrum, double dt, double sensitivity = 100) noexcept {
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
        // A loud introduction must not raise the gate above identical later
        // kicks after a bass layer enters. Estimate the noise floor with
        // median/MAD, so preceding drum peaks do not become the noise gate.
        // The absolute flux minimum still rejects silence and low-level jitter.
        auto sorted = history_;
        std::sort(sorted.begin(), sorted.end());
        const double median = (sorted[44] + sorted[45]) * .5;
        for (double& value : sorted) value = std::abs(value - median);
        std::sort(sorted.begin(), sorted.end());
        const double deviation = (sorted[44] + sorted[45]) * .5 * 1.4826;
        const double threshold = (std::max)(parameter(.05, .028, .016),
            median + gain * deviation);

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
        // A decaying tail is not a second attack when the refractory timer
        // expires. Require a new rise-to-fall transition for each onset.
        const bool triggered = refractory_ <= 0 && rising_ && flux_ > threshold
            && flux_ >= nextFlux && flux_ >= minimum;
        const double displayFlux = triggered ? flux_ : nextFlux;
        if (triggered) refractory_ = .12;
        // Track attack increments, not their smoothed decay. At fast tempos
        // decays occupy most frames and would otherwise be learned as noise,
        // raising the threshold above the next quieter but identical kick.
        history_[cursor_] = changes[window_];
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
    void reset() noexcept { *this = KickResponse{}; }
private:
    std::array<double, 4> scores_{}, previous_{};
    std::array<double, 90> history_{};
    std::size_t window_{1}, cursor_{};
    double flux_{}, refractory_{}, floor_{}, envelope_{};
    bool rising_ = false;
};
}

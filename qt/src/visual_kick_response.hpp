#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cmath>
namespace agplayer::visual {
// Standalone numerical study component; not connected to production playback.
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
        double mean = 0;
        for (double value : history_) mean += value;
        mean /= history_.size();
        double variance = 0;
        for (double value : history_) variance += (value - mean) * (value - mean);
        const double threshold = (std::max)(parameter(.05, .028, .016),
            mean + gain * std::sqrt(variance / history_.size()));

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
        const bool triggered = refractory_ <= 0 && flux_ > threshold && flux_ >= nextFlux && flux_ >= minimum;
        const double displayFlux = triggered ? flux_ : nextFlux;
        if (triggered) refractory_ = .12;
        history_[cursor_] = nextFlux;
        cursor_ = (cursor_ + 1) % history_.size();
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
};
}

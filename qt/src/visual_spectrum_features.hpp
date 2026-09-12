#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace agplayer {

// Visual-only descriptors of the 512 byte spectrum. No audio-core state,
// allocations, wall-clock dependency, or renderer configuration.
class VisualSpectrumFeatures {
public:
    using Spectrum = std::array<std::uint8_t, 512>;
    struct Features {
        // sub, legacy bass, lowMid, legacy mid, highMid, presence, brilliance, air.
        std::array<double, 8> bands{};
        double energy = 0, warmth = 0, brightness = 0, sharpness = 0;
        double smoothness = 0, density = 0, spectralCentroid = 0;
    };

    void reset() noexcept { state_ = {}; previous_ = {}; previousBrightness_ = 0; }

    const Features& update(const Spectrum& bytes, bool playing, bool releasing) noexcept {
        Features target;
        target.smoothness = 1;
        if (playing) {
            std::array<double, 512> current{};
            double total = 0, weighted = 0, change = 0;
            for (std::size_t i = 0; i < current.size(); ++i) {
                current[i] = static_cast<double>(bytes[i]) / 255.0;
                total += current[i]; weighted += static_cast<double>(i) * current[i];
                change += std::abs(current[i] - previous_[i]);
            }
            const auto sum = [&current](std::size_t first, std::size_t end) {
                double value = 0;
                for (auto i = first; i < end; ++i) value += current[i];
                return value;
            };
            constexpr std::array<std::size_t, 9> edges{0,2,4,8,19,47,94,187,373};
            target.energy = total / 512.0;
            for (std::size_t i = 0; i < 8; ++i) {
                target.bands[i] = sum(edges[i], edges[i+1]) / static_cast<double>(edges[i+1]-edges[i]);
                if (target.bands[i] > target.energy * 1.5) target.density += 1.0 / 8.0;
            }
            target.bands[1] = sum(0,8) / 8.0;
            target.bands[3] = sum(8,47) / 39.0;
            if (total > 0) {
                target.warmth = sum(0,19) / total;
                target.brightness = sum(47,373) / total;
                target.spectralCentroid = weighted / total;
            }
            target.sharpness = (std::max)(0.0, target.brightness - previousBrightness_) * 10.0;
            target.smoothness = (std::max)(0.0, 1.0 - change / 512.0 * 2.0);
            previous_ = current;
        } else {
            previous_ = {};
        }
        previousBrightness_ = target.brightness;
        const double coefficient = target.energy > 0 ? .15 : (releasing ? .035 : .08);
        const auto smooth = [coefficient](double& value, double goal) { value += (goal - value) * coefficient; };
        for (std::size_t i = 0; i < 8; ++i) smooth(state_.bands[i], target.bands[i]);
        smooth(state_.energy, target.energy); smooth(state_.warmth, target.warmth);
        smooth(state_.brightness, target.brightness); smooth(state_.sharpness, target.sharpness);
        smooth(state_.smoothness, target.smoothness); smooth(state_.density, target.density);
        smooth(state_.spectralCentroid, target.spectralCentroid);
        return state_;
    }

private:
    Features state_{};
    std::array<double, 512> previous_{};
    double previousBrightness_ = 0;
};
} // namespace agplayer

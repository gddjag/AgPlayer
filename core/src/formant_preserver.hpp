#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace agplayer {

// Tracks a coarse vocal spectral envelope and remaps it against the pitch
// ratio. Unlike a brightness filter, this applies independent corrections to
// several speech bands so resonant peaks stay near their pre-shift positions.
class FormantPreserver final {
public:
    FormantPreserver(int sampleRate, int channels, double pitchRatio);

    void reset() noexcept;
    void process(float* interleaved, std::size_t frames) noexcept;

private:
    static constexpr std::size_t kBandCount = 14U;

    struct BandState final {
        double b0{};
        double b1{};
        double b2{};
        double a1{};
        double a2{};
        double x1{};
        double x2{};
        double y1{};
        double y2{};
        double envelope{};
        double gain{1.0};

        [[nodiscard]] float filter(float input) noexcept;
        void clear() noexcept;
    };

    [[nodiscard]] double interpolatedEnvelope(std::size_t channel,
                                              std::size_t band) const noexcept;

    int channels_{};
    double pitch_ratio_{1.0};
    double attack_{};
    double release_{};
    double gain_smoothing_{};
    std::array<double, kBandCount> centers_{};
    std::array<std::size_t, kBandCount> source_low_{};
    std::array<std::size_t, kBandCount> source_high_{};
    std::array<double, kBandCount> source_fraction_{};
    std::vector<BandState> states_;
};

} // namespace agplayer

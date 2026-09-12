#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <memory>

namespace agplayer {

inline constexpr std::size_t kGraphicEqBandCount = 18;
inline constexpr std::size_t kGraphicEqMaxChannels = 8;
inline constexpr double kGraphicEqDefaultQ = 2.145;
inline constexpr std::array<double, kGraphicEqBandCount>
    kGraphicEqBandFrequenciesHz{
        20.0, 31.5, 50.0, 80.0, 125.0, 200.0, 315.0, 500.0, 800.0,
        1'250.0, 2'000.0, 3'150.0, 5'000.0, 8'000.0, 10'000.0, 12'500.0,
        16'000.0, 20'000.0};

struct GraphicEqSettings {
    bool enabled = true;
    bool bypassed = false;
    bool auto_clip_protection = true;
    double preamp_db = 0.0;
    std::array<double, kGraphicEqBandCount> band_gain_db{};
    double q = kGraphicEqDefaultQ;
    double transition_ms = 25.0;
};

struct BiquadCoefficients {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
    bool active = false;
};

struct GraphicEqProgram {
    GraphicEqSettings settings;
    int sample_rate = 0;
    std::uint64_t version = 0;
    std::array<BiquadCoefficients, kGraphicEqBandCount> bands{};
    double protection_db = 0.0;
    // Calculated off the audio callback.  Rendering only multiplies by this
    // value, so slider changes never introduce per-sample transcendental work.
    double output_gain = 1.0;
    bool flat = true;
};

[[nodiscard]] bool is_graphic_eq_sample_rate_supported(int sample_rate) noexcept;

[[nodiscard]] std::optional<GraphicEqProgram> prepare_graphic_eq(
    const GraphicEqSettings& settings,
    int sample_rate,
    std::uint64_t version) noexcept;

[[nodiscard]] double graphic_eq_response_db(
    const GraphicEqProgram& program,
    double frequency_hz) noexcept;

class GraphicEqualizerProcessor {
public:
    GraphicEqualizerProcessor() noexcept;
    ~GraphicEqualizerProcessor();

    GraphicEqualizerProcessor(const GraphicEqualizerProcessor&) = delete;
    GraphicEqualizerProcessor& operator=(const GraphicEqualizerProcessor&) = delete;

    [[nodiscard]] bool submit(const GraphicEqProgram& program) noexcept;
    void reset() noexcept;
    void process(float* interleaved,
                 std::size_t frame_count,
                 std::size_t channel_count) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agplayer

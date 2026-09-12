#include "graphic_equalizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <atomic>
#include <utility>

namespace agplayer {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kMinGainDb = -18.0;
constexpr double kMaxGainDb = 18.0;
constexpr double kProtectionMarginDb = 0.5;
constexpr std::size_t kProtectionBins = 4'096;
constexpr std::array<int, 5> kSupportedSampleRates{
    44'100, 48'000, 88'200, 96'000, 192'000};

bool finite_in_range(double value, double minimum, double maximum) noexcept
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

BiquadCoefficients make_peaking_filter(double frequency_hz,
                                       double gain_db,
                                       double q,
                                       int sample_rate) noexcept
{
    if (std::abs(gain_db) <= std::numeric_limits<double>::epsilon()) {
        return {};
    }

    const double omega = 2.0 * kPi * frequency_hz
                         / static_cast<double>(sample_rate);
    const double cosine = std::cos(omega);
    const double alpha = std::sin(omega) / (2.0 * q);
    const double amplitude = std::pow(10.0, gain_db / 40.0);

    const double a0 = 1.0 + alpha / amplitude;
    BiquadCoefficients coefficients;
    coefficients.b0 = (1.0 + alpha * amplitude) / a0;
    coefficients.b1 = (-2.0 * cosine) / a0;
    coefficients.b2 = (1.0 - alpha * amplitude) / a0;
    coefficients.a1 = (-2.0 * cosine) / a0;
    coefficients.a2 = (1.0 - alpha / amplitude) / a0;
    coefficients.active = true;
    return coefficients;
}

double filter_magnitude(const BiquadCoefficients& coefficients,
                        double omega) noexcept
{
    if (!coefficients.active) {
        return 1.0;
    }
    const std::complex<double> z1 = std::polar(1.0, -omega);
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> numerator = coefficients.b0
                                            + coefficients.b1 * z1
                                            + coefficients.b2 * z2;
    const std::complex<double> denominator = 1.0
                                              + coefficients.a1 * z1
                                              + coefficients.a2 * z2;
    const double denominator_magnitude = std::abs(denominator);
    if (!(denominator_magnitude > 0.0)
        || !std::isfinite(denominator_magnitude)) {
        return 0.0;
    }
    return std::abs(numerator) / denominator_magnitude;
}

double unprotected_response_db(const GraphicEqProgram& program,
                               double frequency_hz) noexcept
{
    if (!std::isfinite(frequency_hz) || frequency_hz <= 0.0
        || program.sample_rate <= 0) {
        return -std::numeric_limits<double>::infinity();
    }
    const double nyquist = static_cast<double>(program.sample_rate) * 0.5;
    if (frequency_hz >= nyquist) {
        return -std::numeric_limits<double>::infinity();
    }

    const double omega = 2.0 * kPi * frequency_hz
                         / static_cast<double>(program.sample_rate);
    double magnitude = 1.0;
    for (const auto& band : program.bands) {
        magnitude *= filter_magnitude(band, omega);
    }
    if (!(magnitude > 0.0) || !std::isfinite(magnitude)) {
        return -std::numeric_limits<double>::infinity();
    }
    return program.settings.preamp_db + 20.0 * std::log10(magnitude);
}

double maximum_audible_response_db(const GraphicEqProgram& program) noexcept
{
    const double maximum_frequency = std::min(
        20'000.0, static_cast<double>(program.sample_rate) * 0.475);
    if (maximum_frequency <= 20.0) {
        return 0.0;
    }
    const double ratio = maximum_frequency / 20.0;
    double maximum = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < kProtectionBins; ++index) {
        const double position = static_cast<double>(index)
                                / static_cast<double>(kProtectionBins - 1);
        const double frequency = 20.0 * std::pow(ratio, position);
        maximum = std::max(maximum,
                           unprotected_response_db(program, frequency));
    }
    return std::isfinite(maximum) ? maximum : 0.0;
}

} // namespace

bool is_graphic_eq_sample_rate_supported(int sample_rate) noexcept
{
    return std::find(kSupportedSampleRates.begin(),
                     kSupportedSampleRates.end(), sample_rate)
           != kSupportedSampleRates.end();
}

std::optional<GraphicEqProgram> prepare_graphic_eq(
    const GraphicEqSettings& settings,
    int sample_rate,
    std::uint64_t version) noexcept
{
    if (!is_graphic_eq_sample_rate_supported(sample_rate)
        || !finite_in_range(settings.preamp_db, kMinGainDb, kMaxGainDb)
        || !finite_in_range(settings.q, 0.1, 20.0)
        || !finite_in_range(settings.transition_ms, 1.0, 1'000.0)) {
        return std::nullopt;
    }
    for (const double gain : settings.band_gain_db) {
        if (!finite_in_range(gain, kMinGainDb, kMaxGainDb)) {
            return std::nullopt;
        }
    }

    GraphicEqProgram program;
    program.settings = settings;
    program.sample_rate = sample_rate;
    program.version = version;
    program.flat = std::abs(settings.preamp_db) <= 1e-12;
    for (std::size_t index = 0; index < kGraphicEqBandCount; ++index) {
        program.bands[index] = make_peaking_filter(
            kGraphicEqBandFrequenciesHz[index],
            settings.band_gain_db[index], settings.q, sample_rate);
        program.flat = program.flat && !program.bands[index].active;
    }

    if (settings.auto_clip_protection && !program.flat) {
        const double peak_db = maximum_audible_response_db(program);
        if (peak_db > 0.0) {
            program.protection_db = -(peak_db + kProtectionMarginDb);
        }
    }
    program.output_gain = std::pow(
        10.0, (settings.preamp_db + program.protection_db) / 20.0);
    return program;
}

double graphic_eq_response_db(const GraphicEqProgram& program,
                              double frequency_hz) noexcept
{
    return unprotected_response_db(program, frequency_hz);
}

struct GraphicEqualizerProcessor::Impl {
    static constexpr std::size_t kMailboxCapacity = 3;

    enum class SlotState : std::uint8_t {
        Free,
        Writing,
        Ready,
        Reading
    };

    struct MailboxSlot {
        GraphicEqProgram program{};
        std::atomic<SlotState> state{SlotState::Free};
    };

    struct FilterState {
        double z1 = 0.0;
        double z2 = 0.0;
    };

    struct FilterBank {
        std::array<std::array<FilterState, kGraphicEqBandCount>,
                   kGraphicEqMaxChannels> states{};

        void clear() noexcept
        {
            for (auto& channel : states) {
                for (auto& state : channel) {
                    state = {};
                }
            }
        }
    };

    std::array<MailboxSlot, kMailboxCapacity> mailbox{};
    std::atomic<std::uint64_t> reset_generation{0};
    std::uint64_t consumed_reset_generation = 0;

    std::array<GraphicEqProgram, 2> programs{};
    std::array<FilterBank, 2> banks{};
    std::size_t active_bank = 0;
    std::size_t transition_bank = 1;
    bool has_program = false;
    bool transitioning = false;
    std::size_t transition_samples_total = 1;
    std::size_t transition_samples_remaining = 0;
    double wet_mix = 1.0;
    double wet_target = 1.0;
    double wet_step = 1.0;

    bool push(const GraphicEqProgram& program) noexcept
    {
        for (const SlotState candidate : {SlotState::Free, SlotState::Ready}) {
            for (MailboxSlot& slot : mailbox) {
                SlotState expected = candidate;
                if (!slot.state.compare_exchange_strong(
                        expected, SlotState::Writing,
                        std::memory_order_acquire,
                        std::memory_order_relaxed)) {
                    continue;
                }
                slot.program = program;
                slot.state.store(SlotState::Ready, std::memory_order_release);
                return true;
            }
        }
        return false;
    }

    bool take_latest(GraphicEqProgram& latest) noexcept
    {
        bool found = false;
        for (MailboxSlot& slot : mailbox) {
            SlotState expected = SlotState::Ready;
            if (!slot.state.compare_exchange_strong(
                    expected, SlotState::Reading,
                    std::memory_order_acquire,
                    std::memory_order_relaxed)) {
                continue;
            }
            const GraphicEqProgram candidate = slot.program;
            if (!found || candidate.version >= latest.version) {
                latest = candidate;
                found = true;
            }
            slot.state.store(SlotState::Free, std::memory_order_release);
        }
        return found;
    }

    static double process_filter(double input,
                                 const BiquadCoefficients& coefficients,
                                 FilterState& state) noexcept
    {
        if (!coefficients.active) {
            return input;
        }
        const double output = coefficients.b0 * input + state.z1;
        state.z1 = coefficients.b1 * input
                   - coefficients.a1 * output + state.z2;
        state.z2 = coefficients.b2 * input
                   - coefficients.a2 * output;
        if (!std::isfinite(output) || !std::isfinite(state.z1)
            || !std::isfinite(state.z2)) {
            state = {};
            return 0.0;
        }
        return output;
    }

    double process_wet(std::size_t bank_index,
                       const GraphicEqProgram& program,
                       double input,
                       std::size_t channel) noexcept
    {
        double output = input;
        if (!program.flat) {
            for (std::size_t band = 0; band < kGraphicEqBandCount; ++band) {
                output = process_filter(output, program.bands[band],
                                        banks[bank_index].states[channel][band]);
            }
        }
        output *= program.output_gain;
        return std::isfinite(output) ? output : 0.0;
    }

    void clear_processing_state() noexcept
    {
        for (auto& bank : banks) {
            bank.clear();
        }
        transitioning = false;
        transition_samples_remaining = 0;
    }

    void begin_program(const GraphicEqProgram& program) noexcept
    {
        const double requested_wet =
            program.settings.enabled && !program.settings.bypassed ? 1.0 : 0.0;
        const std::size_t ramp_samples = static_cast<std::size_t>(std::max(
            1.0, program.settings.transition_ms
                     * static_cast<double>(program.sample_rate) / 1'000.0));
        wet_target = requested_wet;
        wet_step = 1.0 / static_cast<double>(ramp_samples);

        if (!has_program) {
            programs[active_bank] = program;
            banks[active_bank].clear();
            has_program = true;
            wet_mix = requested_wet;
            return;
        }

        transition_bank = 1 - active_bank;
        programs[transition_bank] = program;
        banks[transition_bank].clear();
        transition_samples_total = ramp_samples;
        transition_samples_remaining = ramp_samples;
        transitioning = true;
    }

    void consume_commands() noexcept
    {
        const std::uint64_t requested_reset =
            reset_generation.load(std::memory_order_acquire);
        if (requested_reset != consumed_reset_generation) {
            clear_processing_state();
            consumed_reset_generation = requested_reset;
        }

        GraphicEqProgram latest;
        if (take_latest(latest)) {
            if (has_program && latest.sample_rate
                                   != programs[active_bank].sample_rate) {
                clear_processing_state();
            }
            begin_program(latest);
        }
    }

    double update_wet_mix() noexcept
    {
        if (wet_mix < wet_target) {
            wet_mix = std::min(wet_target, wet_mix + wet_step);
        } else if (wet_mix > wet_target) {
            wet_mix = std::max(wet_target, wet_mix - wet_step);
        }
        return wet_mix;
    }

    double blend_programs(double old_wet,
                          double new_wet) noexcept
    {
        if (!transitioning || transition_samples_total == 0) {
            return old_wet;
        }
        const double position = 1.0
            - static_cast<double>(transition_samples_remaining)
                  / static_cast<double>(transition_samples_total);
        const double old_gain = std::cos(position * kPi * 0.5);
        const double new_gain = std::sin(position * kPi * 0.5);
        return old_wet * old_gain + new_wet * new_gain;
    }

    void advance_transition() noexcept
    {
        if (!transitioning || transition_samples_remaining == 0) {
            return;
        }
        --transition_samples_remaining;
        if (transition_samples_remaining == 0) {
            active_bank = transition_bank;
            transitioning = false;
            banks[1 - active_bank].clear();
        }
    }

    void process(float* interleaved,
                 std::size_t frame_count,
                 std::size_t channel_count) noexcept
    {
        if (interleaved == nullptr || frame_count == 0 || channel_count == 0) {
            return;
        }
        consume_commands();
        if (!has_program || channel_count > kGraphicEqMaxChannels) {
            const std::size_t sample_count = frame_count * channel_count;
            for (std::size_t index = 0; index < sample_count; ++index) {
                if (!std::isfinite(interleaved[index])) {
                    interleaved[index] = 0.0F;
                }
            }
            return;
        }

        for (std::size_t frame = 0; frame < frame_count; ++frame) {
            const double mix = update_wet_mix();
            for (std::size_t channel = 0; channel < channel_count; ++channel) {
                const std::size_t index = frame * channel_count + channel;
                const double dry = std::isfinite(interleaved[index])
                                       ? static_cast<double>(interleaved[index])
                                       : 0.0;
                const double old_wet = process_wet(
                    active_bank, programs[active_bank], dry, channel);
                double wet = old_wet;
                if (transitioning) {
                    const double new_wet = process_wet(
                        transition_bank, programs[transition_bank], dry, channel);
                    wet = blend_programs(old_wet, new_wet);
                }
                const double output = dry + (wet - dry) * mix;
                interleaved[index] = static_cast<float>(
                    std::isfinite(output) ? output : 0.0);
            }
            advance_transition();
        }
    }
};

GraphicEqualizerProcessor::GraphicEqualizerProcessor() noexcept
    : impl_(std::make_unique<Impl>())
{
}

GraphicEqualizerProcessor::~GraphicEqualizerProcessor() = default;

bool GraphicEqualizerProcessor::submit(const GraphicEqProgram& program) noexcept
{
    return impl_->push(program);
}

void GraphicEqualizerProcessor::reset() noexcept
{
    impl_->reset_generation.fetch_add(1, std::memory_order_release);
}

void GraphicEqualizerProcessor::process(float* interleaved,
                                        std::size_t frame_count,
                                        std::size_t channel_count) noexcept
{
    impl_->process(interleaved, frame_count, channel_count);
}

} // namespace agplayer

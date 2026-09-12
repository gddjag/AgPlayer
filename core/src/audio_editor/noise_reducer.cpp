#include "noise_reducer.hpp"

#include "document_renderer.hpp"
#include "../decoder.hpp"

#include <agplayer/c_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <fstream>
#include <limits>
#include <numeric>
#include <vector>

namespace agplayer::editor {
namespace {

constexpr std::size_t fft_size = 2'048;
constexpr std::size_t hop_size = fft_size / 4;
constexpr float pi = 3.14159265358979323846F;

void fft(std::array<std::complex<float>, fft_size>& values,
         const bool inverse) noexcept
{
    for (std::size_t index = 1, reversed = 0; index < fft_size; ++index) {
        std::size_t bit = fft_size >> 1U;
        while ((reversed & bit) != 0U) {
            reversed ^= bit;
            bit >>= 1U;
        }
        reversed ^= bit;
        if (index < reversed) std::swap(values[index], values[reversed]);
    }
    for (std::size_t length = 2; length <= fft_size; length <<= 1U) {
        const float angle = (inverse ? 2.0F : -2.0F) * pi
            / static_cast<float>(length);
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (std::size_t offset = 0; offset < fft_size; offset += length) {
            std::complex<float> phase(1.0F, 0.0F);
            const std::size_t half = length / 2U;
            for (std::size_t index = 0; index < half; ++index) {
                const auto even = values[offset + index];
                const auto odd = values[offset + index + half] * phase;
                values[offset + index] = even + odd;
                values[offset + index + half] = even - odd;
                phase *= step;
            }
        }
    }
    if (inverse) {
        for (auto& value : values) value /= static_cast<float>(fft_size);
    }
}

std::array<float, fft_size> hann_window()
{
    std::array<float, fft_size> window{};
    for (std::size_t index = 0; index < fft_size; ++index) {
        window[index] = 0.5F - 0.5F * std::cos(
            2.0F * pi * static_cast<float>(index)
            / static_cast<float>(fft_size - 1));
    }
    return window;
}

void write_u16(std::ostream& stream, const std::uint16_t value)
{
    stream.put(static_cast<char>(value & 0xFFU));
    stream.put(static_cast<char>((value >> 8U) & 0xFFU));
}

void write_u32(std::ostream& stream, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8) {
        stream.put(static_cast<char>((value >> shift) & 0xFFU));
    }
}

bool write_float_wav(const std::filesystem::path& path,
                     const std::vector<float>& samples,
                     const std::uint32_t sample_rate,
                     const std::uint16_t channels)
{
    const std::uint64_t bytes = samples.size() * sizeof(float);
    if (bytes > std::numeric_limits<std::uint32_t>::max()) return false;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write("RIFF", 4);
    write_u32(output, 36U + static_cast<std::uint32_t>(bytes));
    output.write("WAVEfmt ", 8);
    write_u32(output, 16U);
    write_u16(output, 3U);
    write_u16(output, channels);
    write_u32(output, sample_rate);
    write_u32(output, sample_rate * channels * sizeof(float));
    write_u16(output, static_cast<std::uint16_t>(channels * sizeof(float)));
    write_u16(output, 32U);
    output.write("data", 4);
    write_u32(output, static_cast<std::uint32_t>(bytes));
    output.write(reinterpret_cast<const char*>(samples.data()),
                 static_cast<std::streamsize>(bytes));
    output.flush();
    return output.good();
}

bool cancelled(const std::atomic_bool* value) noexcept
{
    return value != nullptr && value->load(std::memory_order_relaxed);
}

} // namespace

NoiseReductionResult NoiseReducer::reduce(
    const TimelineSnapshot& snapshot, const std::optional<Selection>& range,
    const std::filesystem::path& output_path,
    const std::atomic_bool* cancel, std::function<void(float)> progress)
{
    const std::filesystem::path rendered = std::filesystem::path(
        output_path.native() + std::filesystem::path(L".render.wav").native());
    DocumentRenderer renderer;
    const RenderResult render = renderer.renderFloatWav(
        snapshot, range, rendered, cancel,
        progress ? [progress](const float value) { progress(value * 0.2F); }
                 : std::function<void(float)>{});
    if (!render.success) return {false, render.message, {}};
    const auto cleanup = [&rendered] {
        std::error_code ignored;
        std::filesystem::remove(rendered, ignored);
    };
    constexpr std::uint64_t max_working_bytes = 512ULL * 1024ULL * 1024ULL;
    const std::uint64_t samples = static_cast<std::uint64_t>(render.frames)
        * static_cast<std::uint64_t>(render.channels);
    if (render.channels == 0
        || samples > max_working_bytes / (3ULL * sizeof(float))) {
        cleanup();
        return {false, "selection is too long for lightweight noise reduction", {}};
    }

    agplayer::Decoder decoder;
    if (decoder.open(rendered.u8string(), static_cast<int>(render.sample_rate),
                     static_cast<int>(render.channels)) != AG_OK) {
        cleanup();
        return {false, "cannot decode rendered audio", {}};
    }
    std::vector<float> input;
    agplayer::DecodedAudioBlock block;
    do {
        if (cancelled(cancel)) {
            cleanup();
            return {false, "cancelled", {}};
        }
        if (decoder.read(block) != AG_OK) {
            cleanup();
            return {false, "cannot read rendered audio", {}};
        }
        input.insert(input.end(), block.samples.begin(), block.samples.end());
    } while (!block.end_of_stream);
    cleanup();
    if (input.empty() || render.channels == 0) {
        return {false, "rendered audio is empty", {}};
    }

    const std::size_t channels = render.channels;
    const std::size_t frames = input.size() / channels;
    const std::size_t windows = (frames + hop_size - 1) / hop_size;
    const auto window = hann_window();
    std::vector<float> output(input.size(), 0.0F);
    std::vector<float> weights(frames, 0.0F);
    std::array<std::complex<float>, fft_size> spectrum{};

    for (std::size_t channel = 0; channel < channels; ++channel) {
        std::vector<std::pair<float, std::size_t>> energies;
        energies.reserve(windows);
        for (std::size_t window_index = 0; window_index < windows; ++window_index) {
            if (cancelled(cancel)) {
                std::error_code ignored;
                std::filesystem::remove(output_path, ignored);
                return {false, "cancelled", {}};
            }
            float energy = 0.0F;
            const std::size_t offset = window_index * hop_size;
            for (std::size_t index = 0; index < fft_size; ++index) {
                const float sample = offset + index < frames
                    ? input[(offset + index) * channels + channel] : 0.0F;
                energy += sample * sample;
            }
            energies.emplace_back(energy, window_index);
        }
        std::sort(energies.begin(), energies.end());
        const std::size_t noise_windows = std::clamp<std::size_t>(
            windows / 10U, 1U, 20U);
        std::array<float, fft_size> noise{};
        for (std::size_t rank = 0; rank < noise_windows; ++rank) {
            const std::size_t offset = energies[rank].second * hop_size;
            for (std::size_t index = 0; index < fft_size; ++index) {
                const float sample = offset + index < frames
                    ? input[(offset + index) * channels + channel] : 0.0F;
                spectrum[index] = sample * window[index];
            }
            fft(spectrum, false);
            for (std::size_t bin = 0; bin < fft_size; ++bin) {
                noise[bin] += std::abs(spectrum[bin])
                    / static_cast<float>(noise_windows);
            }
        }

        for (std::size_t window_index = 0; window_index < windows; ++window_index) {
            if (cancelled(cancel)) {
                std::error_code ignored;
                std::filesystem::remove(output_path, ignored);
                return {false, "cancelled", {}};
            }
            const std::size_t offset = window_index * hop_size;
            for (std::size_t index = 0; index < fft_size; ++index) {
                const float sample = offset + index < frames
                    ? input[(offset + index) * channels + channel] : 0.0F;
                spectrum[index] = sample * window[index];
            }
            fft(spectrum, false);
            for (std::size_t bin = 0; bin < fft_size; ++bin) {
                const float magnitude = std::abs(spectrum[bin]);
                const float ratio = noise[bin] / (magnitude + 1.0e-9F);
                const float gain = std::sqrt(std::clamp(
                    1.0F - 1.25F * ratio, 0.08F, 1.0F));
                spectrum[bin] *= gain;
            }
            fft(spectrum, true);
            for (std::size_t index = 0; index < fft_size
                 && offset + index < frames; ++index) {
                const float weight = window[index] * window[index];
                output[(offset + index) * channels + channel]
                    += spectrum[index].real() * window[index];
                if (channel == 0) weights[offset + index] += weight;
            }
            if (progress) progress(0.2F + 0.75F
                * static_cast<float>(channel * windows + window_index + 1)
                / static_cast<float>(channels * windows));
        }
    }
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float weight = (std::max)(weights[frame], 1.0e-6F);
        for (std::size_t channel = 0; channel < channels; ++channel) {
            output[frame * channels + channel] = std::clamp(
                output[frame * channels + channel] / weight, -1.0F, 1.0F);
        }
    }
    if (!write_float_wav(output_path, output, render.sample_rate,
                         static_cast<std::uint16_t>(render.channels))) {
        return {false, "cannot write noise-reduced audio", {}};
    }
    if (progress) progress(1.0F);
    return {true, {}, output_path, render.sample_rate, render.channels,
            static_cast<SampleFrame>(frames)};
}

} // namespace agplayer::editor

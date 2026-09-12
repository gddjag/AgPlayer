#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>

namespace {

void write_u16(std::ofstream& output, const std::uint16_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
    };
    output.write(bytes, sizeof(bytes));
}

void write_u32(std::ofstream& output, const std::uint32_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
        static_cast<char>((value >> 16U) & 0xFFU),
        static_cast<char>((value >> 24U) & 0xFFU),
    };
    output.write(bytes, sizeof(bytes));
}

} // namespace

int main(const int argc, char** argv)
{
    if (argc != 2 && argc != 4 && argc != 6) {
        return 1;
    }

    std::uint32_t sample_rate = 44'100U;
    std::uint16_t channels = 2U;
    constexpr std::uint16_t bits_per_sample = 16U;
    std::uint32_t start_frame = 0U;
    std::uint32_t frame_count = sample_rate * 2U;
    if (argc >= 4) {
        try {
            const unsigned long parsed_start = std::stoul(argv[2]);
            const unsigned long parsed_count = std::stoul(argv[3]);
            if (parsed_start > std::numeric_limits<std::uint32_t>::max()
                || parsed_count == 0U
                || parsed_count > std::numeric_limits<std::uint32_t>::max()) {
                return 1;
            }
            start_frame = static_cast<std::uint32_t>(parsed_start);
            frame_count = static_cast<std::uint32_t>(parsed_count);
        } catch (...) {
            return 1;
        }
    }
    if (argc == 6) {
        try {
            const unsigned long parsed_rate = std::stoul(argv[4]);
            const unsigned long parsed_channels = std::stoul(argv[5]);
            if (parsed_rate == 0U
                || parsed_rate > std::numeric_limits<std::uint32_t>::max()
                || parsed_channels == 0U
                || parsed_channels > std::numeric_limits<std::uint16_t>::max()) {
                return 1;
            }
            sample_rate = static_cast<std::uint32_t>(parsed_rate);
            channels = static_cast<std::uint16_t>(parsed_channels);
        } catch (...) {
            return 1;
        }
    }
    const std::uint32_t bytes_per_frame =
        static_cast<std::uint32_t>(channels) * (bits_per_sample / 8U);
    const std::uint64_t data_size_wide =
        static_cast<std::uint64_t>(frame_count) * bytes_per_frame;
    if (data_size_wide > std::numeric_limits<std::uint32_t>::max()) {
        return 1;
    }
    const std::uint32_t data_size = static_cast<std::uint32_t>(data_size_wide);
    constexpr double amplitude = 0.251188643150958;
    constexpr double frequency = 440.0;
    const double pi = std::acos(-1.0);

    std::ofstream output(argv[1], std::ios::binary);
    if (!output) {
        return 2;
    }

    output.write("RIFF", 4);
    write_u32(output, 36U + data_size);
    output.write("WAVE", 4);
    output.write("fmt ", 4);
    write_u32(output, 16U);
    write_u16(output, 1U);
    write_u16(output, channels);
    write_u32(output, sample_rate);
    write_u32(output, sample_rate * bytes_per_frame);
    write_u16(output, static_cast<std::uint16_t>(bytes_per_frame));
    write_u16(output, bits_per_sample);
    output.write("data", 4);
    write_u32(output, data_size);

    for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
        const double phase = 2.0 * pi * frequency
                             * static_cast<double>(start_frame + frame)
                             / static_cast<double>(sample_rate);
        const auto sample = static_cast<std::int16_t>(
            std::lround(std::sin(phase) * amplitude * 32'767.0));
        for (std::uint16_t channel = 0U; channel < channels; ++channel) {
            write_u16(output, static_cast<std::uint16_t>(sample));
        }
    }

    output.flush();
    output.close();
    return output ? 0 : 3;
}

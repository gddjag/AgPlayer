#include <cmath>
#include <cstdint>
#include <fstream>

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
    if (argc != 2) {
        return 1;
    }

    constexpr std::uint32_t sample_rate = 44'100U;
    constexpr std::uint16_t channels = 2U;
    constexpr std::uint16_t bits_per_sample = 16U;
    constexpr std::uint32_t duration_seconds = 2U;
    constexpr std::uint32_t frame_count = sample_rate * duration_seconds;
    constexpr std::uint32_t bytes_per_frame = channels * (bits_per_sample / 8U);
    constexpr std::uint32_t data_size = frame_count * bytes_per_frame;
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
        const double phase = 2.0 * pi * frequency * static_cast<double>(frame)
                             / static_cast<double>(sample_rate);
        const auto sample = static_cast<std::int16_t>(
            std::lround(std::sin(phase) * amplitude * 32'767.0));
        write_u16(output, static_cast<std::uint16_t>(sample));
        write_u16(output, static_cast<std::uint16_t>(sample));
    }

    return output ? 0 : 3;
}

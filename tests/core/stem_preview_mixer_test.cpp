// Test files deliberately keep assert-like checks active in Release builds.
#undef NDEBUG

#include "stem_preview_mixer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <crtdbg.h>
#endif

#define AG_CHECK(expr)                                                        \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::fprintf(stderr, "AG_CHECK failed at %s:%d: %s\n",            \
                         __FILE__, __LINE__, #expr);                          \
            std::exit(1);                                                     \
        }                                                                     \
    } while (0)

namespace {

constexpr std::uint32_t sample_rate = 44'100U;
constexpr std::uint16_t channel_count = 2U;
constexpr std::uint16_t bits_per_sample = 16U;
constexpr std::size_t fixture_frames = sample_rate / 10U;

class FixtureDirectory final {
public:
    FixtureDirectory()
    {
        const auto suffix = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path_ = std::filesystem::temp_directory_path()
                / ("agplayer-stem-preview-mixer-"
                   + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }

    ~FixtureDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    FixtureDirectory(const FixtureDirectory&) = delete;
    FixtureDirectory& operator=(const FixtureDirectory&) = delete;

    [[nodiscard]] std::filesystem::path file(const std::string& name) const
    {
        return path_ / name;
    }

private:
    std::filesystem::path path_;
};

void write_u16(std::ofstream& stream, const std::uint16_t value)
{
    const std::array<char, 2U> bytes{
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
    };
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_u32(std::ofstream& stream, const std::uint32_t value)
{
    const std::array<char, 4U> bytes{
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU),
        static_cast<char>((value >> 24U) & 0xffU),
    };
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_constant_pcm_wav(const std::filesystem::path& path,
                            const float sample,
                            const std::size_t frame_count = fixture_frames,
                            const std::uint16_t output_channels = channel_count)
{
    const float bounded = std::clamp(sample, -1.0F, 1.0F);
    const auto pcm_sample = static_cast<std::int16_t>(
        std::lround(bounded * 32'767.0F));
    const std::uint32_t bytes_per_frame =
        output_channels * (bits_per_sample / 8U);
    const std::uint32_t data_size = static_cast<std::uint32_t>(
        frame_count * bytes_per_frame);

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    AG_CHECK(stream.is_open());
    stream.write("RIFF", 4);
    write_u32(stream, 36U + data_size);
    stream.write("WAVE", 4);
    stream.write("fmt ", 4);
    write_u32(stream, 16U);
    write_u16(stream, 1U);
    write_u16(stream, output_channels);
    write_u32(stream, sample_rate);
    write_u32(stream, sample_rate * bytes_per_frame);
    write_u16(stream, static_cast<std::uint16_t>(bytes_per_frame));
    write_u16(stream, bits_per_sample);
    stream.write("data", 4);
    write_u32(stream, data_size);
    for (std::size_t frame = 0U; frame < frame_count; ++frame) {
        for (std::uint16_t channel = 0U; channel < output_channels; ++channel) {
            write_u16(stream, static_cast<std::uint16_t>(pcm_sample));
        }
    }
    AG_CHECK(stream.good());
}

std::vector<float> render_frames(agplayer::StemPreviewMixer& mixer,
                                 const std::size_t frame_count)
{
    std::vector<float> output(frame_count * channel_count, -9.0F);
    mixer.render(output.data(), frame_count);
    return output;
}

void check_all_samples_near(const std::vector<float>& samples,
                            const float expected,
                            const float tolerance)
{
    AG_CHECK(!samples.empty());
    for (const float sample : samples) {
        AG_CHECK(std::abs(sample - expected) <= tolerance);
    }
}

void two_sources_are_summed_sample_by_sample_with_per_stem_gain(
    const FixtureDirectory& fixtures)
{
    const auto first = fixtures.file("sum-a.wav");
    const auto second = fixtures.file("sum-b.wav");
    write_constant_pcm_wav(first, 0.25F);
    write_constant_pcm_wav(second, 0.50F);

    agplayer::StemPreviewMixer mixer(agplayer::AudioBackend::Manual, 4'096U);
    mixer.load({{"stem-a", first.string(), 1.0F},
                {"stem-b", second.string(), 1.0F}},
               0);
    mixer.set_gain("stem-b", 0.5F);
    mixer.play();

    const auto mixed = render_frames(mixer, 128U);
    check_all_samples_near(mixed, 0.50F, 0.002F);
}

void mixed_peaks_are_clamped_to_full_scale(
    const FixtureDirectory& fixtures)
{
    const auto first = fixtures.file("clip-a.wav");
    const auto second = fixtures.file("clip-b.wav");
    write_constant_pcm_wav(first, 0.80F);
    write_constant_pcm_wav(second, 0.80F);

    agplayer::StemPreviewMixer mixer(agplayer::AudioBackend::Manual, 4'096U);
    mixer.load({{"stem-a", first.string(), 1.0F},
                {"stem-b", second.string(), 1.0F}},
               0);
    mixer.play();

    const auto mixed = render_frames(mixer, 128U);
    check_all_samples_near(mixed, 1.0F, 0.002F);
    AG_CHECK(std::all_of(mixed.begin(), mixed.end(), [](const float sample) {
        return sample >= -1.0F && sample <= 1.0F;
    }));
}

void seek_moves_every_source_to_one_shared_position(
    const FixtureDirectory& fixtures)
{
    const auto first = fixtures.file("seek-a.wav");
    const auto second = fixtures.file("seek-b.wav");
    write_constant_pcm_wav(first, 0.20F);
    write_constant_pcm_wav(second, 0.30F);

    agplayer::StemPreviewMixer mixer(agplayer::AudioBackend::Manual, 4'096U);
    mixer.load({{"stem-a", first.string(), 1.0F},
                {"stem-b", second.string(), 1.0F}},
               20);
    AG_CHECK(mixer.snapshot().position_ms == 20);

    mixer.seek(60);
    AG_CHECK(mixer.snapshot().position_ms == 60);
    mixer.play();
    const auto mixed = render_frames(mixer, 441U);
    check_all_samples_near(mixed, 0.50F, 0.002F);

    const auto after_render = mixer.snapshot();
    AG_CHECK(after_render.position_ms >= 69);
    AG_CHECK(after_render.position_ms <= 70);
}

void pause_holds_the_shared_playhead_and_resume_continues_it(
    const FixtureDirectory& fixtures)
{
    const auto first = fixtures.file("pause-a.wav");
    const auto second = fixtures.file("pause-b.wav");
    write_constant_pcm_wav(first, 0.20F);
    write_constant_pcm_wav(second, 0.30F);

    agplayer::StemPreviewMixer mixer(agplayer::AudioBackend::Manual, 4'096U);
    mixer.load({{"stem-a", first.string(), 1.0F},
                {"stem-b", second.string(), 1.0F}},
               0);
    mixer.play();
    (void)render_frames(mixer, 441U);
    const auto playing = mixer.snapshot();
    AG_CHECK(playing.state == agplayer::EngineState::Playing);
    AG_CHECK(playing.position_ms >= 9);

    mixer.pause();
    const auto paused = mixer.snapshot();
    AG_CHECK(paused.state == agplayer::EngineState::Paused);
    const auto paused_output = render_frames(mixer, 441U);
    check_all_samples_near(paused_output, 0.0F, 0.0001F);
    AG_CHECK(mixer.snapshot().position_ms == paused.position_ms);

    mixer.play();
    (void)render_frames(mixer, 441U);
    const auto resumed = mixer.snapshot();
    AG_CHECK(resumed.state == agplayer::EngineState::Playing);
    AG_CHECK(resumed.position_ms >= paused.position_ms + 9);
}

void shorter_sources_are_silent_past_their_end(
    const FixtureDirectory& fixtures)
{
    const auto shortSource = fixtures.file("short.wav");
    const auto longSource = fixtures.file("long.wav");
    write_constant_pcm_wav(shortSource, 0.20F, sample_rate / 20U);
    write_constant_pcm_wav(longSource, 0.30F, sample_rate / 10U);

    agplayer::StemPreviewMixer mixer(agplayer::AudioBackend::Manual, 4'096U);
    AG_CHECK(mixer.load({{"short", shortSource.string(), 1.0F},
                         {"long", longSource.string(), 1.0F}},
                        80) == AG_OK);
    AG_CHECK(mixer.play() == AG_OK);
    const auto mixed = render_frames(mixer, 441U);
    check_all_samples_near(mixed, 0.30F, 0.002F);
    AG_CHECK(mixer.snapshot().position_ms >= 89);
}

void unicode_paths_load_through_the_utf8_core_contract(
    const FixtureDirectory& fixtures)
{
    const auto source = fixtures.file(
        std::string(u8"\u4eba\u58f0-\u4f34\u594f.wav"));
    write_constant_pcm_wav(source, 0.25F);

    agplayer::StemPreviewMixer mixer(agplayer::AudioBackend::Manual, 4'096U);
    AG_CHECK(mixer.load({{"unicode", source.u8string(), 1.0F}}, 0) == AG_OK);
    AG_CHECK(mixer.play() == AG_OK);
    check_all_samples_near(render_frames(mixer, 128U), 0.25F, 0.002F);
}

void non_stereo_sources_are_rejected_before_rendering(
    const FixtureDirectory& fixtures)
{
    const auto mono = fixtures.file("mono.wav");
    const auto surround = fixtures.file("surround.wav");
    write_constant_pcm_wav(mono, 0.25F, fixture_frames, 1U);
    write_constant_pcm_wav(surround, 0.25F, fixture_frames, 6U);

    agplayer::StemPreviewMixer mixer(agplayer::AudioBackend::Manual, 4'096U);
    AG_CHECK(mixer.load({{"mono", mono.string(), 1.0F}}, 0)
             == AG_UNSUPPORTED_FORMAT);
    AG_CHECK(mixer.load({{"surround", surround.string(), 1.0F}}, 0)
             == AG_UNSUPPORTED_FORMAT);
}

void non_integral_millisecond_tail_reaches_end_of_stream(
    const FixtureDirectory& fixtures)
{
    constexpr std::size_t source_frames = 4'433U;
    constexpr std::size_t first_render_frames = 4'096U;
    constexpr std::size_t decoded_tail_frames =
        source_frames - first_render_frames;
    const auto source = fixtures.file("fractional-millisecond-tail.wav");
    write_constant_pcm_wav(source, 0.25F, source_frames);

    agplayer::StemPreviewMixer mixer(agplayer::AudioBackend::Manual, 4'096U);
    AG_CHECK(mixer.load({{"tail", source.string(), 1.0F}}, 0) == AG_OK);
    AG_CHECK(mixer.play() == AG_OK);
    check_all_samples_near(render_frames(mixer, first_render_frames),
                           0.25F, 0.002F);

    const auto tail = render_frames(mixer, 512U);
    for (std::size_t frame = 0U; frame < decoded_tail_frames; ++frame) {
        for (std::size_t channel = 0U; channel < channel_count; ++channel) {
            AG_CHECK(std::abs(tail[frame * channel_count + channel] - 0.25F)
                     <= 0.002F);
        }
    }
    for (std::size_t frame = decoded_tail_frames; frame < 512U; ++frame) {
        for (std::size_t channel = 0U; channel < channel_count; ++channel) {
            AG_CHECK(std::abs(tail[frame * channel_count + channel])
                     <= 0.0001F);
        }
    }
    AG_CHECK(mixer.snapshot().state == agplayer::EngineState::Stopped);
}

} // namespace

int main()
{
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    const FixtureDirectory fixtures;
    two_sources_are_summed_sample_by_sample_with_per_stem_gain(fixtures);
    mixed_peaks_are_clamped_to_full_scale(fixtures);
    seek_moves_every_source_to_one_shared_position(fixtures);
    pause_holds_the_shared_playhead_and_resume_continues_it(fixtures);
    shorter_sources_are_silent_past_their_end(fixtures);
    unicode_paths_load_through_the_utf8_core_contract(fixtures);
    non_stereo_sources_are_rejected_before_rendering(fixtures);
    non_integral_millisecond_tail_reaches_end_of_stream(fixtures);
}

// Test files deliberately keep assert() active even in Release builds: many
// test cases embed function calls with side effects inside assert() (e.g.
// ag_metadata_open), and silencing them under NDEBUG would leave dangling
// pointers that crash on cleanup. Undefine NDEBUG before <cassert> so the
// macro always evaluates its argument and aborts on failure.
#undef NDEBUG

#include <agplayer/c_api.h>

#include "decoder.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#if defined(_WIN32)
#include <process.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

bool create_fixture(const std::filesystem::path& generator,
                    const std::filesystem::path& output,
                    const char* mode)
{
    const std::string generator_name = generator.string();
    const std::string output_name = output.string();
#if defined(_WIN32)
    const char* arguments[] = {
        generator_name.c_str(), output_name.c_str(), mode, nullptr,
    };
    return _spawnv(_P_WAIT, generator_name.c_str(), arguments) == 0;
#else
    const pid_t child = fork();
    if (child < 0) return false;
    if (child == 0) {
        execl(generator_name.c_str(), generator_name.c_str(), output_name.c_str(),
              mode, nullptr);
        _exit(127);
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

void read_first_audio_block(agplayer::Decoder& decoder,
                            agplayer::DecodedAudioBlock& block)
{
    do {
        assert(decoder.read(block) == AG_OK);
    } while (block.frames == 0U && !block.end_of_stream);
    assert(block.frames > 0U);
}

} // namespace

int main(const int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path sine_path = argv[1];
    const std::filesystem::path test_executable = argv[0];
    const std::filesystem::path fixture_generator = test_executable.parent_path()
        / ("fixture_generator" + test_executable.extension().string());
    const std::filesystem::path stereo_path =
        sine_path.parent_path() / "decoder-stereo-independent.wav";
    const std::filesystem::path antiphase_path =
        sine_path.parent_path() / "decoder-stereo-antiphase.wav";
    const std::filesystem::path surround_path =
        sine_path.parent_path() / "decoder-surround-5.1.wav";
    const std::filesystem::path unknown_surround_path =
        sine_path.parent_path() / "decoder-surround-5.1-unknown.wav";
    const std::filesystem::path durationless_path =
        sine_path.parent_path() / "decoder-durationless.wav";
    assert(create_fixture(fixture_generator, stereo_path, "stereo-independent"));
    assert(create_fixture(fixture_generator, antiphase_path, "stereo-antiphase"));
    assert(create_fixture(fixture_generator, surround_path,
                          "surround-5.1-independent"));
    assert(create_fixture(fixture_generator, unknown_surround_path,
                          "surround-5.1-unknown"));
    assert(create_fixture(fixture_generator, durationless_path, "durationless"));

    const std::uint64_t open_count_before = agplayer::Decoder::threadOpenCount();
    agplayer::Decoder counted_decoder;
    assert(counted_decoder.open(sine_path.string()) == AG_OK);
    assert(agplayer::Decoder::threadOpenCount() == open_count_before + 1U);
    assert(counted_decoder.open(sine_path.string()) == AG_OK);
    assert(agplayer::Decoder::threadOpenCount() == open_count_before + 2U);
    counted_decoder.close();

    ag_metadata* metadata = reinterpret_cast<ag_metadata*>(
        static_cast<std::uintptr_t>(1U));
    assert(ag_metadata_open(nullptr, &metadata) == AG_INVALID_ARGUMENT);
    assert(metadata == nullptr);
    assert(ag_metadata_open(argv[1], nullptr) == AG_INVALID_ARGUMENT);

    // The noexcept probe boundary must translate allocation failures into a
    // result code instead of terminating the process.
    agplayer::MediaMetadata failed_probe_metadata;
    failed_probe_metadata.title = "stale";
    agplayer::MediaMetadataProbeTestHooks probe_hooks;
    probe_hooks.throw_allocation_failure = true;
    assert(agplayer::probe_media_metadata(argv[1], failed_probe_metadata,
                                          &probe_hooks)
           == AG_INTERNAL_ERROR);
    assert(failed_probe_metadata.title.empty());

    assert(ag_metadata_open(argv[1], &metadata) == AG_OK);
    assert(metadata != nullptr);
    assert(ag_metadata_date(metadata) != nullptr);
    assert(ag_metadata_sample_rate(metadata) == 44'100);
    assert(ag_metadata_channels(metadata) == 2);
    assert(ag_metadata_bits_per_sample(metadata) == 16);
    assert(ag_metadata_duration_ms(metadata) >= 1'990);
    assert(std::strcmp(ag_metadata_format(metadata), "wav") == 0);
    std::size_t cover_size = 1U;
    const char* cover_mime_type = nullptr;
    assert(ag_metadata_cover(metadata, &cover_size, &cover_mime_type) == nullptr);
    assert(cover_size == 0U);
    assert(cover_mime_type != nullptr);
    assert(std::strcmp(cover_mime_type, "") == 0);
    ag_metadata_destroy(metadata);

    const std::filesystem::path missing_path =
        sine_path.parent_path() / "does-not-exist.wav";
    std::filesystem::remove(missing_path);
    metadata = nullptr;
    const std::string missing_filename = missing_path.string();
    assert(ag_metadata_open(missing_filename.c_str(), &metadata) == AG_IO_ERROR);
    assert(metadata == nullptr);

    agplayer::Decoder decoder;
    assert(!decoder.is_open());
    assert(decoder.open(sine_path.string()) == AG_OK);
    assert(decoder.is_open());
    agplayer::DecodedAudioBlock block;
    assert(decoder.read(block) == AG_OK);
    assert(block.frames > 0U);
    assert(block.samples.size() == block.frames * 2U);

    // Mutation caught: applying AnalysisMono to the parameter-free legacy open.
    agplayer::Decoder legacy_stereo_decoder;
    assert(legacy_stereo_decoder.open(stereo_path.string()) == AG_OK);
    assert(legacy_stereo_decoder.output_format().channels == 2);

    // Mutation caught: making the sample-rate/channel legacy overload select
    // the analysis-mono mode rather than preserving its explicit two channels.
    agplayer::Decoder legacy_resampled_decoder;
    assert(legacy_resampled_decoder.open(stereo_path.string(), 48'000, 2) == AG_OK);
    assert(legacy_resampled_decoder.output_format().sample_rate == 48'000);
    assert(legacy_resampled_decoder.output_format().channels == 2);
    read_first_audio_block(legacy_resampled_decoder, block);
    assert(std::abs(block.samples[0] - 0.25F) < 1.0e-4F);
    assert(std::abs(block.samples[1] - 0.50F) < 1.0e-4F);

    agplayer::DecoderOpenOptions analysis_options;
    analysis_options.output_sample_rate = 48'000;
    analysis_options.downmix = agplayer::DecoderDownmix::AnalysisMono;
    agplayer::DecoderOpenOptions matrix_options = analysis_options;
    matrix_options.output_sample_rate = 0;
    agplayer::Decoder analysis_stereo_decoder;
    assert(analysis_stereo_decoder.open(stereo_path.string(), analysis_options)
           == AG_OK);
    assert(analysis_stereo_decoder.output_format().channels == 1);
    assert(analysis_stereo_decoder.output_format().has_timeline);
    assert(analysis_stereo_decoder.output_format().timeline_frames == 96'000U);
    assert(analysis_stereo_decoder.output_format().timestamp_quantization_frames
           == 2U);
    assert(analysis_stereo_decoder.output_format().leading_padding_frames == 0U);
    read_first_audio_block(analysis_stereo_decoder, block);
    assert(block.samples.size() == block.frames);
    // Mutation caught: replacing the L2-normalized stereo matrix with a
    // sum, average, or a single-channel selection.
    assert(std::abs(block.samples.front() - 0.75F / std::sqrt(2.0F)) < 1.0e-4F);

    agplayer::Decoder antiphase_decoder;
    assert(antiphase_decoder.open(antiphase_path.string(), matrix_options)
           == AG_OK);
    read_first_audio_block(antiphase_decoder, block);
    // Mutation caught: losing the signed contribution of one stereo role.
    assert(std::abs(block.samples.front()) < 1.0e-4F);

    agplayer::Decoder surround_decoder;
    assert(surround_decoder.open(surround_path.string(), matrix_options) == AG_OK);
    read_first_audio_block(surround_decoder, block);
    assert(block.samples.size() == block.frames);
    // Mutation caught: changing any FL/FR/FC/LFE/BL/BR role weight or omitting
    // the known-layout L2 normalization.
    assert(std::abs(block.samples.front() - 0.776877F) < 1.0e-4F);
    for (const float sample : block.samples) {
        assert(std::isfinite(sample));
        assert(sample >= -1.0F && sample <= 1.0F);
    }

    agplayer::Decoder unknown_surround_decoder;
    assert(unknown_surround_decoder.open(unknown_surround_path.string(),
                                         matrix_options)
           == AG_OK);
    read_first_audio_block(unknown_surround_decoder, block);
    // Mutation caught: treating a missing channel mask as known 5.1 roles
    // instead of applying the unknown-layout equal-energy fallback.
    assert(std::abs(block.samples.front() - 0.857299F) < 1.0e-4F);

    agplayer::Decoder durationless_decoder;
    assert(durationless_decoder.open(durationless_path.string(), analysis_options)
           == AG_OK);
    // Mutation caught: inventing a time axis for an absent/non-positive stream duration.
    assert(!durationless_decoder.output_format().has_timeline);
    assert(durationless_decoder.output_format().timeline_frames == 0U);

    agplayer::Decoder unsupported_analysis_decoder;
    agplayer::DecoderOpenOptions invalid_analysis_options = analysis_options;
    invalid_analysis_options.output_sample_rate = std::numeric_limits<int>::max();
    // Mutation caught: mapping FFmpeg's deterministic resampler allocation
    // failure to unsupported/decode instead of AG_INTERNAL_ERROR.
    assert(unsupported_analysis_decoder.open(stereo_path.string(),
                                             invalid_analysis_options)
           == AG_INTERNAL_ERROR);
    legacy_stereo_decoder.close();
    legacy_resampled_decoder.close();
    analysis_stereo_decoder.close();
    antiphase_decoder.close();
    surround_decoder.close();
    unknown_surround_decoder.close();
    durationless_decoder.close();

    constexpr std::int64_t seek_target_ms = 1'517;
    assert(decoder.seek(seek_target_ms) == AG_OK);
    do {
        assert(decoder.read(block) == AG_OK);
    } while (block.frames == 0U && !block.end_of_stream);
    assert(block.frames > 0U);
    assert(block.timestamp_ms >= seek_target_ms);
    assert(block.timestamp_ms <= seek_target_ms + 1);
    constexpr std::int64_t sample_rate = 44'100;
    const std::int64_t expected_frame =
        (seek_target_ms * sample_rate + 999) / 1'000;
    const double pi = std::acos(-1.0);
    const double phase = 2.0 * pi * 440.0 * static_cast<double>(expected_frame)
                         / static_cast<double>(sample_rate);
    const auto expected_pcm = static_cast<std::int16_t>(
        std::lround(std::sin(phase) * 0.251188643150958 * 32'767.0));
    const float expected_sample = static_cast<float>(expected_pcm) / 32'768.0F;
    assert(std::abs(block.samples[0] - expected_sample) < 0.000'1F);
    assert(std::abs(block.samples[1] - expected_sample) < 0.000'1F);

    constexpr std::int64_t exact_seek_frame = 66'913;
    assert(decoder.seekFrame(exact_seek_frame) == AG_OK);
    do {
        assert(decoder.read(block) == AG_OK);
    } while (block.frames == 0U && !block.end_of_stream);
    assert(block.frames > 0U);
    const double exact_phase = 2.0 * pi * 440.0
        * static_cast<double>(exact_seek_frame)
        / static_cast<double>(sample_rate);
    const auto exact_pcm = static_cast<std::int16_t>(
        std::lround(std::sin(exact_phase) * 0.251188643150958 * 32'767.0));
    const float exact_sample = static_cast<float>(exact_pcm) / 32'768.0F;
    assert(std::abs(block.samples[0] - exact_sample) < 0.000'1F);
    assert(std::abs(block.samples[1] - exact_sample) < 0.000'1F);

    agplayer::Decoder failed_decoder;
    assert(failed_decoder.open(missing_filename) == AG_IO_ERROR);
    assert(!failed_decoder.is_open());

    const std::filesystem::path utf8_path =
        sine_path.parent_path()
        / std::filesystem::path(L"\u97F3\u9891-\u6D4B\u8BD5.wav");
    std::filesystem::copy_file(
        sine_path, utf8_path, std::filesystem::copy_options::overwrite_existing);
    metadata = nullptr;
    const std::string utf8_filename = utf8_path.u8string();
    assert(ag_metadata_open(utf8_filename.c_str(), &metadata) == AG_OK);
    assert(metadata != nullptr);
    assert(ag_metadata_sample_rate(metadata) == 44'100);
    ag_metadata_destroy(metadata);

    agplayer::Decoder full_decoder;
    assert(full_decoder.open(sine_path.string()) == AG_OK);
    std::size_t decoded_frames = 0U;
    do {
        assert(full_decoder.read(block) == AG_OK);
        decoded_frames += block.frames;
        for (const float sample : block.samples) {
            assert(std::isfinite(sample));
            assert(sample >= -1.0F && sample <= 1.0F);
        }
    } while (!block.end_of_stream);
    assert(decoded_frames == 88'200U);

    const std::filesystem::path empty_path = sine_path.parent_path() / "empty.bin";
    std::ofstream(empty_path, std::ios::binary).close();
    metadata = nullptr;
    assert(ag_metadata_open(empty_path.string().c_str(), &metadata) == AG_UNSUPPORTED_FORMAT);
    assert(metadata == nullptr);

    const std::filesystem::path truncated_path = sine_path.parent_path() / "truncated.wav";
    {
        std::ofstream truncated(truncated_path, std::ios::binary);
        truncated.write("RIFF\x24\0\0\0WAVEfmt ", 16);
    }
    metadata = nullptr;
    const ag_result first_result =
        ag_metadata_open(truncated_path.string().c_str(), &metadata);
    assert(first_result != AG_OK);
    assert(metadata == nullptr);
    assert(ag_metadata_open(truncated_path.string().c_str(), &metadata) == first_result);
    assert(metadata == nullptr);

    // Metadata probing must recover technical fields from a short FLAC's
    // STREAMINFO block without depending on decoder-open side effects.
    const std::filesystem::path short_flac =
        sine_path.parent_path() / "decoder-short-streaminfo.flac";
    std::filesystem::remove(short_flac);
    assert(ag_transcode(sine_path.u8string().c_str(),
                        short_flac.u8string().c_str(), "flac", 0, 44'100, 2,
                        nullptr, nullptr, nullptr)
           == AG_OK);
    metadata = nullptr;
    assert(ag_metadata_open(short_flac.u8string().c_str(), &metadata) == AG_OK);
    assert(metadata != nullptr);
    assert(ag_metadata_sample_rate(metadata) == 44'100);
    assert(ag_metadata_channels(metadata) == 2);
    assert(ag_metadata_duration_ms(metadata) >= 1'990);
    ag_metadata_destroy(metadata);

    // Raw ADTS has no container duration table. The bounded codec-free probe
    // must still recover sample rate, channels and a useful duration estimate.
    const std::filesystem::path raw_aac =
        sine_path.parent_path() / "decoder-codec-free.aac";
    std::filesystem::remove(raw_aac);
    assert(ag_transcode(sine_path.u8string().c_str(), raw_aac.u8string().c_str(),
                        "aac", 128'000, 44'100, 2,
                        nullptr, nullptr, nullptr)
           == AG_OK);
    metadata = nullptr;
    assert(ag_metadata_open(raw_aac.u8string().c_str(), &metadata) == AG_OK);
    assert(metadata != nullptr);
    assert(ag_metadata_sample_rate(metadata) == 44'100);
    assert(ag_metadata_channels(metadata) == 2);
    assert(ag_metadata_duration_ms(metadata) > 0);
    ag_metadata_destroy(metadata);

    std::filesystem::remove(short_flac);
    std::filesystem::remove(raw_aac);
    std::filesystem::remove(stereo_path);
    std::filesystem::remove(antiphase_path);
    std::filesystem::remove(surround_path);
    std::filesystem::remove(unknown_surround_path);
    std::filesystem::remove(durationless_path);
}

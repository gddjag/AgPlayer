#undef NDEBUG

#include "transcode_probe.hpp"
#include "transcode_verifier.hpp"
#include "transcoder.hpp"

#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::string read_all(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

void write_all(const std::filesystem::path& path, const std::string& contents)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    assert(output.good());
}

} // namespace

int main(const int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path input = argv[1];
    const std::filesystem::path directory = input.parent_path();

    agplayer::MediaProbe source_probe;
    std::string error;
    assert(agplayer::probe_transcode_input(input.u8string(), source_probe,
                                           error) == AG_OK);
    assert(!source_probe.audio_streams.empty());

    const std::filesystem::path output =
        directory / "verified-transcode.flac";
    std::filesystem::remove(output);

    agplayer::TranscodeConfig config;
    config.output_path = output.u8string();
    config.container_name = "flac";
    config.codec_name = "flac";
    config.sample_rate = 48000;
    config.sample_format = "s16";
    config.channel_layout = "mono";
    config.audio_stream_index = source_probe.audio_streams.front().stream_index;
    config.keep_metadata = true;

    int stage_count = 0;
    config.stage_callback = [&stage_count](const std::string_view) {
        ++stage_count;
    };
    assert(agplayer::transcode(input.u8string(), config, nullptr, nullptr,
                               error) == AG_OK);
    assert(stage_count >= 3);
    assert(std::filesystem::exists(output));

    const std::filesystem::path no_cover_output =
        directory / "no-cover-keep-cover.mp3";
    std::filesystem::remove(no_cover_output);
    agplayer::TranscodeConfig no_cover = config;
    no_cover.output_path = no_cover_output.u8string();
    no_cover.container_name = "mp3";
    no_cover.codec_name = "libmp3lame";
    no_cover.sample_format.clear();
    no_cover.keep_cover = true;
    error.clear();
    assert(agplayer::transcode(input.u8string(), no_cover, nullptr, nullptr,
                               error) == AG_OK);
    assert(std::filesystem::exists(no_cover_output));

    agplayer::MediaProbe output_probe;
    assert(agplayer::probe_transcode_input(output.u8string(), output_probe,
                                           error) == AG_OK);
    assert(output_probe.audio_streams.size() == 1);
    assert(output_probe.audio_streams.front().sample_rate == 48000);
    assert(output_probe.audio_streams.front().channel_layout == "mono");

    agplayer::TranscodeVerificationPlan plan;
    plan.expected_duration_ms =
        source_probe.audio_streams.front().duration_ms;
    plan.lossless = true;
    plan.expect_metadata = false;
    plan.expect_cover = false;
    plan.expected_audio_streams = 1;
    agplayer::TranscodeVerificationResult verification;
    assert(agplayer::verify_transcoded_output(output.u8string(), plan,
                                               verification, error) == AG_OK);
    assert(verification.decoded_samples > 0);
    assert(verification.decoded_duration_ms > 0);

    const std::filesystem::path truncated =
        directory / "truncated-transcode.flac";
    std::filesystem::copy_file(
        output, truncated, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::resize_file(truncated, 32);
    verification = {};
    error.clear();
    assert(agplayer::verify_transcoded_output(truncated.u8string(), plan,
                                               verification, error) != AG_OK);
    assert(!error.empty());

    const std::filesystem::path protected_output =
        directory / "protected-existing.flac";
    const std::string sentinel = "pre-existing-output";
    write_all(protected_output, sentinel);
    agplayer::TranscodeConfig rejected = config;
    rejected.output_path = protected_output.u8string();
    rejected.sample_format = "definitely_invalid";
    error.clear();
    assert(agplayer::transcode(input.u8string(), rejected, nullptr, nullptr,
                               error) != AG_OK);
    assert(read_all(protected_output) == sentinel);

    const std::filesystem::path cancelled_output =
        directory / "cancelled-transcode.flac";
    std::filesystem::remove(cancelled_output);
    std::atomic_bool cancelled{true};
    agplayer::TranscodeConfig cancelled_config = config;
    cancelled_config.output_path = cancelled_output.u8string();
    error.clear();
    assert(agplayer::transcode(input.u8string(), cancelled_config, &cancelled,
                               nullptr, error) == AG_CANCELLED);
    assert(!std::filesystem::exists(cancelled_output));

    std::filesystem::remove(cancelled_output);
    std::filesystem::remove(protected_output);
    std::filesystem::remove(truncated);
    std::filesystem::remove(output);
    std::filesystem::remove(no_cover_output);
    std::cout << "transcode verifier tests passed\n";
    return 0;
}

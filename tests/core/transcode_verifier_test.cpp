#undef NDEBUG

#include "transcode_probe.hpp"
#include "transcode_verifier.hpp"
#include "transcoder.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
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

std::uint16_t read_u16_le(const std::string& bytes, const std::size_t offset)
{
    assert(offset + 2U <= bytes.size());
    return static_cast<std::uint16_t>(
        static_cast<unsigned char>(bytes[offset])
        | (static_cast<unsigned int>(
               static_cast<unsigned char>(bytes[offset + 1U])) << 8U));
}

std::uint32_t read_u32_le(const std::string& bytes, const std::size_t offset)
{
    assert(offset + 4U <= bytes.size());
    std::uint32_t value = 0;
    for (unsigned int byte = 0; byte < 4U; ++byte) {
        value |= static_cast<std::uint32_t>(
                     static_cast<unsigned char>(bytes[offset + byte]))
                 << (byte * 8U);
    }
    return value;
}

void write_u32_le(std::string& bytes, const std::size_t offset,
                  const std::uint32_t value)
{
    assert(offset + 4U <= bytes.size());
    for (unsigned int byte = 0; byte < 4U; ++byte) {
        bytes[offset + byte] = static_cast<char>(value >> (byte * 8U));
    }
}

void write_short_decodable_wav(const std::filesystem::path& source,
                               const std::filesystem::path& output)
{
    std::string bytes = read_all(source);
    assert(bytes.size() > 44U);
    assert(bytes.compare(0, 4, "RIFF") == 0);
    assert(bytes.compare(8, 4, "WAVE") == 0);
    const std::size_t fmt = bytes.find("fmt ", 12U);
    const std::size_t data = bytes.find("data", 12U);
    assert(fmt != std::string::npos && data != std::string::npos);
    const std::uint16_t block_align = read_u16_le(bytes, fmt + 12U);
    const std::uint32_t data_size = read_u32_le(bytes, data + 4U);
    const std::size_t data_offset = data + 8U;
    const std::size_t available = std::min<std::size_t>(
        data_size, bytes.size() - data_offset);
    const std::size_t short_size =
        std::max<std::size_t>(block_align, (available / 4U) / block_align
                                              * block_align);
    bytes.resize(data_offset + short_size);
    write_u32_le(bytes, data + 4U, static_cast<std::uint32_t>(short_size));
    write_u32_le(bytes, 4U, static_cast<std::uint32_t>(bytes.size() - 8U));
    write_all(output, bytes);
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

    // A raw AAC probe reports only a bitrate-derived estimate. The production
    // transcode path must carry the complete source decode length into staged
    // output verification, so a shorter but otherwise valid/decodable output
    // cannot be committed as success.
    const std::filesystem::path raw_aac =
        directory / "reliable-duration-source.aac";
    const std::filesystem::path short_wav =
        directory / "short-decodable-output.wav";
    const std::filesystem::path incomplete_output =
        directory / "raw-aac-completeness.wav";
    std::filesystem::remove(raw_aac);
    std::filesystem::remove(short_wav);
    std::filesystem::remove(incomplete_output);
    agplayer::TranscodeConfig raw_aac_config;
    raw_aac_config.output_path = raw_aac.u8string();
    raw_aac_config.container_name = "adts";
    raw_aac_config.codec_name = "aac";
    raw_aac_config.sample_rate = 44'100;
    raw_aac_config.channels = 2;
    error.clear();
    assert(agplayer::transcode(input.u8string(), raw_aac_config,
                               nullptr, nullptr, error) == AG_OK);

    agplayer::TranscodeVerificationPlan source_plan;
    agplayer::TranscodeVerificationResult source_verification;
    error.clear();
    assert(agplayer::verify_transcoded_output(
               raw_aac.u8string(), source_plan, source_verification, error)
           == AG_OK);
    write_short_decodable_wav(input, short_wav);
    agplayer::TranscodeVerificationResult short_verification;
    error.clear();
    assert(agplayer::verify_transcoded_output(
               short_wav.u8string(), source_plan, short_verification, error)
           == AG_OK);
    assert(short_verification.decoded_duration_ms + 500
           < source_verification.decoded_duration_ms);

    agplayer::TranscodeConfig incomplete_config;
    incomplete_config.output_path = incomplete_output.u8string();
    incomplete_config.container_name = "wav";
    incomplete_config.codec_name = "pcm_s16le";
    bool replaced_staged_output = false;
    incomplete_config.stage_callback = [&](const std::string_view stage) {
        if (stage != "verifying") return;
        for (const auto& item : std::filesystem::directory_iterator(directory)) {
            const std::string name = item.path().filename().u8string();
            if (name.rfind("raw-aac-completeness.agpart-", 0) == 0
                && item.path().extension() == ".wav") {
                std::filesystem::copy_file(
                    short_wav, item.path(),
                    std::filesystem::copy_options::overwrite_existing);
                replaced_staged_output = true;
                return;
            }
        }
        assert(false && "production staging output not found");
    };
    error.clear();
    const ag_result incomplete_result = agplayer::transcode(
        raw_aac.u8string(), incomplete_config, nullptr, nullptr, error);
    assert(replaced_staged_output);
    assert(incomplete_result == AG_DECODE_ERROR);
    assert(error.find("decoded duration is incomplete") != std::string::npos);
    assert(error.find("actual=") != std::string::npos);
    assert(error.find("expected=") != std::string::npos);
    assert(!std::filesystem::exists(incomplete_output));

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
    std::filesystem::remove(incomplete_output);
    std::filesystem::remove(short_wav);
    std::filesystem::remove(raw_aac);
    std::cout << "transcode verifier tests passed\n";
    return 0;
}

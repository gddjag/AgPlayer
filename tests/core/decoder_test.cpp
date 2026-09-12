// Test files deliberately keep assert() active even in Release builds: many
// test cases embed function calls with side effects inside assert() (e.g.
// ag_metadata_open), and silencing them under NDEBUG would leave dangling
// pointers that crash on cleanup. Undefine NDEBUG before <cassert> so the
// macro always evaluates its argument and aborts on failure.
#undef NDEBUG

#include <agplayer/c_api.h>

#include "decoder.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

int main(const int argc, char** argv)
{
    assert(argc == 6);
    const std::filesystem::path sine_path = argv[1];
    const std::filesystem::path video_with_audio_path = argv[2];
    const std::filesystem::path video_only_path = argv[3];
    const std::filesystem::path audio_with_attached_picture_path = argv[4];
    const std::filesystem::path rotated_video_path = argv[5];

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

    agplayer::MediaMetadata video_with_audio;
    assert(agplayer::probe_media_metadata(video_with_audio_path.string(),
                                          video_with_audio)
           == AG_OK);
    assert(video_with_audio.has_audio);
    assert(video_with_audio.has_video);
    assert(video_with_audio.video_width == 320);
    assert(video_with_audio.video_height == 180);

    agplayer::MediaMetadata video_only;
    assert(agplayer::probe_media_metadata(video_only_path.string(), video_only)
           == AG_OK);
    assert(!video_only.has_audio);
    assert(video_only.has_video);
    assert(video_only.duration_ms > 0);

    agplayer::MediaMetadata cover_only;
    assert(agplayer::probe_media_metadata(audio_with_attached_picture_path.string(),
                                          cover_only)
           == AG_OK);
    assert(cover_only.has_audio);
    assert(!cover_only.has_video);

    agplayer::MediaMetadata rotated_video;
    assert(agplayer::probe_media_metadata(rotated_video_path.string(), rotated_video)
           == AG_OK);
    assert(!rotated_video.has_audio);
    assert(rotated_video.has_video);
    assert(rotated_video.video_width == 320);
    assert(rotated_video.video_height == 180);

    metadata = nullptr;
    assert(ag_metadata_open(video_with_audio_path.string().c_str(), &metadata)
           == AG_OK);
    assert(metadata != nullptr);
    assert(ag_metadata_has_audio(metadata) == 1);
    assert(ag_metadata_has_video(metadata) == 1);
    assert(ag_metadata_video_width(metadata) == 320);
    assert(ag_metadata_video_height(metadata) == 180);
    ag_metadata_destroy(metadata);
    assert(ag_metadata_has_audio(nullptr) == 0);
    assert(ag_metadata_has_video(nullptr) == 0);
    assert(ag_metadata_video_width(nullptr) == 0);
    assert(ag_metadata_video_height(nullptr) == 0);

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

    // Video-only inputs stay unsupported for normal audio callers. The
    // playback-only opt-in instead exposes a finite, silent 48 kHz stereo
    // clock so the player retains its existing audio timeline.
    agplayer::Decoder strict_video_decoder;
    assert(strict_video_decoder.open(video_only_path.string())
           == AG_UNSUPPORTED_FORMAT);
    assert(!strict_video_decoder.is_open());

    agplayer::DecoderOpenOptions silent_clock_options;
    silent_clock_options.allow_silent_video_clock = true;
    agplayer::Decoder silent_video_decoder;
    assert(silent_video_decoder.open(video_only_path.string(),
                                     silent_clock_options)
           == AG_OK);
    assert(silent_video_decoder.is_open());
    assert(silent_video_decoder.output_format().sample_rate == 48'000);
    assert(silent_video_decoder.output_format().channels == 2);

    constexpr std::int64_t silent_sample_rate = 48'000;
    const std::int64_t silent_total_frames =
        (video_only.duration_ms * silent_sample_rate + 999) / 1'000;
    assert(silent_total_frames > 0);
    std::int64_t silent_frames_read = 0;
    std::int64_t previous_silent_end_frame = 0;
    std::int64_t previous_silent_timestamp_ms = -1;
    do {
        assert(silent_video_decoder.read(block) == AG_OK);
        if (block.frames > 0U) {
            assert(block.timestamp_frame == previous_silent_end_frame);
            assert(block.timestamp_ms >= previous_silent_timestamp_ms);
            assert(block.samples.size() == block.frames * 2U);
            assert(std::all_of(block.samples.begin(), block.samples.end(),
                               [](const float sample) { return sample == 0.0F; }));
            previous_silent_end_frame += static_cast<std::int64_t>(block.frames);
            previous_silent_timestamp_ms = block.timestamp_ms;
            silent_frames_read += static_cast<std::int64_t>(block.frames);
        }
    } while (!block.end_of_stream);
    assert(silent_frames_read == silent_total_frames);

    constexpr std::int64_t silent_seek_ms = 1'000;
    assert(silent_video_decoder.seek(silent_seek_ms) == AG_OK);
    assert(silent_video_decoder.read(block) == AG_OK);
    assert(block.frames > 0U);
    assert(block.timestamp_frame >= silent_seek_ms * silent_sample_rate / 1'000);
    assert(block.timestamp_frame
           <= silent_seek_ms * silent_sample_rate / 1'000
              + static_cast<std::int64_t>(block.frames));
    assert(std::all_of(block.samples.begin(), block.samples.end(),
                       [](const float sample) { return sample == 0.0F; }));

    assert(silent_video_decoder.seekFrame(silent_sample_rate) == AG_OK);
    assert(silent_video_decoder.read(block) == AG_OK);
    assert(block.frames > 0U);
    assert(block.timestamp_frame == silent_sample_rate);

    assert(silent_video_decoder.seekFrame(silent_total_frames + 1'024) == AG_OK);
    assert(silent_video_decoder.read(block) == AG_OK);
    assert(block.frames == 0U);
    assert(block.end_of_stream);
    silent_video_decoder.close();
    assert(!silent_video_decoder.is_open());

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
}

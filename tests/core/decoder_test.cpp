// Test files deliberately keep assert() active even in Release builds: many
// test cases embed function calls with side effects inside assert() (e.g.
// ag_metadata_open), and silencing them under NDEBUG would leave dangling
// pointers that crash on cleanup. Undefine NDEBUG before <cassert> so the
// macro always evaluates its argument and aborts on failure.
#undef NDEBUG

#include <agplayer/c_api.h>

#include "decoder.hpp"
#include "audio_editor/audio_file_analyzer.hpp"
#include "lossless/lossless_analyzer.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

// Optional, read-only integration check against user-provided FLAC files.
// Originals are never rewritten. Transcode output uses an owned temporary folder.
int check_external_flac(const std::filesystem::path& directory)
{
    int failures = 0;
    int checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".flac") continue;
        ++checked;
        const auto path = entry.path().u8string();
        agplayer::MediaMetadata metadata;
        const auto probe = agplayer::probe_media_metadata(path, metadata);
        agplayer::Decoder decoder;
        const auto opened = decoder.open(path);
        std::cout << path << " probe=" << probe << " open=" << opened
                  << " rate=" << metadata.sample_rate
                  << " channels=" << metadata.channels << std::endl;
        if (probe != AG_OK || opened != AG_OK) { ++failures; continue; }
        std::uint64_t frames = 0;
        agplayer::DecodedAudioBlock block;
        ag_result result = AG_OK;
        do {
            result = decoder.read(block);
            if (result != AG_OK) break;
            frames += block.frames;
        } while (!block.end_of_stream);
        std::cout << "read=" << result << " frames=" << frames << std::endl;
        if (result != AG_OK || frames == 0) { ++failures; continue; }
        for (const auto position : {std::int64_t{0}, metadata.duration_ms / 2,
                                   std::max(std::int64_t{0}, metadata.duration_ms - 1000)}) {
            result = decoder.seek(position);
            if (result == AG_OK) result = decoder.read(block);
            if (result != AG_OK || block.frames == 0) ++failures;
        }
        decoder.close();
        result = decoder.open(path);
        agplayer::DecodedAnalysisBlock analysis;
        std::uint64_t analysis_frames = 0;
        while (result == AG_OK) {
            result = decoder.readAnalysis(analysis);
            if (result != AG_OK) break;
            analysis_frames += analysis.frames;
            if (analysis.end_of_stream) break;
        }
        std::cout << "analysis=" << result << " frames=" << analysis_frames << std::endl;
        if (result != AG_OK || analysis_frames != frames) ++failures;
        ag_waveform* waveform = nullptr;
        result = ag_waveform_analyze(path.c_str(), 512, nullptr, nullptr, nullptr, &waveform);
        std::cout << "waveform=" << result << " points=" << ag_waveform_count(waveform) << std::endl;
        if (result != AG_OK || ag_waveform_count(waveform) == 0) ++failures;
        ag_waveform_destroy(waveform);
        const auto editor = agplayer::editor::AudioFileAnalyzer::analyze(entry.path(), 512);
        std::cout << "editor=" << editor.success << " error=" << editor.message << std::endl;
        if (!editor.success || editor.visual_mix_peaks.empty()) ++failures;
        const std::atomic_bool cancelled{false};
        const auto identification = agplayer::lossless::analyzeFile(path, {}, cancelled);
        std::cout << "lossless_error=" << identification.error << std::endl;
        if (!identification.error.empty() || identification.cancelled) ++failures;
        const auto output_dir = std::filesystem::temp_directory_path()
            / ("agplayer-flac-check-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(output_dir)) return 1;
        const auto output_file = output_dir / "converted.wav";
        result = ag_transcode(path.c_str(), output_file.u8string().c_str(),
                              "pcm_s16le", 0, 44'100, 2, nullptr, nullptr, nullptr);
        std::cout << "transcode=" << result << " error=" << ag_last_error() << std::endl;
        if (result != AG_OK) ++failures;
        std::filesystem::remove(output_file);
        std::filesystem::remove(output_dir);
    }
    std::cout << "files=" << checked << " failures=" << failures << std::endl;
    return checked > 0 && failures == 0 ? 0 : 1;
}

void check_id3_prefixed_wave(const std::filesystem::path& source)
{
    const auto directory = std::filesystem::temp_directory_path()
        / ("agplayer-id3-wave-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(std::filesystem::create_directory(directory));
    const auto file = directory / "tagged-wave.mp3";
    {
        std::ifstream input(source, std::ios::binary);
        std::ofstream output(file, std::ios::binary);
        // ID3v2.3 with 16 padding bytes, then an ordinary WAVE container.
        const char tag[26] = {'I', 'D', '3', 3, 0, 0, 0, 0, 0, 16};
        output.write(tag, sizeof tag);
        output << input.rdbuf();
        assert(output.good());
    }
    agplayer::MediaMetadata original, tagged;
    assert(agplayer::probe_media_metadata(source.u8string(), original) == AG_OK);
    assert(agplayer::probe_media_metadata(file.u8string(), tagged) == AG_OK);
    assert(tagged.sample_rate == original.sample_rate);
    assert(tagged.duration_ms == original.duration_ms);
    agplayer::Decoder decoder;
    assert(decoder.open(file.u8string()) == AG_OK);
    agplayer::DecodedAudioBlock block;
    std::uint64_t frames = 0;
    do {
        assert(decoder.read(block) == AG_OK);
        frames += block.frames;
    } while (!block.end_of_stream);
    assert(frames > 0);
    assert(decoder.seek(tagged.duration_ms / 2) == AG_OK);
    assert(decoder.read(block) == AG_OK && block.frames > 0);
    decoder.close();
    // Never skip an invalid synchsafe size to make malformed input look valid.
    {
        std::fstream corrupt(file, std::ios::binary | std::ios::in | std::ios::out);
        corrupt.seekp(6);
        corrupt.put(static_cast<char>(0x80));
    }
    assert(decoder.open(file.u8string()) != AG_OK);
    std::filesystem::remove(file);
    std::filesystem::remove(directory);
}

// Read-only regression check for a user-supplied file with recoverable MP3 damage.
int check_external_audio(const std::filesystem::path& file)
{
    const std::string path = file.u8string();
    agplayer::Decoder decoder;
    if (decoder.open(path, 44'100, 2) != AG_OK) return 1;
    agplayer::DecodedAudioBlock block;
    std::uint64_t frames = 0;
    ag_result result = AG_OK;
    do {
        result = decoder.read(block);
        if (result != AG_OK) break;
        frames += block.frames;
    } while (!block.end_of_stream);
    if (result != AG_OK || frames == 0) return 1;

    decoder.close();
    if (decoder.open(path, 44'100, 2) != AG_OK) return 1;
    agplayer::DecodedAnalysisBlock analysis;
    std::uint64_t analysis_frames = 0;
    do {
        result = decoder.readAnalysis(analysis);
        if (result != AG_OK) break;
        analysis_frames += analysis.frames;
    } while (!analysis.end_of_stream);
    if (result != AG_OK || analysis_frames != frames) return 1;

    ag_waveform* waveform = nullptr;
    result = ag_waveform_analyze(path.c_str(), 512, nullptr, nullptr,
                                 nullptr, &waveform);
    const bool waveform_ok = result == AG_OK && ag_waveform_count(waveform) > 0;
    ag_waveform_destroy(waveform);
    std::cout << "decoded_frames=" << frames
              << " analysis_frames=" << analysis_frames
              << " waveform=" << waveform_ok << std::endl;
    return waveform_ok ? 0 : 1;
}

int main(const int argc, char** argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--id3-wave") == 0) {
        check_id3_prefixed_wave(std::filesystem::path(argv[2]));
        return 0;
    }
    if (argc == 3 && std::strcmp(argv[1], "--external-audio") == 0) {
        return check_external_audio(std::filesystem::path(argv[2]));
    }
    if (argc == 3 && std::strcmp(argv[1], "--external-flac") == 0) {
        return check_external_flac(std::filesystem::path(argv[2]));
    }
    assert(argc == 6);
    const std::filesystem::path sine_path = argv[1];
    check_id3_prefixed_wave(sine_path);
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

    // A valid FLAC followed by a non-audio trailer must stop at STREAMINFO's
    // exact sample count. This reproduces the supplied music-library files.
    {
        std::ofstream trailer(short_flac, std::ios::binary | std::ios::app);
        const unsigned char bytes[] = {
            0x00, 0xff, 0x0f, 0x47, 0x40, 0x38, 0x40, 0x48,
            0x46, 0x3c, 0x36, 0x23, 0x23, 0x25, 0x41, 0x43,
            0x27, 0x3f, 0x4a, 0x24, 0x3c, 0x4d, 0x5e, 0x23,
            0x69, 0x0e, 0x55, 0xff, 0xf0};
        trailer.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    }
    for (const bool analysis_mode : {false, true}) {
        agplayer::Decoder flac_decoder;
        assert(flac_decoder.open(short_flac.u8string()) == AG_OK);
        std::uint64_t flac_frames = 0;
        bool ended = false;
        while (!ended) {
            if (analysis_mode) {
                agplayer::DecodedAnalysisBlock analysis_block;
                assert(flac_decoder.readAnalysis(analysis_block) == AG_OK);
                flac_frames += analysis_block.frames;
                ended = analysis_block.end_of_stream;
            } else {
                agplayer::DecodedAudioBlock audio_block;
                assert(flac_decoder.read(audio_block) == AG_OK);
                flac_frames += audio_block.frames;
                ended = audio_block.end_of_stream;
            }
        }
        assert(flac_frames == 88'200);
    }
    const auto converted_flac = sine_path.parent_path() / "decoder-flac-trailer.wav";
    std::filesystem::remove(converted_flac);
    assert(ag_transcode(short_flac.u8string().c_str(), converted_flac.u8string().c_str(),
                        "pcm_s16le", 0, 48'000, 2, nullptr, nullptr, nullptr) == AG_OK);
    agplayer::Decoder converted_decoder;
    assert(converted_decoder.open(converted_flac.u8string()) == AG_OK);
    std::uint64_t converted_frames = 0;
    agplayer::DecodedAudioBlock converted_block;
    do {
        assert(converted_decoder.read(converted_block) == AG_OK);
        converted_frames += converted_block.frames;
    } while (!converted_block.end_of_stream);
    assert(converted_frames == 96'000);
    converted_decoder.close();
    std::filesystem::remove(converted_flac);

    // Unknown sample count, or garbage before the declared end, must not be
    // accepted as a successful stream merely because some PCM was decoded.
    for (const std::uint32_t declared_frames : {0U, 88'201U}) {
        std::fstream streaminfo(short_flac, std::ios::binary | std::ios::in | std::ios::out);
        streaminfo.seekp(22);
        for (int shift = 24; shift >= 0; shift -= 8) {
            streaminfo.put(static_cast<char>((declared_frames >> shift) & 0xff));
        }
        streaminfo.close();
        agplayer::Decoder invalid_decoder;
        assert(invalid_decoder.open(short_flac.u8string()) == AG_OK);
        agplayer::DecodedAudioBlock invalid_block;
        ag_result invalid_result = AG_OK;
        for (int count = 0; count < 128; ++count) {
            invalid_result = invalid_decoder.read(invalid_block);
            if (invalid_result != AG_OK || invalid_block.end_of_stream) break;
        }
        assert(invalid_result == AG_DECODE_ERROR);
    }

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

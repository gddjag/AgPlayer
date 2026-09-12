#undef NDEBUG

#include "transcoder.hpp"

#include "decoder.hpp"

#include <cmath>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
}

namespace {

constexpr unsigned char kOnePixelBmp[] = {
    0x42, 0x4d, 0x3a, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x13, 0x0b,
    0x00, 0x00, 0x13, 0x0b, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0x00};

void append_u32_le(std::string& bytes, const std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

void append_riff_info(const std::filesystem::path& path)
{
    std::string payload("INFO", 4);
    const auto append_tag = [&payload](const char id[4],
                                       const std::string& value) {
        payload.append(id, 4);
        const std::uint32_t size =
            static_cast<std::uint32_t>(value.size() + 1U);
        append_u32_le(payload, size);
        payload.append(value);
        payload.push_back('\0');
        if ((size & 1U) != 0U) {
            payload.push_back('\0');
        }
    };
    append_tag("INAM", "Policy Title");
    append_tag("IART", "Policy Artist");

    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    assert(file);
    file.seekp(0, std::ios::end);
    file.write("LIST", 4);
    std::string size_bytes;
    append_u32_le(size_bytes, static_cast<std::uint32_t>(payload.size()));
    file.write(size_bytes.data(), static_cast<std::streamsize>(size_bytes.size()));
    file.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    file.flush();

    const std::uint32_t riff_size =
        static_cast<std::uint32_t>(std::filesystem::file_size(path) - 8U);
    size_bytes.clear();
    append_u32_le(size_bytes, riff_size);
    file.seekp(4, std::ios::beg);
    file.write(size_bytes.data(), static_cast<std::streamsize>(size_bytes.size()));
    assert(file);
}

std::filesystem::path make_corrupt_adpcm_wav(const std::filesystem::path& source)
{
    const std::filesystem::path dest =
        source.parent_path() / "corrupt-adpcm.wav";
    std::filesystem::remove(dest);
    std::filesystem::copy_file(
        source, dest, std::filesystem::copy_options::overwrite_existing);

    std::fstream file(dest, std::ios::in | std::ios::out | std::ios::binary);
    assert(file);
    file.seekp(20, std::ios::beg);
    const char tag[] = {static_cast<char>(0x02), static_cast<char>(0x00)};
    file.write(tag, sizeof(tag));
    assert(file);
    return dest;
}

float scan_peak(const std::filesystem::path& path)
{
    agplayer::Decoder decoder;
    if (decoder.open(path.string()) != AG_OK) {
        return -1.0f;
    }
    agplayer::DecodedAudioBlock block;
    float peak = 0.0f;
    do {
        if (decoder.read(block) != AG_OK) {
            return -1.0f;
        }
        for (float sample : block.samples) {
            const float abs_sample = std::abs(sample);
            if (abs_sample > peak) {
                peak = abs_sample;
            }
        }
    } while (!block.end_of_stream);
    return peak;
}

std::vector<float> decode_samples(const std::filesystem::path& path)
{
    agplayer::Decoder decoder;
    assert(decoder.open(path.string(), 44100, 2) == AG_OK);
    std::vector<float> samples;
    agplayer::DecodedAudioBlock block;
    do {
        assert(decoder.read(block) == AG_OK);
        samples.insert(samples.end(), block.samples.begin(), block.samples.end());
    } while (!block.end_of_stream);
    return samples;
}

std::filesystem::path make_stream_tagged_matroska(
    const std::filesystem::path& source)
{
    const std::filesystem::path dest =
        source.parent_path() / "transcoder-stream-tagged.mka";
    std::filesystem::remove(dest);

    AVFormatContext* input = nullptr;
    assert(avformat_open_input(&input, source.string().c_str(), nullptr, nullptr)
           >= 0);
    assert(avformat_find_stream_info(input, nullptr) >= 0);
    const int input_index =
        av_find_best_stream(input, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    assert(input_index >= 0);

    AVFormatContext* output = nullptr;
    assert(avformat_alloc_output_context2(
               &output, nullptr, "matroska", dest.string().c_str())
           >= 0);
    assert(output != nullptr);
    AVStream* output_stream = avformat_new_stream(output, nullptr);
    assert(output_stream != nullptr);
    AVStream* input_stream = input->streams[input_index];
    assert(avcodec_parameters_copy(
               output_stream->codecpar, input_stream->codecpar)
           >= 0);
    output_stream->time_base = input_stream->time_base;
    assert(av_dict_set(&output_stream->metadata, "title",
                       "Stream Policy Title", 0)
           >= 0);
    assert(av_dict_set(&output_stream->metadata, "language", "eng", 0) >= 0);

    assert(avio_open(&output->pb, dest.string().c_str(), AVIO_FLAG_WRITE) >= 0);
    assert(avformat_write_header(output, nullptr) >= 0);

    AVPacket* packet = av_packet_alloc();
    assert(packet != nullptr);
    while (av_read_frame(input, packet) >= 0) {
        if (packet->stream_index == input_index) {
            av_packet_rescale_ts(packet, input_stream->time_base,
                                 output_stream->time_base);
            packet->stream_index = output_stream->index;
            packet->pos = -1;
            assert(av_interleaved_write_frame(output, packet) >= 0);
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    assert(av_write_trailer(output) >= 0);
    avio_closep(&output->pb);
    avformat_free_context(output);
    avformat_close_input(&input);
    assert(std::filesystem::exists(dest));
    return dest;
}

std::pair<std::string, std::string> read_audio_stream_tags(
    const std::filesystem::path& path)
{
    AVFormatContext* input = nullptr;
    assert(avformat_open_input(&input, path.string().c_str(), nullptr, nullptr)
           >= 0);
    assert(avformat_find_stream_info(input, nullptr) >= 0);
    const int stream_index =
        av_find_best_stream(input, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    assert(stream_index >= 0);
    const AVDictionary* tags = input->streams[stream_index]->metadata;
    const AVDictionaryEntry* title =
        av_dict_get(tags, "title", nullptr, 0);
    const AVDictionaryEntry* language =
        av_dict_get(tags, "language", nullptr, 0);
    const std::pair<std::string, std::string> result{
        title != nullptr ? title->value : "",
        language != nullptr ? language->value : ""};
    avformat_close_input(&input);
    return result;
}

std::string read_cover_stream_title(const std::filesystem::path& path)
{
    AVFormatContext* input = nullptr;
    assert(avformat_open_input(&input, path.string().c_str(), nullptr, nullptr)
           >= 0);
    assert(avformat_find_stream_info(input, nullptr) >= 0);
    std::string title;
    for (unsigned int index = 0; index < input->nb_streams; ++index) {
        const AVStream* stream = input->streams[index];
        if ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0) {
            continue;
        }
        const AVDictionaryEntry* entry =
            av_dict_get(stream->metadata, "title", nullptr, 0);
        title = entry != nullptr && entry->value != nullptr ? entry->value : "";
        break;
    }
    avformat_close_input(&input);
    return title;
}

} // namespace

int main(const int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path input_path = argv[1];

    // Happy path: valid sine wave transcodes successfully.
    const std::filesystem::path happy_output =
        input_path.parent_path() / "transcoder-out.wav";
    std::filesystem::remove(happy_output);

    agplayer::TranscodeConfig happy_config;
    happy_config.output_path = happy_output.string();
    happy_config.codec_name = "pcm_s16le";
    std::string error;
    ag_result result =
        agplayer::transcode(input_path.string(), happy_config,
                            nullptr, nullptr, error);
    if (result != AG_OK) {
        std::cerr << "transcode happy path failed: " << static_cast<int>(result)
                  << " " << error << "\n";
    }
    assert(result == AG_OK);
    assert(std::filesystem::exists(happy_output));

    // Lossless output must preserve decoded PCM samples.
    const std::filesystem::path lossless_output =
        input_path.parent_path() / "transcoder-lossless.flac";
    std::filesystem::remove(lossless_output);
    agplayer::TranscodeConfig lossless_config;
    lossless_config.output_path = lossless_output.string();
    lossless_config.codec_name = "flac";
    error.clear();
    result = agplayer::transcode(input_path.string(), lossless_config,
                                 nullptr, nullptr, error);
    assert(result == AG_OK);
    const std::vector<float> source_samples = decode_samples(input_path);
    const std::vector<float> lossless_samples = decode_samples(lossless_output);
    assert(source_samples.size() == lossless_samples.size());
    for (std::size_t index = 0; index < source_samples.size(); ++index) {
        assert(std::abs(source_samples[index] - lossless_samples[index])
               <= 1.0e-5f);
    }
    std::filesystem::remove(lossless_output);

    // Failure path: corrupted ADPCM-tagged WAV triggers avcodec_send_packet
    // failure. The tool must return a non-AG_OK status and must not leave a
    // complete/successful output file.
    const std::filesystem::path corrupt_path = make_corrupt_adpcm_wav(input_path);
    const std::filesystem::path failure_output =
        corrupt_path.parent_path() / "transcoder-out.wav";
    std::filesystem::remove(failure_output);

    agplayer::TranscodeConfig failure_config;
    failure_config.output_path = failure_output.string();
    failure_config.codec_name = "pcm_s16le";
    error.clear();
    result = agplayer::transcode(corrupt_path.string(), failure_config,
                                 nullptr, nullptr, error);
    if (result == AG_OK) {
        std::cerr << "expected transcode failure for corrupted input\n";
    }
    assert(result != AG_OK);
    assert(!std::filesystem::exists(failure_output));

    // Invalid argument: input and output paths must differ.
    const std::filesystem::path collision_path =
        input_path.parent_path() / "transcoder-collision.wav";
    std::filesystem::remove(collision_path);
    std::filesystem::copy_file(input_path, collision_path);

    agplayer::TranscodeConfig collision_config;
    collision_config.output_path = collision_path.string();
    collision_config.codec_name = "pcm_s16le";
    error.clear();
    result = agplayer::transcode(collision_path.string(), collision_config,
                                 nullptr, nullptr, error);
    if (result != AG_INVALID_ARGUMENT) {
        std::cerr << "expected AG_INVALID_ARGUMENT for input/output collision, got "
                  << static_cast<int>(result) << " " << error << "\n";
    }
    assert(result == AG_INVALID_ARGUMENT);
    std::filesystem::remove(collision_path);

    // Distinct UTF-8 paths must not be treated as equal when the output does
    // not exist yet. This catches failed canonicalization being compared as
    // two empty paths on Windows.
    const std::filesystem::path unicode_input_directory =
        input_path.parent_path()
        / std::filesystem::u8path(u8"\u4e0b\u8f7d");
    const std::filesystem::path unicode_output_directory =
        input_path.parent_path()
        / std::filesystem::u8path(u8"\u684c\u9762");
    const std::filesystem::path unicode_input =
        unicode_input_directory
        / std::filesystem::u8path(u8"\u8f93\u5165.wav");
    const std::filesystem::path unicode_output =
        unicode_output_directory
        / std::filesystem::u8path(u8"\u8f93\u51fa.wav");
    std::filesystem::create_directories(unicode_input_directory);
    std::filesystem::create_directories(unicode_output_directory);
    std::filesystem::remove(unicode_input);
    std::filesystem::remove(unicode_output);
    std::filesystem::copy_file(input_path, unicode_input);

    agplayer::TranscodeConfig unicode_config;
    unicode_config.output_path = unicode_output.u8string();
    unicode_config.codec_name = "pcm_s16le";
    error.clear();
    result = agplayer::transcode(unicode_input.u8string(), unicode_config,
                                 nullptr, nullptr, error);
    if (result != AG_OK) {
        std::cerr << "distinct UTF-8 path transcode failed: "
                  << static_cast<int>(result) << " " << error << "\n";
    }
    assert(result == AG_OK);
    assert(std::filesystem::exists(unicode_output));
    std::filesystem::remove_all(unicode_input_directory);
    std::filesystem::remove_all(unicode_output_directory);

    // Volume normalize: output peak should approach -1 dBFS for a quiet fixture.
    // For sine fixture (already loud), gain should be capped at 1.0.
    const std::filesystem::path normalize_output =
        input_path.parent_path() / "transcoder-normalized.wav";
    std::filesystem::remove(normalize_output);
    agplayer::TranscodeConfig normalize_config;
    normalize_config.output_path = normalize_output.string();
    normalize_config.codec_name = "pcm_s16le";
    normalize_config.volume_normalize = true;
    error.clear();
    result = agplayer::transcode(input_path.string(), normalize_config,
                                 nullptr, nullptr, error);
    if (result != AG_OK) {
        std::cerr << "transcode volume normalize failed: "
                  << static_cast<int>(result) << " " << error << "\n";
    }
    assert(result == AG_OK);
    assert(std::filesystem::exists(normalize_output));
    const float input_peak = scan_peak(input_path);
    const float normalized_peak = scan_peak(normalize_output);
    assert(input_peak > 0.0f);
    assert(normalized_peak > 0.0f);
    assert(normalized_peak <= input_peak * 1.01f);
    std::filesystem::remove(normalize_output);

    // Metadata policy: keep_metadata copies tags; false strips them.
    const std::filesystem::path tagged_input =
        input_path.parent_path() / "transcoder-tagged.wav";
    std::filesystem::remove(tagged_input);
    std::filesystem::copy_file(input_path, tagged_input);
    append_riff_info(tagged_input);

    const auto transcode_with_metadata_policy =
        [&](const char* file_name, const bool keep_metadata) {
            const std::filesystem::path output =
                input_path.parent_path() / file_name;
            std::filesystem::remove(output);
            agplayer::TranscodeConfig config;
            config.output_path = output.string();
            config.codec_name = "pcm_s16le";
            config.keep_metadata = keep_metadata;
            error.clear();
            const ag_result transcode_result =
                agplayer::transcode(tagged_input.string(), config,
                                    nullptr, nullptr, error);
            if (transcode_result != AG_OK) {
                std::cerr << "metadata policy transcode failed: "
                          << transcode_result << " " << error << "\n";
            }
            assert(transcode_result == AG_OK);
            {
                agplayer::Decoder decoder;
                const ag_result open_result = decoder.open(output.string());
                if (open_result != AG_OK) {
                    std::cerr << "metadata policy output open failed: "
                              << open_result << "\n";
                }
                assert(open_result == AG_OK);
                const agplayer::MediaMetadata metadata = decoder.metadata();
                if (keep_metadata) {
                    if (metadata.title != "Policy Title"
                        || metadata.artist != "Policy Artist") {
                        std::cerr << "metadata was not preserved: title='"
                                  << metadata.title << "' artist='"
                                  << metadata.artist << "'\n";
                    }
                    assert(metadata.title == "Policy Title");
                    assert(metadata.artist == "Policy Artist");
                } else {
                    if (!metadata.title.empty() || !metadata.artist.empty()) {
                        std::cerr << "metadata was not stripped: title='"
                                  << metadata.title << "' artist='"
                                  << metadata.artist << "'\n";
                    }
                    assert(metadata.title.empty());
                    assert(metadata.artist.empty());
                }
            }
            std::filesystem::remove(output);
        };

    transcode_with_metadata_policy("transcoder-meta-stripped.wav", false);
    transcode_with_metadata_policy("transcoder-meta-kept.wav", true);

    // Audio-stream tags follow the same policy as container-level tags.
    const std::filesystem::path stream_tagged_input =
        make_stream_tagged_matroska(input_path);
    const auto transcode_stream_tags =
        [&](const char* file_name, const bool keep_metadata) {
            const std::filesystem::path output =
                input_path.parent_path() / file_name;
            std::filesystem::remove(output);
            agplayer::TranscodeConfig config;
            config.output_path = output.string();
            config.codec_name = "pcm_s16le";
            config.keep_metadata = keep_metadata;
            error.clear();
            const ag_result transcode_result =
                agplayer::transcode(stream_tagged_input.string(), config,
                                    nullptr, nullptr, error);
            if (transcode_result != AG_OK) {
                std::cerr << "stream metadata transcode failed: "
                          << transcode_result << " " << error << "\n";
            }
            assert(transcode_result == AG_OK);
            const auto [title, language] = read_audio_stream_tags(output);
            if (keep_metadata) {
                assert(title == "Stream Policy Title");
                assert(language == "eng");
            } else {
                assert(title.empty());
                assert(language.empty());
            }
            std::filesystem::remove(output);
        };
    transcode_stream_tags("transcoder-stream-stripped.mka", false);
    transcode_stream_tags("transcoder-stream-kept.mka", true);

    // Canonical Title edits target track metadata, not the attached picture's
    // descriptive dictionary. Cover Keep must preserve "Album cover" while
    // the output track receives its new title.
    const std::filesystem::path cover_source =
        input_path.parent_path() / "transcoder-cover-title-source.mp3";
    const std::filesystem::path cover_output =
        input_path.parent_path() / "transcoder-cover-title-output.mp3";
    std::filesystem::remove(cover_source);
    std::filesystem::remove(cover_source.u8string() + ".agbak");
    std::filesystem::remove(cover_output);
    assert(ag_transcode(input_path.u8string().c_str(),
                        cover_source.u8string().c_str(), "libmp3lame",
                        192'000, 44'100, 2, nullptr, nullptr, nullptr)
           == AG_OK);
    assert(ag_metadata_write_extended(
               cover_source.u8string().c_str(), nullptr, nullptr, nullptr,
               nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
               nullptr, nullptr, nullptr, nullptr, kOnePixelBmp,
               sizeof(kOnePixelBmp), "image/bmp")
           == AG_OK);
    assert(read_cover_stream_title(cover_source) == "Album cover");

    agplayer::TranscodeConfig cover_config;
    cover_config.output_path = cover_output.u8string();
    cover_config.codec_name = "libmp3lame";
    cover_config.keep_metadata = true;
    cover_config.keep_cover = true;
    cover_config.metadata_edit_plan.fields = {{
        agplayer::CanonicalField::Title,
        agplayer::MetadataAction::Set,
        "Edited Track"}};
    error.clear();
    assert(agplayer::transcode(cover_source.u8string(), cover_config,
                               nullptr, nullptr, error)
           == AG_OK);
    assert(read_cover_stream_title(cover_output) == "Album cover");

    std::filesystem::remove(cover_output);
    std::filesystem::remove(cover_source);
    std::filesystem::remove(cover_source.u8string() + ".agbak");

    std::filesystem::remove(stream_tagged_input);
    std::filesystem::remove(tagged_input);
    std::filesystem::remove(happy_output);
    std::filesystem::remove(corrupt_path);
    return 0;
}

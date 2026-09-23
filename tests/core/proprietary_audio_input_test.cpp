#undef NDEBUG
#include "proprietary_audio_input.hpp"
#include "qmc_key_codec.hpp"
#include "decoder.hpp"
#include "transcoder.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

std::vector<std::uint8_t> load(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    assert(stream);
    return {std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
}

void verify_vector(const fs::path& fixtures, const fs::path& directory,
                   const std::string& name, const std::string& extension)
{
    auto encoded = load(fixtures / (name + "_raw.bin"));
    const fs::path suffix = fixtures / (name + "_suffix.bin");
    if (fs::exists(suffix)) {
        const auto trailer = load(suffix);
        encoded.insert(encoded.end(), trailer.begin(), trailer.end());
    }
    const auto expected = load(fixtures / (name + "_target.bin"));
    const fs::path path = directory / (name + extension);
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(encoded.data()),
                     static_cast<std::streamsize>(encoded.size()));
    }
    std::string error;
    auto input = agplayer::ProprietaryAudioInput::open(path.u8string(), error);
    assert(input && error.empty());
    assert(input->seek(0, 0x10000) == static_cast<std::int64_t>(expected.size()));
    std::vector<std::uint8_t> actual;
    std::array<std::uint8_t, 713> chunk{};
    for (;;) {
        const int count = input->read(chunk.data(), static_cast<int>(chunk.size()));
        if (count < 0) break;
        actual.insert(actual.end(), chunk.begin(), chunk.begin() + count);
    }
    if (actual != expected) {
        const auto mismatch = std::mismatch(actual.begin(), actual.end(),
                                            expected.begin(), expected.end());
        std::cerr << "payload mismatch near "
                  << (mismatch.first - actual.begin()) << std::endl;
        assert(false);
    }
    for (const std::size_t offset : {0U, 127U, 128U, 5119U, 5120U,
                                     32767U, 32768U, 65525U}) {
        assert(input->seek(static_cast<std::int64_t>(offset), SEEK_SET)
               == static_cast<std::int64_t>(offset));
        const int count = input->read(chunk.data(), 11);
        assert(count == 11);
        assert(std::equal(chunk.begin(), chunk.begin() + count,
                          expected.begin() + offset));
    }
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    const fs::path fixtures(argv[1]);
    assert(!agplayer::ProprietaryAudioInput::recognizes_path("ordinary.mp3"));
    const fs::path directory = fs::temp_directory_path()
        / ("agplayer-proprietary-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(directory);
    for (const std::string name : {"mflac_map", "mflac_rc4",
                                   "mflac0_rc4", "mgg_map"}) {
        const auto encoded = load(fixtures / (name + "_key_raw.bin"));
        const auto expected = load(fixtures / (name + "_key.bin"));
        std::vector<std::uint8_t> actual;
        assert(agplayer::decode_qmc_embedded_key(
            std::string_view(reinterpret_cast<const char*>(encoded.data()),
                             encoded.size()), actual));
        assert(actual == expected);
    }
    verify_vector(fixtures, directory, "qmc0_static", ".qmc0");
    verify_vector(fixtures, directory, "mflac_map", ".mflac");
    verify_vector(fixtures, directory, "mflac_rc4", ".mflac");
    verify_vector(fixtures, directory, "mflac0_rc4", ".mflac0");
    verify_vector(fixtures, directory, "mgg_map", ".mgg");
    const auto wav = load(fixtures / "synthetic.wav");
    for (const std::string extension : {"ncm", "qmc0", "mgg", "mflac",
                                        "kgm", "kgma", "vpr", "kwm"}) {
        const fs::path path = fixtures / ("synthetic." + extension);
        std::string error;
        auto input = agplayer::ProprietaryAudioInput::open(path.u8string(), error);
        assert(input && error.empty());
        std::vector<std::uint8_t> actual;
        std::array<std::uint8_t, 173> block{};
        for (;;) {
            const int count = input->read(block.data(), static_cast<int>(block.size()));
            if (count < 0) break;
            actual.insert(actual.end(), block.begin(), block.begin() + count);
        }
        if (actual != wav) {
            std::cerr << "synthetic mismatch: " << extension << std::endl;
            assert(false);
        }
        agplayer::MediaMetadata metadata;
        assert(agplayer::probe_media_metadata(path.u8string(), metadata) == AG_OK);
        assert(metadata.has_audio && metadata.sample_rate == 22050);
        if (extension == "ncm") {
            assert(metadata.title == "合成测试");
            assert(metadata.artist == "测试歌手");
            assert(metadata.album == "测试专辑");
            assert(metadata.cover_mime_type == "image/png");
            assert(!metadata.cover.empty());
        }
        agplayer::Decoder decoder;
        assert(decoder.open(path.u8string()) == AG_OK);
        agplayer::DecodedAudioBlock decoded;
        assert(decoder.read(decoded) == AG_OK && decoded.frames > 0);
        assert(decoder.seek(100) == AG_OK);
        assert(decoder.read(decoded) == AG_OK && decoded.frames > 0);
        agplayer::TranscodeConfig config;
        config.output_path = (directory / ("converted-" + extension + ".wav")).u8string();
        config.codec_name = "pcm_s16le";
        std::string convert_error;
        const ag_result converted = agplayer::transcode(
            path.u8string(), config, nullptr, nullptr, convert_error);
        if (converted != AG_OK) {
            std::cerr << "transcode error " << extension << ": "
                      << convert_error << std::endl;
        }
        assert(converted == AG_OK);
        agplayer::MediaMetadata converted_metadata;
        assert(agplayer::probe_media_metadata(config.output_path,
                                              converted_metadata) == AG_OK);
        assert(converted_metadata.sample_rate == 22050);
    }
    agplayer::TranscodeConfig preserve;
    preserve.output_path = (directory / "ncm-preserved.flac").u8string();
    preserve.codec_name = "flac";
    preserve.keep_metadata = true;
    preserve.keep_cover = true;
    std::string preserve_error;
    assert(agplayer::transcode(
        (fixtures / "synthetic.ncm").u8string(), preserve,
        nullptr, nullptr, preserve_error) == AG_OK);
    agplayer::MediaMetadata preserved;
    assert(agplayer::probe_media_metadata(preserve.output_path, preserved) == AG_OK);
    assert(preserved.title == "合成测试");
    assert(preserved.artist == "测试歌手");
    assert(preserved.album == "测试专辑");
    assert(!preserved.cover.empty());
    agplayer::MediaMetadata jpeg_metadata;
    assert(agplayer::probe_media_metadata(
        (fixtures / "synthetic-jpeg.ncm").u8string(), jpeg_metadata) == AG_OK);
    assert(jpeg_metadata.title == "JPEG 测试");
    assert(jpeg_metadata.cover_mime_type == "image/jpeg");
    assert(!jpeg_metadata.cover.empty());
    const fs::path collision = fixtures / "synthetic-collision.ncm";
    agplayer::MediaMetadata collision_metadata;
    assert(agplayer::probe_media_metadata(collision.u8string(),
                                          collision_metadata) == AG_OK);
    assert(collision_metadata.title == "album");
    assert(collision_metadata.album == "Correct Album");
    assert(collision_metadata.artist == "First / Second");
    agplayer::TranscodeConfig collision_output;
    collision_output.output_path = (directory / "collision-preserved.flac").u8string();
    collision_output.codec_name = "flac";
    collision_output.keep_metadata = true;
    std::string collision_error;
    assert(agplayer::transcode(collision.u8string(), collision_output,
                               nullptr, nullptr, collision_error) == AG_OK);
    agplayer::MediaMetadata collision_converted;
    assert(agplayer::probe_media_metadata(collision_output.output_path,
                                          collision_converted) == AG_OK);
    assert(collision_converted.album == "Correct Album");
    assert(collision_converted.artist == "First / Second");
    agplayer::TranscodeConfig mp3;
    mp3.output_path = (directory / "qmc-converted.mp3").u8string();
    mp3.codec_name = "libmp3lame";
    std::string mp3_error;
    assert(agplayer::transcode((fixtures / "synthetic.mgg").u8string(),
                               mp3, nullptr, nullptr, mp3_error) == AG_OK);
    agplayer::MediaMetadata mp3_metadata;
    assert(agplayer::probe_media_metadata(mp3.output_path,
                                          mp3_metadata) == AG_OK);
    assert(mp3_metadata.has_audio && mp3_metadata.duration_ms > 0);
    const fs::path missing_key = directory / "missing-key.mflac";
    {
        std::ofstream output(missing_key, std::ios::binary);
        output << "synthetic audio without an embedded keySTag";
    }
    std::string missing_key_error;
    assert(!agplayer::ProprietaryAudioInput::open(
        missing_key.u8string(), missing_key_error));
    assert(missing_key_error.find("no embedded key") != std::string::npos);
    const fs::path raw_aac = directory / "raw-adts.kwm";
    std::array<std::uint8_t, 64> adts{};
    adts[0] = 0xff;
    adts[1] = 0xf1;
    {
        std::ofstream output(raw_aac, std::ios::binary);
        output.write(reinterpret_cast<const char*>(adts.data()), adts.size());
    }
    std::string raw_error;
    auto passthrough = agplayer::ProprietaryAudioInput::open(
        raw_aac.u8string(), raw_error);
    assert(passthrough && raw_error.empty());
    std::array<std::uint8_t, 64> raw_output{};
    assert(passthrough->read(raw_output.data(),
                             static_cast<int>(raw_output.size()))
           == static_cast<int>(adts.size()));
    assert(raw_output == adts);
    passthrough.reset();
    fs::remove_all(directory);
}

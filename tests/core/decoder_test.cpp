#include <agplayer/c_api.h>

#include "decoder.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

int main(const int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path sine_path = argv[1];

    ag_metadata* metadata = reinterpret_cast<ag_metadata*>(
        static_cast<std::uintptr_t>(1U));
    assert(ag_metadata_open(nullptr, &metadata) == AG_INVALID_ARGUMENT);
    assert(metadata == nullptr);
    assert(ag_metadata_open(argv[1], nullptr) == AG_INVALID_ARGUMENT);

    assert(ag_metadata_open(argv[1], &metadata) == AG_OK);
    assert(metadata != nullptr);
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

    agplayer::Decoder decoder;
    assert(decoder.open(sine_path.string()) == AG_OK);
    agplayer::DecodedAudioBlock block;
    assert(decoder.read(block) == AG_OK);
    assert(block.frames > 0U);
    assert(block.samples.size() == block.frames * 2U);

    assert(decoder.seek(1'500) == AG_OK);
    do {
        assert(decoder.read(block) == AG_OK);
    } while (block.frames == 0U && !block.end_of_stream);
    assert(block.frames > 0U);
    assert(block.timestamp_ms >= 1'490);

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
}

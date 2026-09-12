#include "audio_editor/audio_document.hpp"
#include "audio_editor/document_writer.hpp"
#include "decoder.hpp"

#include <agplayer/c_api.h>

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <vector>

namespace {

[[noreturn]] void fail(const char* message)
{
    std::cerr << message << '\n';
    std::exit(1);
}

void require(const bool condition, const char* message)
{
    if (!condition) {
        fail(message);
    }
}

std::vector<unsigned char> read_bytes(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
}

agplayer::editor::SampleFrame decoded_frames(
    const std::filesystem::path& path,
    const int sample_rate,
    const int channels)
{
    agplayer::Decoder decoder;
    require(decoder.open(path.u8string(), sample_rate, channels) == AG_OK,
            "decoder open failed");
    agplayer::DecodedAudioBlock block;
    agplayer::editor::SampleFrame frames = 0;
    for (;;) {
        require(decoder.read(block) == AG_OK, "decoder read failed");
        frames += static_cast<agplayer::editor::SampleFrame>(block.frames);
        if (block.end_of_stream) {
            break;
        }
    }
    return frames;
}

} // namespace

int main(const int argc, char** argv)
{
    require(argc == 2, "fixture argument required");
    namespace fs = std::filesystem;
    using namespace agplayer::editor;

    const fs::path input = fs::u8path(argv[1]);
    agplayer::Decoder probe;
    require(probe.open(input.u8string()) == AG_OK, "fixture probe failed");
    const agplayer::MediaMetadata metadata = probe.metadata();
    probe.close();
    const SampleFrame total = decoded_frames(
        input, metadata.sample_rate, metadata.channels);
    const AudioDocument document = AudioDocument::fromSource(AudioSource{
        input, static_cast<std::uint32_t>(metadata.sample_rate),
        static_cast<std::uint32_t>(metadata.channels), total});

    const fs::path protected_output = input.parent_path() / "writer-protected.wav";
    {
        std::ofstream stream(protected_output, std::ios::binary);
        stream << "original-data";
    }
    const auto before = read_bytes(protected_output);
    WriteRequest invalid;
    invalid.snapshot = document.timelineSnapshot();
    invalid.output_path = protected_output;
    invalid.codec_name = "encoder-that-does-not-exist";
    DocumentWriter writer;
    const WriteResult rejected = writer.write(invalid);
    require(rejected.error == WriteError::UnsupportedEncoder,
            "unsupported encoder was accepted");
    require(read_bytes(protected_output) == before,
            "failed validation changed original");

    const fs::path selection_output = input.parent_path() / "writer-selection.wav";
    fs::remove(selection_output);
    WriteRequest selection;
    selection.snapshot = document.timelineSnapshot();
    selection.output_path = selection_output;
    selection.range = Selection{100, 1'100};
    const WriteResult written = writer.write(selection);
    if (!written.ok()) {
        std::cerr << written.message << '\n';
    }
    require(written.ok(), "selection write failed");
    require(decoded_frames(selection_output,
                           metadata.sample_rate,
                           metadata.channels) == 1'000,
            "selection frame count mismatch");

    const fs::path parameter_output = input.parent_path() / "writer-parameters.flac";
    fs::remove(parameter_output);
    WriteRequest parameters;
    parameters.snapshot = document.timelineSnapshot();
    parameters.output_path = parameter_output;
    parameters.codec_name = "flac";
    parameters.sample_rate = 48'000;
    parameters.channels = 1;
    parameters.range = Selection{0, 44'100};
    const WriteResult parameter_result = writer.write(parameters);
    if (!parameter_result.ok()) std::cerr << parameter_result.message << '\n';
    require(parameter_result.ok(), "parameterized write failed");
    agplayer::MediaMetadata parameter_metadata;
    require(agplayer::probe_media_metadata(
                parameter_output.u8string(), parameter_metadata) == AG_OK,
            "parameterized output probe failed");
    require(parameter_metadata.sample_rate == 48'000,
            "requested export sample rate was not applied");
    require(parameter_metadata.channels == 1,
            "requested export channel count was not applied");

    struct OutputCase final {
        const char* name;
        const char* codec;
        bool exact_decoded_frames;
    };
    for (const OutputCase output_case : {
             OutputCase{"writer-matrix.wav", "pcm_s24le", true},
             OutputCase{"writer-matrix.flac", "flac", true},
             OutputCase{"writer-matrix.mp3", "libmp3lame", true},
             OutputCase{"writer-matrix.m4a", "aac", false},
             OutputCase{"writer-matrix.ogg", "libvorbis", false}}) {
        const fs::path output = input.parent_path() / output_case.name;
        fs::remove(output);
        WriteRequest matrix;
        matrix.snapshot = document.timelineSnapshot();
        matrix.output_path = output;
        matrix.codec_name = output_case.codec;
        matrix.range = Selection{0, 44'100};
        const WriteResult result = writer.write(matrix);
        if (!result.ok()) std::cerr << output_case.name << ": "
                                    << result.message << '\n';
        require(result.ok(), "format matrix write failed");
        if (output_case.exact_decoded_frames) {
            require(decoded_frames(output, metadata.sample_rate,
                                   metadata.channels) == 44'100,
                    "format matrix frame count mismatch");
        } else {
            agplayer::MediaMetadata written_metadata;
            require(agplayer::probe_media_metadata(
                        output.u8string(), written_metadata) == AG_OK,
                    "format matrix probe failed");
            require(written_metadata.duration_ms == 1'000,
                    "format matrix duration mismatch");
        }
        fs::remove(output);
    }
    fs::remove(selection_output);
    fs::remove(parameter_output);
    fs::remove(protected_output);
    return 0;
}

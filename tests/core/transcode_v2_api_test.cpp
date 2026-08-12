#undef NDEBUG

#include <agplayer/c_api.h>

#include <cassert>
#include <filesystem>
#include <iostream>

int main(const int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path input = argv[1];
    const std::filesystem::path output =
        input.parent_path() / "transcode-v2.flac";
    std::filesystem::remove(output);

    ag_transcode_request_v2 invalid{};
    invalid.struct_size = sizeof(invalid);
    invalid.api_version = 99;
    invalid.output_path = output.u8string().c_str();
    assert(ag_transcode_v2(input.u8string().c_str(), &invalid, nullptr,
                           nullptr, nullptr) == AG_INVALID_ARGUMENT);

    const std::string output_utf8 = output.u8string();
    ag_transcode_request_v2 request{};
    request.struct_size = sizeof(request);
    request.api_version = AG_TRANSCODE_REQUEST_V2_VERSION;
    request.output_path = output_utf8.c_str();
    request.muxer_name = "flac";
    request.codec_name = "flac";
    request.sample_rate = 48000;
    request.channel_layout = "mono";
    request.sample_format = "s16";
    request.audio_stream_index = 0;
    request.keep_metadata = 1;
    request.bitrate_mode = 0;
    request.quality = 75;

    const std::string input_utf8 = input.u8string();
    const ag_result result = ag_transcode_v2(
        input_utf8.c_str(), &request, nullptr, nullptr, nullptr);
    if (result != AG_OK) {
        std::cerr << "ag_transcode_v2 failed: " << ag_last_error() << '\n';
    }
    assert(result == AG_OK);
    assert(std::filesystem::exists(output));
    assert(std::filesystem::file_size(output) > 0);

    std::filesystem::remove(output);
    return 0;
}

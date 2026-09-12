#undef NDEBUG

#include <agplayer/c_api.h>

#include <algorithm>
#include <cassert>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::uint32_t decode_syncsafe(const unsigned char* value)
{
    return (static_cast<std::uint32_t>(value[0] & 0x7fU) << 21U)
        | (static_cast<std::uint32_t>(value[1] & 0x7fU) << 14U)
        | (static_cast<std::uint32_t>(value[2] & 0x7fU) << 7U)
        | static_cast<std::uint32_t>(value[3] & 0x7fU);
}

void encode_syncsafe(const std::uint32_t value, unsigned char* output)
{
    output[0] = static_cast<unsigned char>((value >> 21U) & 0x7fU);
    output[1] = static_cast<unsigned char>((value >> 14U) & 0x7fU);
    output[2] = static_cast<unsigned char>((value >> 7U) & 0x7fU);
    output[3] = static_cast<unsigned char>(value & 0x7fU);
}

std::uint32_t decode_be32(const unsigned char* value)
{
    return (static_cast<std::uint32_t>(value[0]) << 24U)
        | (static_cast<std::uint32_t>(value[1]) << 16U)
        | (static_cast<std::uint32_t>(value[2]) << 8U)
        | static_cast<std::uint32_t>(value[3]);
}

// FFmpeg writes an ID3v2 APIC frame for MP3 cover art. Duplicate that frame
// with a different picture type so the source has two independently visible
// attached pictures without relying on an external command-line tool.
void duplicate_mp3_apic_frame(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input);
    std::vector<unsigned char> bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    assert(bytes.size() > 10U);
    assert(bytes[0] == 'I' && bytes[1] == 'D' && bytes[2] == '3');
    assert(bytes[3] == 3U || bytes[3] == 4U);

    const std::size_t tag_size = decode_syncsafe(bytes.data() + 6U);
    assert(tag_size + 10U <= bytes.size());
    std::size_t frame_offset = 10U;
    while (frame_offset + 10U <= tag_size + 10U) {
        if (bytes[frame_offset] == 0U) break;
        const std::size_t frame_size = bytes[3] == 4U
            ? decode_syncsafe(bytes.data() + frame_offset + 4U)
            : decode_be32(bytes.data() + frame_offset + 4U);
        assert(frame_offset + 10U + frame_size <= tag_size + 10U);
        if (bytes[frame_offset] == 'A' && bytes[frame_offset + 1U] == 'P'
            && bytes[frame_offset + 2U] == 'I'
            && bytes[frame_offset + 3U] == 'C') {
            std::vector<unsigned char> duplicate(
                bytes.begin() + static_cast<std::ptrdiff_t>(frame_offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(
                    frame_offset + 10U + frame_size));
            const auto mime_end = std::find(
                duplicate.begin() + 11, duplicate.end(), 0U);
            assert(mime_end != duplicate.end());
            const auto picture_type = mime_end + 1;
            assert(picture_type != duplicate.end());
            *picture_type = 4U; // back cover; original is front cover (3)
            bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(
                             tag_size + 10U),
                         duplicate.begin(), duplicate.end());
            encode_syncsafe(static_cast<std::uint32_t>(
                                tag_size + duplicate.size()),
                            bytes.data() + 6U);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            assert(output);
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
            assert(output);
            return;
        }
        frame_offset += 10U + frame_size;
    }
    assert(false && "MP3 fixture did not contain an APIC frame");
}

} // namespace

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

    std::array<ag_metadata_field_edit, 3> metadata_edits{{
        {AG_METADATA_FIELD_TITLE, AG_METADATA_EDIT_SET, u8"转换标题"},
        {AG_METADATA_FIELD_YEAR, AG_METADATA_EDIT_SET, "2026"},
        {AG_METADATA_FIELD_DATE, AG_METADATA_EDIT_SET, "2026-08-20"},
    }};
    request.metadata_fields = metadata_edits.data();
    request.metadata_field_count = metadata_edits.size();

    constexpr std::array<unsigned char, 67> cover{{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
        0x89, 0x00, 0x00, 0x00, 0x0a, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x9c, 0x63, 0x00, 0x01, 0x00, 0x00,
        0x05, 0x00, 0x01, 0x0d, 0x0a, 0x2d, 0xb4, 0x00,
        0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae,
        0x42, 0x60, 0x82}};
    request.metadata_cover_action = AG_METADATA_COVER_SET;
    request.metadata_cover_data = cover.data();
    request.metadata_cover_size = cover.size();
    request.metadata_cover_mime_type = "image/png";

    const std::string input_utf8 = input.u8string();
    const ag_result result = ag_transcode_v2(
        input_utf8.c_str(), &request, nullptr, nullptr, nullptr);
    if (result != AG_OK) {
        std::cerr << "ag_transcode_v2 failed: " << ag_last_error() << '\n';
    }
    assert(result == AG_OK);
    assert(std::filesystem::exists(output));
    assert(std::filesystem::file_size(output) > 0);

    ag_metadata* metadata = nullptr;
    assert(ag_metadata_open(output_utf8.c_str(), &metadata) == AG_OK);
    assert(std::string(ag_metadata_title(metadata)) == u8"转换标题");
    assert(std::string(ag_metadata_year(metadata)) == "2026");
    assert(std::string(ag_metadata_date(metadata)) == "2026-08-20");
    size_t cover_size = 0;
    const char* cover_mime = nullptr;
    assert(ag_metadata_cover(metadata, &cover_size, &cover_mime) != nullptr);
    assert(cover_size == cover.size());
    assert(std::string(cover_mime) == "image/png");
    ag_metadata_destroy(metadata);

    // The extended API has separate logical Year and Date fields. Supplying
    // the date argument must not overwrite Year.
    assert(ag_metadata_write_extended(
               output_utf8.c_str(), nullptr, nullptr, nullptr, nullptr,
               "2025-02-03", nullptr, nullptr, nullptr, nullptr, nullptr,
               nullptr, nullptr, nullptr, nullptr, nullptr, 0U, nullptr)
           == AG_OK);
    metadata = nullptr;
    assert(ag_metadata_open(output_utf8.c_str(), &metadata) == AG_OK);
    assert(std::string(ag_metadata_year(metadata)) == "2026");
    assert(std::string(ag_metadata_date(metadata)) == "2025-02-03");
    ag_metadata_destroy(metadata);

    // A caller compiled against the original v2 prefix (through quality) must
    // remain ABI-compatible after metadata fields were appended.
    struct LegacyTranscodeRequestV2 {
        size_t struct_size;
        std::uint32_t api_version;
        const char* output_path;
        const char* muxer_name;
        const char* codec_name;
        long long bit_rate;
        int sample_rate;
        const char* channel_layout;
        const char* sample_format;
        int audio_stream_index;
        int keep_metadata;
        int keep_cover;
        int bitrate_mode;
        int quality;
    };
    const std::filesystem::path legacy_output =
        input.parent_path() / "transcode-v2-legacy-prefix.flac";
    std::filesystem::remove(legacy_output);
    const std::string legacy_utf8 = legacy_output.u8string();
    LegacyTranscodeRequestV2 legacy{};
    legacy.struct_size = sizeof(legacy);
    legacy.api_version = AG_TRANSCODE_REQUEST_V2_VERSION;
    legacy.output_path = legacy_utf8.c_str();
    legacy.muxer_name = "flac";
    legacy.codec_name = "flac";
    legacy.sample_rate = 44'100;
    legacy.channel_layout = "stereo";
    legacy.audio_stream_index = 0;
    legacy.quality = 75;
    assert(ag_transcode_v2(
               input_utf8.c_str(),
               reinterpret_cast<const ag_transcode_request_v2*>(&legacy),
               nullptr, nullptr, nullptr)
           == AG_OK);
    assert(std::filesystem::exists(legacy_output));

    // The legacy writer's single temporal argument is Year. Keeping that API
    // source- and ABI-compatible must not redirect the value to Date now that
    // the extended writer exposes Date independently.
    assert(ag_metadata_write(
               legacy_utf8.c_str(), nullptr, nullptr, nullptr, "2042", nullptr,
               nullptr, nullptr, 0U, nullptr)
           == AG_OK);
    metadata = nullptr;
    assert(ag_metadata_open(legacy_utf8.c_str(), &metadata) == AG_OK);
    assert(std::string(ag_metadata_year(metadata)) == "2042");
    assert(std::string(ag_metadata_date(metadata)).empty());
    ag_metadata_destroy(metadata);

    const std::filesystem::path unsupported_output =
        input.parent_path() / "transcode-v2-unsupported.aac";
    std::filesystem::remove(unsupported_output);
    const std::string unsupported_utf8 = unsupported_output.u8string();
    request.output_path = unsupported_utf8.c_str();
    request.muxer_name = "adts";
    request.codec_name = "aac";
    request.sample_format = nullptr;
    request.metadata_fields = nullptr;
    request.metadata_field_count = 0;
    request.metadata_cover_action = AG_METADATA_COVER_KEEP;
    request.metadata_cover_data = nullptr;
    request.metadata_cover_size = 0;
    request.metadata_cover_mime_type = nullptr;
    assert(ag_transcode_v2(input_utf8.c_str(), &request, nullptr,
                           nullptr, nullptr) == AG_OK);
    assert(std::filesystem::exists(unsupported_output));

    // MP4 accepts the PNG cover from the source FLAC. Confirm the positive
    // path still preserves it while unsupported containers are downgraded by
    // the earlier ADTS case.
    const std::filesystem::path incompatible_cover_output =
        input.parent_path() / "transcode-v2-incompatible-cover-output.m4a";
    std::filesystem::remove(incompatible_cover_output);
    const std::string incompatible_cover_output_utf8 =
        incompatible_cover_output.u8string();
    request.output_path = incompatible_cover_output_utf8.c_str();
    request.muxer_name = "mp4";
    request.codec_name = "aac";
    request.keep_metadata = 0;
    request.keep_cover = 1;
    request.metadata_cover_action = AG_METADATA_COVER_KEEP;
    request.metadata_cover_data = nullptr;
    request.metadata_cover_size = 0;
    request.metadata_cover_mime_type = nullptr;
    assert(ag_transcode_v2(output_utf8.c_str(), &request, nullptr,
                           nullptr, nullptr) == AG_OK);
    assert(std::filesystem::exists(incompatible_cover_output));
    metadata = nullptr;
    assert(ag_metadata_open(incompatible_cover_output_utf8.c_str(), &metadata)
           == AG_OK);
    cover_size = 0;
    cover_mime = nullptr;
    assert(ag_metadata_cover(metadata, &cover_size, &cover_mime) != nullptr);
    assert(cover_size > 0);
    ag_metadata_destroy(metadata);

    const std::filesystem::path date_conflict_output =
        input.parent_path() / "transcode-v2-date-conflict.mp3";
    std::filesystem::remove(date_conflict_output);
    const std::string date_conflict_utf8 = date_conflict_output.u8string();
    std::array<ag_metadata_field_edit, 2> date_conflict{{
        {AG_METADATA_FIELD_YEAR, AG_METADATA_EDIT_SET, "2026"},
        {AG_METADATA_FIELD_DATE, AG_METADATA_EDIT_SET, "2026-08-20"},
    }};
    request.output_path = date_conflict_utf8.c_str();
    request.muxer_name = "mp3";
    request.codec_name = "libmp3lame";
    request.metadata_fields = date_conflict.data();
    request.metadata_field_count = date_conflict.size();
    assert(ag_transcode_v2(input_utf8.c_str(), &request, nullptr,
                           nullptr, nullptr) == AG_UNSUPPORTED_FORMAT);
    assert(!std::filesystem::exists(date_conflict_output));

    // MP3 has one physical date field. Editing Year while Date is Keep (here,
    // omitted) would silently rewrite Date, so it must fail before encoding.
    const std::filesystem::path year_keep_date_output =
        input.parent_path() / "transcode-v2-year-keep-date.mp3";
    std::filesystem::remove(year_keep_date_output);
    const std::string year_keep_date_utf8 = year_keep_date_output.u8string();
    const std::array<ag_metadata_field_edit, 1> year_only{{
        {AG_METADATA_FIELD_YEAR, AG_METADATA_EDIT_SET, "2027"},
    }};
    request.output_path = year_keep_date_utf8.c_str();
    request.metadata_fields = year_only.data();
    request.metadata_field_count = year_only.size();
    assert(ag_transcode_v2(input_utf8.c_str(), &request, nullptr,
                           nullptr, nullptr) == AG_UNSUPPORTED_FORMAT);
    assert(std::string(ag_last_error()).find("Keep cannot preserve")
           != std::string::npos);
    assert(!std::filesystem::exists(year_keep_date_output));

    // A cover limitation must not prevent a usable audio conversion.
    const std::filesystem::path multi_cover_input =
        input.parent_path() / "transcode-v2-multi-cover-input.mp3";
    std::filesystem::remove(multi_cover_input);
    const std::string multi_cover_input_utf8 = multi_cover_input.u8string();
    request.output_path = multi_cover_input_utf8.c_str();
    request.metadata_fields = nullptr;
    request.metadata_field_count = 0;
    request.keep_metadata = 0;
    request.keep_cover = 0;
    request.metadata_cover_action = AG_METADATA_COVER_SET;
    request.metadata_cover_data = cover.data();
    request.metadata_cover_size = cover.size();
    request.metadata_cover_mime_type = "image/png";
    assert(ag_transcode_v2(input_utf8.c_str(), &request, nullptr,
                           nullptr, nullptr) == AG_OK);
    duplicate_mp3_apic_frame(multi_cover_input);

    const std::filesystem::path multi_cover_output =
        input.parent_path() / "transcode-v2-multi-cover-output.mp3";
    std::filesystem::remove(multi_cover_output);
    const std::string multi_cover_output_utf8 = multi_cover_output.u8string();
    request.output_path = multi_cover_output_utf8.c_str();
    request.keep_cover = 1;
    request.metadata_cover_action = AG_METADATA_COVER_KEEP;
    request.metadata_cover_data = nullptr;
    request.metadata_cover_size = 0;
    request.metadata_cover_mime_type = nullptr;
    assert(ag_transcode_v2(multi_cover_input_utf8.c_str(), &request, nullptr,
                           nullptr, nullptr) == AG_OK);
    assert(std::filesystem::exists(multi_cover_output));

    std::filesystem::remove(output);
    std::filesystem::remove(output_utf8 + ".agbak");
    std::filesystem::remove(legacy_output);
    std::filesystem::remove(legacy_utf8 + ".agbak");
    std::filesystem::remove(multi_cover_input);
    std::filesystem::remove(multi_cover_output);
    std::filesystem::remove(unsupported_output);
    std::filesystem::remove(incompatible_cover_output);
    return 0;
}

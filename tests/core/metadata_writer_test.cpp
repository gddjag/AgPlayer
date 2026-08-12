#undef NDEBUG
#include <agplayer/c_api.h>
#include "metadata_writer.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {

constexpr unsigned char kOnePixelImage[] = {
    0x42, 0x4d, 0x3a, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x13, 0x0b,
    0x00, 0x00, 0x13, 0x0b, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0x00};

void assert_cover(const std::filesystem::path& path, const bool expected)
{
    ag_metadata* metadata = nullptr;
    assert(ag_metadata_open(path.u8string().c_str(), &metadata) == AG_OK);
    assert(metadata != nullptr);
    size_t cover_size = 0;
    const char* mime_type = nullptr;
    const unsigned char* cover =
        ag_metadata_cover(metadata, &cover_size, &mime_type);
    if (expected) {
        assert(cover != nullptr);
        assert(cover_size == sizeof(kOnePixelImage));
        assert(std::memcmp(cover, kOnePixelImage, cover_size) == 0);
        assert(mime_type != nullptr);
    } else {
        assert(cover == nullptr);
        assert(cover_size == 0);
    }
    ag_metadata_destroy(metadata);

}

} // namespace

int main(const int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path fixture = argv[1];
    const std::filesystem::path work_dir = fixture.parent_path();

    // The canonical plan is the sole core write contract: Set is read back
    // from the replacement file, while Keep leaves unrelated tags untouched.
    const std::filesystem::path plan_src = work_dir / "meta-plan-src.wav";
    std::filesystem::copy_file(fixture, plan_src,
                               std::filesystem::copy_options::overwrite_existing);
    agplayer::MetadataEditPlan write_plan;
    write_plan.fields = {
        {agplayer::CanonicalField::Title, agplayer::MetadataAction::Set,
         "Plan Title"},
        {agplayer::CanonicalField::Artist, agplayer::MetadataAction::Set,
         "Plan Artist"},
    };
    agplayer::MetadataFileResult plan_result;
    assert(agplayer::write_metadata_plan(plan_src.u8string(), write_plan,
                                         plan_result) == AG_OK);
    assert(plan_result.final_status == agplayer::FileResultStatus::Completed);
    assert(plan_result.used_stream_copy);
    assert(plan_result.audio_verified_unchanged);
    ag_metadata* metadata = nullptr;
    assert(ag_metadata_open(plan_src.u8string().c_str(), &metadata) == AG_OK);
    assert(std::strcmp(ag_metadata_title(metadata), "Plan Title") == 0);
    assert(std::strcmp(ag_metadata_artist(metadata), "Plan Artist") == 0);
    ag_metadata_destroy(metadata);

    // A blank Set must never be silently converted to Clear, and BPM must be
    // a finite positive number before any temporary output is created.
    agplayer::MetadataEditPlan invalid_plan;
    invalid_plan.fields = {
        {agplayer::CanonicalField::Title, agplayer::MetadataAction::Set, ""},
    };
    std::string validation_error;
    assert(!agplayer::validate_metadata_edit_plan(invalid_plan, validation_error));
    assert(!validation_error.empty());

    invalid_plan.fields = {
        {agplayer::CanonicalField::Bpm, agplayer::MetadataAction::Set, "NaN"},
    };
    assert(!agplayer::validate_metadata_edit_plan(invalid_plan, validation_error));

    agplayer::MetadataEditPlan valid_plan;
    valid_plan.fields = {
        {agplayer::CanonicalField::Title, agplayer::MetadataAction::Keep, std::nullopt},
        {agplayer::CanonicalField::Artist, agplayer::MetadataAction::Set, "Ag Music"},
        {agplayer::CanonicalField::Bpm, agplayer::MetadataAction::Clear, std::nullopt},
    };
    assert(agplayer::validate_metadata_edit_plan(valid_plan, validation_error));
    assert(agplayer::preflight_metadata_edit(fixture.u8string(), valid_plan,
                                             validation_error) == AG_OK);

    const std::filesystem::path src = work_dir / "meta-write-src.wav";
    std::filesystem::copy_file(fixture, src,
                               std::filesystem::copy_options::overwrite_existing);

    const ag_result result = ag_metadata_write(
        src.u8string().c_str(),
        "New Title", "New Artist", "New Album",
        "2024", "Rock", nullptr,
        nullptr, 0U, nullptr);
    assert(result == AG_OK);

    metadata = nullptr;
    assert(ag_metadata_open(src.u8string().c_str(), &metadata) == AG_OK);
    assert(metadata != nullptr);
    assert(std::strcmp(ag_metadata_title(metadata), "New Title") == 0);
    assert(std::strcmp(ag_metadata_artist(metadata), "New Artist") == 0);
    ag_metadata_destroy(metadata);

    const std::filesystem::path backup = src.string() + ".agbak";
    assert(std::filesystem::exists(backup));

    const std::filesystem::path mp3 = work_dir / "meta-cover.mp3";
    assert(ag_transcode(fixture.u8string().c_str(), mp3.u8string().c_str(),
                        "libmp3lame", 192000, 44100, 2,
                        nullptr, nullptr, nullptr) == AG_OK);
    assert(ag_metadata_write_extended(
               mp3.u8string().c_str(), nullptr, nullptr, nullptr,
               "Album Artist", "2025-08-09", nullptr, "3/12", "1/2",
               "Composer", "Comment", "128.50", "Copyright", "AgPlayer",
               "Lyrics", nullptr, 0U, nullptr) == AG_OK);
    assert(ag_metadata_open(mp3.u8string().c_str(), &metadata) == AG_OK);
    assert(std::strcmp(ag_metadata_album_artist(metadata), "Album Artist") == 0);
    assert(std::strcmp(ag_metadata_track(metadata), "3/12") == 0);
    assert(std::strcmp(ag_metadata_disc(metadata), "1/2") == 0);
    assert(std::strcmp(ag_metadata_composer(metadata), "Composer") == 0);
    assert(std::strcmp(ag_metadata_comment(metadata), "Comment") == 0);
    assert(std::strcmp(ag_metadata_bpm_tag(metadata), "128.50") == 0);
    assert(std::strcmp(ag_metadata_copyright(metadata), "Copyright") == 0);
    assert(std::strcmp(ag_metadata_encoder(metadata), "AgPlayer") == 0);
    ag_metadata_destroy(metadata);

    agplayer::MetadataEditPlan cover_plan;
    cover_plan.cover_action = agplayer::CoverAction::Set;
    cover_plan.cover_data = kOnePixelImage;
    cover_plan.cover_size = sizeof(kOnePixelImage);
    cover_plan.cover_mime_type = "image/bmp";
    agplayer::MetadataFileResult cover_write_result;
    const ag_result cover_result = agplayer::write_metadata_plan(
        mp3.u8string(), cover_plan, cover_write_result);
    if (cover_result != AG_OK) {
        std::fprintf(stderr, "cover write failed: %d\n",
                     static_cast<int>(cover_result));
    }
    assert(cover_result == AG_OK);
    assert(cover_write_result.final_status
           == agplayer::FileResultStatus::Completed);
    assert_cover(mp3, true);

    // A tag-only update must preserve the existing attached picture.
    assert(ag_metadata_write(
               mp3.u8string().c_str(), "Cover Preserved", nullptr, nullptr,
               nullptr, nullptr, nullptr, nullptr, 0U, nullptr) == AG_OK);
    assert_cover(mp3, true);

    // A non-null pointer with a zero size is the explicit clear operation.
    const unsigned char clear_marker = 0;
    assert(ag_metadata_write(
               mp3.u8string().c_str(), nullptr, nullptr, nullptr, nullptr,
               nullptr, nullptr, &clear_marker, 0U, nullptr) == AG_OK);
    assert_cover(mp3, false);

    std::filesystem::remove(src);
    std::filesystem::remove(plan_src);
    std::filesystem::remove(backup);
    std::filesystem::remove(mp3);
    std::filesystem::remove(mp3.string() + ".agbak");
    return 0;
}

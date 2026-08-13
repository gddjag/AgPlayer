#undef NDEBUG
#include <agplayer/c_api.h>
#include "metadata_writer.hpp"
#include <atomic>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <array>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

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

std::vector<unsigned char> file_bytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
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
    assert(plan_result.runtime.packets_copied > 0);
    assert(plan_result.runtime.decoder_open_count == 0);
    assert(plan_result.runtime.encoder_open_count == 0);
    assert(plan_result.runtime.audio_streams_before
           == plan_result.runtime.audio_streams_after);
    assert(plan_result.runtime.chapters_before
           == plan_result.runtime.chapters_after);
    assert(plan_result.runtime.attachments_before
           == plan_result.runtime.attachments_after);
    assert(plan_result.fields.size() == 2);
    assert(plan_result.fields[0].before_value.empty());
    assert(plan_result.fields[0].requested_value == "Plan Title");
    assert(plan_result.fields[0].actual_value == "Plan Title");
    assert(plan_result.fields[0].status == agplayer::FieldWriteStatus::Updated);
    ag_metadata* metadata = nullptr;
    assert(ag_metadata_open(plan_src.u8string().c_str(), &metadata) == AG_OK);
    assert(std::strcmp(ag_metadata_title(metadata), "Plan Title") == 0);
    assert(std::strcmp(ag_metadata_artist(metadata), "Plan Artist") == 0);
    ag_metadata_destroy(metadata);

    const auto& title_aliases =
        agplayer::known_metadata_aliases(agplayer::CanonicalField::Title);
    const auto& album_artist_aliases =
        agplayer::known_metadata_aliases(agplayer::CanonicalField::AlbumArtist);
    assert(std::find(title_aliases.begin(), title_aliases.end(),
                     std::string("TIT2")) != title_aliases.end());
    assert(std::find(album_artist_aliases.begin(), album_artist_aliases.end(),
                     std::string("WM/AlbumArtist")) != album_artist_aliases.end());

    // Every injected failure point must preserve the source byte-for-byte and
    // remove both the stage and any uncommitted backup.
    struct FailureCase {
        agplayer::MetadataFailurePoint point;
        agplayer::MetadataErrorCode code;
    };
    constexpr std::array<FailureCase, 9> failure_cases{{
        {agplayer::MetadataFailurePoint::HeaderWrite,
         agplayer::MetadataErrorCode::HeaderWriteFailed},
        {agplayer::MetadataFailurePoint::PacketRead,
         agplayer::MetadataErrorCode::PacketReadFailed},
        {agplayer::MetadataFailurePoint::PacketWrite,
         agplayer::MetadataErrorCode::PacketWriteFailed},
        {agplayer::MetadataFailurePoint::TrailerWrite,
         agplayer::MetadataErrorCode::TrailerWriteFailed},
        {agplayer::MetadataFailurePoint::Reprobe,
         agplayer::MetadataErrorCode::VerificationFailed},
        {agplayer::MetadataFailurePoint::Verification,
         agplayer::MetadataErrorCode::VerificationFailed},
        {agplayer::MetadataFailurePoint::SourceChanged,
         agplayer::MetadataErrorCode::SourceChanged},
        {agplayer::MetadataFailurePoint::AtomicReplace,
         agplayer::MetadataErrorCode::AtomicReplaceFailed},
        {agplayer::MetadataFailurePoint::PostReplaceReadback,
         agplayer::MetadataErrorCode::VerificationFailed},
    }};
    int failure_index = 0;
    for (const FailureCase& failure : failure_cases) {
        const std::filesystem::path failure_path = work_dir
            / ("meta-injected-" + std::to_string(failure_index++) + ".wav");
        std::filesystem::copy_file(fixture, failure_path,
            std::filesystem::copy_options::overwrite_existing);
        const auto before_failure = file_bytes(failure_path);
        agplayer::MetadataWriterTestHooks hooks{failure.point};
        agplayer::MetadataFileResult failure_result;
        assert(agplayer::write_metadata_plan(failure_path.u8string(), write_plan,
                                             failure_result, nullptr, &hooks)
               != AG_OK);
        assert(failure_result.final_status
               == agplayer::FileResultStatus::Failed);
        assert(failure_result.error_code == failure.code);
        assert(file_bytes(failure_path) == before_failure);
        assert(!std::filesystem::exists(failure_path.u8string() + ".agbak"));
        for (const auto& item : std::filesystem::directory_iterator(work_dir)) {
            assert(item.path().filename().u8string().find(".agmeta-stage-")
                   == std::string::npos);
        }
        std::filesystem::remove(failure_path);
    }

    // Metadata writes must use the same UTF-8 path contract as the QML UI.
    // This catches a Windows-only regression where staging used an ANSI path.
    const std::filesystem::path unicode_plan_src = work_dir / std::filesystem::u8path(
        u8"\u5143\u6570\u636e-\u4e2d\u6587\u8def\u5f84.wav");
    const std::filesystem::path unicode_plan_backup = std::filesystem::path(
        unicode_plan_src.native() + std::filesystem::u8path(".agbak").native());
    std::filesystem::copy_file(fixture, unicode_plan_src,
                               std::filesystem::copy_options::overwrite_existing);
    agplayer::MetadataFileResult unicode_plan_result;
    const ag_result unicode_plan_write = agplayer::write_metadata_plan(
        unicode_plan_src.u8string(), write_plan, unicode_plan_result);
    if (unicode_plan_write != AG_OK) {
        std::fprintf(stderr, "unicode metadata write failed: %d (%s)\n",
                     static_cast<int>(unicode_plan_write),
                     unicode_plan_result.message.c_str());
    }
    assert(unicode_plan_write == AG_OK);
    assert(unicode_plan_result.final_status
           == agplayer::FileResultStatus::Completed);
    metadata = nullptr;
    assert(ag_metadata_open(unicode_plan_src.u8string().c_str(), &metadata) == AG_OK);
    assert(std::strcmp(ag_metadata_title(metadata), "Plan Title") == 0);
    ag_metadata_destroy(metadata);

    // A blank Set must never be silently converted to Clear, and BPM must be
    // a finite positive number before any temporary output is created.
    agplayer::MetadataEditPlan invalid_plan;
    std::string validation_error;
    assert(!agplayer::validate_metadata_edit_plan(invalid_plan, validation_error));
    assert(validation_error.find("At least one") != std::string::npos);
    invalid_plan.fields = {
        {agplayer::CanonicalField::Title, agplayer::MetadataAction::Set, ""},
    };
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
        {agplayer::CanonicalField::Bpm, agplayer::MetadataAction::Keep, std::nullopt},
    };
    assert(agplayer::validate_metadata_edit_plan(valid_plan, validation_error));
    agplayer::MetadataPreflightReport preflight_report;
    assert(agplayer::preflight_metadata_edit(fixture.u8string(), valid_plan,
                                             preflight_report) == AG_OK);
    assert(preflight_report.supported);
    assert(preflight_report.error_code == agplayer::MetadataErrorCode::None);
    assert(preflight_report.container.size() > 0);

#ifdef _WIN32
    const std::filesystem::path readonly_src = work_dir / "meta-readonly.wav";
    std::filesystem::copy_file(fixture, readonly_src,
        std::filesystem::copy_options::overwrite_existing);
    assert(SetFileAttributesW(readonly_src.c_str(), FILE_ATTRIBUTE_READONLY) != 0);
    agplayer::MetadataPreflightReport readonly_report;
    assert(agplayer::preflight_metadata_edit(readonly_src.u8string(), valid_plan,
                                             readonly_report) == AG_IO_ERROR);
    assert(readonly_report.error_code == agplayer::MetadataErrorCode::ReadOnlyFile);
    assert(SetFileAttributesW(readonly_src.c_str(), FILE_ATTRIBUTE_NORMAL) != 0);
    std::filesystem::remove(readonly_src);

    const std::filesystem::path locked_src = work_dir / "meta-locked.wav";
    std::filesystem::copy_file(fixture, locked_src,
        std::filesystem::copy_options::overwrite_existing);
    HANDLE locked_handle = CreateFileW(locked_src.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(locked_handle != INVALID_HANDLE_VALUE);
    agplayer::MetadataPreflightReport locked_report;
    assert(agplayer::preflight_metadata_edit(locked_src.u8string(), valid_plan,
                                             locked_report) == AG_IO_ERROR);
    assert(locked_report.error_code == agplayer::MetadataErrorCode::FileInUse);
    CloseHandle(locked_handle);
    std::filesystem::remove(locked_src);
#endif

    std::atomic_bool cancelled{true};
    const std::filesystem::path cancelled_src = work_dir / "meta-cancelled.wav";
    std::filesystem::copy_file(fixture, cancelled_src,
                               std::filesystem::copy_options::overwrite_existing);
    const auto cancelled_size = std::filesystem::file_size(cancelled_src);
    agplayer::MetadataFileResult cancelled_result;
    assert(agplayer::write_metadata_plan(cancelled_src.u8string(), write_plan,
                                         cancelled_result, &cancelled)
           == AG_CANCELLED);
    assert(cancelled_result.final_status
           == agplayer::FileResultStatus::Cancelled);
    assert(cancelled_result.error_code
           == agplayer::MetadataErrorCode::Cancelled);
    assert(std::filesystem::file_size(cancelled_src) == cancelled_size);
    assert(!std::filesystem::exists(cancelled_src.u8string() + ".agbak"));

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
    std::filesystem::remove(mp3);
    std::filesystem::remove(mp3.u8string() + ".agbak");
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
    assert(cover_write_result.cover.requested_action
           == agplayer::CoverAction::Set);
    assert(cover_write_result.cover.status
           == agplayer::FieldWriteStatus::Updated);
    assert(cover_write_result.cover.has_cover);
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

    struct MatrixEntry {
        const char* extension;
        const char* codec;
        bool cover_expected;
    };
    constexpr std::array<MatrixEntry, 7> matrix{{
        {"wav", nullptr, false},
        {"mp3", "libmp3lame", true},
        {"flac", "flac", true},
        {"ogg", "vorbis", false},
        {"opus", "opus", false},
        {"m4a", "aac", true},
        {"wma", "wmav2", false},
    }};
    agplayer::MetadataEditPlan full_field_plan;
    full_field_plan.fields = {
        {agplayer::CanonicalField::Title, agplayer::MetadataAction::Set,
         "Unicode Title"},
        {agplayer::CanonicalField::Artist, agplayer::MetadataAction::Set,
         "Unicode Artist"},
        {agplayer::CanonicalField::Album, agplayer::MetadataAction::Set,
         "Unicode Album"},
        {agplayer::CanonicalField::AlbumArtist, agplayer::MetadataAction::Set,
         "Unicode Album Artist"},
        {agplayer::CanonicalField::Genre, agplayer::MetadataAction::Set,
         "Unicode Genre"},
        {agplayer::CanonicalField::Year, agplayer::MetadataAction::Set, "2026"},
        {agplayer::CanonicalField::Composer, agplayer::MetadataAction::Set,
         "Unicode Composer"},
        {agplayer::CanonicalField::Bpm, agplayer::MetadataAction::Set, "128.5"},
    };
    for (const MatrixEntry& entry : matrix) {
        const std::filesystem::path path = work_dir
            / (std::string("meta-matrix.") + entry.extension);
        std::filesystem::remove(path);
        std::filesystem::remove(path.u8string() + ".agbak");
        if (entry.codec == nullptr) {
            std::filesystem::copy_file(
                fixture, path, std::filesystem::copy_options::overwrite_existing);
        } else {
            assert(ag_transcode(fixture.u8string().c_str(), path.u8string().c_str(),
                                entry.codec, 192000, 44100, 2,
                                nullptr, nullptr, nullptr) == AG_OK);
        }
        const agplayer::MetadataEditPlan* effective_plan = &full_field_plan;
        agplayer::MetadataEditPlan supported_field_plan;
        agplayer::MetadataPreflightReport full_field_report;
        const ag_result full_preflight = agplayer::preflight_metadata_edit(
            path.u8string(), full_field_plan, full_field_report);
        if (full_preflight == AG_UNSUPPORTED_FORMAT
            && full_field_report.error_code
                == agplayer::MetadataErrorCode::UnsupportedField) {
            for (const agplayer::FieldEdit& field : full_field_plan.fields) {
                if (std::find(full_field_report.unsupported_fields.begin(),
                              full_field_report.unsupported_fields.end(),
                              field.field)
                    == full_field_report.unsupported_fields.end()) {
                    supported_field_plan.fields.push_back(field);
                }
            }
            assert(!supported_field_plan.fields.empty());
            effective_plan = &supported_field_plan;
        } else {
            assert(full_preflight == AG_OK);
        }
        agplayer::MetadataFileResult text_result;
        const ag_result full_field_write = agplayer::write_metadata_plan(
            path.u8string(), *effective_plan, text_result);
        if (full_field_write != AG_OK) {
            std::fprintf(stderr, "%s full metadata write failed: %d (%s)\n",
                         entry.extension, static_cast<int>(full_field_write),
                         text_result.message.c_str());
        }
        assert(full_field_write == AG_OK);
        assert(text_result.final_status == agplayer::FileResultStatus::Completed);
        assert(text_result.used_stream_copy);
        assert(text_result.fields.size() == effective_plan->fields.size());
        for (const agplayer::FieldResult& field : text_result.fields) {
            assert(field.status == agplayer::FieldWriteStatus::Updated);
            assert(field.actual_value == field.requested_value);
        }

        if (std::strcmp(entry.extension, "m4a") == 0) {
            agplayer::MetadataEditPlan integer_bpm_plan;
            integer_bpm_plan.fields = {{agplayer::CanonicalField::Bpm,
                agplayer::MetadataAction::Set, "128"}};
            const auto before_integer_bpm = file_bytes(path);
            agplayer::MetadataFileResult integer_bpm_result;
            const ag_result integer_bpm_write = agplayer::write_metadata_plan(
                path.u8string(), integer_bpm_plan, integer_bpm_result);
            assert(integer_bpm_write == AG_UNSUPPORTED_FORMAT);
            assert(integer_bpm_result.final_status
                   == agplayer::FileResultStatus::Unsupported);
            assert(integer_bpm_result.error_code
                   == agplayer::MetadataErrorCode::UnsupportedField);
            assert(file_bytes(path) == before_integer_bpm);
        }

        agplayer::MetadataEditPlan matrix_cover_plan = cover_plan;
        const auto before_cover_attempt = file_bytes(path);
        agplayer::MetadataFileResult matrix_cover_result;
        const ag_result matrix_cover_write = agplayer::write_metadata_plan(
            path.u8string(), matrix_cover_plan, matrix_cover_result);
        if (entry.cover_expected) {
            assert(matrix_cover_write == AG_OK);
            assert(matrix_cover_result.cover.has_cover);
        } else {
            assert(matrix_cover_write == AG_UNSUPPORTED_FORMAT);
            assert(matrix_cover_result.final_status
                   == agplayer::FileResultStatus::Unsupported);
            assert(matrix_cover_result.error_code
                   == agplayer::MetadataErrorCode::UnsupportedCover);
            assert(file_bytes(path) == before_cover_attempt);
        }
        for (const auto& item : std::filesystem::directory_iterator(work_dir)) {
            assert(item.path().filename().u8string().find(".agmeta-stage-")
                   == std::string::npos);
        }
        std::filesystem::remove(path);
        std::filesystem::remove(path.u8string() + ".agbak");
    }

    // A corrupt input must fail during preflight without changing its bytes or
    // leaving a staging/backup artifact behind.
    const std::filesystem::path corrupt = work_dir / "meta-corrupt.mp3";
    {
        std::ofstream output(corrupt, std::ios::binary | std::ios::trunc);
        output << "not an audio container";
    }
    const auto corrupt_before = file_bytes(corrupt);
    agplayer::MetadataFileResult corrupt_result;
    assert(agplayer::write_metadata_plan(corrupt.u8string(), write_plan,
                                         corrupt_result) == AG_IO_ERROR);
    assert(corrupt_result.final_status == agplayer::FileResultStatus::Failed);
    assert(corrupt_result.error_code == agplayer::MetadataErrorCode::InputOpenFailed);
    assert(file_bytes(corrupt) == corrupt_before);
    assert(!std::filesystem::exists(corrupt.u8string() + ".agbak"));
    std::filesystem::remove(corrupt);

    std::filesystem::remove(src);
    std::filesystem::remove(plan_src);
    std::filesystem::remove(unicode_plan_src);
    std::filesystem::remove(unicode_plan_backup);
    std::filesystem::remove(cancelled_src);
    std::filesystem::remove(backup);
    std::filesystem::remove(mp3);
    std::filesystem::remove(mp3.string() + ".agbak");
    return 0;
}

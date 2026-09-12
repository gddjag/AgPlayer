#undef NDEBUG
#include <agplayer/c_api.h>
#include "metadata_writer.hpp"
#include "transcode_probe.hpp"
#include "transcode_verifier.hpp"
#include <atomic>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
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

void assert_cover(const std::filesystem::path& path, const bool expected,
                  const unsigned char* expected_data = kOnePixelImage,
                  const std::size_t expected_size = sizeof(kOnePixelImage))
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
        assert(cover_size == expected_size);
        assert(std::memcmp(cover, expected_data, cover_size) == 0);
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

std::vector<unsigned char> adts_audio_bytes(const std::filesystem::path& path)
{
    const std::vector<unsigned char> bytes = file_bytes(path);
    std::size_t offset = 0;
    if (bytes.size() >= 10U && bytes[0] == 'I' && bytes[1] == 'D'
        && bytes[2] == '3') {
        const std::size_t tag_size =
            (static_cast<std::size_t>(bytes[6] & 0x7fU) << 21U)
            | (static_cast<std::size_t>(bytes[7] & 0x7fU) << 14U)
            | (static_cast<std::size_t>(bytes[8] & 0x7fU) << 7U)
            | static_cast<std::size_t>(bytes[9] & 0x7fU);
        offset = 10U + tag_size + ((bytes[5] & 0x10U) != 0U ? 10U : 0U);
    }
    assert(offset < bytes.size());
    assert(bytes[offset] == 0xffU);
    assert((bytes[offset + 1U] & 0xf6U) == 0xf0U);
    return {bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end()};
}

struct AudioFacts {
    int sample_rate = 0;
    int channels = 0;
    std::int64_t container_duration_ms = 0;
    std::int64_t probed_duration_ms = 0;
    std::int64_t decoded_duration_ms = 0;
    std::int64_t decoded_samples = 0;
};

AudioFacts read_audio_facts(const std::filesystem::path& path)
{
    AudioFacts facts;
    ag_metadata* metadata = nullptr;
    assert(ag_metadata_open(path.u8string().c_str(), &metadata) == AG_OK);
    assert(metadata != nullptr);
    facts.sample_rate = ag_metadata_sample_rate(metadata);
    facts.channels = ag_metadata_channels(metadata);
    facts.container_duration_ms = ag_metadata_duration_ms(metadata);
    ag_metadata_destroy(metadata);

    agplayer::MediaProbe probe;
    std::string error;
    assert(agplayer::probe_transcode_input(path.u8string(), probe, error)
           == AG_OK);
    assert(probe.audio_streams.size() == 1U);
    facts.probed_duration_ms = probe.audio_streams.front().duration_ms;

    agplayer::TranscodeVerificationPlan verification_plan;
    agplayer::TranscodeVerificationResult verification;
    assert(agplayer::verify_transcoded_output(
               path.u8string(), verification_plan, verification, error)
           == AG_OK);
    facts.decoded_duration_ms = verification.decoded_duration_ms;
    facts.decoded_samples = verification.decoded_samples;
    return facts;
}

void assert_audio_facts_equal(const AudioFacts& actual,
                              const AudioFacts& expected)
{
    assert(actual.sample_rate == expected.sample_rate);
    assert(actual.channels == expected.channels);
    // Raw ADTS has no container duration. FFmpeg estimates it from bitrate and
    // file size, so an ID3 prefix can move this coarse value by a few ms even
    // when the audio is byte-identical. The decoded duration below is exact.
    assert(actual.container_duration_ms > 0);
    assert(actual.probed_duration_ms > 0);
    assert(std::llabs(actual.container_duration_ms
                      - expected.container_duration_ms) <= 100);
    assert(std::llabs(actual.probed_duration_ms
                      - expected.probed_duration_ms) <= 100);
    assert(actual.decoded_duration_ms == expected.decoded_duration_ms);
    assert(actual.decoded_samples == expected.decoded_samples);
}

} // namespace

int main(const int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path fixture = argv[1];
    const std::filesystem::path work_dir = fixture.parent_path();

    agplayer::AudioStreamEvidence exact_before;
    exact_before.codec_id = 86018;
    exact_before.sample_rate = 48'000;
    exact_before.channels = 2;
    exact_before.format = 8;
    exact_before.bits_coded = 16;
    exact_before.bits_raw = 16;
    exact_before.time_base_num = 1;
    exact_before.time_base_den = 48'000;
    exact_before.duration = 96'000;
    exact_before.payload_hash = 0x1234U;
    exact_before.payload_bytes = 4'096;
    exact_before.packet_count = 8;
    exact_before.timestamp_hash = 0x5678U;
    assert(agplayer::classify_audio_stream_evidence(
               {exact_before}, {exact_before})
           == agplayer::AudioEquivalence::ExactPacketCopy);

    agplayer::AudioStreamEvidence normalized_after = exact_before;
    normalized_after.time_base_num = 1;
    normalized_after.time_base_den = 1'000;
    normalized_after.duration = 2'000;
    normalized_after.packet_count = 6;
    normalized_after.timestamp_hash = 0x9abcU;
    assert(agplayer::classify_audio_stream_evidence(
               {exact_before}, {normalized_after})
           == agplayer::AudioEquivalence::NormalizedPacketTiming);

    // Demuxers may leave stream-copy-only codec details unknown on the source
    // and fill them after remuxing. Unknown sample-format/bit-depth values must
    // not turn identical encoded payload into a false audio-change failure.
    agplayer::AudioStreamEvidence unknown_codec_details = exact_before;
    unknown_codec_details.format = -1;
    unknown_codec_details.bits_coded = 0;
    unknown_codec_details.bits_raw = 0;
    assert(agplayer::classify_audio_stream_evidence(
               {unknown_codec_details}, {exact_before})
           == agplayer::AudioEquivalence::NormalizedPacketTiming);

    agplayer::AudioStreamEvidence changed_payload = normalized_after;
    changed_payload.payload_hash ^= 1U;
    assert(agplayer::classify_audio_stream_evidence(
               {exact_before}, {changed_payload})
           == agplayer::AudioEquivalence::Different);
    agplayer::AudioStreamEvidence changed_codec = normalized_after;
    changed_codec.sample_rate = 44'100;
    assert(agplayer::classify_audio_stream_evidence(
               {exact_before}, {changed_codec})
           == agplayer::AudioEquivalence::Different);

    // Hand-derived AAC-LC/stereo ADTS fixed-header bytes. A parser that reads
    // sampling_frequency_index from any bits except byte 2 bits 5..2 fails
    // this table, including the common 44.1 kHz case and every legal rate.
    constexpr std::array<std::pair<unsigned char, int>, 13> adts_rates{{
        {static_cast<unsigned char>(0x40), 96'000},
        {static_cast<unsigned char>(0x44), 88'200},
        {static_cast<unsigned char>(0x48), 64'000},
        {static_cast<unsigned char>(0x4c), 48'000},
        {static_cast<unsigned char>(0x50), 44'100},
        {static_cast<unsigned char>(0x54), 32'000},
        {static_cast<unsigned char>(0x58), 24'000},
        {static_cast<unsigned char>(0x5c), 22'050},
        {static_cast<unsigned char>(0x60), 16'000},
        {static_cast<unsigned char>(0x64), 12'000},
        {static_cast<unsigned char>(0x68), 11'025},
        {static_cast<unsigned char>(0x6c), 8'000},
        {static_cast<unsigned char>(0x70), 7'350},
    }};
    for (const auto& [byte2, expected_rate] : adts_rates) {
        int parsed_rate = 0;
        int parsed_channels = 0;
        assert(agplayer::metadata_writer_detail::parse_adts_audio_parameters(
            byte2, static_cast<unsigned char>(0x80),
            parsed_rate, parsed_channels));
        assert(parsed_rate == expected_rate);
        assert(parsed_channels == 2);
    }

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
    assert(plan_result.runtime.demuxer_open_count > 0);
    assert(plan_result.runtime.codec_free_probe_count > 0);
    assert(plan_result.runtime.stream_info_probe_count == 0);
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

    const std::filesystem::path strict_normalized_src =
        work_dir / "meta-strict-normalized-src.wav";
    std::filesystem::copy_file(fixture, strict_normalized_src,
                               std::filesystem::copy_options::overwrite_existing);
    const std::vector<unsigned char> strict_original =
        file_bytes(strict_normalized_src);
    agplayer::MetadataEditPlan strict_normalized_plan;
    strict_normalized_plan.fields = {{agplayer::CanonicalField::Title,
                                      agplayer::MetadataAction::Set,
                                      "Strict Normalized"}};
    strict_normalized_plan.audio_policy =
        agplayer::MetadataAudioPolicy::StrictPacketIdentity;
    agplayer::MetadataWriterTestHooks normalized_hooks;
    normalized_hooks.simulate_normalized_packet_timing = true;
    agplayer::MetadataFileResult strict_normalized_result;
    assert(agplayer::write_metadata_plan(
               strict_normalized_src.u8string(), strict_normalized_plan,
               strict_normalized_result, nullptr, &normalized_hooks)
           == AG_DECODE_ERROR);
    assert(file_bytes(strict_normalized_src) == strict_original);
    assert(!std::filesystem::exists(
        std::filesystem::path(strict_normalized_src.u8string() + ".agbak")));

    agplayer::MetadataEditPlan force_normalized_plan = strict_normalized_plan;
    force_normalized_plan.audio_policy =
        agplayer::MetadataAudioPolicy::ForceVerifiedNormalization;
    agplayer::MetadataFileResult force_normalized_result;
    assert(agplayer::write_metadata_plan(
               strict_normalized_src.u8string(), force_normalized_plan,
               force_normalized_result, nullptr, &normalized_hooks)
           == AG_OK);
    assert(force_normalized_result.audio_equivalence
           == agplayer::AudioEquivalence::NormalizedPacketTiming);
    assert(force_normalized_result.used_force_fallback);
    assert(force_normalized_result.audio_verified_unchanged);
    assert(force_normalized_result.message.find("normalized container timing")
           != std::string::npos);

    // WAV exposes one physical RIFF date tag. An omitted counterpart has Keep
    // semantics, so a DATE-only or YEAR-only request must be rejected rather
    // than silently changing the omitted canonical field.
    agplayer::MetadataEditPlan date_plan;
    date_plan.fields = {
        {agplayer::CanonicalField::Date, agplayer::MetadataAction::Set,
         "2026-08-20"},
    };
    agplayer::MetadataPreflightReport omitted_year_report;
    assert(agplayer::preflight_metadata_edit(plan_src.u8string(), date_plan,
                                              omitted_year_report)
           == AG_UNSUPPORTED_FORMAT);
    assert(omitted_year_report.error_code
           == agplayer::MetadataErrorCode::PhysicalTagConflict);

    agplayer::MetadataEditPlan year_plan;
    year_plan.fields = {
        {agplayer::CanonicalField::Year, agplayer::MetadataAction::Set, "2026"},
    };
    agplayer::MetadataPreflightReport omitted_date_report;
    assert(agplayer::preflight_metadata_edit(plan_src.u8string(), year_plan,
                                              omitted_date_report)
           == AG_UNSUPPORTED_FORMAT);
    assert(omitted_date_report.error_code
           == agplayer::MetadataErrorCode::PhysicalTagConflict);

    agplayer::MetadataEditPlan conflicting_date_plan;
    conflicting_date_plan.fields = {
        {agplayer::CanonicalField::Year, agplayer::MetadataAction::Set, "2026"},
        {agplayer::CanonicalField::Date, agplayer::MetadataAction::Set,
         "2026-08-20"},
    };
    std::string date_validation_error;
    assert(agplayer::validate_metadata_edit_plan(conflicting_date_plan,
                                                 date_validation_error));
    agplayer::MetadataPreflightReport conflict_report;
    assert(agplayer::preflight_metadata_edit(plan_src.u8string(),
                                              conflicting_date_plan,
                                              conflict_report)
           == AG_UNSUPPORTED_FORMAT);
    assert(conflict_report.error_code
           == agplayer::MetadataErrorCode::PhysicalTagConflict);

    // A Keep request is a preservation guarantee. A shared physical date tag
    // cannot change YEAR while preserving DATE (or the reverse), so preflight
    // must reject both plans instead of silently changing the kept field.
    agplayer::MetadataEditPlan keep_date_plan;
    keep_date_plan.fields = {
        {agplayer::CanonicalField::Year, agplayer::MetadataAction::Set, "2027"},
        {agplayer::CanonicalField::Date, agplayer::MetadataAction::Keep,
         std::nullopt},
    };
    agplayer::MetadataPreflightReport keep_date_report;
    assert(agplayer::preflight_metadata_edit(plan_src.u8string(),
                                              keep_date_plan,
                                              keep_date_report)
           == AG_UNSUPPORTED_FORMAT);
    assert(keep_date_report.error_code
           == agplayer::MetadataErrorCode::PhysicalTagConflict);

    agplayer::MetadataEditPlan keep_year_plan;
    keep_year_plan.fields = {
        {agplayer::CanonicalField::Year, agplayer::MetadataAction::Keep,
         std::nullopt},
        {agplayer::CanonicalField::Date, agplayer::MetadataAction::Set,
         "2027-08-20"},
    };
    agplayer::MetadataPreflightReport keep_year_report;
    assert(agplayer::preflight_metadata_edit(plan_src.u8string(),
                                              keep_year_plan,
                                              keep_year_report)
           == AG_UNSUPPORTED_FORMAT);
    assert(keep_year_report.error_code
           == agplayer::MetadataErrorCode::PhysicalTagConflict);

    // Equal actions on a shared physical tag are not a conflict.
    agplayer::MetadataEditPlan equivalent_date_plan;
    equivalent_date_plan.fields = {
        {agplayer::CanonicalField::Year, agplayer::MetadataAction::Set, "2026"},
        {agplayer::CanonicalField::Date, agplayer::MetadataAction::Set, "2026"},
    };
    agplayer::MetadataPreflightReport equivalent_report;
    assert(agplayer::preflight_metadata_edit(plan_src.u8string(),
                                              equivalent_date_plan,
                                              equivalent_report) == AG_OK);
    agplayer::MetadataFileResult equivalent_result;
    assert(agplayer::write_metadata_plan(plan_src.u8string(),
                                          equivalent_date_plan,
                                          equivalent_result) == AG_OK);
    metadata = nullptr;
    assert(ag_metadata_open(plan_src.u8string().c_str(), &metadata) == AG_OK);
    assert(std::strcmp(ag_metadata_year(metadata), "2026") == 0);
    assert(std::strcmp(ag_metadata_date(metadata), "2026") == 0);
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
        if (failure_result.error_code != failure.code) {
            std::fprintf(stderr,
                         "failure point %d returned code %d instead of %d: %s\n",
                         static_cast<int>(failure.point),
                         static_cast<int>(failure_result.error_code),
                         static_cast<int>(failure.code),
                         failure_result.message.c_str());
        }
        assert(failure_result.error_code == failure.code);
        assert(file_bytes(failure_path) == before_failure);
        assert(!std::filesystem::exists(failure_path.u8string() + ".agbak"));
        for (const auto& item : std::filesystem::directory_iterator(work_dir)) {
            assert(item.path().filename().u8string().find(".agmeta-stage-")
                   == std::string::npos);
        }
        std::filesystem::remove(failure_path);
    }

    // Size and timestamp are not an identity check. A same-size external
    // writer that restores the timestamp must still win over our staged edit.
    const std::filesystem::path fingerprint_path =
        work_dir / "meta-source-fingerprint.wav";
    std::filesystem::copy_file(fixture, fingerprint_path,
        std::filesystem::copy_options::overwrite_existing);
    const auto fingerprint_time =
        std::filesystem::last_write_time(fingerprint_path);
    auto external_bytes = file_bytes(fingerprint_path);
    assert(external_bytes.size() > 64U);
    external_bytes[external_bytes.size() / 2U] ^= 0x01U;
    agplayer::MetadataWriterTestHooks fingerprint_hooks;
    fingerprint_hooks.before_source_commit = [&] {
        std::ofstream output(fingerprint_path,
                             std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(external_bytes.data()),
                     static_cast<std::streamsize>(external_bytes.size()));
        output.close();
        std::filesystem::last_write_time(fingerprint_path, fingerprint_time);
    };
    agplayer::MetadataFileResult fingerprint_result;
    assert(agplayer::write_metadata_plan(
               fingerprint_path.u8string(), write_plan, fingerprint_result,
               nullptr, &fingerprint_hooks) != AG_OK);
    assert(fingerprint_result.error_code
           == agplayer::MetadataErrorCode::SourceChanged);
    assert(file_bytes(fingerprint_path) == external_bytes);
    assert(!std::filesystem::exists(fingerprint_path.u8string() + ".agbak"));
    std::filesystem::remove(fingerprint_path);

    const std::filesystem::path commit_lock_path =
        work_dir / "meta-atomic-commit-lock.wav";
    std::filesystem::copy_file(fixture, commit_lock_path,
        std::filesystem::copy_options::overwrite_existing);
    bool commit_hook_called = false;
    bool competing_write_blocked = false;
    agplayer::MetadataWriterTestHooks commit_lock_hooks;
#ifdef _WIN32
    commit_lock_hooks.before_atomic_replace = [&] {
        commit_hook_called = true;
        SetLastError(ERROR_SUCCESS);
        HANDLE competing_writer = CreateFileW(
            commit_lock_path.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (competing_writer != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER offset{};
            offset.QuadPart = 64;
            assert(SetFilePointerEx(competing_writer, offset, nullptr,
                                    FILE_BEGIN) != 0);
            const unsigned char byte = 0x5aU;
            DWORD written = 0;
            SetLastError(ERROR_SUCCESS);
            const BOOL write_ok = WriteFile(competing_writer, &byte, 1,
                                            &written, nullptr);
            competing_write_blocked = write_ok == 0
                && GetLastError() == ERROR_LOCK_VIOLATION;
            CloseHandle(competing_writer);
        }
    };
#else
    commit_lock_hooks.before_atomic_replace = [&] {
        commit_hook_called = true;
        std::fstream competing_writer(commit_lock_path,
            std::ios::binary | std::ios::in | std::ios::out);
        competing_writer.seekp(64);
        competing_writer.put(static_cast<char>(0x5a));
        competing_writer.flush();
        competing_write_blocked = !competing_writer.good();
    };
#endif
    agplayer::MetadataFileResult commit_lock_result;
    const ag_result commit_lock_write = agplayer::write_metadata_plan(
        commit_lock_path.u8string(), write_plan, commit_lock_result,
        nullptr, &commit_lock_hooks);
#ifdef _WIN32
    assert(commit_hook_called);
    assert(competing_write_blocked);
    assert(commit_lock_write == AG_OK);
#else
    assert(commit_hook_called);
    if (competing_write_blocked) {
        assert(commit_lock_write == AG_OK);
    } else {
        assert(commit_lock_write != AG_OK);
        assert(commit_lock_result.error_code
            == agplayer::MetadataErrorCode::SourceChanged);
    }
#endif
    std::filesystem::remove(commit_lock_path.u8string() + ".agbak");
    std::filesystem::remove(commit_lock_path);

#ifdef _WIN32
    const std::filesystem::path path_swap_path =
        work_dir / "meta-final-path-swap.wav";
    const std::filesystem::path displaced_path =
        work_dir / "meta-final-path-swap.displaced.wav";
    std::filesystem::copy_file(fixture, path_swap_path,
        std::filesystem::copy_options::overwrite_existing);
    auto replacement_bytes = file_bytes(fixture);
    assert(replacement_bytes.size() > 64U);
    replacement_bytes[64] ^= 0x5aU;
    bool path_swap_hook_called = false;
    agplayer::MetadataWriterTestHooks path_swap_hooks;
    path_swap_hooks.before_atomic_replace = [&] {
        path_swap_hook_called = true;
        assert(MoveFileExW(path_swap_path.c_str(), displaced_path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0);
        std::ofstream replacement(path_swap_path,
            std::ios::binary | std::ios::trunc);
        replacement.write(
            reinterpret_cast<const char*>(replacement_bytes.data()),
            static_cast<std::streamsize>(replacement_bytes.size()));
        replacement.close();
    };
    agplayer::MetadataFileResult path_swap_result;
    assert(agplayer::write_metadata_plan(path_swap_path.u8string(), write_plan,
        path_swap_result, nullptr, &path_swap_hooks) == AG_IO_ERROR);
    assert(path_swap_hook_called);
    assert(path_swap_result.error_code
        == agplayer::MetadataErrorCode::SourceChanged);
    assert(file_bytes(path_swap_path) == replacement_bytes);
    assert(!std::filesystem::exists(path_swap_path.u8string() + ".agbak"));
    std::filesystem::remove(path_swap_path);
    std::filesystem::remove(displaced_path);

    const std::filesystem::path cas_swap_path =
        work_dir / "meta-post-check-path-swap.wav";
    const std::filesystem::path cas_displaced_path =
        work_dir / "meta-post-check-path-swap.displaced.wav";
    std::filesystem::copy_file(fixture, cas_swap_path,
        std::filesystem::copy_options::overwrite_existing);
    auto cas_replacement_bytes = file_bytes(fixture);
    assert(cas_replacement_bytes.size() > 96U);
    cas_replacement_bytes[96] ^= 0x33U;
    bool cas_swap_hook_called = false;
    agplayer::MetadataWriterTestHooks cas_swap_hooks;
    cas_swap_hooks.before_replace_file = [&] {
        cas_swap_hook_called = true;
        assert(MoveFileExW(cas_swap_path.c_str(), cas_displaced_path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0);
        std::ofstream replacement(cas_swap_path,
            std::ios::binary | std::ios::trunc);
        replacement.write(
            reinterpret_cast<const char*>(cas_replacement_bytes.data()),
            static_cast<std::streamsize>(cas_replacement_bytes.size()));
        replacement.close();
    };
    agplayer::MetadataFileResult cas_swap_result;
    assert(agplayer::write_metadata_plan(cas_swap_path.u8string(), write_plan,
        cas_swap_result, nullptr, &cas_swap_hooks) == AG_IO_ERROR);
    assert(cas_swap_hook_called);
    assert(cas_swap_result.error_code
        == agplayer::MetadataErrorCode::SourceChanged);
    assert(file_bytes(cas_swap_path) == cas_replacement_bytes);
    assert(!std::filesystem::exists(cas_swap_path.u8string() + ".agbak"));
    std::filesystem::remove(cas_swap_path);
    std::filesystem::remove(cas_displaced_path);

    const std::filesystem::path error_1177_path =
        work_dir / "meta-replace-error-1177.wav";
    std::filesystem::copy_file(fixture, error_1177_path,
        std::filesystem::copy_options::overwrite_existing);
    const auto error_1177_original = file_bytes(error_1177_path);
    const std::filesystem::path error_1177_backup =
        error_1177_path.u8string() + ".agbak";
    constexpr std::array<unsigned char, 8> prior_backup_marker{
        'p', 'r', 'i', 'o', 'r', '-', 'b', '\n'};
    {
        std::ofstream prior_backup(error_1177_backup, std::ios::binary);
        prior_backup.write(
            reinterpret_cast<const char*>(prior_backup_marker.data()),
            static_cast<std::streamsize>(prior_backup_marker.size()));
    }
    agplayer::MetadataWriterTestHooks error_1177_hooks;
    error_1177_hooks.simulate_replace_error_1177 = true;
    agplayer::MetadataFileResult error_1177_result;
    assert(agplayer::write_metadata_plan(error_1177_path.u8string(), write_plan,
        error_1177_result, nullptr, &error_1177_hooks) == AG_IO_ERROR);
    assert(error_1177_result.error_code
        == agplayer::MetadataErrorCode::AtomicReplaceFailed);
    assert(file_bytes(error_1177_path) == error_1177_original);
    assert(file_bytes(error_1177_backup)
        == std::vector<unsigned char>(prior_backup_marker.begin(),
                                      prior_backup_marker.end()));
    std::filesystem::remove(error_1177_backup);
    std::filesystem::remove(error_1177_path);

    const std::filesystem::path error_1177_recovery_path =
        work_dir / "meta-replace-error-1177-recovery.wav";
    std::filesystem::copy_file(fixture, error_1177_recovery_path,
        std::filesystem::copy_options::overwrite_existing);
    const auto error_1177_recovery_original =
        file_bytes(error_1177_recovery_path);
    agplayer::MetadataWriterTestHooks error_1177_recovery_hooks;
    error_1177_recovery_hooks.simulate_replace_error_1177 = true;
    error_1177_recovery_hooks.fail_source_restore = true;
    agplayer::MetadataFileResult error_1177_recovery_result;
    assert(agplayer::write_metadata_plan(error_1177_recovery_path.u8string(),
        write_plan, error_1177_recovery_result, nullptr,
        &error_1177_recovery_hooks) == AG_IO_ERROR);
    assert(error_1177_recovery_result.error_code
        == agplayer::MetadataErrorCode::AtomicReplaceFailed);
    assert(error_1177_recovery_result.message.find(
        "original recovery backup remains at") != std::string::npos);
    bool found_error_1177_recovery = false;
    for (const auto& item : std::filesystem::directory_iterator(work_dir)) {
        if (item.path().filename().u8string().find(
                "meta-replace-error-1177-recovery.wav.agbak.recovery-")
            == std::string::npos) {
            continue;
        }
        assert(file_bytes(item.path()) == error_1177_recovery_original);
        std::filesystem::remove(item.path());
        found_error_1177_recovery = true;
        break;
    }
    assert(found_error_1177_recovery);
    std::filesystem::remove(error_1177_recovery_path);

    const std::filesystem::path commit_lock_failure_path =
        work_dir / "meta-commit-lock-failure.wav";
    std::filesystem::copy_file(fixture, commit_lock_failure_path,
        std::filesystem::copy_options::overwrite_existing);
    const auto commit_lock_failure_before =
        file_bytes(commit_lock_failure_path);
    agplayer::MetadataWriterTestHooks commit_lock_failure_hooks;
    commit_lock_failure_hooks.fail_commit_lock = true;
    agplayer::MetadataFileResult commit_lock_failure_result;
    assert(agplayer::write_metadata_plan(
        commit_lock_failure_path.u8string(), write_plan,
        commit_lock_failure_result, nullptr,
        &commit_lock_failure_hooks) == AG_IO_ERROR);
    assert(commit_lock_failure_result.error_code
        == agplayer::MetadataErrorCode::FileInUse);
    assert(file_bytes(commit_lock_failure_path)
        == commit_lock_failure_before);
    assert(!std::filesystem::exists(
        commit_lock_failure_path.u8string() + ".agbak"));
    std::filesystem::remove(commit_lock_failure_path);

    const std::filesystem::path stream_recovery_path =
        work_dir / "meta-backup-stream-recovery.wav";
    std::filesystem::copy_file(fixture, stream_recovery_path,
        std::filesystem::copy_options::overwrite_existing);
    const std::wstring alternate_stream =
        stream_recovery_path.native() + L":agplayer-recovery";
    HANDLE stream_writer = CreateFileW(alternate_stream.c_str(), GENERIC_WRITE,
        0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(stream_writer != INVALID_HANDLE_VALUE);
    constexpr std::array<unsigned char, 8> stream_marker{
        'a', 'g', '-', 's', 't', 'r', 'm', '\n'};
    DWORD stream_written = 0;
    assert(WriteFile(stream_writer, stream_marker.data(),
        static_cast<DWORD>(stream_marker.size()), &stream_written, nullptr)
        != 0);
    assert(stream_written == stream_marker.size());
    CloseHandle(stream_writer);
    agplayer::MetadataWriterTestHooks stream_recovery_hooks{
        agplayer::MetadataFailurePoint::PostReplaceReadback};
    agplayer::MetadataFileResult stream_recovery_result;
    assert(agplayer::write_metadata_plan(stream_recovery_path.u8string(),
        write_plan, stream_recovery_result, nullptr,
        &stream_recovery_hooks) == AG_DECODE_ERROR);
    assert(stream_recovery_result.error_code
        == agplayer::MetadataErrorCode::VerificationFailed);
    HANDLE stream_reader = CreateFileW(alternate_stream.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
        nullptr);
    assert(stream_reader != INVALID_HANDLE_VALUE);
    std::array<unsigned char, stream_marker.size()> recovered_marker{};
    DWORD stream_read = 0;
    assert(ReadFile(stream_reader, recovered_marker.data(),
        static_cast<DWORD>(recovered_marker.size()), &stream_read, nullptr)
        != 0);
    CloseHandle(stream_reader);
    assert(stream_read == recovered_marker.size());
    assert(recovered_marker == stream_marker);
    assert(!std::filesystem::exists(
        stream_recovery_path.u8string() + ".agbak"));
    std::filesystem::remove(stream_recovery_path);
#endif

#ifndef _WIN32
    // A non-cooperating writer can replace the source path after the final
    // identity check. The atomic exchange must detect that the displaced path
    // is not the locked source object, exchange it back, and leave the
    // competing writer's file at the source path.
    const std::filesystem::path posix_cas_swap_path =
        work_dir / "meta-posix-post-check-path-swap.wav";
    const std::filesystem::path posix_cas_displaced_path =
        work_dir / "meta-posix-post-check-path-swap.displaced.wav";
    std::filesystem::copy_file(fixture, posix_cas_swap_path,
        std::filesystem::copy_options::overwrite_existing);
    auto posix_cas_replacement_bytes = file_bytes(fixture);
    assert(posix_cas_replacement_bytes.size() > 96U);
    posix_cas_replacement_bytes[96] ^= 0x33U;
    bool posix_cas_swap_hook_called = false;
    agplayer::MetadataWriterTestHooks posix_cas_swap_hooks;
    posix_cas_swap_hooks.before_replace_file = [&] {
        posix_cas_swap_hook_called = true;
        std::filesystem::rename(posix_cas_swap_path,
                                posix_cas_displaced_path);
        std::ofstream replacement(posix_cas_swap_path,
            std::ios::binary | std::ios::trunc);
        replacement.write(
            reinterpret_cast<const char*>(posix_cas_replacement_bytes.data()),
            static_cast<std::streamsize>(posix_cas_replacement_bytes.size()));
        replacement.close();
    };
    agplayer::MetadataFileResult posix_cas_swap_result;
    assert(agplayer::write_metadata_plan(posix_cas_swap_path.u8string(),
        write_plan, posix_cas_swap_result, nullptr, &posix_cas_swap_hooks)
        == AG_IO_ERROR);
    assert(posix_cas_swap_hook_called);
#if defined(__linux__) || defined(__APPLE__)
    assert(posix_cas_swap_result.error_code
        == agplayer::MetadataErrorCode::SourceChanged);
#else
    assert(posix_cas_swap_result.error_code
        == agplayer::MetadataErrorCode::AtomicReplaceFailed);
#endif
    assert(file_bytes(posix_cas_swap_path) == posix_cas_replacement_bytes);
    assert(!std::filesystem::exists(
        posix_cas_swap_path.u8string() + ".agbak"));
    std::filesystem::remove(posix_cas_swap_path);
    std::filesystem::remove(posix_cas_displaced_path);
#endif

    // A failed write must not overwrite or delete a pre-existing recovery
    // point. This exercises both failure before replacement and readback
    // failure after replacement/rollback.
    for (const auto point : {agplayer::MetadataFailurePoint::AtomicReplace,
                             agplayer::MetadataFailurePoint::PostReplaceReadback}) {
        const std::filesystem::path failure_path = work_dir
            / (point == agplayer::MetadataFailurePoint::AtomicReplace
                   ? "meta-old-backup-atomic.wav"
                   : "meta-old-backup-readback.wav");
        std::filesystem::copy_file(fixture, failure_path,
            std::filesystem::copy_options::overwrite_existing);
        const std::filesystem::path old_backup = failure_path.u8string() + ".agbak";
        const std::array<unsigned char, 8> recovery_point{
            'o', 'l', 'd', '-', 'b', 'a', 'k', '\n'};
        {
            std::ofstream backup_output(old_backup, std::ios::binary);
            backup_output.write(
                reinterpret_cast<const char*>(recovery_point.data()),
                static_cast<std::streamsize>(recovery_point.size()));
        }
        agplayer::MetadataWriterTestHooks hooks{point};
        agplayer::MetadataFileResult failure_result;
        assert(agplayer::write_metadata_plan(failure_path.u8string(), write_plan,
                                             failure_result, nullptr, &hooks)
               != AG_OK);
        assert(file_bytes(old_backup)
               == std::vector<unsigned char>(recovery_point.begin(),
                                             recovery_point.end()));
        std::filesystem::remove(old_backup);
        std::filesystem::remove(failure_path);
    }

    // If restoring a prior .agbak fails, the result must expose the surviving
    // recovery file instead of claiming rollback succeeded.
    {
        const std::filesystem::path failure_path =
            work_dir / "meta-backup-restore-failure.wav";
        std::filesystem::copy_file(fixture, failure_path,
            std::filesystem::copy_options::overwrite_existing);
        const std::filesystem::path old_backup = failure_path.u8string() + ".agbak";
        const std::array<unsigned char, 8> recovery_point{
            'o', 'l', 'd', '-', 'b', 'a', 'k', '\n'};
        {
            std::ofstream output(old_backup, std::ios::binary);
            output.write(reinterpret_cast<const char*>(recovery_point.data()),
                         static_cast<std::streamsize>(recovery_point.size()));
        }
        agplayer::MetadataWriterTestHooks hooks{
            agplayer::MetadataFailurePoint::PostReplaceReadback};
        hooks.fail_backup_restore = true;
        agplayer::MetadataFileResult failure_result;
        assert(agplayer::write_metadata_plan(failure_path.u8string(), write_plan,
                                             failure_result, nullptr, &hooks)
               == AG_IO_ERROR);
        assert(failure_result.error_code
               == agplayer::MetadataErrorCode::AtomicReplaceFailed);
        assert(failure_result.message.find("prior backup remains preserved at")
               != std::string::npos);
        bool found_preserved = false;
        for (const auto& item : std::filesystem::directory_iterator(work_dir)) {
            if (item.path().filename().u8string().find(
                    "meta-backup-restore-failure.wav.agbak.preserved-")
                == 0) {
                assert(file_bytes(item.path())
                       == std::vector<unsigned char>(recovery_point.begin(),
                                                     recovery_point.end()));
                std::filesystem::remove(item.path());
                found_preserved = true;
            }
        }
        assert(found_preserved);
        std::filesystem::remove(old_backup);
        std::filesystem::remove(failure_path);
    }

    // If verification fails after replacement and backup -> source recovery
    // also fails, the newly created original-file backup is the only current
    // recovery point. Preserve it separately before restoring an older .agbak.
    for (const int recovery_case : {0, 1, 2}) {
        const bool has_prior_backup = recovery_case != 0;
        const bool fail_prior_restore = recovery_case == 2;
        const std::string filename = recovery_case == 0
            ? "meta-only-source-restore-failure.wav"
            : fail_prior_restore ? "meta-both-restores-failure.wav"
                                 : "meta-source-restore-failure.wav";
        const std::filesystem::path failure_path = work_dir / filename;
        std::filesystem::copy_file(fixture, failure_path,
            std::filesystem::copy_options::overwrite_existing);
        const auto original_source = file_bytes(failure_path);
        const std::filesystem::path old_backup = failure_path.u8string() + ".agbak";
        const std::array<unsigned char, 8> prior_recovery{
            'o', 'l', 'd', '-', 'b', 'a', 'k', '\n'};
        if (has_prior_backup) {
            std::ofstream output(old_backup, std::ios::binary);
            output.write(reinterpret_cast<const char*>(prior_recovery.data()),
                         static_cast<std::streamsize>(prior_recovery.size()));
        }
        agplayer::MetadataWriterTestHooks hooks{
            agplayer::MetadataFailurePoint::PostReplaceReadback};
        hooks.fail_source_restore = true;
        hooks.fail_backup_restore = fail_prior_restore;
        agplayer::MetadataFileResult failure_result;
        assert(agplayer::write_metadata_plan(failure_path.u8string(), write_plan,
                                             failure_result, nullptr, &hooks)
               == AG_IO_ERROR);
        assert(failure_result.error_code
               == agplayer::MetadataErrorCode::AtomicReplaceFailed);
        assert(failure_result.message.find(
                   "original recovery backup remains at") != std::string::npos);
        bool found_original_recovery = false;
        bool found_prior_recovery = false;
        for (const auto& item : std::filesystem::directory_iterator(work_dir)) {
            if (item.path().filename().u8string().find(
                    filename + ".agbak.recovery-") == 0) {
                assert(file_bytes(item.path()) == original_source);
                assert(failure_result.message.find(item.path().u8string())
                       != std::string::npos);
                std::filesystem::remove(item.path());
                found_original_recovery = true;
            } else if (item.path().filename().u8string().find(
                           filename + ".agbak.preserved-") == 0) {
                assert(file_bytes(item.path())
                       == std::vector<unsigned char>(prior_recovery.begin(),
                                                     prior_recovery.end()));
                assert(failure_result.message.find(item.path().u8string())
                       != std::string::npos);
                std::filesystem::remove(item.path());
                found_prior_recovery = true;
            }
        }
        assert(found_original_recovery);
        if (!has_prior_backup) {
            assert(!std::filesystem::exists(old_backup));
            assert(!found_prior_recovery);
            assert(failure_result.message.find("prior backup")
                   == std::string::npos);
        } else if (fail_prior_restore) {
            assert(!std::filesystem::exists(old_backup));
            assert(found_prior_recovery);
            assert(failure_result.message.find(
                       "prior backup remains preserved at") != std::string::npos);
        } else {
            assert(file_bytes(old_backup)
                   == std::vector<unsigned char>(prior_recovery.begin(),
                                                 prior_recovery.end()));
            assert(!found_prior_recovery);
            assert(failure_result.message.find(
                       "prior backup restored at") != std::string::npos);
        }
        std::filesystem::remove(old_backup);
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

    // The preflight budget covers both staged output and backup, plus tags and
    // the safety margin, without creating a huge sparse file during the test.
    constexpr std::uintmax_t source_size = 64U * 1024U * 1024U;
    const auto required = agplayer::metadata_staging_space_required(
        source_size, valid_plan);
    assert(required.has_value());
    assert(*required >= source_size * 2U + 4U * 1024U * 1024U);
    assert(!agplayer::metadata_staging_space_required(
        (std::numeric_limits<std::uintmax_t>::max)(), valid_plan).has_value());
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
    const std::filesystem::path backup = src.string() + ".agbak";
    {
        std::ofstream stale_backup(backup, std::ios::binary);
        stale_backup << "stale-internal-rollback";
    }

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

    assert(!std::filesystem::exists(backup));
    for (const auto& item : std::filesystem::directory_iterator(work_dir)) {
        assert(item.path().filename().u8string().find(
                   "meta-write-src.wav.agbak.preserved-")
               == std::string::npos);
    }

    const std::filesystem::path mp3 = work_dir / "meta-cover.mp3";
    std::filesystem::remove(mp3);
    std::filesystem::remove(mp3.u8string() + ".agbak");
    assert(ag_transcode(fixture.u8string().c_str(), mp3.u8string().c_str(),
                        "libmp3lame", 192000, 44100, 2,
                        nullptr, nullptr, nullptr) == AG_OK);
    metadata = nullptr;
    assert(ag_metadata_open(mp3.u8string().c_str(), &metadata) == AG_OK);
    assert(ag_metadata_sample_rate(metadata) == 44'100);
    ag_metadata_destroy(metadata);

    // The container-level `encoder` tag is generated by libavformat and can
    // legitimately change when a file is remuxed by a newer AgPlayer build.
    // It must not be confused with the user-editable `encoded_by` field.
    const std::filesystem::path muxer_encoder_mp3 =
        work_dir / "meta-muxer-encoder.mp3";
    std::filesystem::copy_file(mp3, muxer_encoder_mp3,
        std::filesystem::copy_options::overwrite_existing);
    agplayer::MetadataEditPlan muxer_encoder_plan;
    muxer_encoder_plan.fields = {
        {agplayer::CanonicalField::Title, agplayer::MetadataAction::Set,
         "Muxer encoder migration"},
    };
    agplayer::MetadataWriterTestHooks muxer_encoder_hooks;
    muxer_encoder_hooks.simulate_source_muxer_encoder_metadata = true;
    agplayer::MetadataFileResult muxer_encoder_result;
    assert(agplayer::write_metadata_plan(
               muxer_encoder_mp3.u8string(), muxer_encoder_plan,
               muxer_encoder_result, nullptr, &muxer_encoder_hooks) == AG_OK);
    assert(muxer_encoder_result.audio_verified_unchanged);
    std::filesystem::remove(muxer_encoder_mp3);

    // CustomTag is a player-owned tag: writing a different field must
    // preserve it, while Clear removes only that tag.
    const std::filesystem::path custom_tag_flac = work_dir / "meta-custom-tag.flac";
    std::filesystem::remove(custom_tag_flac);
    std::filesystem::remove(custom_tag_flac.u8string() + ".agbak");
    assert(ag_transcode(fixture.u8string().c_str(), custom_tag_flac.u8string().c_str(),
                        "flac", 0, 44100, 2, nullptr, nullptr, nullptr) == AG_OK);
    agplayer::MetadataEditPlan custom_tag_set_plan;
    custom_tag_set_plan.fields = {
        {agplayer::CanonicalField::CustomTag, agplayer::MetadataAction::Set,
         "电子"},
    };
    agplayer::MetadataFileResult custom_tag_set_result;
    assert(agplayer::write_metadata_plan(custom_tag_flac.u8string(),
                                          custom_tag_set_plan,
                                          custom_tag_set_result) == AG_OK);
    assert(custom_tag_set_result.fields.size() == 1);
    assert(custom_tag_set_result.fields[0].actual_value == "电子");
    assert(custom_tag_set_result.fields[0].status
           == agplayer::FieldWriteStatus::Updated);

    agplayer::MetadataEditPlan custom_tag_keep_plan;
    custom_tag_keep_plan.fields = {
        {agplayer::CanonicalField::Title, agplayer::MetadataAction::Set,
         "Custom Tag Preserved"},
        {agplayer::CanonicalField::CustomTag, agplayer::MetadataAction::Keep,
         std::nullopt},
    };
    agplayer::MetadataFileResult custom_tag_keep_result;
    assert(agplayer::write_metadata_plan(custom_tag_flac.u8string(),
                                          custom_tag_keep_plan,
                                          custom_tag_keep_result) == AG_OK);
    assert(custom_tag_keep_result.fields.size() == 2);
    assert(custom_tag_keep_result.fields[1].actual_value == "电子");
    assert(custom_tag_keep_result.fields[1].status
           == agplayer::FieldWriteStatus::Kept);

    agplayer::MetadataEditPlan custom_tag_clear_plan;
    custom_tag_clear_plan.fields = {
        {agplayer::CanonicalField::CustomTag, agplayer::MetadataAction::Clear,
         std::nullopt},
    };
    agplayer::MetadataFileResult custom_tag_clear_result;
    assert(agplayer::write_metadata_plan(custom_tag_flac.u8string(),
                                          custom_tag_clear_plan,
                                          custom_tag_clear_result) == AG_OK);
    assert(custom_tag_clear_result.fields.size() == 1);
    assert(custom_tag_clear_result.fields[0].actual_value.empty());
    assert(custom_tag_clear_result.fields[0].status
           == agplayer::FieldWriteStatus::Cleared);

    assert(ag_metadata_write_extended(
               mp3.u8string().c_str(), nullptr, nullptr, nullptr,
               "Album Artist", "2025-08-09", nullptr, "3/12", "1/2",
               "Composer", "Comment", "128.50", "Copyright", "AgPlayer",
               "Lyrics", nullptr, 0U, nullptr) == AG_OK);
    assert(ag_metadata_open(mp3.u8string().c_str(), &metadata) == AG_OK);
    assert(ag_metadata_sample_rate(metadata) == 44'100);
    assert(std::strcmp(ag_metadata_album_artist(metadata), "Album Artist") == 0);
    assert(std::strcmp(ag_metadata_track(metadata), "3/12") == 0);
    assert(std::strcmp(ag_metadata_disc(metadata), "1/2") == 0);
    assert(std::strcmp(ag_metadata_composer(metadata), "Composer") == 0);
    assert(std::strcmp(ag_metadata_comment(metadata), "Comment") == 0);
    assert(std::strcmp(ag_metadata_bpm_tag(metadata), "128.50") == 0);
    assert(std::strcmp(ag_metadata_copyright(metadata), "Copyright") == 0);
    assert(std::strcmp(ag_metadata_encoder(metadata), "AgPlayer") == 0);
    ag_metadata_destroy(metadata);

    // Seed real unmanaged format/stream metadata plus a chapter into the MP3.
    // A normal canonical edit must preserve all of it. Deliberately dropping
    // each category from a staged output must fail verification and leave the
    // original file byte-for-byte unchanged.
    agplayer::MetadataUpdate preservation_seed;
    preservation_seed.title = "Preservation Seed";
    agplayer::MetadataWriterTestHooks seed_hooks;
    seed_hooks.seed_preservation_fixture = true;
    std::string preservation_error;
    assert(agplayer::write_metadata(mp3.u8string(), preservation_seed,
                                    preservation_error, nullptr, nullptr,
                                    &seed_hooks) == AG_OK);

    agplayer::MetadataEditPlan preservation_plan;
    preservation_plan.fields = {
        {agplayer::CanonicalField::Title, agplayer::MetadataAction::Set,
         "Preserved Metadata"},
    };
    agplayer::MetadataFileResult preservation_result;
    assert(agplayer::write_metadata_plan(mp3.u8string(), preservation_plan,
                                          preservation_result) == AG_OK);
    metadata = nullptr;
    assert(ag_metadata_open(mp3.u8string().c_str(), &metadata) == AG_OK);
    assert(std::strcmp(ag_metadata_track(metadata), "3/12") == 0);
    assert(std::strcmp(ag_metadata_disc(metadata), "1/2") == 0);
    assert(std::strcmp(ag_metadata_comment(metadata), "Comment") == 0);
    assert(std::strcmp(ag_metadata_lyrics(metadata), "Lyrics") == 0);
    assert(std::strcmp(ag_metadata_copyright(metadata), "Copyright") == 0);
    assert(std::strcmp(ag_metadata_encoder(metadata), "AgPlayer") == 0);
    ag_metadata_destroy(metadata);

    const auto expect_preservation_drop = [&preservation_plan](
        const std::filesystem::path& path,
        const agplayer::MetadataFailurePoint drop) {
        const auto before_drop = file_bytes(path);
        agplayer::MetadataWriterTestHooks drop_hooks{drop};
        agplayer::MetadataFileResult drop_result;
        const ag_result drop_write = agplayer::write_metadata_plan(
            path.u8string(), preservation_plan, drop_result, nullptr, &drop_hooks);
        if (drop_write != AG_DECODE_ERROR) {
            std::fprintf(stderr, "preservation drop %d returned %d: %s\n",
                         static_cast<int>(drop), static_cast<int>(drop_write),
                         drop_result.message.c_str());
        }
        assert(drop_write == AG_DECODE_ERROR);
        assert(drop_result.error_code
               == agplayer::MetadataErrorCode::VerificationFailed);
        assert(file_bytes(path) == before_drop);
    };
    expect_preservation_drop(mp3,
        agplayer::MetadataFailurePoint::ChapterDrop);

    const std::filesystem::path preservation_flac =
        work_dir / "meta-preservation.flac";
    std::filesystem::remove(preservation_flac);
    std::filesystem::remove(preservation_flac.u8string() + ".agbak");
    assert(ag_transcode(fixture.u8string().c_str(),
                        preservation_flac.u8string().c_str(),
                        "flac", 0, 44100, 2,
                        nullptr, nullptr, nullptr) == AG_OK);
    preservation_error.clear();
    assert(agplayer::write_metadata(preservation_flac.u8string(),
                                    preservation_seed, preservation_error,
                                    nullptr, nullptr, &seed_hooks) == AG_OK);
    agplayer::MetadataFileResult flac_preservation_result;
    assert(agplayer::write_metadata_plan(preservation_flac.u8string(),
                                          preservation_plan,
                                          flac_preservation_result) == AG_OK);
    expect_preservation_drop(preservation_flac,
        agplayer::MetadataFailurePoint::UnmanagedFormatMetadataDrop);

    const std::filesystem::path preservation_m4a =
        work_dir / "meta-preservation.m4a";
    std::filesystem::remove(preservation_m4a);
    std::filesystem::remove(preservation_m4a.u8string() + ".agbak");
    assert(ag_transcode(fixture.u8string().c_str(),
                        preservation_m4a.u8string().c_str(),
                        "aac", 192000, 44100, 2,
                        nullptr, nullptr, nullptr) == AG_OK);
    preservation_error.clear();
    assert(agplayer::write_metadata(preservation_m4a.u8string(),
                                    preservation_seed, preservation_error,
                                    nullptr, nullptr, &seed_hooks) == AG_OK);
    agplayer::MetadataFileResult m4a_preservation_result;
    assert(agplayer::write_metadata_plan(preservation_m4a.u8string(),
                                          preservation_plan,
                                          m4a_preservation_result) == AG_OK);
    expect_preservation_drop(preservation_m4a,
        agplayer::MetadataFailurePoint::UnmanagedStreamMetadataDrop);

    // Vorbis Comment containers have independent YEAR and DATE keys. Editing
    // YEAR alone must preserve DATE byte-for-byte at the metadata level.
    const std::filesystem::path dated_flac = work_dir / "meta-year-date.flac";
    std::filesystem::remove(dated_flac);
    std::filesystem::remove(dated_flac.u8string() + ".agbak");
    assert(ag_transcode(fixture.u8string().c_str(), dated_flac.u8string().c_str(),
                        "flac", 0, 44100, 2, nullptr, nullptr, nullptr) == AG_OK);
    metadata = nullptr;
    assert(ag_metadata_open(dated_flac.u8string().c_str(), &metadata) == AG_OK);
    assert(ag_metadata_sample_rate(metadata) == 44'100);
    ag_metadata_destroy(metadata);
    agplayer::MetadataFileResult distinct_date_result;
    assert(agplayer::write_metadata_plan(dated_flac.u8string(),
                                          conflicting_date_plan,
                                          distinct_date_result) == AG_OK);
    metadata = nullptr;
    assert(ag_metadata_open(dated_flac.u8string().c_str(), &metadata) == AG_OK);
    assert(std::strcmp(ag_metadata_year(metadata), "2026") == 0);
    assert(std::strcmp(ag_metadata_date(metadata), "2026-08-20") == 0);
    ag_metadata_destroy(metadata);
    agplayer::MetadataEditPlan year_only_plan;
    year_only_plan.fields = {
        {agplayer::CanonicalField::Year, agplayer::MetadataAction::Set, "2027"},
        {agplayer::CanonicalField::Date, agplayer::MetadataAction::Keep,
         std::nullopt},
    };
    agplayer::MetadataFileResult year_only_result;
    assert(agplayer::write_metadata_plan(dated_flac.u8string(), year_only_plan,
                                          year_only_result) == AG_OK);
    metadata = nullptr;
    assert(ag_metadata_open(dated_flac.u8string().c_str(), &metadata) == AG_OK);
    assert(std::strcmp(ag_metadata_year(metadata), "2027") == 0);
    assert(std::strcmp(ag_metadata_date(metadata), "2026-08-20") == 0);
    ag_metadata_destroy(metadata);

    agplayer::MetadataEditPlan cover_plan;
    cover_plan.cover_action = agplayer::CoverAction::Set;
    cover_plan.cover_data = kOnePixelImage;
    cover_plan.cover_size = sizeof(kOnePixelImage);
    cover_plan.cover_mime_type = "image/bmp";

    agplayer::MetadataEditPlan mismatched_cover_plan = cover_plan;
    mismatched_cover_plan.cover_mime_type = "image/png";
    agplayer::MetadataPreflightReport mismatched_cover_report;
    assert(agplayer::preflight_metadata_edit(mp3.u8string(),
                                              mismatched_cover_plan,
                                              mismatched_cover_report)
           == AG_INVALID_ARGUMENT);
    assert(mismatched_cover_report.error_code
           == agplayer::MetadataErrorCode::InvalidEditPlan);
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

    // Canonical TITLE belongs to format/audio metadata. The attached picture's
    // title/comment describe cover semantics and must survive a TITLE edit.
    agplayer::MetadataEditPlan cover_title_plan;
    cover_title_plan.fields = {
        {agplayer::CanonicalField::Title, agplayer::MetadataAction::Set,
         "Cover Metadata Preserved"},
    };
    agplayer::MetadataFileResult cover_title_result;
    assert(agplayer::write_metadata_plan(mp3.u8string(), cover_title_plan,
                                          cover_title_result) == AG_OK);
    assert_cover(mp3, true);

    const auto before_cover_metadata_drop = file_bytes(mp3);
    agplayer::MetadataWriterTestHooks cover_metadata_drop_hooks{
        agplayer::MetadataFailurePoint::UnmanagedStreamMetadataDrop};
    agplayer::MetadataFileResult cover_metadata_drop_result;
    assert(agplayer::write_metadata_plan(
               mp3.u8string(), cover_title_plan, cover_metadata_drop_result,
               nullptr, &cover_metadata_drop_hooks) == AG_DECODE_ERROR);
    assert(cover_metadata_drop_result.error_code
           == agplayer::MetadataErrorCode::VerificationFailed);
    assert(file_bytes(mp3) == before_cover_metadata_drop);

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

    // ADTS sampling_frequency_index occupies byte 2 bits 5..2. Exercise the
    // real metadata-only production path after it has gained an ID3 prefix,
    // because that is when FFmpeg needs the codec-free ADTS header fallback.
    // Each successful Set A -> Set B -> Clear round must preserve every audio
    // property and the complete ADTS payload after the metadata prefix.
    for (const int sample_rate : {44'100, 48'000, 32'000, 24'000, 8'000}) {
        const std::filesystem::path adts = work_dir
            / ("meta-adts-" + std::to_string(sample_rate) + ".aac");
        std::filesystem::remove(adts);
        std::filesystem::remove(adts.u8string() + ".agbak");
        assert(ag_transcode(fixture.u8string().c_str(), adts.u8string().c_str(),
                            "aac", 128'000, sample_rate, 2,
                            nullptr, nullptr, nullptr) == AG_OK);
        const AudioFacts baseline = read_audio_facts(adts);
        assert(baseline.sample_rate == sample_rate);
        assert(baseline.channels == 2);
        assert(baseline.container_duration_ms > 0);
        assert(baseline.probed_duration_ms > 0);
        assert(baseline.decoded_duration_ms > 0);
        assert(baseline.decoded_samples > 0);
        const std::vector<unsigned char> baseline_audio = adts_audio_bytes(adts);

        const auto apply_title = [&](const agplayer::MetadataAction action,
                                     const std::optional<std::string>& value) {
            agplayer::MetadataEditPlan plan;
            plan.fields = {{agplayer::CanonicalField::Title, action, value}};
            agplayer::MetadataFileResult write_result;
            assert(agplayer::write_metadata_plan(adts.u8string(), plan,
                                                  write_result) == AG_OK);
            assert(write_result.audio_verified_unchanged);
            assert_audio_facts_equal(read_audio_facts(adts), baseline);
            assert(adts_audio_bytes(adts) == baseline_audio);
            assert(!std::filesystem::exists(adts.u8string() + ".agbak"));
        };
        apply_title(agplayer::MetadataAction::Set, "ADTS title A");
        apply_title(agplayer::MetadataAction::Set, "ADTS title B");
        apply_title(agplayer::MetadataAction::Clear, std::nullopt);
        std::filesystem::remove(adts);
    }

    struct MatrixEntry {
        const char* extension;
        const char* codec;
        bool cover_expected;
    };
    constexpr std::array<MatrixEntry, 8> matrix{{
        {"wav", nullptr, false},
        {"mp3", "libmp3lame", true},
        {"flac", "flac", true},
        {"aac", "aac", false},
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
        {agplayer::CanonicalField::Date, agplayer::MetadataAction::Set, "2026"},
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
        assert_cover(path, false);
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
        assert(!std::filesystem::exists(path.u8string() + ".agbak"));

        agplayer::MetadataEditPlan replace_title_plan;
        replace_title_plan.fields = {
            {agplayer::CanonicalField::Title, agplayer::MetadataAction::Set,
             "Replacement Title"},
        };
        agplayer::MetadataFileResult replace_title_result;
        assert(agplayer::write_metadata_plan(path.u8string(),
                                              replace_title_plan,
                                              replace_title_result) == AG_OK);
        assert(replace_title_result.fields.size() == 1U);
        assert(replace_title_result.fields.front().before_value
               == "Unicode Title");
        assert(replace_title_result.fields.front().actual_value
               == "Replacement Title");
        assert(!std::filesystem::exists(path.u8string() + ".agbak"));

        agplayer::MetadataEditPlan clear_title_plan;
        clear_title_plan.fields = {
            {agplayer::CanonicalField::Title, agplayer::MetadataAction::Clear,
             std::nullopt},
        };
        agplayer::MetadataFileResult clear_title_result;
        assert(agplayer::write_metadata_plan(path.u8string(), clear_title_plan,
                                              clear_title_result) == AG_OK);
        assert(clear_title_result.fields.size() == 1U);
        assert(clear_title_result.fields.front().before_value
               == "Replacement Title");
        assert(clear_title_result.fields.front().actual_value.empty());
        metadata = nullptr;
        assert(ag_metadata_open(path.u8string().c_str(), &metadata) == AG_OK);
        assert(std::strlen(ag_metadata_title(metadata)) == 0U);
        ag_metadata_destroy(metadata);
        assert(!std::filesystem::exists(path.u8string() + ".agbak"));

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
            if (matrix_cover_write != AG_OK) {
                std::fprintf(stderr, "%s cover metadata write failed: %d (%s)\n",
                             entry.extension,
                             static_cast<int>(matrix_cover_write),
                             matrix_cover_result.message.c_str());
            }
            assert(matrix_cover_write == AG_OK);
            assert(matrix_cover_result.cover.has_cover);
            assert_cover(path, true);

            agplayer::MetadataEditPlan keep_cover_plan;
            keep_cover_plan.fields = {
                {agplayer::CanonicalField::Title,
                 agplayer::MetadataAction::Set, "Cover Kept"},
            };
            agplayer::MetadataFileResult keep_cover_result;
            assert(agplayer::write_metadata_plan(path.u8string(),
                                                  keep_cover_plan,
                                                  keep_cover_result) == AG_OK);
            assert(keep_cover_result.cover.requested_action
                   == agplayer::CoverAction::Keep);
            assert(keep_cover_result.cover.status
                   == agplayer::FieldWriteStatus::Kept);
            assert_cover(path, true);

            std::vector<unsigned char> replacement_cover(
                kOnePixelImage, kOnePixelImage + sizeof(kOnePixelImage));
            replacement_cover[54] = 0xff;
            replacement_cover[55] = 0x00;
            replacement_cover[56] = 0x00;
            agplayer::MetadataEditPlan replace_cover_plan;
            replace_cover_plan.cover_action = agplayer::CoverAction::Set;
            replace_cover_plan.cover_data = replacement_cover.data();
            replace_cover_plan.cover_size = replacement_cover.size();
            replace_cover_plan.cover_mime_type = "image/bmp";
            agplayer::MetadataFileResult replace_cover_result;
            assert(agplayer::write_metadata_plan(path.u8string(),
                                                  replace_cover_plan,
                                                  replace_cover_result) == AG_OK);
            assert(replace_cover_result.cover.status
                   == agplayer::FieldWriteStatus::Updated);
            assert_cover(path, true, replacement_cover.data(),
                         replacement_cover.size());

            agplayer::MetadataEditPlan clear_cover_plan;
            clear_cover_plan.cover_action = agplayer::CoverAction::Clear;
            agplayer::MetadataFileResult clear_cover_result;
            assert(agplayer::write_metadata_plan(path.u8string(),
                                                  clear_cover_plan,
                                                  clear_cover_result) == AG_OK);
            assert(clear_cover_result.cover.status
                   == agplayer::FieldWriteStatus::Cleared);
            assert(!clear_cover_result.cover.has_cover);
            assert_cover(path, false);
        } else {
            assert(matrix_cover_write == AG_UNSUPPORTED_FORMAT);
            assert(matrix_cover_result.final_status
                   == agplayer::FileResultStatus::Unsupported);
            assert(matrix_cover_result.error_code
                   == agplayer::MetadataErrorCode::UnsupportedCover);
            assert(file_bytes(path) == before_cover_attempt);
            assert_cover(path, false);
        }
        for (const auto& item : std::filesystem::directory_iterator(work_dir)) {
            assert(item.path().filename().u8string().find(".agmeta-stage-")
                   == std::string::npos);
        }
        std::filesystem::remove(path);
        std::filesystem::remove(path.u8string() + ".agbak");
    }

    // APE has no registered safe metadata-only muxer. Reject it before any
    // staging or replacement, even when its bytes happen to be parseable as a
    // different container.
    const std::filesystem::path unsupported_ape =
        work_dir / "meta-unsupported.ape";
    std::filesystem::copy_file(
        fixture, unsupported_ape,
        std::filesystem::copy_options::overwrite_existing);
    const auto unsupported_ape_before = file_bytes(unsupported_ape);
    agplayer::MetadataPreflightReport unsupported_ape_report;
    assert(agplayer::preflight_metadata_edit(unsupported_ape.u8string(),
                                              write_plan,
                                              unsupported_ape_report)
           == AG_UNSUPPORTED_FORMAT);
    assert(unsupported_ape_report.error_code
               == agplayer::MetadataErrorCode::UnsupportedContainer
           || unsupported_ape_report.error_code
               == agplayer::MetadataErrorCode::UnsupportedField);
    agplayer::MetadataFileResult unsupported_ape_result;
    assert(agplayer::write_metadata_plan(unsupported_ape.u8string(), write_plan,
                                          unsupported_ape_result)
           == AG_UNSUPPORTED_FORMAT);
    assert(unsupported_ape_result.final_status
           == agplayer::FileResultStatus::Unsupported);
    assert(unsupported_ape_result.error_code
               == agplayer::MetadataErrorCode::UnsupportedContainer
           || unsupported_ape_result.error_code
               == agplayer::MetadataErrorCode::UnsupportedField);
    assert(file_bytes(unsupported_ape) == unsupported_ape_before);
    assert(!std::filesystem::exists(unsupported_ape.u8string() + ".agbak"));
    std::filesystem::remove(unsupported_ape);

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
    std::filesystem::remove(custom_tag_flac);
    std::filesystem::remove(custom_tag_flac.u8string() + ".agbak");
    std::filesystem::remove(dated_flac);
    std::filesystem::remove(dated_flac.u8string() + ".agbak");
    std::filesystem::remove(preservation_flac);
    std::filesystem::remove(preservation_flac.u8string() + ".agbak");
    std::filesystem::remove(preservation_m4a);
    std::filesystem::remove(preservation_m4a.u8string() + ".agbak");
    return 0;
}

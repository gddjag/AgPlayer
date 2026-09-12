#pragma once

#include <agplayer/c_api.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace agplayer {

namespace metadata_writer_detail {

bool parse_adts_audio_parameters(unsigned char byte2, unsigned char byte3,
                                 int& sample_rate, int& channels) noexcept;

} // namespace metadata_writer_detail

// QML and container adapters communicate exclusively through canonical fields;
// FFmpeg dictionary keys remain an implementation detail of this module.
enum class MetadataAction { Keep, Set, Clear };

enum class CoverAction { Keep, Set, Clear };

enum class MetadataAudioPolicy {
    StrictPacketIdentity,
    ForceVerifiedNormalization,
};

enum class CanonicalField {
    Title,
    Artist,
    Album,
    AlbumArtist,
    Genre,
    Year,
    Date,
    Composer,
    Bpm,
    CustomTag,
};

// One authoritative read/write priority table for normalized fields.
const std::vector<const char*>& known_metadata_aliases(CanonicalField field);

struct FieldEdit {
    CanonicalField field;
    MetadataAction action = MetadataAction::Keep;
    std::optional<std::string> value_utf8;
};

struct MetadataEditPlan {
    std::vector<FieldEdit> fields;
    CoverAction cover_action = CoverAction::Keep;
    MetadataAudioPolicy audio_policy = MetadataAudioPolicy::StrictPacketIdentity;
    const unsigned char* cover_data = nullptr;
    std::size_t cover_size = 0;
    std::string cover_mime_type;
};

enum class FileResultStatus { Completed, Unsupported, Failed, Cancelled };

enum class MetadataErrorCode {
    None,
    InvalidEditPlan,
    PhysicalTagConflict,
    PermissionDenied,
    ReadOnlyFile,
    FileInUse,
    InsufficientDiskSpace,
    UnsupportedContainer,
    UnsupportedMuxer,
    UnsupportedField,
    UnsupportedCover,
    UnsupportedStructure,
    InputOpenFailed,
    OutputCreateFailed,
    HeaderWriteFailed,
    PacketReadFailed,
    PacketWriteFailed,
    TrailerWriteFailed,
    VerificationFailed,
    SourceChanged,
    AtomicReplaceFailed,
    Cancelled,
    InternalError,
};

enum class FieldWriteStatus {
    Kept,
    Updated,
    Cleared,
    Unsupported,
    Failed,
    NotApplicable,
};

struct FieldResult {
    CanonicalField field;
    MetadataAction requested_action = MetadataAction::Keep;
    std::string actual_value;
    std::string reason;
    FieldWriteStatus status = FieldWriteStatus::NotApplicable;
    std::string before_value;
    std::string requested_value;
    std::string user_message;
};

struct MetadataPreflightReport {
    bool supported = false;
    MetadataErrorCode error_code = MetadataErrorCode::None;
    std::string container;
    std::string raw_error;
    std::string user_message;
    std::vector<CanonicalField> unsupported_fields;
};

struct MetadataRuntimeMetrics {
    std::size_t packets_copied = 0;
    // Positive evidence for the codec-free demux path. Every metadata input
    // context is opened through the guarded helper; stream-info probing is
    // forbidden because FFmpeg may open a decoder while executing it.
    std::size_t demuxer_open_count = 0;
    std::size_t codec_free_probe_count = 0;
    std::size_t stream_info_probe_count = 0;
    std::size_t decoder_open_count = 0;
    std::size_t encoder_open_count = 0;
    std::size_t audio_streams_before = 0;
    std::size_t audio_streams_after = 0;
    std::size_t chapters_before = 0;
    std::size_t chapters_after = 0;
    std::size_t attachments_before = 0;
    std::size_t attachments_after = 0;
};

enum class MetadataFailurePoint {
    None,
    HeaderWrite,
    PacketRead,
    PacketWrite,
    TrailerWrite,
    Reprobe,
    Verification,
    SourceChanged,
    AtomicReplace,
    PostReplaceReadback,
    UnmanagedFormatMetadataDrop,
    UnmanagedStreamMetadataDrop,
    ChapterDrop,
};

// Deterministic failure injection used by the core safety tests. Production
// callers leave this null.
struct MetadataWriterTestHooks {
    MetadataFailurePoint fail_at = MetadataFailurePoint::None;
    bool seed_preservation_fixture = false;
    bool fail_source_restore = false;
    bool fail_backup_restore = false;
    bool fail_commit_lock = false;
    bool simulate_replace_error_1177 = false;
    bool simulate_normalized_packet_timing = false;
    bool simulate_source_muxer_encoder_metadata = false;
    std::function<void()> before_source_commit{};
    std::function<void()> before_atomic_replace{};
    std::function<void()> before_replace_file{};
};

struct CoverResult {
    CoverAction requested_action = CoverAction::Keep;
    FieldWriteStatus status = FieldWriteStatus::NotApplicable;
    bool had_cover = false;
    bool has_cover = false;
    std::string mime_type;
    std::string reason;
};

enum class AudioEquivalence {
    Different,
    ExactPacketCopy,
    NormalizedPacketTiming,
};

// Decoder-free evidence used to prove that a metadata-only remux preserved
// audio payloads. Container timing is deliberately separate from codec and
// payload identity because muxers may legally normalize it during stream copy.
struct AudioStreamEvidence {
    int codec_id = 0;
    int sample_rate = 0;
    int channels = 0;
    int format = -1;
    int bits_coded = 0;
    int bits_raw = 0;
    int time_base_num = 0;
    int time_base_den = 1;
    std::int64_t duration = 0;
    std::uint64_t payload_hash = 0;
    std::uint64_t payload_bytes = 0;
    std::uint64_t packet_count = 0;
    std::uint64_t timestamp_hash = 0;
};

AudioEquivalence classify_audio_stream_evidence(
    const std::vector<AudioStreamEvidence>& before,
    const std::vector<AudioStreamEvidence>& after) noexcept;

struct MetadataFileResult {
    FileResultStatus final_status = FileResultStatus::Failed;
    bool used_stream_copy = false;
    bool audio_verified_unchanged = false;
    bool used_force_fallback = false;
    AudioEquivalence audio_equivalence = AudioEquivalence::Different;
    std::vector<FieldResult> fields;
    CoverResult cover;
    std::string message;
    MetadataErrorCode error_code = MetadataErrorCode::None;
    MetadataRuntimeMetrics runtime;
};

// Validates requests before a writer creates any temporary output.
bool validate_metadata_edit_plan(const MetadataEditPlan& plan,
                                 std::string& error);

// Returns the complete staging budget (temporary output, backup, metadata and
// safety margin), or nullopt when the calculation would overflow.
std::optional<std::uintmax_t> metadata_staging_space_required(
    std::uintmax_t source_size, const MetadataEditPlan& plan) noexcept;

// Checks the input and matching output container without creating a file.
ag_result preflight_metadata_edit(const std::string& utf8_path,
                                  const MetadataEditPlan& plan,
                                  std::string& error);

ag_result preflight_metadata_edit(const std::string& utf8_path,
                                  const MetadataEditPlan& plan,
                                  MetadataPreflightReport& report);

ag_result write_metadata_plan(const std::string& utf8_path,
                              const MetadataEditPlan& plan,
                              MetadataFileResult& result,
                              const std::atomic_bool* cancel = nullptr,
                              const MetadataWriterTestHooks* test_hooks = nullptr);

// nullopt keeps a tag, an empty string clears it, and a non-empty string sets it.
struct MetadataUpdate {
    std::optional<std::string> title;
    std::optional<std::string> artist;
    std::optional<std::string> album;
    std::optional<std::string> album_artist;
    std::optional<std::string> track;
    std::optional<std::string> disc;
    std::optional<std::string> composer;
    std::optional<std::string> comment;
    std::optional<std::string> bpm;
    std::optional<std::string> custom_tag;
    std::optional<std::string> copyright;
    std::optional<std::string> encoder;
    std::optional<std::string> year;
    std::optional<std::string> date;
    std::optional<std::string> genre;
    std::optional<std::string> lyrics;
    CoverAction cover_action = CoverAction::Keep;
    const unsigned char* cover_data = nullptr;
    std::size_t cover_size = 0;
    std::string cover_mime_type;
};

// Write metadata to an audio file using FFmpeg stream copy (no re-encoding).
// Writes to a temp file then atomically replaces the original.
// Returns AG_OK on success, or an error code. On failure, error is set.
ag_result write_metadata(const std::string& utf8_path,
                         const MetadataUpdate& update,
                         std::string& error,
                         const MetadataEditPlan* verification_plan = nullptr,
                         const std::atomic_bool* cancel = nullptr,
                         const MetadataWriterTestHooks* test_hooks = nullptr,
                         MetadataRuntimeMetrics* runtime = nullptr);

} // namespace agplayer

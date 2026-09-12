#include "metadata_writer.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avstring.h>
#include <libavutil/dict.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/sha.h>
}

#include <chrono>
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#if defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <stdio.h>
#endif
#include <unistd.h>
#endif

namespace agplayer {

constexpr std::uintmax_t kMetadataWriteMargin = 4U * 1024U * 1024U;
constexpr std::size_t kMaxMetadataFieldBytes = 1U * 1024U * 1024U;
constexpr std::size_t kMaxMetadataCoverBytes = 32U * 1024U * 1024U;

using FileSha256 = std::array<unsigned char, 32>;

bool file_sha256(const std::filesystem::path& path, FileSha256& digest)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    AVSHA* sha = av_sha_alloc();
    if (sha == nullptr || av_sha_init(sha, 256) < 0) {
        av_free(sha);
        return false;
    }
    std::array<unsigned char, 64U * 1024U> buffer{};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read = input.gcount();
        if (read > 0) {
            av_sha_update(sha, buffer.data(),
                          static_cast<unsigned int>(read));
        }
    }
    const bool ok = input.eof();
    if (ok) av_sha_final(sha, digest.data());
    av_free(sha);
    return ok;
}

AudioEquivalence classify_audio_stream_evidence(
    const std::vector<AudioStreamEvidence>& before,
    const std::vector<AudioStreamEvidence>& after) noexcept
{
    if (before.size() != after.size()) return AudioEquivalence::Different;

    bool timing_is_exact = true;
    for (std::size_t index = 0; index < before.size(); ++index) {
        const AudioStreamEvidence& source = before[index];
        const AudioStreamEvidence& staged = after[index];
        const bool format_compatible = source.format < 0 || staged.format < 0
            || source.format == staged.format;
        const bool coded_bits_compatible = source.bits_coded <= 0
            || staged.bits_coded <= 0 || source.bits_coded == staged.bits_coded;
        const bool raw_bits_compatible = source.bits_raw <= 0
            || staged.bits_raw <= 0 || source.bits_raw == staged.bits_raw;
        if (source.codec_id != staged.codec_id
            || source.sample_rate != staged.sample_rate
            || source.channels != staged.channels
            || !format_compatible
            || !coded_bits_compatible
            || !raw_bits_compatible
            || source.payload_hash != staged.payload_hash
            || source.payload_bytes != staged.payload_bytes) {
            return AudioEquivalence::Different;
        }
        timing_is_exact = timing_is_exact
            && source.format == staged.format
            && source.bits_coded == staged.bits_coded
            && source.bits_raw == staged.bits_raw
            && source.time_base_num == staged.time_base_num
            && source.time_base_den == staged.time_base_den
            && source.duration == staged.duration
            && source.packet_count == staged.packet_count
            && source.timestamp_hash == staged.timestamp_hash;
    }
    return timing_is_exact ? AudioEquivalence::ExactPacketCopy
                           : AudioEquivalence::NormalizedPacketTiming;
}

const std::vector<const char*>& known_metadata_aliases(const CanonicalField field)
{
    static const std::vector<const char*> title{
        "title", "TIT2", "©nam", "WM/Title"};
    static const std::vector<const char*> artist{
        "artist", "TPE1", "©ART", "Author", "WM/Artist"};
    static const std::vector<const char*> album{
        "album", "TALB", "©alb", "WM/AlbumTitle"};
    static const std::vector<const char*> album_artist{
        "album_artist", "albumartist", "TPE2", "aART", "WM/AlbumArtist"};
    static const std::vector<const char*> genre{
        "genre", "TCON", "©gen", "WM/Genre"};
    static const std::vector<const char*> year{
        "year", "TYER", "WM/Year"};
    static const std::vector<const char*> date{
        "date", "TDRC", "©day"};
    static const std::vector<const char*> composer{
        "composer", "TCOM", "©wrt", "WM/Composer"};
    static const std::vector<const char*> bpm{
        "bpm", "TBPM", "tmpo", "WM/BeatsPerMinute"};
    static const std::vector<const char*> custom_tag{"AGPLAYER_TAG"};
    switch (field) {
    case CanonicalField::Title: return title;
    case CanonicalField::Artist: return artist;
    case CanonicalField::Album: return album;
    case CanonicalField::AlbumArtist: return album_artist;
    case CanonicalField::Genre: return genre;
    case CanonicalField::Year: return year;
    case CanonicalField::Date: return date;
    case CanonicalField::Composer: return composer;
    case CanonicalField::Bpm: return bpm;
    case CanonicalField::CustomTag: return custom_tag;
    }
    return title;
}

bool metadata_writer_detail::parse_adts_audio_parameters(
    const unsigned char byte2, const unsigned char byte3,
    int& sample_rate, int& channels) noexcept
{
    static constexpr std::array<int, 13> sample_rates{
        96'000, 88'200, 64'000, 48'000, 44'100, 32'000, 24'000,
        22'050, 16'000, 12'000, 11'025, 8'000, 7'350};
    const unsigned int rate_index = (byte2 >> 2U) & 0x0fU;
    const unsigned int channel_count =
        ((byte2 & 0x01U) << 2U) | ((byte3 >> 6U) & 0x03U);
    if (rate_index >= sample_rates.size() || channel_count == 0U) {
        return false;
    }
    sample_rate = sample_rates[rate_index];
    channels = static_cast<int>(channel_count);
    return true;
}

namespace {

int metadata_only_stream_info_probe_forbidden(AVFormatContext*,
                                               AVDictionary**) = delete;
#define avformat_find_stream_info metadata_only_stream_info_probe_forbidden

std::filesystem::path filesystem_path_from_utf8(const std::string& utf8_path)
{
    return std::filesystem::u8path(utf8_path);
}

std::string filesystem_path_as_utf8(const std::filesystem::path& path)
{
    return path.u8string();
}

bool atomic_replace(const std::filesystem::path& temp_path,
                    const std::filesystem::path& target_path)
{
#ifdef _WIN32
    if (ReplaceFileW(target_path.c_str(), temp_path.c_str(),
                     nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) != 0) {
        return true;
    }
    return MoveFileExW(temp_path.c_str(), target_path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(temp_path.c_str(), target_path.c_str()) == 0;
#endif
}

#ifndef _WIN32
enum class AtomicExchangeResult {
    Exchanged,
    Unsupported,
    Failed,
};

AtomicExchangeResult atomic_exchange(const std::filesystem::path& first,
                                     const std::filesystem::path& second)
{
#if defined(__linux__) && defined(SYS_renameat2) && defined(RENAME_EXCHANGE)
    errno = 0;
    if (::syscall(SYS_renameat2, AT_FDCWD, first.c_str(), AT_FDCWD,
                  second.c_str(), RENAME_EXCHANGE) == 0) {
        return AtomicExchangeResult::Exchanged;
    }
    return errno == ENOSYS || errno == EINVAL || errno == EOPNOTSUPP
        ? AtomicExchangeResult::Unsupported
        : AtomicExchangeResult::Failed;
#elif defined(__APPLE__) && defined(RENAME_SWAP)
    errno = 0;
    if (::renamex_np(first.c_str(), second.c_str(), RENAME_SWAP) == 0) {
        return AtomicExchangeResult::Exchanged;
    }
    return errno == ENOTSUP || errno == EINVAL
        ? AtomicExchangeResult::Unsupported
        : AtomicExchangeResult::Failed;
#else
    (void)first;
    (void)second;
    return AtomicExchangeResult::Unsupported;
#endif
}
#endif

class ExistingBackupGuard final {
public:
    struct FailedSourceRecovery {
        bool prior_backup_restored = false;
        std::string original_backup_path;
        std::string prior_backup_path;
    };

    ExistingBackupGuard(const std::filesystem::path& backup,
                        const std::string& nonce)
        : backup_(backup), saved_(backup.native()
            + std::filesystem::u8path(".preserved-" + nonce).native()),
          recovery_(backup.native()
            + std::filesystem::u8path(".recovery-" + nonce).native())
    {
        std::error_code ec;
        had_existing_ = std::filesystem::exists(backup_, ec) && !ec;
        if (!had_existing_) {
            valid_ = !ec;
            return;
        }
        std::filesystem::rename(backup_, saved_, ec);
        valid_ = !ec;
    }

    ExistingBackupGuard(const ExistingBackupGuard&) = delete;
    ExistingBackupGuard& operator=(const ExistingBackupGuard&) = delete;

    ~ExistingBackupGuard()
    {
        if (active_) restore();
    }

    bool valid() const noexcept { return valid_; }

    bool restore(const bool inject_failure = false) noexcept
    {
        if (!active_) return true;
        active_ = false;
        if (inject_failure) return false;
        std::error_code ec;
        if (!had_existing_) {
            std::filesystem::remove(backup_, ec);
            return !ec;
        }
        std::filesystem::remove(backup_, ec);
        ec.clear();
        std::filesystem::rename(saved_, backup_, ec);
        return !ec;
    }

    std::string preserved_path() const { return saved_.u8string(); }

    FailedSourceRecovery preserve_after_source_restore_failure(
        const bool inject_prior_restore_failure = false) noexcept
    {
        FailedSourceRecovery result;
        if (!active_) return result;
        active_ = false;

        // backup_ now contains the original source. Never remove or overwrite
        // it after backup -> source recovery has failed. Move it aside first so
        // a pre-existing .agbak can be restored independently.
        std::error_code ec;
        const bool has_original_backup = std::filesystem::exists(backup_, ec)
            && !ec;
        if (has_original_backup) {
            std::filesystem::rename(backup_, recovery_, ec);
            result.original_backup_path = ec
                ? backup_.u8string() : recovery_.u8string();
        }

        if (!had_existing_) {
            result.prior_backup_restored = true;
            return result;
        }
        if (!ec && !inject_prior_restore_failure) {
            std::filesystem::rename(saved_, backup_, ec);
        }
        result.prior_backup_restored = !ec && !inject_prior_restore_failure;
        result.prior_backup_path = result.prior_backup_restored
            ? backup_.u8string() : saved_.u8string();
        return result;
    }

    void discard() noexcept
    {
        if (!active_) return;
        active_ = false;
        std::error_code ec;
        std::filesystem::remove(backup_, ec);
        ec.clear();
        std::filesystem::remove(saved_, ec);
    }

private:
    std::filesystem::path backup_;
    std::filesystem::path saved_;
    std::filesystem::path recovery_;
    bool had_existing_ = false;
    bool valid_ = false;
    bool active_ = true;
};

void cleanup_output(AVFormatContext*& out_ctx)
{
    if (out_ctx != nullptr) {
        if (out_ctx->pb != nullptr) {
            avio_closep(&out_ctx->pb);
        }
        avformat_free_context(out_ctx);
        out_ctx = nullptr;
    }
}

// Metadata-only operations must never call avformat_find_stream_info(): FFmpeg
// may open a decoder while probing. Every input context in this module is
// therefore opened through this auditable codec-free boundary.
int open_metadata_demuxer(AVFormatContext** context, const std::string& path,
                          MetadataRuntimeMetrics* runtime)
{
    if (runtime != nullptr) {
        ++runtime->demuxer_open_count;
        ++runtime->codec_free_probe_count;
    }
    return avformat_open_input(context, path.c_str(), nullptr, nullptr);
}

bool populate_adts_parameters_without_decoder(AVFormatContext* context,
                                              const std::string& path)
{
    AVCodecParameters* parameters = nullptr;
    for (unsigned int index = 0; index < context->nb_streams; ++index) {
        AVCodecParameters* candidate = context->streams[index]->codecpar;
        if (candidate->codec_type == AVMEDIA_TYPE_AUDIO
            && candidate->codec_id == AV_CODEC_ID_AAC) {
            parameters = candidate;
            break;
        }
    }
    if (parameters == nullptr) return false;

    std::ifstream input(filesystem_path_from_utf8(path), std::ios::binary);
    if (!input) return false;
    std::array<unsigned char, 10> id3{};
    input.read(reinterpret_cast<char*>(id3.data()),
               static_cast<std::streamsize>(id3.size()));
    std::uintmax_t audio_offset = 0;
    if (input.gcount() == static_cast<std::streamsize>(id3.size())
        && id3[0] == 'I' && id3[1] == 'D' && id3[2] == '3') {
        const std::uintmax_t tag_size =
            (static_cast<std::uintmax_t>(id3[6] & 0x7fU) << 21U)
            | (static_cast<std::uintmax_t>(id3[7] & 0x7fU) << 14U)
            | (static_cast<std::uintmax_t>(id3[8] & 0x7fU) << 7U)
            | static_cast<std::uintmax_t>(id3[9] & 0x7fU);
        audio_offset = 10U + tag_size + ((id3[5] & 0x10U) != 0U ? 10U : 0U);
    }
    input.clear();
    input.seekg(static_cast<std::streamoff>(audio_offset), std::ios::beg);
    std::array<unsigned char, 8'192> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    const std::size_t count = static_cast<std::size_t>(input.gcount());
    for (std::size_t offset = 0; offset + 6U < count; ++offset) {
        if (bytes[offset] != 0xffU || (bytes[offset + 1U] & 0xf6U) != 0xf0U) {
            continue;
        }
        int sample_rate = 0;
        int channels = 0;
        if (!metadata_writer_detail::parse_adts_audio_parameters(
                bytes[offset + 2U], bytes[offset + 3U],
                sample_rate, channels)) {
            continue;
        }
        parameters->sample_rate = sample_rate;
        av_channel_layout_uninit(&parameters->ch_layout);
        av_channel_layout_default(&parameters->ch_layout, channels);
        return true;
    }
    return false;
}

bool prime_audio_headers_without_codec(AVFormatContext* context,
                                       const std::string& path)
{
    const auto ready = [context] {
        bool has_audio = false;
        for (unsigned int index = 0; index < context->nb_streams; ++index) {
            const AVCodecParameters* parameters = context->streams[index]->codecpar;
            if (parameters->codec_type != AVMEDIA_TYPE_AUDIO) continue;
            has_audio = true;
            if (parameters->codec_id == AV_CODEC_ID_NONE
                || parameters->sample_rate <= 0
                || parameters->ch_layout.nb_channels <= 0) {
                return false;
            }
        }
        return has_audio;
    };
    if (ready()) return true;
    populate_adts_parameters_without_decoder(context, path);
    if (ready()) return true;
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) return false;
    for (int count = 0; count < 64 && !ready(); ++count) {
        const int read_result = av_read_frame(context, packet);
        av_packet_unref(packet);
        if (read_result < 0) break;
    }
    av_packet_free(&packet);
    if (!ready()) return false;
    const int seek_result = avformat_seek_file(
        context, -1, (std::numeric_limits<std::int64_t>::min)(), 0,
        (std::numeric_limits<std::int64_t>::max)(), AVSEEK_FLAG_BACKWARD);
    if (seek_result < 0) return false;
    avformat_flush(context);
    return true;
}

AVCodecID cover_codec_id(const std::string& mime_type)
{
    if (mime_type == "image/png") return AV_CODEC_ID_PNG;
    if (mime_type == "image/webp") return AV_CODEC_ID_WEBP;
    if (mime_type == "image/gif") return AV_CODEC_ID_GIF;
    if (mime_type == "image/bmp") return AV_CODEC_ID_BMP;
    return AV_CODEC_ID_MJPEG;
}

bool cover_dimensions(const std::string& mime_type, const unsigned char* data,
                      const std::size_t size, int& width, int& height)
{
    if (data == nullptr || size < 10) {
        return false;
    }
    const auto be32 = [data](const std::size_t offset) {
        return (static_cast<unsigned int>(data[offset]) << 24U)
            | (static_cast<unsigned int>(data[offset + 1]) << 16U)
            | (static_cast<unsigned int>(data[offset + 2]) << 8U)
            | static_cast<unsigned int>(data[offset + 3]);
    };
    if (mime_type == "image/png" && size >= 24
        && std::memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
        width = static_cast<int>(be32(16));
        height = static_cast<int>(be32(20));
        return width > 0 && height > 0;
    }
    if ((mime_type == "image/jpeg" || mime_type == "image/jpg")
        && data[0] == 0xff && data[1] == 0xd8) {
        for (std::size_t offset = 2; offset + 9 < size;) {
            if (data[offset] != 0xff) { ++offset; continue; }
            const unsigned char marker = data[offset + 1];
            if (marker == 0xd8 || marker == 0xd9) { offset += 2; continue; }
            if (offset + 4 >= size) break;
            const std::size_t length = (static_cast<std::size_t>(data[offset + 2]) << 8U)
                | data[offset + 3];
            const bool sof = marker >= 0xc0 && marker <= 0xc3;
            if (sof && offset + 8 < size) {
                height = (static_cast<int>(data[offset + 5]) << 8) | data[offset + 6];
                width = (static_cast<int>(data[offset + 7]) << 8) | data[offset + 8];
                return width > 0 && height > 0;
            }
            if (length < 2 || offset + 2 + length > size) break;
            offset += 2 + length;
        }
    }
    if (mime_type == "image/bmp" && size >= 26
        && data[0] == 'B' && data[1] == 'M') {
        const auto le32 = [data](const std::size_t offset) {
            return static_cast<unsigned int>(data[offset])
                | (static_cast<unsigned int>(data[offset + 1]) << 8U)
                | (static_cast<unsigned int>(data[offset + 2]) << 16U)
                | (static_cast<unsigned int>(data[offset + 3]) << 24U);
        };
        width = static_cast<int>(le32(18));
        height = static_cast<int>(le32(22));
        return width > 0 && height != 0;
    }
    return false;
}

const char* canonical_key(const CanonicalField field)
{
    switch (field) {
    case CanonicalField::Title: return "title";
    case CanonicalField::Artist: return "artist";
    case CanonicalField::Album: return "album";
    case CanonicalField::AlbumArtist: return "album_artist";
    case CanonicalField::Genre: return "genre";
    case CanonicalField::Year: return "year";
    case CanonicalField::Date: return "date";
    case CanonicalField::Composer: return "composer";
    case CanonicalField::Bpm: return "bpm";
    case CanonicalField::CustomTag: return "AGPLAYER_TAG";
    }
    return "";
}

std::string folded_tag_key(const char* key)
{
    std::string folded;
    if (key == nullptr) return folded;
    for (const unsigned char byte : std::string(key)) {
        if (byte < 0x80U && std::isalnum(byte)) {
            folded.push_back(static_cast<char>(std::tolower(byte)));
        } else if (byte >= 0x80U) {
            folded.push_back(static_cast<char>(byte));
        }
    }
    return folded;
}

bool is_alias_key(const char* key, const CanonicalField field)
{
    const std::string candidate = folded_tag_key(key);
    return std::any_of(known_metadata_aliases(field).begin(),
                       known_metadata_aliases(field).end(),
                       [&candidate](const char* alias) {
                           return candidate == folded_tag_key(alias);
                       });
}

void clear_field_aliases(AVDictionary** dictionary, const CanonicalField field)
{
    std::vector<std::string> keys;
    const AVDictionaryEntry* entry = nullptr;
    while ((entry = av_dict_get(*dictionary, "", entry,
                                AV_DICT_IGNORE_SUFFIX)) != nullptr) {
        if (is_alias_key(entry->key, field)) keys.emplace_back(entry->key);
    }
    for (const std::string& key : keys) av_dict_set(dictionary, key.c_str(), nullptr, 0);
}

std::string read_canonical_value(AVDictionary* preferred,
                                 AVDictionary* fallback,
                                 const CanonicalField field)
{
    for (const char* alias : known_metadata_aliases(field)) {
        const AVDictionaryEntry* entry = av_dict_get(preferred, alias, nullptr, 0);
        if (entry == nullptr) entry = av_dict_get(fallback, alias, nullptr, 0);
        if (entry != nullptr && entry->value != nullptr && entry->value[0] != '\0') {
            return entry->value;
        }
    }
    return {};
}

struct MetadataSnapshot {
    using Dictionary = std::vector<std::pair<std::string, std::string>>;

    struct Cover {
        AVCodecID codec_id = AV_CODEC_ID_NONE;
        int width = 0;
        int height = 0;
        std::vector<unsigned char> data;

        bool operator==(const Cover& other) const
        {
            return codec_id == other.codec_id && width == other.width
                && height == other.height && data == other.data;
        }
    };
    struct Stream {
        AVMediaType type = AVMEDIA_TYPE_UNKNOWN;
        AVCodecID codec_id = AV_CODEC_ID_NONE;
        int disposition = 0;
        bool attached_picture = false;
        Dictionary metadata;
    };
    struct Chapter {
        std::int64_t id = 0;
        AVRational time_base{0, 1};
        std::int64_t start = 0;
        std::int64_t end = 0;
        Dictionary metadata;

        bool operator==(const Chapter& other) const
        {
            return id == other.id && time_base.num == other.time_base.num
                && time_base.den == other.time_base.den
                && start == other.start && end == other.end
                && metadata == other.metadata;
        }
    };
    std::array<std::string, 10> fields;
    Dictionary format_metadata;
    std::vector<Stream> streams;
    std::vector<Chapter> chapter_details;
    std::vector<unsigned char> cover;
    std::string cover_mime;
    std::vector<Cover> covers;
    std::size_t audio_streams = 0;
    std::size_t chapters = 0;
    std::size_t attachments = 0;
};

MetadataSnapshot::Dictionary snapshot_dictionary(AVDictionary* dictionary)
{
    MetadataSnapshot::Dictionary entries;
    const AVDictionaryEntry* entry = nullptr;
    while ((entry = av_dict_get(dictionary, "", entry,
                                AV_DICT_IGNORE_SUFFIX)) != nullptr) {
        entries.emplace_back(entry->key != nullptr ? entry->key : "",
                             entry->value != nullptr ? entry->value : "");
    }
    std::sort(entries.begin(), entries.end());
    return entries;
}

std::string explicit_muxer_for_path(const std::filesystem::path& path);
bool muxer_shares_year_date(const std::string& muxer);

constexpr std::array<CanonicalField, 10> kCanonicalFields{
    CanonicalField::Title, CanonicalField::Artist, CanonicalField::Album,
    CanonicalField::AlbumArtist, CanonicalField::Genre, CanonicalField::Year,
    CanonicalField::Date, CanonicalField::Composer, CanonicalField::Bpm,
    CanonicalField::CustomTag};

std::size_t canonical_index(const CanonicalField field)
{
    switch (field) {
    case CanonicalField::Title: return 0;
    case CanonicalField::Artist: return 1;
    case CanonicalField::Album: return 2;
    case CanonicalField::AlbumArtist: return 3;
    case CanonicalField::Genre: return 4;
    case CanonicalField::Year: return 5;
    case CanonicalField::Date: return 6;
    case CanonicalField::Composer: return 7;
    case CanonicalField::Bpm: return 8;
    case CanonicalField::CustomTag: return 9;
    }
    return 0;
}

bool read_metadata_snapshot(const std::string& path,
                            MetadataSnapshot& snapshot,
                            MetadataRuntimeMetrics* runtime = nullptr)
{
    snapshot = {};
    AVFormatContext* context = nullptr;
    if (open_metadata_demuxer(&context, path, runtime) < 0) {
        avformat_close_input(&context);
        return false;
    }
    AVDictionary* audio_metadata = nullptr;
    snapshot.chapters = context->nb_chapters;
    snapshot.format_metadata = snapshot_dictionary(context->metadata);
    for (unsigned int index = 0; index < context->nb_streams; ++index) {
        AVStream* stream = context->streams[index];
        MetadataSnapshot::Stream stream_snapshot;
        stream_snapshot.type = stream->codecpar->codec_type;
        stream_snapshot.codec_id = stream->codecpar->codec_id;
        stream_snapshot.disposition = stream->disposition;
        stream_snapshot.attached_picture =
            (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;
        stream_snapshot.metadata = snapshot_dictionary(stream->metadata);
        snapshot.streams.push_back(std::move(stream_snapshot));
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            ++snapshot.audio_streams;
        }
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_ATTACHMENT) {
            ++snapshot.attachments;
        }
        if (audio_metadata == nullptr
            && stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_metadata = stream->metadata;
        }
        if ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0
            && stream->attached_pic.data != nullptr && stream->attached_pic.size > 0) {
            MetadataSnapshot::Cover cover;
            cover.codec_id = stream->codecpar->codec_id;
            cover.width = stream->codecpar->width;
            cover.height = stream->codecpar->height;
            cover.data.assign(stream->attached_pic.data,
                              stream->attached_pic.data + stream->attached_pic.size);
            snapshot.covers.push_back(cover);
            if (snapshot.cover.empty()) {
                snapshot.cover = cover.data;
                switch (stream->codecpar->codec_id) {
                case AV_CODEC_ID_PNG: snapshot.cover_mime = "image/png"; break;
                case AV_CODEC_ID_WEBP: snapshot.cover_mime = "image/webp"; break;
                case AV_CODEC_ID_BMP: snapshot.cover_mime = "image/bmp"; break;
                default: snapshot.cover_mime = "image/jpeg"; break;
                }
            }
        }
    }
    snapshot.chapter_details.reserve(context->nb_chapters);
    for (unsigned int index = 0; index < context->nb_chapters; ++index) {
        const AVChapter* chapter = context->chapters[index];
        snapshot.chapter_details.push_back({chapter->id, chapter->time_base,
            chapter->start, chapter->end, snapshot_dictionary(chapter->metadata)});
    }
    for (const CanonicalField field : kCanonicalFields) {
        snapshot.fields[canonical_index(field)] =
            read_canonical_value(audio_metadata, context->metadata, field);
    }
    if (muxer_shares_year_date(explicit_muxer_for_path(
            filesystem_path_from_utf8(path)))) {
        const std::string& year =
            snapshot.fields[canonical_index(CanonicalField::Year)];
        const std::string& date =
            snapshot.fields[canonical_index(CanonicalField::Date)];
        const std::string shared = !date.empty() ? date : year;
        snapshot.fields[canonical_index(CanonicalField::Year)] = shared;
        snapshot.fields[canonical_index(CanonicalField::Date)] = shared;
    }
    avformat_close_input(&context);
    return true;
}

bool audio_packet_signatures(const std::string& path,
                             std::vector<AudioStreamEvidence>& signatures,
                             MetadataRuntimeMetrics* runtime)
{
    AVFormatContext* context = nullptr;
    if (open_metadata_demuxer(&context, path, runtime) < 0) {
        avformat_close_input(&context);
        return false;
    }
    std::vector<int> mapping(context->nb_streams, -1);
    for (unsigned int index = 0; index < context->nb_streams; ++index) {
        const AVStream* stream = context->streams[index];
        const AVCodecParameters* parameters = stream->codecpar;
        if (parameters->codec_type != AVMEDIA_TYPE_AUDIO) continue;
        mapping[index] = static_cast<int>(signatures.size());
        signatures.push_back({static_cast<int>(parameters->codec_id), parameters->sample_rate,
            parameters->ch_layout.nb_channels, parameters->format,
            parameters->bits_per_coded_sample, parameters->bits_per_raw_sample,
            stream->time_base.num, stream->time_base.den, stream->duration,
            1469598103934665603ULL, 0, 0, 1469598103934665603ULL});
    }
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) {
        avformat_close_input(&context);
        return false;
    }
    int read_result = 0;
    while ((read_result = av_read_frame(context, packet)) >= 0) {
        if (packet->stream_index >= 0
            && packet->stream_index < static_cast<int>(mapping.size())) {
            const int audio_index = mapping[static_cast<std::size_t>(packet->stream_index)];
            if (audio_index >= 0) {
                AudioStreamEvidence& signature =
                    signatures[static_cast<std::size_t>(audio_index)];
                for (int offset = 0; offset < packet->size; ++offset) {
                    signature.payload_hash ^= packet->data[offset];
                    signature.payload_hash *= 1099511628211ULL;
                }
                signature.payload_bytes += static_cast<std::uint64_t>(packet->size);
                ++signature.packet_count;
                const std::array<std::int64_t, 3> timing{
                    packet->pts, packet->dts, packet->duration};
                for (const std::int64_t value : timing) {
                    for (unsigned int byte = 0; byte < sizeof(value); ++byte) {
                        signature.timestamp_hash ^=
                            static_cast<std::uint8_t>(value >> (byte * 8U));
                        signature.timestamp_hash *= 1099511628211ULL;
                    }
                }
            }
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    avformat_close_input(&context);
    return read_result == AVERROR_EOF;
}

AudioEquivalence audio_streams_equivalent(const std::string& source_path,
                                          const std::string& staged_path,
                                          MetadataRuntimeMetrics* runtime)
{
    std::vector<AudioStreamEvidence> source;
    std::vector<AudioStreamEvidence> staged;
    if (!audio_packet_signatures(source_path, source, runtime)
        || !audio_packet_signatures(staged_path, staged, runtime)) {
        return AudioEquivalence::Different;
    }
    return classify_audio_stream_evidence(source, staged);
}

class SourceCommitGuard final {
public:
    explicit SourceCommitGuard(const std::filesystem::path& path)
        : path_(path)
    {
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) return;
        if (LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK
                | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD,
                &lock_range_) == 0) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            return;
        }
        locked_ = true;
#else
        descriptor_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (descriptor_ < 0) return;
        if (::flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
            ::close(descriptor_);
            descriptor_ = -1;
            return;
        }
        locked_ = true;
#endif
    }

    ~SourceCommitGuard()
    {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            if (locked_) {
                UnlockFileEx(handle_, 0, MAXDWORD, MAXDWORD, &lock_range_);
            }
            CloseHandle(handle_);
        }
#else
        if (descriptor_ >= 0) {
            if (locked_) ::flock(descriptor_, LOCK_UN);
            ::close(descriptor_);
        }
#endif
    }

    SourceCommitGuard(const SourceCommitGuard&) = delete;
    SourceCommitGuard& operator=(const SourceCommitGuard&) = delete;

    [[nodiscard]] bool valid() const noexcept
    {
#ifdef _WIN32
        return handle_ != INVALID_HANDLE_VALUE && locked_;
#else
        return descriptor_ >= 0 && locked_;
#endif
    }

    [[nodiscard]] bool fingerprint(FileSha256& digest) const
    {
        if (!valid()) return false;
        AVSHA* sha = av_sha_alloc();
        if (sha == nullptr || av_sha_init(sha, 256) < 0) {
            av_free(sha);
            return false;
        }
        std::array<unsigned char, 64U * 1024U> buffer{};
        bool ok = true;
#ifdef _WIN32
        LARGE_INTEGER zero{};
        LARGE_INTEGER original{};
        ok = SetFilePointerEx(handle_, zero, &original, FILE_CURRENT) != 0
            && SetFilePointerEx(handle_, zero, nullptr, FILE_BEGIN) != 0;
        while (ok) {
            DWORD read = 0;
            if (ReadFile(handle_, buffer.data(),
                         static_cast<DWORD>(buffer.size()), &read, nullptr) == 0) {
                ok = false;
                break;
            }
            if (read == 0) break;
            av_sha_update(sha, buffer.data(), read);
        }
        if (SetFilePointerEx(handle_, original, nullptr, FILE_BEGIN) == 0)
            ok = false;
#else
        off_t offset = 0;
        while (ok) {
            const ssize_t read = ::pread(descriptor_, buffer.data(),
                                         buffer.size(), offset);
            if (read < 0) {
                ok = false;
                break;
            }
            if (read == 0) break;
            av_sha_update(sha, buffer.data(),
                          static_cast<unsigned int>(read));
            offset += read;
        }
#endif
        if (ok) av_sha_final(sha, digest.data());
        av_free(sha);
        return ok;
    }

    [[nodiscard]] bool matchesPath(
        const std::filesystem::path& candidate) const
    {
        if (!valid()) return false;
#ifdef _WIN32
        BY_HANDLE_FILE_INFORMATION locked_info{};
        if (GetFileInformationByHandle(handle_, &locked_info) == 0) {
            return false;
        }
        HANDLE candidate_handle = CreateFileW(candidate.c_str(),
            FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (candidate_handle == INVALID_HANDLE_VALUE) return false;
        BY_HANDLE_FILE_INFORMATION candidate_info{};
        const bool read = GetFileInformationByHandle(
            candidate_handle, &candidate_info) != 0;
        CloseHandle(candidate_handle);
        return read
            && locked_info.dwVolumeSerialNumber
                == candidate_info.dwVolumeSerialNumber
            && locked_info.nFileIndexHigh == candidate_info.nFileIndexHigh
            && locked_info.nFileIndexLow == candidate_info.nFileIndexLow;
#else
        struct stat locked_info{};
        struct stat candidate_info{};
        return ::fstat(descriptor_, &locked_info) == 0
            && ::lstat(candidate.c_str(), &candidate_info) == 0
            && locked_info.st_dev == candidate_info.st_dev
            && locked_info.st_ino == candidate_info.st_ino;
#endif
    }

    [[nodiscard]] bool copyTo(const std::filesystem::path& destination) const
    {
        if (!matchesPath(path_)) return false;
#ifdef _WIN32
        const bool copied = CopyFileW(path_.c_str(), destination.c_str(), TRUE)
            != 0;
#else
        std::error_code copy_error;
        const bool copied = std::filesystem::copy_file(
            path_, destination, std::filesystem::copy_options::none,
            copy_error) && !copy_error;
#endif
        return copied && matchesPath(path_);
    }

private:
    std::filesystem::path path_;
    bool locked_ = false;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    OVERLAPPED lock_range_{};
#else
    int descriptor_ = -1;
#endif
};

bool fail_at(const MetadataWriterTestHooks* hooks, const MetadataFailurePoint point)
{
    return hooks != nullptr && hooks->fail_at == point;
}

bool file_can_be_replaced(const std::filesystem::path& path)
{
#ifdef _WIN32
    HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    CloseHandle(handle);
#endif
    return true;
}

bool source_is_read_only(const std::filesystem::path& path)
{
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_READONLY) != 0;
#else
    const auto permissions = std::filesystem::status(path).permissions();
    using P = std::filesystem::perms;
    return (permissions & (P::owner_write | P::group_write | P::others_write)) == P::none;
#endif
}

bool add_space_requirement(std::uintmax_t& total,
                           const std::uintmax_t value) noexcept
{
    if (value > (std::numeric_limits<std::uintmax_t>::max)() - total) return false;
    total += value;
    return true;
}

const char* cover_mime_type(const AVCodecID codec_id)
{
    switch (codec_id) {
    case AV_CODEC_ID_PNG: return "image/png";
    case AV_CODEC_ID_BMP: return "image/bmp";
    case AV_CODEC_ID_MJPEG: return "image/jpeg";
    default: return "";
    }
}

bool has_staging_space(const std::filesystem::path& source,
                       const MetadataEditPlan& plan)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(source, ec);
    if (ec) return false;
    const auto info = std::filesystem::space(source.parent_path(), ec);
    if (ec) return false;
    const auto required = metadata_staging_space_required(size, plan);
    return required.has_value() && info.available >= *required;
}

std::string explicit_muxer_for_path(const std::filesystem::path& path)
{
    std::string extension = path.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    if (extension == ".wav") return "wav";
    if (extension == ".mp3") return "mp3";
    if (extension == ".flac") return "flac";
    if (extension == ".aac") return "adts";
    if (extension == ".ogg" || extension == ".opus") return "ogg";
    if (extension == ".m4a") return "ipod";
    if (extension == ".aif" || extension == ".aiff") return "aiff";
    if (extension == ".wma") return "asf";
    if (extension == ".ape") return "ape";
    return {};
}

bool muxer_supports_canonical_field(const std::string& muxer,
                                    const CanonicalField field)
{
    if (muxer == "wav") {
        return field == CanonicalField::Title || field == CanonicalField::Artist
            || field == CanonicalField::Album || field == CanonicalField::Genre
            || field == CanonicalField::Year || field == CanonicalField::Date;
    }
    return muxer == "mp3" || muxer == "flac" || muxer == "adts"
        || muxer == "ogg"
        || muxer == "ipod" || muxer == "asf";
}

bool muxer_supports_edit(const std::string& muxer, const FieldEdit& edit)
{
    if (!muxer_supports_canonical_field(muxer, edit.field)) return false;
    if (muxer == "ipod" && edit.field == CanonicalField::Bpm
        && edit.action == MetadataAction::Set) {
        // libavformat's ipod/mov muxer silently discards BPM/tmpo metadata.
        // Reject Set in preflight instead of reporting a false success.
        return false;
    }
    return true;
}

bool muxer_shares_year_date(const std::string& muxer)
{
    return muxer == "wav" || muxer == "mp3" || muxer == "ipod"
        || muxer == "mp4" || muxer == "mov";
}

const FieldEdit* plan_edit(const MetadataEditPlan& plan,
                           const CanonicalField field)
{
    for (const FieldEdit& edit : plan.fields) {
        if (edit.field == field) return &edit;
    }
    return nullptr;
}

bool edits_are_equivalent(const FieldEdit& first, const FieldEdit& second)
{
    if (first.action != second.action) return false;
    return first.action != MetadataAction::Set
        || first.value_utf8 == second.value_utf8;
}

} // namespace

ag_result write_metadata_with_preserved_backup(
    const std::string& utf8_path,
    const MetadataUpdate& update,
    std::string& error,
    const MetadataEditPlan* verification_plan,
    const std::atomic_bool* cancel,
    const MetadataWriterTestHooks* test_hooks,
    MetadataRuntimeMetrics* runtime,
    AudioEquivalence* audio_equivalence,
    ExistingBackupGuard* existing_backup);

std::optional<std::uintmax_t> metadata_staging_space_required(
    const std::uintmax_t source_size, const MetadataEditPlan& plan) noexcept
{
    std::uintmax_t required = kMetadataWriteMargin;
    if (!add_space_requirement(required, source_size)
        || !add_space_requirement(required, source_size)) {
        return std::nullopt;
    }
    for (const FieldEdit& edit : plan.fields) {
        if (edit.action == MetadataAction::Set && edit.value_utf8.has_value()
            && !add_space_requirement(required, edit.value_utf8->size())) {
            return std::nullopt;
        }
    }
    if (plan.cover_action == CoverAction::Set
        && !add_space_requirement(required, plan.cover_size)) {
        return std::nullopt;
    }
    return required;
}

bool validate_metadata_edit_plan(const MetadataEditPlan& plan,
                                 std::string& error)
{
    if (plan.cover_action == CoverAction::Set
        && (plan.cover_data == nullptr || plan.cover_size == 0
            || plan.cover_mime_type.empty())) {
        error = "A replacement cover needs image data and a MIME type";
        return false;
    }
    if (plan.cover_action == CoverAction::Set
        && plan.cover_size > kMaxMetadataCoverBytes) {
        error = "Replacement cover is too large";
        return false;
    }
    if (plan.cover_action == CoverAction::Set) {
        const bool known_mime = plan.cover_mime_type == "image/jpeg"
            || plan.cover_mime_type == "image/jpg"
            || plan.cover_mime_type == "image/png"
            || plan.cover_mime_type == "image/bmp";
        int width = 0;
        int height = 0;
        if (!known_mime || !cover_dimensions(plan.cover_mime_type,
                                              plan.cover_data,
                                              plan.cover_size,
                                              width, height)) {
            error = "Replacement cover content does not match its MIME type";
            return false;
        }
    }
    bool has_edit = plan.cover_action != CoverAction::Keep;
    for (const FieldEdit& edit : plan.fields) {
        if (edit.action == MetadataAction::Keep) {
            continue;
        }
        has_edit = true;
        if (edit.action == MetadataAction::Set
            && (!edit.value_utf8.has_value() || edit.value_utf8->empty())) {
            error = "A Set value must not be empty; use Clear instead";
            return false;
        }
        if (edit.action == MetadataAction::Set
            && edit.value_utf8->size() > kMaxMetadataFieldBytes) {
            error = "Metadata field is too large";
            return false;
        }
        if (edit.field == CanonicalField::Bpm
            && edit.action == MetadataAction::Set) {
            std::size_t parsed = 0;
            double bpm = 0.0;
            try {
                bpm = std::stod(*edit.value_utf8, &parsed);
            } catch (...) {
                error = "BPM must be a positive number";
                return false;
            }
            if (parsed != edit.value_utf8->size() || !std::isfinite(bpm)
                || bpm <= 0.0) {
                error = "BPM must be a positive number";
                return false;
            }
        }
    }
    if (!has_edit) {
        error = "At least one metadata field or cover action is required";
        return false;
    }
    error.clear();
    return true;
}

MetadataSnapshot::Dictionary unmanaged_dictionary(
    const MetadataSnapshot::Dictionary& dictionary,
    const MetadataEditPlan& plan,
    const bool exclude_muxer_owned_fields = false)
{
    MetadataSnapshot::Dictionary unmanaged;
    for (const auto& entry : dictionary) {
        // libavformat writes this container-level implementation tag itself
        // during remuxing (for example "Lavf59.6.100").  It is not the
        // user-editable encoded-by field, which is stored as `encoded_by`.
        // Requiring its byte-for-byte preservation rejects otherwise valid
        // metadata-only writes produced by a different FFmpeg build.
        if (exclude_muxer_owned_fields
            && av_strcasecmp(entry.first.c_str(), "encoder") == 0) {
            continue;
        }
        bool managed = false;
        for (const FieldEdit& edit : plan.fields) {
            if (edit.action != MetadataAction::Keep
                && is_alias_key(entry.first.c_str(), edit.field)) {
                managed = true;
                break;
            }
        }
        if (!managed) unmanaged.push_back(entry);
    }
    return unmanaged;
}

bool dictionary_entries_preserved(
    const MetadataSnapshot::Dictionary& before,
    const MetadataSnapshot::Dictionary& after,
    const MetadataEditPlan& plan,
    const bool exclude_managed_fields = true,
    const bool exclude_muxer_owned_fields = false)
{
    const MetadataSnapshot::Dictionary required = exclude_managed_fields
        ? unmanaged_dictionary(before, plan, exclude_muxer_owned_fields) : before;
    const MetadataSnapshot::Dictionary actual = exclude_managed_fields
        ? unmanaged_dictionary(after, plan, exclude_muxer_owned_fields) : after;
    return std::includes(actual.begin(), actual.end(),
                         required.begin(), required.end());
}

bool unmanaged_metadata_preserved(const MetadataSnapshot& before,
                                  const MetadataSnapshot& after,
                                  const MetadataEditPlan& plan)
{
    if (!dictionary_entries_preserved(before.format_metadata,
                                      after.format_metadata, plan, true, true)
        || before.chapter_details != after.chapter_details) {
        return false;
    }

    std::vector<const MetadataSnapshot::Stream*> before_streams;
    std::vector<const MetadataSnapshot::Stream*> after_streams;
    const auto collect_streams = [&plan](const MetadataSnapshot& snapshot,
                                        auto& destination) {
        for (const MetadataSnapshot::Stream& stream : snapshot.streams) {
            if (plan.cover_action != CoverAction::Keep
                && stream.attached_picture) {
                continue;
            }
            destination.push_back(&stream);
        }
    };
    collect_streams(before, before_streams);
    collect_streams(after, after_streams);
    if (before_streams.size() != after_streams.size()) return false;
    for (std::size_t index = 0; index < before_streams.size(); ++index) {
        const MetadataSnapshot::Stream& left = *before_streams[index];
        const MetadataSnapshot::Stream& right = *after_streams[index];
        if (left.type != right.type || left.codec_id != right.codec_id
            || left.disposition != right.disposition
            || left.attached_picture != right.attached_picture
            || !dictionary_entries_preserved(left.metadata, right.metadata,
                                             plan,
                                             left.type == AVMEDIA_TYPE_AUDIO)) {
            return false;
        }
    }
    return true;
}

ag_result preflight_metadata_edit(const std::string& utf8_path,
                                  const MetadataEditPlan& plan,
                                  std::string& error)
{
    if (!validate_metadata_edit_plan(plan, error)) return AG_INVALID_ARGUMENT;

    const std::filesystem::path source = filesystem_path_from_utf8(utf8_path);
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(source, filesystem_error)) {
        error = "Input file is not a readable regular file";
        return AG_IO_ERROR;
    }
    if (source_is_read_only(source)) {
        error = "Input file is read-only";
        return AG_IO_ERROR;
    }
    if (!file_can_be_replaced(source)) {
        error = "Input file is in use and cannot be replaced";
        return AG_IO_ERROR;
    }
    if (!has_staging_space(source, plan)) {
        error = "Insufficient disk space for staged metadata output";
        return AG_IO_ERROR;
    }
    const std::string muxer_name = explicit_muxer_for_path(source);
    if (muxer_name.empty()) {
        error = "No safe metadata-only adapter is registered for this container";
        return AG_UNSUPPORTED_FORMAT;
    }
    for (const FieldEdit& edit : plan.fields) {
        if (edit.action != MetadataAction::Keep
            && !muxer_supports_edit(muxer_name, edit)) {
            error = "One or more requested fields are not supported by this container";
            return AG_UNSUPPORTED_FORMAT;
        }
    }
    const FieldEdit* year_edit = plan_edit(plan, CanonicalField::Year);
    const FieldEdit* date_edit = plan_edit(plan, CanonicalField::Date);
    const bool changes_shared_date =
        (year_edit != nullptr && year_edit->action != MetadataAction::Keep)
        || (date_edit != nullptr && date_edit->action != MetadataAction::Keep);
    if (muxer_shares_year_date(muxer_name) && changes_shared_date
        && (year_edit == nullptr || date_edit == nullptr
            || !edits_are_equivalent(*year_edit, *date_edit))) {
        error = "Year and date conflict because this container stores one physical date tag";
        return AG_UNSUPPORTED_FORMAT;
    }

    AVFormatContext* input = nullptr;
    if (open_metadata_demuxer(&input, utf8_path, nullptr) < 0) {
        error = "Input container cannot be opened";
        return AG_IO_ERROR;
    }
    const auto close_input = [&input] { avformat_close_input(&input); };
    if (input->nb_programs > 0) {
        close_input();
        error = "Input contains program structures that cannot be preserved losslessly";
        return AG_UNSUPPORTED_FORMAT;
    }
    bool has_audio = false;
    for (unsigned int index = 0; index < input->nb_streams; ++index) {
        const AVStream* stream = input->streams[index];
        const AVMediaType type = stream->codecpar->codec_type;
        const bool attached_picture =
            (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;
        if (type == AVMEDIA_TYPE_AUDIO) {
            has_audio = true;
        }
        if (type != AVMEDIA_TYPE_AUDIO && !attached_picture) {
            close_input();
            error = "Input contains non-audio structures that cannot be preserved losslessly";
            return AG_UNSUPPORTED_FORMAT;
        }
    }
    if (!has_audio) {
        close_input();
        error = "Input has no audio stream";
        return AG_IO_ERROR;
    }
    AVFormatContext* output = nullptr;
    if (avformat_alloc_output_context2(&output, nullptr, muxer_name.c_str(),
                                       utf8_path.c_str()) < 0
        || output == nullptr) {
        close_input();
        error = "No writable muxer is available for this container";
        return AG_IO_ERROR;
    }
    for (unsigned int index = 0; index < input->nb_streams; ++index) {
        const AVStream* stream = input->streams[index];
        const bool attached_picture =
            (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;
        if (attached_picture) continue;
        if (avformat_query_codec(output->oformat, stream->codecpar->codec_id,
                                 FF_COMPLIANCE_NORMAL) == 0) {
            close_input();
            avformat_free_context(output);
            error = "One or more input streams cannot be safely preserved by this muxer";
            return AG_UNSUPPORTED_FORMAT;
        }
    }
    if (plan.cover_action == CoverAction::Set) {
        const std::string muxer = output->oformat != nullptr
            && output->oformat->name != nullptr ? output->oformat->name : "";
        const bool supports_cover = muxer == "mp3" || muxer == "flac"
            || muxer == "ipod" || muxer == "mp4" || muxer == "mov";
        if (!supports_cover) {
            close_input();
            avformat_free_context(output);
            error = "Cover art is not supported by this container";
            return AG_UNSUPPORTED_FORMAT;
        }
    }
    close_input();
    avformat_free_context(output);
    error.clear();
    return AG_OK;
}

ag_result preflight_metadata_edit(const std::string& utf8_path,
                                  const MetadataEditPlan& plan,
                                  MetadataPreflightReport& report)
{
    report = {};
    std::string error;
    const ag_result result = preflight_metadata_edit(utf8_path, plan, error);
    report.supported = result == AG_OK;
    report.raw_error = error;
    report.user_message = error;
    if (result == AG_INVALID_ARGUMENT) {
        report.error_code = MetadataErrorCode::InvalidEditPlan;
    } else if (result == AG_UNSUPPORTED_FORMAT) {
        report.error_code = error.find("one physical date tag") != std::string::npos
            ? MetadataErrorCode::PhysicalTagConflict
            : error.find("Cover art") != std::string::npos
                ? MetadataErrorCode::UnsupportedCover
                : error.find("structures") != std::string::npos
                    ? MetadataErrorCode::UnsupportedStructure
                    : MetadataErrorCode::UnsupportedContainer;
    } else if (result != AG_OK) {
        if (error.find("read-only") != std::string::npos) {
            report.error_code = MetadataErrorCode::ReadOnlyFile;
        } else if (error.find("in use") != std::string::npos) {
            report.error_code = MetadataErrorCode::FileInUse;
        } else if (error.find("disk space") != std::string::npos) {
            report.error_code = MetadataErrorCode::InsufficientDiskSpace;
        } else if (error.find("writable muxer") != std::string::npos) {
            report.error_code = MetadataErrorCode::UnsupportedMuxer;
        } else {
            report.error_code = MetadataErrorCode::InputOpenFailed;
        }
    }
    const std::string muxer = explicit_muxer_for_path(
        filesystem_path_from_utf8(utf8_path));
    for (const FieldEdit& edit : plan.fields) {
        if (edit.action != MetadataAction::Keep
            && !muxer_supports_edit(muxer, edit)) {
            report.unsupported_fields.push_back(edit.field);
        }
    }
    if (!report.unsupported_fields.empty()) {
        report.error_code = MetadataErrorCode::UnsupportedField;
    }
    AVFormatContext* input = nullptr;
    if (open_metadata_demuxer(&input, utf8_path, nullptr) >= 0
        && input != nullptr && input->iformat != nullptr) {
        report.container = input->iformat->name != nullptr
            ? input->iformat->name : "";
    }
    avformat_close_input(&input);
    return result;
}

namespace {

std::optional<std::string>* update_slot(MetadataUpdate& update,
                                        const CanonicalField field)
{
    switch (field) {
    case CanonicalField::Title: return &update.title;
    case CanonicalField::Artist: return &update.artist;
    case CanonicalField::Album: return &update.album;
    case CanonicalField::AlbumArtist: return &update.album_artist;
    case CanonicalField::Genre: return &update.genre;
    case CanonicalField::Year: return &update.year;
    case CanonicalField::Date: return &update.date;
    case CanonicalField::Composer: return &update.composer;
    case CanonicalField::Bpm: return &update.bpm;
    case CanonicalField::CustomTag: return &update.custom_tag;
    }
    return nullptr;
}

MetadataErrorCode metadata_error_code_for_write(const ag_result result,
                                                const std::string& error)
{
    if (result == AG_CANCELLED) return MetadataErrorCode::Cancelled;
    if (error.find("open input") != std::string::npos
        || error.find("readable regular file") != std::string::npos) {
        return MetadataErrorCode::InputOpenFailed;
    }
    if (error.find("open temp output") != std::string::npos
        || error.find("output context") != std::string::npos
        || error.find("output stream") != std::string::npos) {
        return MetadataErrorCode::OutputCreateFailed;
    }
    if (error.find("write header") != std::string::npos) {
        return MetadataErrorCode::HeaderWriteFailed;
    }
    if (error.find("read packet") != std::string::npos) {
        return MetadataErrorCode::PacketReadFailed;
    }
    if (error.find("write packet") != std::string::npos
        || error.find("cover packet") != std::string::npos) {
        return MetadataErrorCode::PacketWriteFailed;
    }
    if (error.find("write trailer") != std::string::npos) {
        return MetadataErrorCode::TrailerWriteFailed;
    }
    if (error.find("Verification failed") != std::string::npos) {
        return MetadataErrorCode::VerificationFailed;
    }
    if (error.find("Source changed") != std::string::npos) {
        return MetadataErrorCode::SourceChanged;
    }
    if (error.find("commit lock") != std::string::npos) {
        return MetadataErrorCode::FileInUse;
    }
    if (error.find("backup") != std::string::npos
        || error.find("atomically replace") != std::string::npos) {
        return MetadataErrorCode::AtomicReplaceFailed;
    }
    return MetadataErrorCode::InternalError;
}

} // namespace

ag_result write_metadata_plan(const std::string& utf8_path,
                              const MetadataEditPlan& plan,
                              MetadataFileResult& result,
                              const std::atomic_bool* cancel,
                              const MetadataWriterTestHooks* test_hooks)
{
    result = {};
    if (cancel != nullptr && cancel->load(std::memory_order_acquire)) {
        result.final_status = FileResultStatus::Cancelled;
        result.error_code = MetadataErrorCode::Cancelled;
        result.message = "Cancelled before metadata write";
        return AG_CANCELLED;
    }
    MetadataPreflightReport preflight_report;
    const ag_result preflight = preflight_metadata_edit(utf8_path, plan,
                                                        preflight_report);
    if (preflight != AG_OK) {
        result.message = preflight_report.user_message;
        result.error_code = preflight_report.error_code;
        result.final_status = preflight == AG_UNSUPPORTED_FORMAT
            ? FileResultStatus::Unsupported : FileResultStatus::Failed;
        return preflight;
    }

    const std::filesystem::path source = filesystem_path_from_utf8(utf8_path);
    std::error_code ec;
    const auto before_size = std::filesystem::file_size(source, ec);
    const auto before_time = std::filesystem::last_write_time(source, ec);
    if (ec || !std::filesystem::is_regular_file(source)) {
        result.message = "Input file is not a readable regular file";
        result.error_code = MetadataErrorCode::InputOpenFailed;
        return AG_IO_ERROR;
    }

    MetadataSnapshot before_snapshot;
    if (!read_metadata_snapshot(utf8_path, before_snapshot)) {
        result.message = "Input metadata cannot be read";
        result.error_code = MetadataErrorCode::InputOpenFailed;
        return AG_IO_ERROR;
    }
    std::vector<std::string> before_values;
    before_values.reserve(plan.fields.size());
    for (const FieldEdit& edit : plan.fields) {
        before_values.push_back(before_snapshot.fields[canonical_index(edit.field)]);
    }
    const bool had_cover = !before_snapshot.cover.empty();

    MetadataUpdate update;
    update.cover_action = plan.cover_action;
    update.cover_data = plan.cover_data;
    update.cover_size = plan.cover_size;
    update.cover_mime_type = plan.cover_mime_type;
    for (const FieldEdit& edit : plan.fields) {
        if (edit.action == MetadataAction::Keep) continue;
        std::optional<std::string>* slot = update_slot(update, edit.field);
        if (slot != nullptr) *slot = edit.action == MetadataAction::Clear
            ? std::string{} : *edit.value_utf8;
    }
    const std::filesystem::path backup(source.native()
        + std::filesystem::u8path(".agbak").native());
    const std::string backup_nonce = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    ExistingBackupGuard existing_backup(backup, backup_nonce);
    if (!existing_backup.valid()) {
        result.message = "Failed to preserve existing backup before replacement";
        result.error_code = MetadataErrorCode::AtomicReplaceFailed;
        return AG_IO_ERROR;
    }
    std::string error;
    const ag_result write_result = write_metadata_with_preserved_backup(
        utf8_path, update, error, &plan, cancel, test_hooks,
        &result.runtime, &result.audio_equivalence, &existing_backup);
    if (write_result != AG_OK) {
        const bool prior_backup_restored = existing_backup.restore(
            test_hooks != nullptr && test_hooks->fail_backup_restore);
        result.message = error;
        if (!prior_backup_restored) {
            result.message += "; prior backup remains preserved at ";
            result.message += existing_backup.preserved_path();
        }
        result.error_code = metadata_error_code_for_write(write_result, error);
        if (write_result == AG_CANCELLED) {
            result.final_status = FileResultStatus::Cancelled;
        } else if (write_result == AG_UNSUPPORTED_FORMAT) {
            result.final_status = FileResultStatus::Unsupported;
        } else {
            result.final_status = FileResultStatus::Failed;
        }
        return write_result;
    }
    result.used_force_fallback =
        plan.audio_policy == MetadataAudioPolicy::ForceVerifiedNormalization
        && result.audio_equivalence
            == AudioEquivalence::NormalizedPacketTiming;

    const auto restore_original = [&]() {
        if (test_hooks != nullptr && test_hooks->fail_source_restore) {
            return false;
        }
        std::error_code exists_error;
        return std::filesystem::exists(backup, exists_error) && !exists_error
            && atomic_replace(backup, source);
    };
    const auto recover_after_verification = [&](const char* success_message) {
        if (restore_original()) {
            const bool prior_backup_restored = existing_backup.restore(
                test_hooks != nullptr && test_hooks->fail_backup_restore);
            result.message = prior_backup_restored
                ? success_message : "Verification failed and recovery was incomplete";
            if (!prior_backup_restored) {
                result.message += "; prior backup remains preserved at ";
                result.message += existing_backup.preserved_path();
            }
            return prior_backup_restored;
        }

        const ExistingBackupGuard::FailedSourceRecovery recovery =
            existing_backup.preserve_after_source_restore_failure(
                test_hooks != nullptr && test_hooks->fail_backup_restore);
        result.message = "Verification failed and recovery was incomplete";
        if (!recovery.original_backup_path.empty()) {
            result.message += "; original recovery backup remains at ";
            result.message += recovery.original_backup_path;
        } else {
            result.message += "; original recovery backup path is unavailable";
        }
        if (!recovery.prior_backup_path.empty()) {
            result.message += recovery.prior_backup_restored
                ? "; prior backup restored at "
                : "; prior backup remains preserved at ";
            result.message += recovery.prior_backup_path;
        }
        return false;
    };

    // A replacement is only successful after reopening and reading back every
    // requested canonical field. The writer itself uses packet copy only.
    MetadataSnapshot after_snapshot;
    if (fail_at(test_hooks, MetadataFailurePoint::PostReplaceReadback)
        || !read_metadata_snapshot(utf8_path, after_snapshot, &result.runtime)) {
        const bool recovered = recover_after_verification(
            "Verification failed: replacement cannot be reopened; original restored");
        result.error_code = recovered
            ? MetadataErrorCode::VerificationFailed
            : MetadataErrorCode::AtomicReplaceFailed;
        return recovered ? AG_DECODE_ERROR : AG_IO_ERROR;
    }
    bool verified = true;
    std::size_t field_index = 0;
    for (const FieldEdit& edit : plan.fields) {
        const std::string actual = after_snapshot.fields[canonical_index(edit.field)];
        const FieldWriteStatus field_status = edit.action == MetadataAction::Keep
            ? FieldWriteStatus::Kept
            : (edit.action == MetadataAction::Clear
                ? FieldWriteStatus::Cleared : FieldWriteStatus::Updated);
        result.fields.push_back({edit.field, edit.action, actual, {}, field_status,
                                 before_values[field_index],
                                 edit.value_utf8.value_or(std::string{}), {}});
        ++field_index;
        if (edit.action == MetadataAction::Set && actual != *edit.value_utf8) {
            verified = false;
        }
        if (edit.action == MetadataAction::Clear && !actual.empty()) {
            verified = false;
        }
    }
    verified = verified
        && unmanaged_metadata_preserved(before_snapshot, after_snapshot, plan);
    const bool has_cover = !after_snapshot.cover.empty();
    result.cover.requested_action = plan.cover_action;
    result.cover.had_cover = had_cover;
    result.cover.has_cover = has_cover;
    result.cover.mime_type = after_snapshot.cover_mime;
    result.cover.status = plan.cover_action == CoverAction::Keep
        ? FieldWriteStatus::Kept
        : plan.cover_action == CoverAction::Set
            ? (has_cover ? FieldWriteStatus::Updated : FieldWriteStatus::Failed)
            : (!has_cover ? FieldWriteStatus::Cleared : FieldWriteStatus::Failed);
    const bool cover_verified = plan.cover_action == CoverAction::Set
        ? has_cover && after_snapshot.cover.size() == plan.cover_size
            && std::equal(after_snapshot.cover.begin(), after_snapshot.cover.end(),
                          plan.cover_data)
        : plan.cover_action == CoverAction::Clear
            ? !has_cover : after_snapshot.covers == before_snapshot.covers;
    verified = verified && cover_verified;
    if (!verified) {
        const bool recovered = recover_after_verification(
            "Verification failed: metadata readback mismatch; original restored");
        result.used_stream_copy = true;
        result.audio_verified_unchanged = true;
        result.final_status = FileResultStatus::Failed;
        result.error_code = recovered
            ? MetadataErrorCode::VerificationFailed
            : MetadataErrorCode::AtomicReplaceFailed;
        return recovered ? AG_DECODE_ERROR : AG_IO_ERROR;
    }
    result.used_stream_copy = true;
    result.audio_verified_unchanged = true;
    result.final_status = FileResultStatus::Completed;
    result.error_code = MetadataErrorCode::None;
    result.message = result.audio_equivalence
            == AudioEquivalence::NormalizedPacketTiming
        ? "Verified with packet stream copy and normalized container timing"
        : "Verified with exact packet stream copy";
    (void)before_size;
    (void)before_time;
    existing_backup.discard();
    return AG_OK;
}

static ag_result write_metadata_to_temp(const std::string& utf8_path,
                                        const MetadataUpdate& update,
                                        const std::string& temp_path,
                                        std::string& error,
                                        const std::atomic_bool* cancel,
                                        const MetadataWriterTestHooks* test_hooks,
                                        MetadataRuntimeMetrics* runtime)
{
    const std::filesystem::path temp = filesystem_path_from_utf8(temp_path);
    AVFormatContext* in_ctx = nullptr;
    if (open_metadata_demuxer(&in_ctx, utf8_path, runtime) < 0) {
        error = "Failed to open input file";
        return AG_IO_ERROR;
    }
    if (!prime_audio_headers_without_codec(in_ctx, utf8_path)) {
        avformat_close_input(&in_ctx);
        error = "Failed to read codec parameters without opening a decoder";
        return AG_UNSUPPORTED_FORMAT;
    }
    AVFormatContext* out_ctx = nullptr;
    // Use the original path so FFmpeg guesses the output format from extension.
    const std::string muxer_name =
        explicit_muxer_for_path(filesystem_path_from_utf8(utf8_path));
    if (muxer_name.empty()
        || avformat_alloc_output_context2(&out_ctx, nullptr, muxer_name.c_str(),
                                       utf8_path.c_str()) < 0
        || out_ctx == nullptr) {
        avformat_close_input(&in_ctx);
        error = "Failed to allocate output context";
        return AG_INTERNAL_ERROR;
    }

    // Copy all streams without re-encoding. Attached pictures need an explicit
    // packet reference because demuxers do not emit them as regular packets.
    const bool replace_cover = update.cover_action != CoverAction::Keep;
    std::vector<int> stream_mapping(in_ctx->nb_streams, -1);
    for (unsigned int i = 0; i < in_ctx->nb_streams; ++i) {
        AVStream* in_stream = in_ctx->streams[i];
        const bool attached_picture =
            (in_stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;
        if (replace_cover && attached_picture) {
            continue;
        }
        AVStream* out_stream = avformat_new_stream(out_ctx, nullptr);
        if (out_stream == nullptr) {
            avformat_close_input(&in_ctx);
            cleanup_output(out_ctx);
            error = "Failed to create output stream";
            return AG_INTERNAL_ERROR;
        }
        if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0) {
            avformat_close_input(&in_ctx);
            cleanup_output(out_ctx);
            error = "Failed to copy codec parameters";
            return AG_INTERNAL_ERROR;
        }
        stream_mapping[i] = out_stream->index;
        out_stream->time_base = in_stream->time_base;
        out_stream->disposition = in_stream->disposition;
        av_dict_copy(&out_stream->metadata, in_stream->metadata, 0);
        if (attached_picture && in_stream->attached_pic.data != nullptr
            && in_stream->attached_pic.size > 0) {
            if ((out_stream->codecpar->width <= 0
                 || out_stream->codecpar->height == 0)
                && !cover_dimensions(cover_mime_type(out_stream->codecpar->codec_id),
                                     in_stream->attached_pic.data,
                                     static_cast<std::size_t>(
                                         in_stream->attached_pic.size),
                                     out_stream->codecpar->width,
                                     out_stream->codecpar->height)) {
                avformat_close_input(&in_ctx);
                cleanup_output(out_ctx);
                error = "Failed to preserve cover dimensions without decoding";
                return AG_UNSUPPORTED_FORMAT;
            }
            if (av_packet_ref(&out_stream->attached_pic,
                              &in_stream->attached_pic) < 0) {
                avformat_close_input(&in_ctx);
                cleanup_output(out_ctx);
                error = "Failed to preserve cover image";
                return AG_INTERNAL_ERROR;
            }
            out_stream->attached_pic.stream_index = out_stream->index;
        }
    }

    if (in_ctx->nb_chapters > 0) {
        out_ctx->chapters = static_cast<AVChapter**>(
            av_calloc(in_ctx->nb_chapters, sizeof(*out_ctx->chapters)));
        if (out_ctx->chapters == nullptr) {
            avformat_close_input(&in_ctx);
            cleanup_output(out_ctx);
            error = "Failed to allocate output chapters";
            return AG_INTERNAL_ERROR;
        }
        for (unsigned int index = 0; index < in_ctx->nb_chapters; ++index) {
            const AVChapter* input_chapter = in_ctx->chapters[index];
            AVChapter* output_chapter = static_cast<AVChapter*>(
                av_mallocz(sizeof(*output_chapter)));
            if (output_chapter == nullptr) {
                avformat_close_input(&in_ctx);
                cleanup_output(out_ctx);
                error = "Failed to copy output chapters";
                return AG_INTERNAL_ERROR;
            }
            output_chapter->id = input_chapter->id;
            output_chapter->time_base = input_chapter->time_base;
            output_chapter->start = input_chapter->start;
            output_chapter->end = input_chapter->end;
            av_dict_copy(&output_chapter->metadata, input_chapter->metadata, 0);
            out_ctx->chapters[index] = output_chapter;
            ++out_ctx->nb_chapters;
        }
    }

    if (update.cover_action == CoverAction::Set) {
        if (update.cover_data == nullptr || update.cover_size == 0) {
            avformat_close_input(&in_ctx);
            cleanup_output(out_ctx);
            error = "Cover image data is empty";
            return AG_INVALID_ARGUMENT;
        }
        AVStream* cover_stream = avformat_new_stream(out_ctx, nullptr);
        if (cover_stream == nullptr) {
            avformat_close_input(&in_ctx);
            cleanup_output(out_ctx);
            error = "Failed to create cover stream";
            return AG_INTERNAL_ERROR;
        }
        cover_stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        cover_stream->codecpar->codec_id =
            cover_codec_id(update.cover_mime_type);
        if (!cover_dimensions(update.cover_mime_type,
                              update.cover_data, update.cover_size,
                              cover_stream->codecpar->width,
                              cover_stream->codecpar->height)) {
            avformat_close_input(&in_ctx);
            cleanup_output(out_ctx);
            error = "Cover image cannot be decoded";
            return AG_INVALID_ARGUMENT;
        }
        cover_stream->disposition = AV_DISPOSITION_ATTACHED_PIC;
        cover_stream->time_base = AVRational{1, 1'000};
        if (av_new_packet(&cover_stream->attached_pic,
                          static_cast<int>(update.cover_size)) < 0) {
            avformat_close_input(&in_ctx);
            cleanup_output(out_ctx);
            error = "Failed to allocate cover packet";
            return AG_INTERNAL_ERROR;
        }
        std::memcpy(cover_stream->attached_pic.data, update.cover_data,
                    update.cover_size);
        cover_stream->attached_pic.stream_index = cover_stream->index;
        cover_stream->attached_pic.flags |= AV_PKT_FLAG_KEY;
        av_dict_set(&cover_stream->metadata, "title", "Album cover", 0);
        av_dict_set(&cover_stream->metadata, "comment", "Cover (front)", 0);
    }

    // Copy format-level metadata. Canonical edits remove every known alias
    // from both format and stream dictionaries before one canonical value is
    // written, preventing duplicate/conflicting tags after remuxing.
    av_dict_copy(&out_ctx->metadata, in_ctx->metadata, 0);
    const std::string output_muxer = out_ctx->oformat != nullptr
        && out_ctx->oformat->name != nullptr ? out_ctx->oformat->name : "";
    if (output_muxer == "adts") {
        // Raw AAC has no container-level tag block of its own. FFmpeg's ADTS
        // muxer can safely carry the canonical fields in an ID3v2 prefix while
        // the AAC packets themselves remain byte-for-byte stream-copied.
        av_opt_set(out_ctx->priv_data, "write_id3v2", "1", 0);
    }
    if (test_hooks != nullptr && test_hooks->seed_preservation_fixture) {
        if (output_muxer == "flac") {
            av_dict_set(&out_ctx->metadata, "x-agplayer-private",
                        "private-format-value", 0);
        }
        if (output_muxer == "mp3") {
            av_dict_set(&out_ctx->metadata, "track", "3/12", 0);
            av_dict_set(&out_ctx->metadata, "disc", "1/2", 0);
            av_dict_set(&out_ctx->metadata, "comment", "Comment", 0);
            av_dict_set(&out_ctx->metadata, "lyrics", "Lyrics", 0);
            av_dict_set(&out_ctx->metadata, "copyright", "Copyright", 0);
            av_dict_set(&out_ctx->metadata, "encoded_by", "AgPlayer", 0);
        }
        for (unsigned int index = 0; index < out_ctx->nb_streams; ++index) {
            if (output_muxer == "ipod"
                && out_ctx->streams[index]->codecpar->codec_type
                    == AVMEDIA_TYPE_AUDIO) {
                av_dict_set(&out_ctx->streams[index]->metadata,
                            "language", "eng", 0);
                break;
            }
        }

        if (output_muxer == "mp3") {
            AVChapter* chapter = static_cast<AVChapter*>(
                av_mallocz(sizeof(*chapter)));
            if (chapter == nullptr) {
                avformat_close_input(&in_ctx);
                cleanup_output(out_ctx);
                error = "Failed to seed preservation test chapter";
                return AG_INTERNAL_ERROR;
            }
            AVChapter** expanded = static_cast<AVChapter**>(av_realloc_array(
                out_ctx->chapters, out_ctx->nb_chapters + 1U,
                sizeof(*out_ctx->chapters)));
            if (expanded == nullptr) {
                av_free(chapter);
                avformat_close_input(&in_ctx);
                cleanup_output(out_ctx);
                error = "Failed to seed preservation test chapter";
                return AG_INTERNAL_ERROR;
            }
            out_ctx->chapters = expanded;
            chapter->id = 42;
            chapter->time_base = AVRational{1, 1'000};
            chapter->start = 100;
            chapter->end = 900;
            av_dict_set(&chapter->metadata, "title", "Private Chapter", 0);
            av_dict_set(&chapter->metadata, "x-agplayer-chapter-private",
                        "private-chapter-value", 0);
            out_ctx->chapters[out_ctx->nb_chapters++] = chapter;
        }
    }

    if (fail_at(test_hooks,
                MetadataFailurePoint::UnmanagedFormatMetadataDrop)) {
        av_dict_set(&out_ctx->metadata, "x-agplayer-private", nullptr, 0);
    }
    if (fail_at(test_hooks,
                MetadataFailurePoint::UnmanagedStreamMetadataDrop)) {
        for (unsigned int index = 0; index < out_ctx->nb_streams; ++index) {
            AVStream* stream = out_ctx->streams[index];
            if ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0) {
                av_dict_set(&stream->metadata, "title", nullptr, 0);
                av_dict_set(&stream->metadata, "comment", nullptr, 0);
            } else {
                av_dict_set(&stream->metadata, "language", nullptr, 0);
            }
        }
    }
    if (fail_at(test_hooks, MetadataFailurePoint::ChapterDrop)
        && out_ctx->nb_chapters > 0) {
        AVChapter* chapter = out_ctx->chapters[out_ctx->nb_chapters - 1U];
        av_dict_free(&chapter->metadata);
        av_free(chapter);
        --out_ctx->nb_chapters;
    }
    const auto set_canonical_tag = [&out_ctx, &output_muxer](
                                       const CanonicalField field,
                                       const std::optional<std::string>& value) {
        if (!value.has_value()) return;
        const bool shared_date = muxer_shares_year_date(output_muxer)
            && (field == CanonicalField::Year || field == CanonicalField::Date);
        const auto clear_dictionary = [field, shared_date](AVDictionary** dictionary) {
            clear_field_aliases(dictionary, field);
            if (shared_date) {
                clear_field_aliases(dictionary, field == CanonicalField::Year
                    ? CanonicalField::Date : CanonicalField::Year);
            }
        };
        clear_dictionary(&out_ctx->metadata);
        for (unsigned int index = 0; index < out_ctx->nb_streams; ++index) {
            if (out_ctx->streams[index]->codecpar->codec_type
                == AVMEDIA_TYPE_AUDIO) {
                clear_dictionary(&out_ctx->streams[index]->metadata);
            }
        }
        if (!value->empty()) {
            const CanonicalField physical_field = shared_date
                ? CanonicalField::Date : field;
            av_dict_set(&out_ctx->metadata, canonical_key(physical_field),
                        value->c_str(), 0);
        }
    };
    set_canonical_tag(CanonicalField::Title, update.title);
    set_canonical_tag(CanonicalField::Artist, update.artist);
    set_canonical_tag(CanonicalField::Album, update.album);
    set_canonical_tag(CanonicalField::AlbumArtist, update.album_artist);
    set_canonical_tag(CanonicalField::Composer, update.composer);
    set_canonical_tag(CanonicalField::Bpm, update.bpm);
    set_canonical_tag(CanonicalField::CustomTag, update.custom_tag);
    set_canonical_tag(CanonicalField::Year, update.year);
    set_canonical_tag(CanonicalField::Date, update.date);
    set_canonical_tag(CanonicalField::Genre, update.genre);
    const auto set_optional_tag = [&out_ctx](
                                      const char* key,
                                      const std::optional<std::string>& value) {
        if (value.has_value()) {
            av_dict_set(&out_ctx->metadata, key,
                        value->empty() ? nullptr : value->c_str(), 0);
        }
    };
    set_optional_tag("track", update.track);
    set_optional_tag("disc", update.disc);
    set_optional_tag("comment", update.comment);
    set_optional_tag("copyright", update.copyright);
    // `encoder` is reserved by several muxers and may be overwritten with the
    // libavformat version. `encoded_by` maps to the user-editable ID3 TENC tag.
    set_optional_tag("encoded_by", update.encoder);
    if (update.lyrics.has_value()) {
        av_dict_set(&out_ctx->metadata, "lyrics",
                    update.lyrics->empty() ? nullptr : update.lyrics->c_str(), 0);
    }

    if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_ctx->pb, temp_path.c_str(), AVIO_FLAG_WRITE) < 0) {
            avformat_close_input(&in_ctx);
            cleanup_output(out_ctx);
            std::error_code ec;
            std::filesystem::remove(temp, ec);
            error = "Failed to open temp output file";
            return AG_IO_ERROR;
        }
    }

    if (fail_at(test_hooks, MetadataFailurePoint::HeaderWrite)
        || avformat_write_header(out_ctx, nullptr) < 0) {
        avformat_close_input(&in_ctx);
        cleanup_output(out_ctx);
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        error = "Failed to write header";
        return AG_INTERNAL_ERROR;
    }

    // Attached pictures are not returned by av_read_frame(). Submit them
    // explicitly after the muxer header, both for preserved and new covers.
    for (unsigned int i = 0; i < out_ctx->nb_streams; ++i) {
        AVStream* stream = out_ctx->streams[i];
        if ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0
            || stream->attached_pic.data == nullptr
            || stream->attached_pic.size <= 0) {
            continue;
        }
        AVPacket* cover_packet = av_packet_clone(&stream->attached_pic);
        if (cover_packet == nullptr) {
            av_write_trailer(out_ctx);
            avformat_close_input(&in_ctx);
            cleanup_output(out_ctx);
            std::error_code ec;
            std::filesystem::remove(temp, ec);
            error = "Failed to clone cover packet";
            return AG_INTERNAL_ERROR;
        }
        cover_packet->stream_index = stream->index;
        cover_packet->flags |= AV_PKT_FLAG_KEY;
        const int cover_result = av_interleaved_write_frame(out_ctx, cover_packet);
        av_packet_free(&cover_packet);
        if (cover_result < 0) {
            av_write_trailer(out_ctx);
            avformat_close_input(&in_ctx);
            cleanup_output(out_ctx);
            std::error_code ec;
            std::filesystem::remove(temp, ec);
            error = "Failed to write cover packet";
            return AG_INTERNAL_ERROR;
        }
    }

    AVPacket* pkt = av_packet_alloc();
    if (pkt == nullptr) {
        av_write_trailer(out_ctx);
        avformat_close_input(&in_ctx);
        cleanup_output(out_ctx);
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        error = "Failed to allocate packet";
        return AG_INTERNAL_ERROR;
    }

    bool failed = false;
    int read_result = 0;
    if (fail_at(test_hooks, MetadataFailurePoint::PacketRead)) {
        error = "Failed to read packet (injected)";
        failed = true;
    }
    while (!failed && (read_result = av_read_frame(in_ctx, pkt)) >= 0) {
        if (cancel != nullptr && cancel->load(std::memory_order_acquire)) {
            error = "Metadata write cancelled";
            failed = true;
            av_packet_unref(pkt);
            break;
        }
        const int input_index = pkt->stream_index;
        if (input_index >= 0
            && input_index < static_cast<int>(stream_mapping.size())
            && stream_mapping[static_cast<std::size_t>(input_index)] >= 0) {
            AVStream* in_stream = in_ctx->streams[input_index];
            if ((in_stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0) {
                av_packet_unref(pkt);
                continue;
            }
            const int output_index =
                stream_mapping[static_cast<std::size_t>(input_index)];
            AVStream* out_stream = out_ctx->streams[output_index];
            av_packet_rescale_ts(pkt, in_stream->time_base, out_stream->time_base);
            pkt->stream_index = output_index;
            pkt->pos = -1;
            if (fail_at(test_hooks, MetadataFailurePoint::PacketWrite)
                || av_interleaved_write_frame(out_ctx, pkt) < 0) {
                error = "Failed to write packet";
                failed = true;
                av_packet_unref(pkt);
                break;
            }
            if (runtime != nullptr) ++runtime->packets_copied;
        }
        av_packet_unref(pkt);
    }

    if (!failed && read_result != AVERROR_EOF) {
        failed = true;
        error = "Failed to read packet";
    }

    if (!failed && (fail_at(test_hooks, MetadataFailurePoint::TrailerWrite)
                    || av_write_trailer(out_ctx) < 0)) {
        failed = true;
        error = "Failed to write trailer";
    }
    av_packet_free(&pkt);

    if (!failed && out_ctx->pb != nullptr) avio_flush(out_ctx->pb);

    if (failed) {
        avformat_close_input(&in_ctx);
        cleanup_output(out_ctx);
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        if (error.empty()) {
            error = "Metadata write failed";
        }
        return AG_IO_ERROR;
    }

    avformat_close_input(&in_ctx);
    cleanup_output(out_ctx);

    return AG_OK;
}

ag_result write_metadata_with_preserved_backup(
    const std::string& utf8_path,
    const MetadataUpdate& update,
    std::string& error,
    const MetadataEditPlan* verification_plan,
    const std::atomic_bool* cancel,
    const MetadataWriterTestHooks* test_hooks,
    MetadataRuntimeMetrics* runtime,
    AudioEquivalence* audio_equivalence,
    ExistingBackupGuard* existing_backup)
{
    if (runtime != nullptr) *runtime = {};
    if (audio_equivalence != nullptr) {
        *audio_equivalence = AudioEquivalence::Different;
    }
    const std::filesystem::path source = filesystem_path_from_utf8(utf8_path);
    std::error_code ec;
    const auto source_size = std::filesystem::file_size(source, ec);
    const auto source_time = std::filesystem::last_write_time(source, ec);
    if (ec || !std::filesystem::is_regular_file(source)) {
        error = "Input file is not a readable regular file";
        return cancel != nullptr && cancel->load(std::memory_order_acquire)
            ? AG_CANCELLED : AG_IO_ERROR;
    }
    FileSha256 source_fingerprint{};
    if (!file_sha256(source, source_fingerprint)) {
        error = "Input file fingerprint could not be read";
        return AG_IO_ERROR;
    }
    const auto nonce = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path staged = source.parent_path()
        / std::filesystem::path(source.stem().native()
            + std::filesystem::u8path(".agmeta-stage-" + nonce + ".tmp").native()
            + source.extension().native());
    const auto clean_stage = [&staged]() {
        std::error_code cleanup;
        std::filesystem::remove(staged, cleanup);
    };
    MetadataSnapshot source_snapshot;
    if (verification_plan != nullptr
        && !read_metadata_snapshot(utf8_path, source_snapshot, runtime)) {
        error = "Failed to read source metadata before staging";
        return AG_IO_ERROR;
    }
    if (test_hooks != nullptr
        && test_hooks->simulate_source_muxer_encoder_metadata) {
        source_snapshot.format_metadata.emplace_back("encoder", "Lavf-old-build");
        std::sort(source_snapshot.format_metadata.begin(),
                  source_snapshot.format_metadata.end());
    }
    if (runtime != nullptr) {
        runtime->audio_streams_before = source_snapshot.audio_streams;
        runtime->chapters_before = source_snapshot.chapters;
        runtime->attachments_before = source_snapshot.attachments;
    }
    const std::string staged_utf8 = filesystem_path_as_utf8(staged);
    const ag_result staged_result = write_metadata_to_temp(
        utf8_path, update, staged_utf8, error, cancel, test_hooks, runtime);
    if (staged_result != AG_OK) {
        clean_stage();
        return staged_result;
    }

    // The source remains untouched until this probe succeeds. No decoder or
    // encoder is opened: this only proves the staged container can be reopened.
    AVFormatContext* verification = nullptr;
    const int open_result = fail_at(test_hooks, MetadataFailurePoint::Reprobe)
        ? AVERROR_INVALIDDATA
        : open_metadata_demuxer(&verification, staged_utf8, runtime);
    const int info_result = open_result < 0 || verification == nullptr
        || verification->nb_streams == 0 ? AVERROR_INVALIDDATA : 0;
    avformat_close_input(&verification);
    if (open_result < 0 || info_result < 0) {
        clean_stage();
        error = "Verification failed: staged output cannot be reopened";
        return AG_DECODE_ERROR;
    }
    if (verification_plan != nullptr) {
        MetadataSnapshot staged_snapshot;
        const bool snapshot_read = read_metadata_snapshot(
            staged_utf8, staged_snapshot, runtime);
        bool fields_match = snapshot_read;
        if (runtime != nullptr && snapshot_read) {
            runtime->audio_streams_after = staged_snapshot.audio_streams;
            runtime->chapters_after = staged_snapshot.chapters;
            runtime->attachments_after = staged_snapshot.attachments;
        }
        if (fields_match) {
            for (const FieldEdit& edit : verification_plan->fields) {
                if (edit.action == MetadataAction::Keep) continue;
                const std::string& actual =
                    staged_snapshot.fields[canonical_index(edit.field)];
                if ((edit.action == MetadataAction::Set && actual != *edit.value_utf8)
                    || (edit.action == MetadataAction::Clear && !actual.empty())) {
                    fields_match = false;
                    break;
                }
            }
        }
        const bool injected_verification_failure =
            fail_at(test_hooks, MetadataFailurePoint::Verification);
        const bool structure_matches = snapshot_read
            && staged_snapshot.audio_streams == source_snapshot.audio_streams
            && staged_snapshot.chapters == source_snapshot.chapters
            && staged_snapshot.attachments == source_snapshot.attachments;
        const bool unmanaged_matches = snapshot_read
            && unmanaged_metadata_preserved(source_snapshot, staged_snapshot,
                                            *verification_plan);
        if (!snapshot_read || !fields_match || injected_verification_failure
            || !structure_matches || !unmanaged_matches) {
            clean_stage();
            error = "Verification failed: staged metadata differs from request (";
            if (!snapshot_read) error += "snapshot read failed; ";
            if (!fields_match) error += "requested fields differ; ";
            if (injected_verification_failure) error += "injected failure; ";
            if (!structure_matches) error += "stream structure differs; ";
            if (!unmanaged_matches) error += "unmanaged metadata differs; ";
            error += ")";
            return AG_DECODE_ERROR;
        }
        const bool cover_matches = verification_plan->cover_action == CoverAction::Set
            ? staged_snapshot.cover.size() == verification_plan->cover_size
                && std::equal(staged_snapshot.cover.begin(), staged_snapshot.cover.end(),
                              verification_plan->cover_data)
            : verification_plan->cover_action == CoverAction::Clear
                ? staged_snapshot.cover.empty()
                : staged_snapshot.covers == source_snapshot.covers;
        if (!cover_matches) {
            clean_stage();
            error = "Verification failed: staged cover differs from request";
            return AG_DECODE_ERROR;
        }
        AudioEquivalence equivalence =
            audio_streams_equivalent(utf8_path, staged_utf8, runtime);
        if (test_hooks != nullptr
            && test_hooks->simulate_normalized_packet_timing
            && equivalence == AudioEquivalence::ExactPacketCopy) {
            equivalence = AudioEquivalence::NormalizedPacketTiming;
        }
        if (audio_equivalence != nullptr) *audio_equivalence = equivalence;
        if (equivalence == AudioEquivalence::Different) {
            clean_stage();
            error = "Verification failed: audio packet payload changed";
            return AG_DECODE_ERROR;
        }
        if (equivalence == AudioEquivalence::NormalizedPacketTiming
            && verification_plan->audio_policy
                == MetadataAudioPolicy::StrictPacketIdentity) {
            clean_stage();
            error = "Verification failed: container timing was normalized; "
                    "verified force mode is required";
            return AG_DECODE_ERROR;
        }
    }
    if (test_hooks != nullptr && test_hooks->before_source_commit)
        test_hooks->before_source_commit();
    FileSha256 current_fingerprint{};
    const auto current_size = std::filesystem::file_size(source, ec);
    const auto current_time = std::filesystem::last_write_time(source, ec);
    if (fail_at(test_hooks, MetadataFailurePoint::SourceChanged)
        || ec || current_size != source_size || current_time != source_time
        || !file_sha256(source, current_fingerprint)
        || current_fingerprint != source_fingerprint) {
        clean_stage();
        error = "Source changed while metadata was being written";
        return AG_IO_ERROR;
    }
    if (cancel != nullptr && cancel->load(std::memory_order_acquire)) {
        clean_stage();
        error = "Metadata write cancelled";
        return AG_CANCELLED;
    }
    const std::filesystem::path backup = std::filesystem::path(source.native()
        + std::filesystem::u8path(".agbak").native());
    std::optional<ExistingBackupGuard> owned_backup;
    if (existing_backup == nullptr) {
        owned_backup.emplace(backup, nonce);
        existing_backup = &*owned_backup;
        if (!existing_backup->valid()) {
            clean_stage();
            error = "Failed to preserve existing backup before replacement";
            return AG_IO_ERROR;
        }
    }
    SourceCommitGuard commit_guard(source);
    FileSha256 guarded_fingerprint{};
    if ((test_hooks != nullptr && test_hooks->fail_commit_lock)
        || !commit_guard.valid()
        || !commit_guard.fingerprint(guarded_fingerprint)) {
        clean_stage();
        const bool prior_backup_restored = existing_backup->restore(
            test_hooks != nullptr && test_hooks->fail_backup_restore);
        error = "Source commit lock could not be acquired";
        if (!prior_backup_restored) {
            error += "; prior backup remains preserved at ";
            error += existing_backup->preserved_path();
        }
        return AG_IO_ERROR;
    }
    if (!commit_guard.matchesPath(source)
        || guarded_fingerprint != source_fingerprint) {
        clean_stage();
        const bool prior_backup_restored = existing_backup->restore(
            test_hooks != nullptr && test_hooks->fail_backup_restore);
        error = "Source changed while metadata was being written";
        if (!prior_backup_restored) {
            error += "; prior backup remains preserved at ";
            error += existing_backup->preserved_path();
        }
        return AG_IO_ERROR;
    }
#ifndef _WIN32
    FileSha256 backup_fingerprint{};
#endif
#ifdef _WIN32
    // ReplaceFileW moves the exact replaced file to backup atomically. This
    // preserves all filesystem-managed streams and attributes and lets us
    // verify after the commit that the replaced path was still our locked
    // source object.
    const bool backup_created = true;
    const bool backup_fingerprint_read = true;
#else
    // POSIX locks are advisory, so retain identity and digest checks around
    // the eventual rename. copy_file preserves native copy semantics instead
    // of reconstructing only the default byte stream.
    const bool backup_created = commit_guard.copyTo(backup);
    const bool backup_fingerprint_read = backup_created
        && file_sha256(backup, backup_fingerprint);
#endif
    if (!commit_guard.matchesPath(source)
        || !backup_created || !backup_fingerprint_read
#ifndef _WIN32
        || backup_fingerprint != source_fingerprint
#endif
    ) {
        clean_stage();
        const bool prior_backup_restored = existing_backup->restore(
            test_hooks != nullptr && test_hooks->fail_backup_restore);
        error = "Source changed while metadata was being written";
        if (!prior_backup_restored) {
            error += "; prior backup remains preserved at ";
            error += existing_backup->preserved_path();
        }
        return AG_IO_ERROR;
    }
    FileSha256 commit_fingerprint{};
    if (!commit_guard.matchesPath(source)
        || !commit_guard.fingerprint(commit_fingerprint)
        || commit_fingerprint != source_fingerprint) {
        clean_stage();
        const bool prior_backup_restored = existing_backup->restore(
            test_hooks != nullptr && test_hooks->fail_backup_restore);
        error = "Source changed while metadata was being written";
        if (!prior_backup_restored) {
            error += "; prior backup remains preserved at ";
            error += existing_backup->preserved_path();
        }
        return AG_IO_ERROR;
    }
    // The deterministic race hook intentionally sits after the final digest.
    // Identity and digest are checked once more after it, immediately next to
    // the replacement operation.
    if (test_hooks != nullptr && test_hooks->before_atomic_replace)
        test_hooks->before_atomic_replace();
    FileSha256 final_fingerprint{};
    if (!commit_guard.matchesPath(source)
        || !commit_guard.fingerprint(final_fingerprint)
        || final_fingerprint != source_fingerprint) {
        clean_stage();
        const bool prior_backup_restored = existing_backup->restore(
            test_hooks != nullptr && test_hooks->fail_backup_restore);
        error = "Source changed while metadata was being written";
        if (!prior_backup_restored) {
            error += "; prior backup remains preserved at ";
            error += existing_backup->preserved_path();
        }
        return AG_IO_ERROR;
    }
    if (fail_at(test_hooks, MetadataFailurePoint::AtomicReplace)) {
        clean_stage();
        const bool prior_backup_restored = existing_backup->restore(
            test_hooks != nullptr && test_hooks->fail_backup_restore);
        error = "Failed to atomically replace original file";
        if (!prior_backup_restored) {
            error += "; prior backup remains preserved at ";
            error += existing_backup->preserved_path();
        }
        return AG_IO_ERROR;
    }
#ifdef _WIN32
    if (test_hooks != nullptr && test_hooks->before_replace_file)
        test_hooks->before_replace_file();
    bool replace_succeeded = false;
    DWORD replace_error = ERROR_SUCCESS;
    if (test_hooks != nullptr
        && test_hooks->simulate_replace_error_1177) {
        // Reproduce ReplaceFileW's documented 1177 partial state: the
        // original target has moved to backup while staged retains its name.
        if (MoveFileExW(source.c_str(), backup.c_str(),
                MOVEFILE_WRITE_THROUGH) != 0) {
            replace_error = ERROR_UNABLE_TO_MOVE_REPLACEMENT_2;
        } else {
            replace_error = GetLastError();
        }
    } else {
        replace_succeeded = ReplaceFileW(source.c_str(), staged.c_str(),
            backup.c_str(), REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr,
            nullptr) != 0;
        if (!replace_succeeded) replace_error = GetLastError();
    }
    if (!replace_succeeded) {
        if (replace_error == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2) {
            const bool source_restored =
                !(test_hooks != nullptr && test_hooks->fail_source_restore)
                && MoveFileExW(backup.c_str(), source.c_str(),
                               MOVEFILE_WRITE_THROUGH) != 0;
            clean_stage();
            error = "Failed to atomically replace original file after "
                    "the original moved to backup";
            if (source_restored) {
                const bool prior_backup_restored = existing_backup->restore(
                    test_hooks != nullptr
                    && test_hooks->fail_backup_restore);
                if (!prior_backup_restored) {
                    error += "; prior backup remains preserved at ";
                    error += existing_backup->preserved_path();
                }
            } else {
                const auto recovery =
                    existing_backup->preserve_after_source_restore_failure(
                        test_hooks != nullptr
                        && test_hooks->fail_backup_restore);
                if (!recovery.original_backup_path.empty()) {
                    error += "; original recovery backup remains at ";
                    error += recovery.original_backup_path;
                } else {
                    error += "; original recovery backup path is unavailable";
                }
                if (!recovery.prior_backup_path.empty()) {
                    error += recovery.prior_backup_restored
                        ? "; prior backup restored at "
                        : "; prior backup remains preserved at ";
                    error += recovery.prior_backup_path;
                }
            }
            return AG_IO_ERROR;
        }
        clean_stage();
        const bool prior_backup_restored = existing_backup->restore(
            test_hooks != nullptr && test_hooks->fail_backup_restore);
        error = "Failed to atomically replace original file";
        if (!prior_backup_restored) {
            error += "; prior backup remains preserved at ";
            error += existing_backup->preserved_path();
        }
        return AG_IO_ERROR;
    }
    FileSha256 replaced_fingerprint{};
    if (!commit_guard.matchesPath(backup)
        || !commit_guard.fingerprint(replaced_fingerprint)
        || replaced_fingerprint != source_fingerprint) {
        // If the path changed after our final pre-check, ReplaceFileW captured
        // the competing file in backup. Restore it so the competing writer
        // wins instead of being silently overwritten.
        const bool foreign_source_restored = atomic_replace(backup, source);
        clean_stage();
        error = "Source changed during atomic metadata replacement";
        if (foreign_source_restored) {
            const bool prior_backup_restored = existing_backup->restore(
                test_hooks != nullptr && test_hooks->fail_backup_restore);
            if (!prior_backup_restored) {
                error += "; prior backup remains preserved at ";
                error += existing_backup->preserved_path();
            }
        } else {
            const auto recovery =
                existing_backup->preserve_after_source_restore_failure(
                    test_hooks != nullptr
                    && test_hooks->fail_backup_restore);
            error += "; competing source recovery remains at ";
            error += recovery.original_backup_path;
            if (!recovery.prior_backup_restored) {
                error += "; prior backup remains preserved at ";
                error += recovery.prior_backup_path;
            }
        }
        return AG_IO_ERROR;
    }
#else
    // POSIX locks are advisory. Exchange the two directory entries atomically,
    // then prove that the displaced entry is still the locked source object.
    // If another writer won the interval after the final pre-check, exchange
    // back so its file remains at the public source path.
    if (test_hooks != nullptr && test_hooks->before_replace_file)
        test_hooks->before_replace_file();
    const AtomicExchangeResult exchange_result = atomic_exchange(source, staged);
    if (exchange_result != AtomicExchangeResult::Exchanged) {
        clean_stage();
        const bool prior_backup_restored = existing_backup->restore(
            test_hooks != nullptr && test_hooks->fail_backup_restore);
        error = exchange_result == AtomicExchangeResult::Unsupported
            ? "Failed to atomically replace original file: atomic path "
              "exchange is unsupported"
            : "Failed to atomically replace original file";
        if (!prior_backup_restored) {
            error += "; prior backup remains preserved at ";
            error += existing_backup->preserved_path();
        }
        return AG_IO_ERROR;
    }
    FileSha256 displaced_fingerprint{};
    const bool displaced_source_matches = commit_guard.matchesPath(staged)
        && commit_guard.fingerprint(displaced_fingerprint)
        && displaced_fingerprint == source_fingerprint
        && commit_guard.matchesPath(staged);
    if (!displaced_source_matches) {
        const bool source_restored =
            atomic_exchange(source, staged) == AtomicExchangeResult::Exchanged;
        error = "Source changed during atomic metadata replacement";
        if (source_restored) {
            clean_stage();
            const bool prior_backup_restored = existing_backup->restore(
                test_hooks != nullptr && test_hooks->fail_backup_restore);
            if (!prior_backup_restored) {
                error += "; prior backup remains preserved at ";
                error += existing_backup->preserved_path();
            }
        } else {
            const auto recovery =
                existing_backup->preserve_after_source_restore_failure(
                    test_hooks != nullptr
                    && test_hooks->fail_backup_restore);
            error += "; displaced source remains at ";
            error += staged.u8string();
            if (!recovery.original_backup_path.empty()) {
                error += "; original recovery backup remains at ";
                error += recovery.original_backup_path;
            }
            if (!recovery.prior_backup_path.empty()) {
                error += recovery.prior_backup_restored
                    ? "; prior backup restored at "
                    : "; prior backup remains preserved at ";
                error += recovery.prior_backup_path;
            }
        }
        return AG_IO_ERROR;
    }
    clean_stage();
#endif
    if (owned_backup.has_value()) existing_backup->discard();
    std::filesystem::remove(std::filesystem::path(staged.native()
                                + std::filesystem::u8path(".agbak").native()), ec);
    return AG_OK;
}

ag_result write_metadata(const std::string& utf8_path,
                         const MetadataUpdate& update,
                         std::string& error,
                         const MetadataEditPlan* verification_plan,
                         const std::atomic_bool* cancel,
                         const MetadataWriterTestHooks* test_hooks,
                         MetadataRuntimeMetrics* runtime)
{
    return write_metadata_with_preserved_backup(
        utf8_path, update, error, verification_plan, cancel, test_hooks,
        runtime, nullptr, nullptr);
}

} // namespace agplayer

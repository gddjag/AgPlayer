#include "metadata_writer.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/opt.h>
}

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace agplayer {

namespace {

bool atomic_replace(const std::string& temp_path, const std::string& target_path)
{
#ifdef _WIN32
    const std::filesystem::path temp_w(temp_path);
    const std::filesystem::path target_w(target_path);
    if (ReplaceFileW(target_w.wstring().c_str(), temp_w.wstring().c_str(),
                     nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) != 0) {
        return true;
    }
    return MoveFileExW(temp_w.wstring().c_str(), target_w.wstring().c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(temp_path.c_str(), target_path.c_str()) == 0;
#endif
}

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

bool audio_streams_equivalent(const std::string& source_path,
                              const std::string& staged_path)
{
    AVFormatContext* source = nullptr;
    AVFormatContext* staged = nullptr;
    const auto close = [] (AVFormatContext*& context) {
        avformat_close_input(&context);
    };
    if (avformat_open_input(&source, source_path.c_str(), nullptr, nullptr) < 0
        || avformat_open_input(&staged, staged_path.c_str(), nullptr, nullptr) < 0
        || avformat_find_stream_info(source, nullptr) < 0
        || avformat_find_stream_info(staged, nullptr) < 0) {
        close(source);
        close(staged);
        return false;
    }

    std::vector<const AVCodecParameters*> source_audio;
    std::vector<const AVCodecParameters*> staged_audio;
    for (unsigned int index = 0; index < source->nb_streams; ++index) {
        const AVCodecParameters* parameters = source->streams[index]->codecpar;
        if (parameters->codec_type == AVMEDIA_TYPE_AUDIO) source_audio.push_back(parameters);
    }
    for (unsigned int index = 0; index < staged->nb_streams; ++index) {
        const AVCodecParameters* parameters = staged->streams[index]->codecpar;
        if (parameters->codec_type == AVMEDIA_TYPE_AUDIO) staged_audio.push_back(parameters);
    }
    bool equal = source_audio.size() == staged_audio.size();
    for (std::size_t index = 0; equal && index < source_audio.size(); ++index) {
        const AVCodecParameters* before = source_audio[index];
        const AVCodecParameters* after = staged_audio[index];
        equal = before->codec_id == after->codec_id
            && before->sample_rate == after->sample_rate
            && before->ch_layout.nb_channels == after->ch_layout.nb_channels
            && before->format == after->format
            && before->bits_per_coded_sample == after->bits_per_coded_sample
            && before->bits_per_raw_sample == after->bits_per_raw_sample;
    }
    close(source);
    close(staged);
    return equal;
}

} // namespace

bool validate_metadata_edit_plan(const MetadataEditPlan& plan,
                                 std::string& error)
{
    if (plan.cover_action == CoverAction::Set
        && (plan.cover_data == nullptr || plan.cover_size == 0
            || plan.cover_mime_type.empty())) {
        error = "A replacement cover needs image data and a MIME type";
        return false;
    }
    bool edits_year = false;
    bool edits_date = false;
    for (const FieldEdit& edit : plan.fields) {
        if (edit.action == MetadataAction::Keep) {
            continue;
        }
        if (edit.action == MetadataAction::Set
            && (!edit.value_utf8.has_value() || edit.value_utf8->empty())) {
            error = "A Set value must not be empty; use Clear instead";
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
        edits_year = edits_year || edit.field == CanonicalField::Year;
        edits_date = edits_date || edit.field == CanonicalField::Date;
    }
    if (edits_year && edits_date) {
        error = "Year and date map to the same physical tag";
        return false;
    }
    error.clear();
    return true;
}

ag_result preflight_metadata_edit(const std::string& utf8_path,
                                  const MetadataEditPlan& plan,
                                  std::string& error)
{
    if (!validate_metadata_edit_plan(plan, error)) return AG_INVALID_ARGUMENT;

    AVFormatContext* input = nullptr;
    if (avformat_open_input(&input, utf8_path.c_str(), nullptr, nullptr) < 0) {
        error = "Input container cannot be opened";
        return AG_IO_ERROR;
    }
    const auto close_input = [&input] { avformat_close_input(&input); };
    if (avformat_find_stream_info(input, nullptr) < 0) {
        close_input();
        error = "Input stream information cannot be read";
        return AG_IO_ERROR;
    }
    bool has_audio = false;
    for (unsigned int index = 0; index < input->nb_streams; ++index) {
        if (input->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            has_audio = true;
            break;
        }
    }
    close_input();
    if (!has_audio) {
        error = "Input has no audio stream";
        return AG_IO_ERROR;
    }
    AVFormatContext* output = nullptr;
    if (avformat_alloc_output_context2(&output, nullptr, nullptr,
                                       utf8_path.c_str()) < 0
        || output == nullptr) {
        error = "No writable muxer is available for this container";
        return AG_IO_ERROR;
    }
    avformat_free_context(output);
    error.clear();
    return AG_OK;
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
    case CanonicalField::Year:
    case CanonicalField::Date: return &update.year;
    case CanonicalField::Composer: return &update.composer;
    case CanonicalField::Bpm: return &update.bpm;
    }
    return nullptr;
}

const char* read_field(const ag_metadata* metadata, const CanonicalField field)
{
    switch (field) {
    case CanonicalField::Title: return ag_metadata_title(metadata);
    case CanonicalField::Artist: return ag_metadata_artist(metadata);
    case CanonicalField::Album: return ag_metadata_album(metadata);
    case CanonicalField::AlbumArtist: return ag_metadata_album_artist(metadata);
    case CanonicalField::Genre: return ag_metadata_genre(metadata);
    case CanonicalField::Year:
    case CanonicalField::Date: return ag_metadata_year(metadata);
    case CanonicalField::Composer: return ag_metadata_composer(metadata);
    case CanonicalField::Bpm: return ag_metadata_bpm_tag(metadata);
    }
    return "";
}

} // namespace

ag_result write_metadata_plan(const std::string& utf8_path,
                              const MetadataEditPlan& plan,
                              MetadataFileResult& result)
{
    result = {};
    std::string validation_error;
    const ag_result preflight = preflight_metadata_edit(utf8_path, plan,
                                                        validation_error);
    if (preflight != AG_OK) {
        result.message = validation_error;
        result.final_status = validation_error.find("No writable muxer")
                    != std::string::npos
                ? FileResultStatus::Unsupported : FileResultStatus::Failed;
        return preflight;
    }

    const std::filesystem::path source(utf8_path);
    std::error_code ec;
    const auto before_size = std::filesystem::file_size(source, ec);
    const auto before_time = std::filesystem::last_write_time(source, ec);
    if (ec || !std::filesystem::is_regular_file(source)) {
        result.message = "Input file is not a readable regular file";
        return AG_IO_ERROR;
    }

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
    std::string error;
    const ag_result write_result = write_metadata(utf8_path, update, error, &plan);
    if (write_result != AG_OK) {
        result.message = error;
        return write_result;
    }

    // A replacement is only successful after reopening and reading back every
    // requested canonical field. The writer itself uses packet copy only.
    ag_metadata* metadata = nullptr;
    if (ag_metadata_open(utf8_path.c_str(), &metadata) != AG_OK || metadata == nullptr) {
        result.message = "Verification failed: replacement cannot be reopened";
        return AG_DECODE_ERROR;
    }
    bool verified = true;
    for (const FieldEdit& edit : plan.fields) {
        const char* read_back = read_field(metadata, edit.field);
        const std::string actual = read_back != nullptr ? read_back : "";
        result.fields.push_back({edit.field, edit.action, actual, {}});
        if (edit.action == MetadataAction::Set && actual != *edit.value_utf8) {
            verified = false;
        }
        if (edit.action == MetadataAction::Clear && !actual.empty()) {
            verified = false;
        }
    }
    ag_metadata_destroy(metadata);
    result.used_stream_copy = true;
    result.audio_verified_unchanged = verified;
    result.final_status = verified ? FileResultStatus::Completed
                                   : FileResultStatus::Failed;
    result.message = verified ? "Verified with packet stream copy"
                              : "Verification failed: metadata readback mismatch";
    (void)before_size;
    (void)before_time;
    return verified ? AG_OK : AG_DECODE_ERROR;
}

static ag_result write_metadata_to_temp(const std::string& utf8_path,
                                        const MetadataUpdate& update,
                                        const std::string& temp_path,
                                        std::string& error)
{
    AVFormatContext* in_ctx = nullptr;
    if (avformat_open_input(&in_ctx, utf8_path.c_str(), nullptr, nullptr) < 0) {
        error = "Failed to open input file";
        return AG_IO_ERROR;
    }
    if (avformat_find_stream_info(in_ctx, nullptr) < 0) {
        avformat_close_input(&in_ctx);
        error = "Failed to find stream info";
        return AG_DECODE_ERROR;
    }

    AVFormatContext* out_ctx = nullptr;
    // Use the original path so FFmpeg guesses the output format from extension.
    if (avformat_alloc_output_context2(&out_ctx, nullptr, nullptr,
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

    // Copy format-level metadata, then override with user-specified fields.
    av_dict_copy(&out_ctx->metadata, in_ctx->metadata, 0);
    if (update.title.has_value()) {
        av_dict_set(&out_ctx->metadata, "title",
                    update.title->empty() ? nullptr : update.title->c_str(), 0);
    }
    if (update.artist.has_value()) {
        av_dict_set(&out_ctx->metadata, "artist",
                    update.artist->empty() ? nullptr : update.artist->c_str(), 0);
    }
    if (update.album.has_value()) {
        av_dict_set(&out_ctx->metadata, "album",
                    update.album->empty() ? nullptr : update.album->c_str(), 0);
    }
    const auto set_optional_tag = [&out_ctx](
                                      const char* key,
                                      const std::optional<std::string>& value) {
        if (value.has_value()) {
            av_dict_set(&out_ctx->metadata, key,
                        value->empty() ? nullptr : value->c_str(), 0);
        }
    };
    set_optional_tag("album_artist", update.album_artist);
    set_optional_tag("track", update.track);
    set_optional_tag("disc", update.disc);
    set_optional_tag("composer", update.composer);
    set_optional_tag("comment", update.comment);
    set_optional_tag("bpm", update.bpm);
    set_optional_tag("copyright", update.copyright);
    // `encoder` is reserved by several muxers and may be overwritten with the
    // libavformat version. `encoded_by` maps to the user-editable ID3 TENC tag.
    set_optional_tag("encoded_by", update.encoder);
    if (update.year.has_value()) {
        av_dict_set(&out_ctx->metadata, "date",
                    update.year->empty() ? nullptr : update.year->c_str(), 0);
    }
    if (update.genre.has_value()) {
        av_dict_set(&out_ctx->metadata, "genre",
                    update.genre->empty() ? nullptr : update.genre->c_str(), 0);
    }
    if (update.lyrics.has_value()) {
        av_dict_set(&out_ctx->metadata, "lyrics",
                    update.lyrics->empty() ? nullptr : update.lyrics->c_str(), 0);
    }

    if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_ctx->pb, temp_path.c_str(), AVIO_FLAG_WRITE) < 0) {
            avformat_close_input(&in_ctx);
            cleanup_output(out_ctx);
            std::error_code ec;
            std::filesystem::remove(temp_path, ec);
            error = "Failed to open temp output file";
            return AG_IO_ERROR;
        }
    }

    if (avformat_write_header(out_ctx, nullptr) < 0) {
        avformat_close_input(&in_ctx);
        cleanup_output(out_ctx);
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
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
            std::filesystem::remove(temp_path, ec);
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
            std::filesystem::remove(temp_path, ec);
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
        std::filesystem::remove(temp_path, ec);
        error = "Failed to allocate packet";
        return AG_INTERNAL_ERROR;
    }

    bool failed = false;
    while (av_read_frame(in_ctx, pkt) >= 0) {
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
            if (av_interleaved_write_frame(out_ctx, pkt) < 0) {
                error = "Failed to write packet";
                failed = true;
                av_packet_unref(pkt);
                break;
            }
        }
        av_packet_unref(pkt);
    }

    if (av_write_trailer(out_ctx) < 0) {
        failed = true;
        error = "Failed to write trailer";
    }
    av_packet_free(&pkt);

    if (failed) {
        avformat_close_input(&in_ctx);
        cleanup_output(out_ctx);
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        if (error.empty()) {
            error = "Metadata write failed";
        }
        return AG_IO_ERROR;
    }

    avformat_close_input(&in_ctx);
    cleanup_output(out_ctx);

    return AG_OK;
}

ag_result write_metadata(const std::string& utf8_path,
                         const MetadataUpdate& update,
                         std::string& error,
                         const MetadataEditPlan* verification_plan)
{
    const std::filesystem::path source(utf8_path);
    std::error_code ec;
    const auto source_size = std::filesystem::file_size(source, ec);
    const auto source_time = std::filesystem::last_write_time(source, ec);
    if (ec || !std::filesystem::is_regular_file(source)) {
        error = "Input file is not a readable regular file";
        return AG_IO_ERROR;
    }
    const auto nonce = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path staged = source.parent_path()
        / (source.stem().string() + ".agmeta-stage-" + nonce
           + ".tmp" + source.extension().string());
    const auto clean_stage = [&staged]() {
        std::error_code cleanup;
        std::filesystem::remove(staged, cleanup);
    };
    const ag_result staged_result = write_metadata_to_temp(utf8_path, update,
                                                            staged.string(), error);
    if (staged_result != AG_OK) {
        clean_stage();
        return staged_result;
    }

    // The source remains untouched until this probe succeeds. No decoder or
    // encoder is opened: this only proves the staged container can be reopened.
    AVFormatContext* verification = nullptr;
    const int open_result = avformat_open_input(&verification,
                                                staged.string().c_str(),
                                                nullptr, nullptr);
    const int info_result = open_result < 0 ? open_result
        : avformat_find_stream_info(verification, nullptr);
    avformat_close_input(&verification);
    if (open_result < 0 || info_result < 0) {
        clean_stage();
        error = "Verification failed: staged output cannot be reopened";
        return AG_DECODE_ERROR;
    }
    if (verification_plan != nullptr) {
        ag_metadata* metadata = nullptr;
        const ag_result metadata_result =
            ag_metadata_open(staged.u8string().c_str(), &metadata);
        bool verified = metadata_result == AG_OK && metadata != nullptr;
        if (verified) {
            for (const FieldEdit& edit : verification_plan->fields) {
                if (edit.action == MetadataAction::Keep) continue;
                const char* value = read_field(metadata, edit.field);
                const std::string actual = value != nullptr ? value : "";
                if ((edit.action == MetadataAction::Set && actual != *edit.value_utf8)
                    || (edit.action == MetadataAction::Clear && !actual.empty())) {
                    verified = false;
                    break;
                }
            }
        }
        if (!verified) {
            ag_metadata_destroy(metadata);
            clean_stage();
            error = "Verification failed: staged metadata differs from request";
            return AG_DECODE_ERROR;
        }
        if (verification_plan->cover_action != CoverAction::Keep) {
            size_t cover_size = 0;
            const unsigned char* cover = ag_metadata_cover(metadata, &cover_size, nullptr);
            const bool cover_matches = verification_plan->cover_action == CoverAction::Set
                ? cover != nullptr && cover_size > 0
                : cover == nullptr || cover_size == 0;
            if (!cover_matches) {
                ag_metadata_destroy(metadata);
                clean_stage();
                error = "Verification failed: staged cover differs from request";
                return AG_DECODE_ERROR;
            }
        }
        ag_metadata_destroy(metadata);
        if (!audio_streams_equivalent(utf8_path, staged.string())) {
            clean_stage();
            error = "Verification failed: audio stream parameters changed";
            return AG_DECODE_ERROR;
        }
    }
    const auto current_size = std::filesystem::file_size(source, ec);
    const auto current_time = std::filesystem::last_write_time(source, ec);
    if (ec || current_size != source_size || current_time != source_time) {
        clean_stage();
        error = "Source changed while metadata was being written";
        return AG_IO_ERROR;
    }
    const std::filesystem::path backup = source.string() + ".agbak";
    std::filesystem::copy_file(source, backup,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec || !atomic_replace(staged.string(), source.string())) {
        clean_stage();
        error = ec ? "Failed to create backup before replacement"
                   : "Failed to atomically replace original file";
        return AG_IO_ERROR;
    }
    std::filesystem::remove(staged.string() + ".agbak", ec);
    return AG_OK;
}

} // namespace agplayer

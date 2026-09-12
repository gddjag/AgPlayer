#include "document_writer.hpp"

#include "../decoder.hpp"
#include "../metadata_writer.hpp"
#include "document_renderer.hpp"
#include "../transcoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <algorithm>
#include <chrono>
#include <cmath>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace agplayer::editor {
namespace {

std::string default_codec(const std::filesystem::path& path)
{
    std::string extension = path.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    if (extension == ".wav") return "pcm_s24le";
    if (extension == ".flac") return "flac";
    if (extension == ".mp3") return "libmp3lame";
    if (extension == ".m4a" || extension == ".aac") return "aac";
    if (extension == ".ogg") return "libvorbis";
    if (extension == ".opus") return "libopus";
    return {};
}

std::filesystem::path temporary_sibling(const std::filesystem::path& target,
                                         const char* role,
                                         const std::string& extension)
{
    const auto ticks = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    return target.parent_path() / std::filesystem::u8path(
        target.filename().u8string() + ".agplayer-" + role + "-"
        + std::to_string(ticks) + extension);
}

SampleFrame count_decoded_frames(const std::filesystem::path& path,
                                 const int sample_rate,
                                 const int channels)
{
    agplayer::Decoder decoder;
    if (decoder.open(path.u8string(), sample_rate, channels) != AG_OK) {
        return -1;
    }
    agplayer::DecodedAudioBlock block;
    SampleFrame result = 0;
    for (;;) {
        if (decoder.read(block) != AG_OK) {
            return -1;
        }
        result += static_cast<SampleFrame>(block.frames);
        if (block.end_of_stream) {
            return result;
        }
    }
}

std::int64_t probed_duration_ms(const std::filesystem::path& path)
{
    agplayer::MediaMetadata metadata;
    return agplayer::probe_media_metadata(path.u8string(), metadata) == AG_OK
        ? metadata.duration_ms : -1;
}

bool commit_file(const std::filesystem::path& staged,
                 const std::filesystem::path& target,
                 std::string& message)
{
#ifdef _WIN32
    const std::wstring staged_w = staged.wstring();
    const std::wstring target_w = target.wstring();
    if (std::filesystem::exists(target)) {
        if (ReplaceFileW(target_w.c_str(), staged_w.c_str(), nullptr,
                         REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != 0) {
            return true;
        }
    } else if (MoveFileExW(staged_w.c_str(), target_w.c_str(),
                           MOVEFILE_WRITE_THROUGH) != 0) {
        return true;
    }
    message = "atomic commit failed: " + std::to_string(GetLastError());
    return false;
#else
    std::error_code error;
    std::filesystem::rename(staged, target, error);
    if (!error) return true;
    message = error.message();
    return false;
#endif
}

std::optional<std::string> present_value(const char* value)
{
    return value != nullptr && value[0] != '\0'
        ? std::optional<std::string>{value} : std::nullopt;
}

bool copy_metadata(const std::filesystem::path& source,
                   const std::filesystem::path& target,
                   std::string& message)
{
    ag_metadata* metadata = nullptr;
    const std::string source_utf8 = source.u8string();
    if (ag_metadata_open(source_utf8.c_str(), &metadata) != AG_OK
        || metadata == nullptr) {
        if (metadata != nullptr) ag_metadata_destroy(metadata);
        message = "source metadata cannot be read";
        return false;
    }
    agplayer::MetadataUpdate update;
    update.title = present_value(ag_metadata_title(metadata));
    update.artist = present_value(ag_metadata_artist(metadata));
    update.album = present_value(ag_metadata_album(metadata));
    update.album_artist = present_value(ag_metadata_album_artist(metadata));
    update.track = present_value(ag_metadata_track(metadata));
    update.disc = present_value(ag_metadata_disc(metadata));
    update.composer = present_value(ag_metadata_composer(metadata));
    update.comment = present_value(ag_metadata_comment(metadata));
    update.bpm = present_value(ag_metadata_bpm_tag(metadata));
    update.copyright = present_value(ag_metadata_copyright(metadata));
    update.encoder = present_value(ag_metadata_encoder(metadata));
    update.year = present_value(ag_metadata_year(metadata));
    update.genre = present_value(ag_metadata_genre(metadata));
    update.lyrics = present_value(ag_metadata_lyrics(metadata));
    const std::string target_utf8 = target.u8string();
    const ag_result result = agplayer::write_metadata(
        target_utf8, update, message);
    ag_metadata_destroy(metadata);
    return result == AG_OK;
}

} // namespace

WriteResult DocumentWriter::write(
    const WriteRequest& request,
    const std::atomic_bool* cancelled,
    std::function<void(float)> progress) const
{
    if (request.output_path.empty() || request.snapshot.events.empty()
        || (request.range && !request.range->valid())) {
        return {WriteError::InvalidRequest, "invalid write request", 0};
    }
    const std::string codec = request.codec_name.empty()
        ? default_codec(request.output_path) : request.codec_name;
    if (codec.empty() || avcodec_find_encoder_by_name(codec.c_str()) == nullptr) {
        return {WriteError::UnsupportedEncoder, "unsupported encoder", 0};
    }
    std::error_code fs_error;
    if (!request.output_path.parent_path().empty()) {
        std::filesystem::create_directories(
            request.output_path.parent_path(), fs_error);
    }
    if (fs_error) {
        return {WriteError::InvalidRequest, fs_error.message(), 0};
    }

    const auto render_path = temporary_sibling(
        request.output_path, "render", ".wav");
    const auto staged_path = temporary_sibling(
        request.output_path, "output", request.output_path.extension().u8string());
    const auto cleanup = [&] {
        std::error_code ignored;
        std::filesystem::remove(render_path, ignored);
        std::filesystem::remove(staged_path, ignored);
    };

    DocumentRenderer renderer;
    const RenderResult rendered = renderer.renderFloatWav(
        request.snapshot, request.range, render_path, cancelled, progress);
    if (!rendered.success) {
        cleanup();
        return {rendered.message == "cancelled" ? WriteError::Cancelled
                                                : WriteError::RenderFailed,
                rendered.message, rendered.frames};
    }

    agplayer::TranscodeConfig config;
    config.output_path = staged_path.u8string();
    config.codec_name = codec;
    config.bit_rate = request.bit_rate;
    config.sample_rate = request.sample_rate > 0
        ? request.sample_rate : static_cast<int>(rendered.sample_rate);
    config.channels = request.channels > 0
        ? request.channels : static_cast<int>(rendered.channels);
    config.keep_metadata = request.keep_metadata;
    config.variable_bit_rate = request.variable_bit_rate;
    config.quality = std::clamp(request.quality, 0, 100);
    if (request.output_path.extension() == ".aif"
        || request.output_path.extension() == ".aiff") {
        config.container_name = "aiff";
    }
    std::string encode_error;
    const ag_result encoded = agplayer::transcode(
        render_path.u8string(), config, cancelled,
        progress ? [progress = std::move(progress)](const float fraction) {
            progress(0.65F + 0.30F * fraction);
        } : std::function<void(float)>{}, encode_error);
    if (encoded != AG_OK) {
        cleanup();
        return {encoded == AG_CANCELLED ? WriteError::Cancelled
                                       : WriteError::EncodeFailed,
                encode_error, rendered.frames};
    }
    if (request.keep_metadata && !request.metadata_source_path.empty()) {
        std::string metadata_error;
        if (!copy_metadata(request.metadata_source_path, staged_path,
                           metadata_error)) {
            cleanup();
            return {WriteError::MetadataFailed, metadata_error,
                    rendered.frames};
        }
    }

    const SampleFrame expected = static_cast<SampleFrame>(std::llround(
        static_cast<long double>(rendered.frames) * config.sample_rate
        / static_cast<long double>(rendered.sample_rate)));
    const SampleFrame verified = count_decoded_frames(
        staged_path, config.sample_rate, config.channels);
    const bool exact_frames = verified == expected;
    const bool framed_lossy = codec == "aac" || codec == "libvorbis"
        || codec == "libopus" || codec == "libmp3lame";
    const std::int64_t expected_ms = static_cast<std::int64_t>(std::llround(
        static_cast<long double>(expected) * 1'000.0L / config.sample_rate));
    const std::int64_t duration_ms = framed_lossy
        ? probed_duration_ms(staged_path) : -1;
    const bool duration_matches = framed_lossy && duration_ms >= 0
        && std::llabs(duration_ms - expected_ms) <= 1;
    if (!exact_frames && !duration_matches) {
        cleanup();
        return {WriteError::VerificationFailed,
                "decoded frame count mismatch: expected "
                    + std::to_string(expected) + ", got "
                    + std::to_string(verified), rendered.frames};
    }
    std::string commit_error;
    if (!commit_file(staged_path, request.output_path, commit_error)) {
        cleanup();
        return {WriteError::CommitFailed, commit_error, rendered.frames};
    }
    std::filesystem::remove(render_path, fs_error);
    return {WriteError::None, {}, rendered.frames};
}

} // namespace agplayer::editor

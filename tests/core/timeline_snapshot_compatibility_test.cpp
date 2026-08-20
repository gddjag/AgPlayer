#include "audio_editor/audio_document.hpp"
#include "audio_editor/document_renderer.hpp"
#include "audio_editor/document_writer.hpp"
#include "audio_editor/event_timeline.hpp"
#include "audio_editor/time_pitch_session.hpp"
#include "decoder.hpp"

#include <agplayer/c_api.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {

[[noreturn]] void fail(const std::string_view message)
{
    std::cerr << message << '\n';
    std::exit(1);
}

void require(const bool condition, const std::string_view message)
{
    if (!condition) fail(message);
}

agplayer::editor::SampleFrame decoded_frames(
    const std::filesystem::path& path, const int sample_rate, const int channels)
{
    agplayer::Decoder decoder;
    require(decoder.open(path.u8string(), sample_rate, channels) == AG_OK,
            "decoder open failed");

    agplayer::DecodedAudioBlock block;
    agplayer::editor::SampleFrame frames = 0;
    for (;;) {
        require(decoder.read(block) == AG_OK, "decoder read failed");
        frames += static_cast<agplayer::editor::SampleFrame>(block.frames);
        if (block.end_of_stream) return frames;
    }
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
    const auto source = std::make_shared<const AudioSource>(AudioSource{
        input, static_cast<std::uint32_t>(metadata.sample_rate),
        static_cast<std::uint32_t>(metadata.channels), total});

    EventTimeline valid_timeline;
    require(valid_timeline.insert(AudioEvent{1, source, 0, total, 0}),
            "valid timeline event rejected");
    const TimelineSnapshot valid = valid_timeline.snapshot();
    const auto legacy = singleEventDocumentSnapshot(valid);
    require(legacy.has_value(), "valid timeline did not adapt to legacy snapshot");

    const fs::path base = input.parent_path();
    const fs::path legacy_render = base / "timeline-legacy-render.wav";
    const fs::path timeline_render = base / "timeline-render.wav";
    const fs::path legacy_write = base / "timeline-legacy-write.wav";
    const fs::path timeline_write = base / "timeline-write.wav";
    const fs::path legacy_pitch = base / "timeline-legacy-pitch.wav";
    const fs::path timeline_pitch = base / "timeline-pitch.wav";
    std::error_code ignored;
    for (const fs::path& output : {legacy_render, timeline_render, legacy_write,
                                   timeline_write, legacy_pitch, timeline_pitch}) {
        fs::remove(output, ignored);
    }

    DocumentRenderer renderer;
    const RenderResult rendered_legacy = renderer.renderFloatWav(
        *legacy, std::nullopt, legacy_render);
    const RenderResult rendered_timeline = renderer.renderFloatWav(
        valid, std::nullopt, timeline_render);
    require(rendered_legacy.success && rendered_timeline.success,
            "valid timeline renderer path failed");
    require(rendered_timeline.frames == rendered_legacy.frames
                && rendered_timeline.sample_rate == rendered_legacy.sample_rate
                && rendered_timeline.channels == rendered_legacy.channels,
            "timeline renderer differs from legacy behavior");

    DocumentWriter writer;
    WriteRequest legacy_request;
    legacy_request.snapshot = *legacy;
    legacy_request.output_path = legacy_write;
    legacy_request.codec_name = "pcm_s24le";
    WriteRequest timeline_request;
    timeline_request.timeline_snapshot = valid;
    timeline_request.output_path = timeline_write;
    timeline_request.codec_name = "pcm_s24le";
    const WriteResult written_legacy = writer.write(legacy_request);
    const WriteResult written_timeline = writer.write(timeline_request);
    require(written_legacy.ok() && written_timeline.ok(),
            "valid timeline writer path failed");
    require(written_timeline.frames == written_legacy.frames,
            "timeline writer differs from legacy behavior");

    TimePitchSession legacy_session;
    TimePitchSession timeline_session;
    const TimePitchResult pitched_legacy = legacy_session.process(
        *legacy, legacy_pitch);
    const TimePitchResult pitched_timeline = timeline_session.process(
        valid, timeline_pitch);
    require(pitched_legacy.success && pitched_timeline.success,
            "valid timeline time/pitch path failed");
    require(pitched_timeline.source.sample_rate == pitched_legacy.source.sample_rate
                && pitched_timeline.source.channels == pitched_legacy.source.channels
                && std::llabs(pitched_timeline.source.total_frames
                               - pitched_legacy.source.total_frames) <= 2'048,
            "timeline time/pitch differs from legacy behavior");

    const SampleFrame segment = total / 4;
    require(segment > 1, "fixture is too short for compatibility snapshots");
    EventTimeline multi_timeline;
    require(multi_timeline.insert(AudioEvent{2, source, 0, segment, 0})
                && multi_timeline.insert(AudioEvent{
                    3, source, segment, segment * 2, segment}),
            "multi-event setup failed");
    EventTimeline gapped_timeline;
    require(gapped_timeline.insert(AudioEvent{4, source, 0, segment, segment}),
            "gapped setup failed");
    TimelineSnapshot modified = valid;
    modified.events.front().fadeIn = 1;

    const auto rejects_without_output = [&](const TimelineSnapshot& snapshot,
                                            const std::string_view label) {
        const fs::path render_output = base / ("timeline-reject-render-"
                                                + std::string(label) + ".wav");
        const fs::path write_output = base / ("timeline-reject-write-"
                                               + std::string(label) + ".wav");
        const fs::path pitch_output = base / ("timeline-reject-pitch-"
                                               + std::string(label) + ".wav");
        for (const fs::path& output : {render_output, write_output, pitch_output}) {
            fs::remove(output, ignored);
        }

        require(!renderer.renderFloatWav(snapshot, std::nullopt, render_output).success
                    && !fs::exists(render_output),
                "renderer accepted unsupported timeline snapshot");

        WriteRequest rejected_request;
        rejected_request.timeline_snapshot = snapshot;
        rejected_request.output_path = write_output;
        rejected_request.codec_name = "pcm_s24le";
        const WriteResult rejected_write = writer.write(rejected_request);
        require(rejected_write.error == WriteError::RenderFailed
                    && !fs::exists(write_output),
                "writer exported unsupported timeline snapshot");

        TimePitchSession rejected_session;
        require(!rejected_session.process(snapshot, pitch_output).success
                    && !fs::exists(pitch_output),
                "time/pitch exported unsupported timeline snapshot");
    };

    rejects_without_output(multi_timeline.snapshot(), "multi");
    rejects_without_output(gapped_timeline.snapshot(), "gap");
    rejects_without_output(modified, "modified");

    for (const fs::path& output : {legacy_render, timeline_render, legacy_write,
                                   timeline_write, legacy_pitch, timeline_pitch}) {
        fs::remove(output, ignored);
    }
    return 0;
}

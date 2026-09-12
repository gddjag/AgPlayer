#include "audio_editor/document_renderer.hpp"
#include "audio_editor/document_writer.hpp"
#include "audio_editor/event_timeline.hpp"
#include "audio_editor/time_pitch_session.hpp"
#include "decoder.hpp"

#include <agplayer/c_api.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>

namespace {
[[noreturn]] void fail(const char* message) { std::cerr << message << '\n'; std::exit(1); }
void require(const bool value, const char* message) { if (!value) fail(message); }
}

int main(const int argc, char** argv)
{
    require(argc == 2, "fixture argument required");
    namespace fs = std::filesystem;
    using namespace agplayer::editor;
    const fs::path input = fs::u8path(argv[1]);
    agplayer::Decoder probe;
    require(probe.open(input.u8string()) == AG_OK, "fixture probe failed");
    const auto metadata = probe.metadata();
    probe.close();
    const auto source = std::make_shared<const AudioSource>(AudioSource{
        input, static_cast<std::uint32_t>(metadata.sample_rate),
        static_cast<std::uint32_t>(metadata.channels), metadata.duration_ms
            * metadata.sample_rate / 1'000});

    EventTimeline timeline;
    require(timeline.insert(AudioEvent{1, source, 0, source->total_frames, 0}),
            "timeline setup failed");
    const fs::path base = input.parent_path();
    const fs::path rendered = base / "timeline-compatible-render.wav";
    const fs::path written = base / "timeline-compatible-write.wav";
    const fs::path pitched = base / "timeline-compatible-pitch.wav";
    std::error_code ignored;
    for (const auto& path : {rendered, written, pitched}) fs::remove(path, ignored);

    DocumentRenderer renderer;
    require(renderer.renderFloatWav(timeline.snapshot(), std::nullopt, rendered).success,
            "single event renderer failed");
    WriteRequest write{timeline.snapshot(), written, "pcm_s24le"};
    require(DocumentWriter{}.write(write).ok(), "single event writer failed");
    require(TimePitchSession{}.process(timeline.snapshot(), pitched).success,
            "single event time pitch failed");

    const auto requireAccepted = [&](const TimelineSnapshot& snapshot,
                                     const char* name) {
        const std::string prefix = std::string("timeline-accepted-") + name;
        const fs::path acceptedRender = base / (prefix + "-render.wav");
        const fs::path acceptedWrite = base / (prefix + "-write.wav");
        const fs::path acceptedPitch = base / (prefix + "-pitch.wav");
        for (const auto& path : {acceptedRender, acceptedWrite, acceptedPitch}) {
            fs::remove(path, ignored);
        }
        require(renderer.renderFloatWav(snapshot, std::nullopt, acceptedRender).success
                    && fs::exists(acceptedRender),
                "renderer rejected supported timeline");
        WriteRequest acceptedWriteRequest{snapshot, acceptedWrite, "pcm_s24le"};
        require(DocumentWriter{}.write(acceptedWriteRequest).ok()
                    && fs::exists(acceptedWrite),
                "writer rejected supported timeline");
        require(TimePitchSession{}.process(snapshot, acceptedPitch).success
                    && fs::exists(acceptedPitch),
                "time pitch rejected supported timeline");
        for (const auto& path : {acceptedRender, acceptedWrite, acceptedPitch}) {
            fs::remove(path, ignored);
        }
    };

    const auto requireRejected = [&](TimelineSnapshot snapshot,
                                     const char* name) {
        const fs::path rejected = base / (std::string("timeline-rejected-") + name + ".wav");
        fs::remove(rejected, ignored);
        require(!renderer.renderFloatWav(snapshot, std::nullopt, rejected).success
                    && !fs::exists(rejected), "renderer accepted rejected timeline");
        WriteRequest rejectWrite{std::move(snapshot), rejected, "pcm_s24le"};
        require(DocumentWriter{}.write(rejectWrite).error == WriteError::RenderFailed
                    && !fs::exists(rejected), "writer accepted rejected timeline");
        require(!TimePitchSession{}.process(rejectWrite.snapshot, rejected).success
                    && !fs::exists(rejected), "time pitch accepted rejected timeline");
    };

    EventTimeline multi;
    const SampleFrame half = source->total_frames / 2;
    require(multi.insert(AudioEvent{2, source, 0, half, 0})
            && multi.insert(AudioEvent{3, source, half, source->total_frames, half}),
            "multi timeline setup failed");
    requireAccepted(multi.snapshot(), "multi");

    TimelineSnapshot gap = timeline.snapshot();
    gap.events.front().timelineStart = 1;
    gap.totalFrames = source->total_frames + 1;
    requireAccepted(gap, "gap");
    for (const auto& supported : {
             [&] { auto value = timeline.snapshot(); value.events.front().fadeIn = 1; return value; }(),
             [&] { auto value = timeline.snapshot(); value.events.front().envelope = {{0, 0.5F}}; return value; }(),
         }) {
        requireAccepted(supported, "parameter");
    }
    for (const auto& rejected : {
             [&] { auto value = timeline.snapshot(); value.events.front().speedRatio = 1.25; return value; }(),
             [&] { auto value = timeline.snapshot(); value.events.front().pitchSemitone = 1; return value; }(),
             [&] { auto value = timeline.snapshot(); value.events.front().sourceEnd += 1; return value; }(),
             [&] { auto value = timeline.snapshot(); value.events.front().timelineStart = std::numeric_limits<SampleFrame>::max(); return value; }(),
         }) {
        requireRejected(rejected, "parameter");
    }

    for (const auto& path : {rendered, written, pitched}) fs::remove(path, ignored);
    return 0;
}

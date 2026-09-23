#pragma once

#include "audio_document.hpp"
#include "../decoder.hpp"
#include "../time_pitch_engine.hpp"

#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace agplayer::editor {

// Worker-owned, bounded streaming source. Each track owns at most one decoder;
// no source is opened until its first audible intersection with a read block.
// The output deliberately retains headroom for the single session DSP stage.
class TimelineMixer final {
public:
    static constexpr std::size_t kBlockFrames = 4'096;
    [[nodiscard]] static bool prepare(TimelineSnapshot& snapshot, std::string& error);
    explicit TimelineMixer(TimelineSnapshot snapshot,
                           const std::atomic_bool* cancelled = nullptr,
                           agplayer::TimePitchEngineFactory engineFactory = &agplayer::create_time_pitch_engine);
    void seek(SampleFrame frame);
    [[nodiscard]] ag_result read(std::vector<float>& samples, SampleFrame endFrame);
    [[nodiscard]] SampleFrame cursor() const noexcept { return cursor_; }
    [[nodiscard]] const TimelineSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    struct TrackReader final {
        std::vector<std::size_t> events;
        std::size_t index{};
        std::unique_ptr<agplayer::Decoder> decoder;
        agplayer::DecodedAudioBlock block;
        std::size_t offset{};
        std::unique_ptr<agplayer::ITimePitchEngine> processor;
        std::vector<float> processed;
        SampleFrame source_frames_left{};
        bool flushed{};
    };
    [[nodiscard]] bool cancelled() const noexcept;
    [[nodiscard]] bool open(TrackReader& reader, const AudioEvent& event,
                            SampleFrame localOffset);
    [[nodiscard]] ag_result nextProcessedBlock(TrackReader& reader);
    [[nodiscard]] ag_result mix(TrackReader& reader, std::vector<float>& samples,
                                SampleFrame blockEnd);

    TimelineSnapshot snapshot_;
    std::array<TrackReader, kTrackCount> readers_;
    SampleFrame cursor_{};
    const std::atomic_bool* cancelled_{};
    bool any_solo_{};
    agplayer::TimePitchEngineFactory engine_factory_{};
};

// Applied exactly once, after session TimePitch (or after mixing at unity).
void finalizeTimelineSamples(std::vector<float>& samples) noexcept;

} // namespace agplayer::editor

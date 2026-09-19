#pragma once

#include "audio_event.hpp"

#include <cstdint>
#include <array>
#include <vector>

namespace agplayer::editor {

struct TrackState final {
    bool muted{};
    float gain{1.0F};
};
inline bool operator==(const TrackState& a, const TrackState& b) noexcept
{ return a.muted == b.muted && a.gain == b.gain; }

struct TimelineSnapshot final {
    std::vector<AudioEvent> events;
    SampleFrame totalFrames{};
    std::uint64_t revision{};
    std::array<TrackState, kTrackCount> tracks{};
    std::uint32_t sampleRate{};
    std::uint32_t channels{2};
    float legacyMasterGain{1.0F};
};

class EventTimeline final {
public:
    [[nodiscard]] bool insert(AudioEvent event);
    [[nodiscard]] bool replace(std::vector<AudioEvent> events);
    [[nodiscard]] bool restore(TimelineSnapshot snapshot);
    [[nodiscard]] bool moveEvent(EventId id, SampleFrame timelineStart);
    [[nodiscard]] bool moveEvent(EventId id, SampleFrame timelineStart, int trackIndex);
    [[nodiscard]] bool trimEvent(EventId id, SampleFrame sourceStart,
                                 SampleFrame sourceEnd,
                                 SampleFrame timelineStart);
    [[nodiscard]] const AudioEvent* event(EventId id) const noexcept;
    [[nodiscard]] std::vector<EventId> eventsAt(SampleFrame frame) const;
    [[nodiscard]] SampleFrame totalFrames() const noexcept;
    [[nodiscard]] TimelineSnapshot snapshot() const;
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    [[nodiscard]] bool overlaps(const AudioEvent& candidate) const noexcept;
    [[nodiscard]] bool replaceEvent(EventId id, AudioEvent candidate);
    [[nodiscard]] static SampleFrame endFrame(const AudioEvent& event) noexcept;

    std::vector<AudioEvent> events_;
    std::uint64_t revision_{};
    std::array<TrackState, kTrackCount> tracks_{};
    std::uint32_t sample_rate_{};
    std::uint32_t channels_{2};
    float legacy_master_gain_{1.0F};
};

} // namespace agplayer::editor

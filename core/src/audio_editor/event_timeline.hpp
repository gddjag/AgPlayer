#pragma once

#include "audio_event.hpp"

#include <cstdint>
#include <vector>

namespace agplayer::editor {

struct TimelineSnapshot final {
    std::vector<AudioEvent> events;
    SampleFrame totalFrames{};
    std::uint64_t revision{};
};

class EventTimeline final {
public:
    [[nodiscard]] bool insert(AudioEvent event);
    [[nodiscard]] bool replace(std::vector<AudioEvent> events);
    [[nodiscard]] bool moveEvent(EventId id, SampleFrame timelineStart);
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
};

} // namespace agplayer::editor

#pragma once

#include "edit_command.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace agplayer::editor {

struct AudioSource final {
    std::filesystem::path path;
    std::uint32_t sample_rate{};
    std::uint32_t channels{};
    SampleFrame total_frames{};
};

struct AudioSpan final {
    std::shared_ptr<const AudioSource> source;
    SampleFrame source_start{};
    SampleFrame frame_count{};
    bool silent{};
    float gain_start{1.0F};
    float gain_end{1.0F};
};

struct Selection final {
    SampleFrame start{};
    SampleFrame end{};

    [[nodiscard]] bool valid() const noexcept
    {
        return start >= 0 && end > start;
    }
};

struct Marker final {
    std::string name;
    SampleFrame frame{};
};

struct DocumentSnapshot final {
    std::vector<AudioSpan> spans;
    std::vector<Marker> markers;
    std::optional<Selection> selection;
};

[[nodiscard]] bool operator==(const AudioSpan& left,
                              const AudioSpan& right) noexcept;
[[nodiscard]] bool operator==(const Selection& left,
                              const Selection& right) noexcept;
[[nodiscard]] bool operator==(const Marker& left,
                              const Marker& right) noexcept;
[[nodiscard]] bool operator==(const DocumentSnapshot& left,
                              const DocumentSnapshot& right) noexcept;

class AudioDocument final {
public:
    [[nodiscard]] static AudioDocument fromSource(AudioSource source);

    bool setSelection(Selection selection) noexcept;
    bool clearSelection() noexcept;
    bool addMarker(Marker marker);
    bool insertSource(AudioSource source, SampleFrame frame);
    bool replaceRangeWithSource(AudioSource source, Selection range);
    bool apply(const EditCommand& command);
    bool undo();
    bool redo();

    [[nodiscard]] bool canUndo() const noexcept { return !undo_stack_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redo_stack_.empty(); }
    [[nodiscard]] bool hasClipboard() const noexcept { return !clipboard_.empty(); }
    [[nodiscard]] SampleFrame totalFrames() const noexcept;
    [[nodiscard]] const std::vector<AudioSpan>& spans() const noexcept
    {
        return state_.spans;
    }
    [[nodiscard]] const std::vector<Marker>& markers() const noexcept
    {
        return state_.markers;
    }
    [[nodiscard]] DocumentSnapshot snapshot() const { return state_; }

private:
    using State = DocumentSnapshot;

    [[nodiscard]] bool hasValidSelection() const noexcept;
    [[nodiscard]] bool splitAt(std::vector<AudioSpan>& spans,
                               SampleFrame frame) const;
    [[nodiscard]] std::vector<AudioSpan> selectedSpans(
        std::vector<AudioSpan> spans) const;
    [[nodiscard]] bool deleteSelectedRange(State& candidate) const;
    void commit(State candidate);

    State state_;
    std::vector<AudioSpan> clipboard_;
    std::vector<State> undo_stack_;
    std::vector<State> redo_stack_;
};

} // namespace agplayer::editor

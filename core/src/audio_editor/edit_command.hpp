#pragma once

#include "audio_event.hpp"

namespace agplayer::editor {

class EditCommand final {
public:
    enum class Type {
        CopySelection,
        CutSelection,
        DeleteSelection,
        CropToSelection,
        SilenceSelection,
        FadeIn,
        FadeOut,
        Gain,
        PasteAt,
        InsertSilence
    };

    [[nodiscard]] static EditCommand copySelection() noexcept;
    [[nodiscard]] static EditCommand cutSelection() noexcept;
    [[nodiscard]] static EditCommand deleteSelection() noexcept;
    [[nodiscard]] static EditCommand cropToSelection() noexcept;
    [[nodiscard]] static EditCommand silenceSelection() noexcept;
    [[nodiscard]] static EditCommand fadeIn() noexcept;
    [[nodiscard]] static EditCommand fadeOut() noexcept;
    [[nodiscard]] static EditCommand gain(float linear_gain) noexcept;
    [[nodiscard]] static EditCommand pasteAt(SampleFrame frame) noexcept;
    [[nodiscard]] static EditCommand insertSilence(
        SampleFrame frame, SampleFrame frame_count) noexcept;

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] SampleFrame frame() const noexcept { return frame_; }
    [[nodiscard]] SampleFrame frameCount() const noexcept { return frame_count_; }
    [[nodiscard]] float linearGain() const noexcept { return linear_gain_; }

private:
    explicit EditCommand(Type type, SampleFrame frame = 0,
                         SampleFrame frame_count = 0,
                         float linear_gain = 1.0F) noexcept;

    Type type_;
    SampleFrame frame_{};
    SampleFrame frame_count_{};
    float linear_gain_{1.0F};
};

} // namespace agplayer::editor

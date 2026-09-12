#pragma once

#include "audio_document.hpp"

#include <algorithm>
#include <cmath>

namespace agplayer::editor {

// Maps processed output frames back to the original timeline.  The mapping is
// intentionally independent of a processor's variable flush length so every
// render path applies automation at the same source time.
class AutomationTimeMapper final {
public:
    AutomationTimeMapper(const SampleFrame timelineStart,
                         const SampleFrame inputFrames,
                         const double speedRatio) noexcept
        : timeline_start_(std::max<SampleFrame>(0, timelineStart)),
          input_frames_(std::max<SampleFrame>(0, inputFrames)),
          speed_ratio_(std::isfinite(speedRatio) && speedRatio > 0.0
              ? speedRatio : 1.0),
          timeline_anchor_(std::max<SampleFrame>(0, timelineStart))
    {
    }

    void resetAnchor(const SampleFrame outputFrame,
                     const SampleFrame timelineFrame) noexcept
    {
        output_anchor_ = std::max<SampleFrame>(0, outputFrame);
        timeline_anchor_ = std::clamp(
            timelineFrame, timeline_start_,
            timeline_start_ + std::max<SampleFrame>(0, input_frames_ - 1));
    }

    [[nodiscard]] SampleFrame map(const SampleFrame outputFrame) const noexcept
    {
        if (input_frames_ <= 1 || outputFrame <= output_anchor_) {
            return timeline_anchor_;
        }
        const long double scaled = std::floor(
            static_cast<long double>(outputFrame - output_anchor_)
                * speed_ratio_);
        const SampleFrame anchorRelative = timeline_anchor_ - timeline_start_;
        const long double relativeValue = anchorRelative + scaled;
        const SampleFrame relative = relativeValue
                >= static_cast<long double>(input_frames_ - 1)
            ? input_frames_ - 1 : static_cast<SampleFrame>(relativeValue);
        return timeline_start_ + relative;
    }

private:
    SampleFrame timeline_start_{};
    SampleFrame input_frames_{};
    double speed_ratio_{1.0};
    SampleFrame output_anchor_{};
    SampleFrame timeline_anchor_{};
};

} // namespace agplayer::editor

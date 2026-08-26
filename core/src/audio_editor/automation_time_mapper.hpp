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
              ? speedRatio : 1.0)
    {
    }

    [[nodiscard]] SampleFrame map(const SampleFrame outputFrame) const noexcept
    {
        if (input_frames_ <= 1 || outputFrame <= 0) return timeline_start_;
        const long double scaled = std::floor(
            static_cast<long double>(outputFrame) * speed_ratio_);
        const SampleFrame relative = scaled
                >= static_cast<long double>(input_frames_ - 1)
            ? input_frames_ - 1 : static_cast<SampleFrame>(scaled);
        return timeline_start_ + relative;
    }

private:
    SampleFrame timeline_start_{};
    SampleFrame input_frames_{};
    double speed_ratio_{1.0};
};

} // namespace agplayer::editor

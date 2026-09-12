#pragma once

#include "audio_document.hpp"
#include "event_timeline.hpp"

#include <algorithm>
#include <cmath>

namespace agplayer::editor {

class TimePixelMapper final {
public:
    TimePixelMapper(const TimelineSnapshot& snapshot, const double pixel_width,
                    const double frames_per_pixel) noexcept
        : TimePixelMapper(snapshot.totalFrames, pixel_width, frames_per_pixel)
    {
    }

    TimePixelMapper(const SampleFrame document_frames,
                    const double pixel_width,
                    const double frames_per_pixel) noexcept
        : document_frames_(std::max<SampleFrame>(0, document_frames)),
          pixel_width_(std::isfinite(pixel_width)
                  ? std::max(0.0, pixel_width) : 0.0),
          frames_per_pixel_(std::isfinite(frames_per_pixel)
                  ? std::max(0.0, frames_per_pixel) : 0.0)
    {
        clampVisibleStart();
    }

    void setVisibleStart(const SampleFrame frame) noexcept
    {
        visible_start_ = frame;
        clampVisibleStart();
    }

    [[nodiscard]] double frameToPixel(const SampleFrame frame) const noexcept
    {
        if (frames_per_pixel_ <= 0.0) {
            return 0.0;
        }
        return static_cast<double>(frame - visible_start_) / frames_per_pixel_;
    }

    [[nodiscard]] SampleFrame pixelToFrame(const double pixel) const noexcept
    {
        if (!std::isfinite(pixel) || frames_per_pixel_ <= 0.0) {
            return visible_start_;
        }
        const long double mapped = static_cast<long double>(visible_start_)
            + static_cast<long double>(pixel) * frames_per_pixel_;
        const long double clamped = std::clamp(
            mapped, 0.0L, static_cast<long double>(document_frames_));
        return static_cast<SampleFrame>(std::llround(clamped));
    }

    [[nodiscard]] Selection visibleFrameRange() const noexcept
    {
        if (document_frames_ <= 0 || pixel_width_ <= 0.0
            || frames_per_pixel_ <= 0.0) {
            return {};
        }
        const SampleFrame count = visibleFrameCount();
        return {visible_start_, std::min(document_frames_, visible_start_ + count)};
    }

private:
    [[nodiscard]] SampleFrame visibleFrameCount() const noexcept
    {
        const long double raw = static_cast<long double>(pixel_width_)
            * static_cast<long double>(frames_per_pixel_);
        return static_cast<SampleFrame>(std::min(
            static_cast<long double>(document_frames_), std::ceil(raw)));
    }

    void clampVisibleStart() noexcept
    {
        const SampleFrame maximum = std::max<SampleFrame>(
            0, document_frames_ - visibleFrameCount());
        visible_start_ = std::clamp(visible_start_, SampleFrame{0}, maximum);
    }

    SampleFrame document_frames_{};
    double pixel_width_{};
    double frames_per_pixel_{};
    SampleFrame visible_start_{};
};

} // namespace agplayer::editor

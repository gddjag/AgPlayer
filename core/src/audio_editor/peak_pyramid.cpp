#include "peak_pyramid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace agplayer::editor {
namespace {

PeakBucket merged(const PeakBucket left, const PeakBucket right) noexcept
{
    return {std::min(left.minimum, right.minimum),
            std::max(left.maximum, right.maximum)};
}

bool valid(const PeakBucket bucket) noexcept
{
    return std::isfinite(bucket.minimum) && std::isfinite(bucket.maximum)
        && bucket.minimum <= bucket.maximum;
}

SampleFrame saturatedMultiply(const SampleFrame value,
                              const SampleFrame multiplier) noexcept
{
    if (value <= 0 || multiplier <= 0) {
        return 0;
    }
    if (value > std::numeric_limits<SampleFrame>::max() / multiplier) {
        return std::numeric_limits<SampleFrame>::max();
    }
    return value * multiplier;
}

} // namespace

PeakPyramid PeakPyramid::fromBaseBuckets(
    std::vector<std::vector<PeakBucket>> channels,
    const SampleFrame base_bucket_frames,
    const SampleFrame document_frames)
{
    PeakPyramid result;
    if (base_bucket_frames <= 0 || document_frames <= 0 || channels.empty()) {
        return result;
    }
    result.base_bucket_frames_ = base_bucket_frames;
    result.document_frames_ = document_frames;
    result.channels_.reserve(channels.size());
    for (auto& base : channels) {
        if (base.empty()
            || !std::all_of(base.begin(), base.end(), valid)) {
            return {};
        }
        Channel channel;
        channel.levels.push_back(std::move(base));
        while (channel.levels.back().size() > 1) {
            const auto& previous = channel.levels.back();
            std::vector<PeakBucket> next;
            next.reserve((previous.size() + 1U) / 2U);
            for (std::size_t index = 0; index < previous.size(); index += 2U) {
                next.push_back(index + 1U < previous.size()
                    ? merged(previous[index], previous[index + 1U])
                    : previous[index]);
            }
            channel.levels.push_back(std::move(next));
        }
        result.channels_.push_back(std::move(channel));
    }
    return result;
}

std::vector<PeakBucket> PeakPyramid::read(
    const std::size_t channel,
    const SampleFrame start,
    const SampleFrame frame_count,
    const std::size_t pixel_width) const
{
    return readWindow(channel, start, frame_count, pixel_width).buckets;
}

PeakReadWindow PeakPyramid::readWindow(
    const std::size_t channel,
    const SampleFrame start,
    const SampleFrame frame_count,
    const std::size_t pixel_width) const
{
    if (channel >= channels_.size() || start < 0 || frame_count <= 0
        || pixel_width == 0 || start >= document_frames_) {
        return {};
    }
    const SampleFrame end = std::min(document_frames_,
        frame_count > document_frames_ - start
            ? document_frames_ : start + frame_count);
    const auto& levels = channels_[channel].levels;
    std::size_t level = 0;
    SampleFrame bucket_frames = base_bucket_frames_;
    const std::size_t target = pixel_width > std::numeric_limits<std::size_t>::max() / 2U
        ? std::numeric_limits<std::size_t>::max() : pixel_width * 2U;
    while (level + 1U < levels.size()) {
        const auto first = static_cast<std::size_t>(start / bucket_frames);
        const auto last = static_cast<std::size_t>(
            (end + bucket_frames - 1) / bucket_frames);
        if (last - first <= target) break;
        level += 1U;
        bucket_frames = saturatedMultiply(bucket_frames, 2);
    }
    const auto& buckets = levels[level];
    const std::size_t first = std::min<std::size_t>(
        buckets.size(), static_cast<std::size_t>(start / bucket_frames));
    const std::size_t last = std::min<std::size_t>(buckets.size(),
        static_cast<std::size_t>((end + bucket_frames - 1) / bucket_frames));
    PeakReadWindow result;
    result.buckets.assign(
        buckets.begin() + static_cast<std::ptrdiff_t>(first),
        buckets.begin() + static_cast<std::ptrdiff_t>(last));
    result.start = saturatedMultiply(
        static_cast<SampleFrame>(first), bucket_frames);
    result.bucketFrames = bucket_frames;
    return result;
}

std::size_t PeakPyramid::levelCount() const noexcept
{
    return channels_.empty() ? 0U : channels_.front().levels.size();
}

} // namespace agplayer::editor

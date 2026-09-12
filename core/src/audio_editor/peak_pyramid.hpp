#pragma once

#include "audio_document.hpp"

#include <cstddef>
#include <vector>

namespace agplayer::editor {

struct PeakBucket final {
    float minimum{};
    float maximum{};
};

struct PeakReadWindow final {
    std::vector<PeakBucket> buckets;
    SampleFrame start{};
    SampleFrame bucketFrames{};
};

class PeakPyramid final {
public:
    static PeakPyramid fromBaseBuckets(
        std::vector<std::vector<PeakBucket>> channels,
        SampleFrame base_bucket_frames,
        SampleFrame document_frames);

    [[nodiscard]] std::vector<PeakBucket> read(
        std::size_t channel,
        SampleFrame start,
        SampleFrame frame_count,
        std::size_t pixel_width) const;
    [[nodiscard]] PeakReadWindow readWindow(
        std::size_t channel,
        SampleFrame start,
        SampleFrame frame_count,
        std::size_t pixel_width) const;

    [[nodiscard]] std::size_t channelCount() const noexcept
    {
        return channels_.size();
    }
    [[nodiscard]] std::size_t levelCount() const noexcept;
    [[nodiscard]] SampleFrame documentFrames() const noexcept
    {
        return document_frames_;
    }

private:
    struct Channel final {
        std::vector<std::vector<PeakBucket>> levels;
    };

    std::vector<Channel> channels_;
    SampleFrame base_bucket_frames_{};
    SampleFrame document_frames_{};
};

} // namespace agplayer::editor

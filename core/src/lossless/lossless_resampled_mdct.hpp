#pragma once
#include "lossless_mdct.hpp"

namespace agplayer::lossless {
// A separate analysis view. Never changes decoded source samples or
// metadata. A match after downsampling does not by itself prove upsampling.
class ResampledMdctProbe final {
public:
    ResampledMdctProbe(int inputRate, int channels, std::uint64_t totalInputFrames,
                       int targetRate);
    ~ResampledMdctProbe();
    ResampledMdctProbe(const ResampledMdctProbe&) = delete;
    ResampledMdctProbe& operator=(const ResampledMdctProbe&) = delete;
    bool available() const noexcept;
    void consume(const double* samples, std::size_t frames, const std::atomic_bool& cancel);
    // Drain the resampler once, including its delayed final samples.
    void finish(const std::atomic_bool& cancel);
    std::array<MdctFrameEvidence, 4> result() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

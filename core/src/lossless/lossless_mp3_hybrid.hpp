#pragma once
#include "lossless_mdct.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
namespace agplayer::lossless {
// Native-rate, long-block MPEG hybrid-grid evidence; not codec certification.
// Four disjoint bounded excerpts, with no signed channel downmix.
class Mp3HybridProbe final {
public:
    Mp3HybridProbe(int sampleRate, int channels, std::uint64_t totalFrames);
    ~Mp3HybridProbe();
    Mp3HybridProbe(const Mp3HybridProbe&) = delete;
    Mp3HybridProbe& operator=(const Mp3HybridProbe&) = delete;
    void consume(const double* interleaved, std::size_t frames, const std::atomic_bool& cancel);
    // Optional expensive stage; call after capture. One attempt is idempotent,
    // including an attempt interrupted by cancellation. No decoding is repeated.
    void refine(const std::atomic_bool& cancel);
    [[nodiscard]] MdctFrameEvidence result() const noexcept;
    // Stereo-only fallback: measure the other original channel at each anchor.
    // The caller applies the complete gate to each result independently. This
    // does not mix channels or merge evidence with the primary refinement.
    void refineAlternate(const std::atomic_bool& cancel);
    [[nodiscard]] MdctFrameEvidence alternateResult() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

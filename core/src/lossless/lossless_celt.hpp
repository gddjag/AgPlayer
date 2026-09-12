#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace agplayer::lossless {

struct CeltBandEvidence final {
    double robustZ = 0.0;
    double peakProminence = 0.0;
    double anchorPeakCoherence = 0.0;
    std::size_t halfProminenceWidth = 0;
    std::size_t peakPhase = 0;
    std::size_t activeBlocks = 0;
    std::size_t alignedBlocks = 0;
};

struct CeltFrameEvidence final {
    CeltBandEvidence fullBand;
    CeltBandEvidence lowBand;
    std::size_t phaseAgreementSamples = 0;
};

// Measures native 48 kHz PCM against a 2.5, 5, 10 or 20 ms CELT framing profile. Results are
// signal evidence only; absent framing is not proof of lossless provenance.
class CeltFramingProbe final {
public:
    CeltFramingProbe(int sampleRate, int channels, std::uint64_t totalFrames,
                     std::size_t frameSamples = 960, std::size_t framesPerAnchor = 12);
    ~CeltFramingProbe();
    CeltFramingProbe(const CeltFramingProbe&) = delete;
    CeltFramingProbe& operator=(const CeltFramingProbe&) = delete;
    void consume(const double* interleaved, std::size_t frames,
                 const std::atomic_bool& cancel);
    // Depths 48 and short-frame-only 192/768 capture during consume; refine runs FFTs.
    // Repeated calls are idempotent; cancellation retains complete anchors only.
    void refine(const std::atomic_bool& cancel);
    [[nodiscard]] CeltFrameEvidence result() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agplayer::lossless

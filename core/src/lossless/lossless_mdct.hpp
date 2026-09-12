#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace agplayer::lossless {

struct MdctFrameEvidence final {
    const char* window = "";
    double coherentPeakDb = 0.0;
    double phaseConcentration = 0.0;
    double meanPeakZ = 0.0;
    std::size_t activeBlocks = 0;
    std::size_t alignedBlocks = 0;
};

// Bounded, native-rate PCM framing probe. Four disjoint excerpts are measured;
// raw channels are never mixed. The 1152 sine profile is a coarse frame probe,
// not a reconstruction of MP3's hybrid filterbank or proof of a specific codec.
class MdctFramingProbe final {
public:
    MdctFramingProbe(int sampleRate, int channels, std::uint64_t totalFrames);
    ~MdctFramingProbe();
    MdctFramingProbe(const MdctFramingProbe&) = delete;
    MdctFramingProbe& operator=(const MdctFramingProbe&) = delete;
    void consume(const double* interleaved, std::size_t frames,
                 const std::atomic_bool& cancel);
    [[nodiscard]] std::array<MdctFrameEvidence, 4> result() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::unique_ptr<Impl> mp3_;
};
} // namespace agplayer::lossless

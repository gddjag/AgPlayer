#pragma once
// Internal numerical seams, shared only with the independent oracle tests.
#include <array>
#include <cstddef>
extern "C" {
#include <libavutil/tx.h>
}
namespace agplayer::lossless::mp3_hybrid_detail {
// latest points to the newest sample, with 511 valid preceding samples.
void analysisSubbands(const double* latest, std::array<double,32>& output) noexcept;
void aliasReduction(double* bandsTimes18) noexcept;
// Adds the unchanged floored log-magnitude sum; false rejects nonfinite input.
bool accumulateLogEnergy(const double* coefficients, std::size_t count, double& energy) noexcept;
class LongMdct final {
public:
    LongMdct();
    ~LongMdct();
    LongMdct(const LongMdct&) = delete;
    LongMdct& operator=(const LongMdct&) = delete;
    void transform(const double* samples36, double* coefficients18);
private:
    AVTXContext* context_ = nullptr;
    av_tx_fn transform_ = nullptr;
    std::array<AVComplexDouble,36> input_{}, output_{}, pre_{};
    std::array<AVComplexDouble,18> post_{};
    std::array<double,36> window_{};
};
}

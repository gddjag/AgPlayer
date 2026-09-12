#include "lossless_resampled_mdct.hpp"
extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace agplayer::lossless {
namespace {
constexpr int chunkFrames = 2048;
// Bound Swr's ratio-dependent filter state; covers PCM rates through 768 kHz.
constexpr int maximumInputRate = 768000;
// Reject corrupt finite input well before squared-energy arithmetic can overflow.
constexpr double maximumSampleMagnitude = 1e100;
}
struct ResampledMdctProbe::Impl final {
    SwrContext* swr = nullptr;
    int channels;
    bool finished = false;
    std::vector<double> output;
    MdctFramingProbe probe;
    Impl(int rate, int count, std::uint64_t total, int target)
        : channels(count), output(static_cast<std::size_t>(chunkFrames) * count),
          probe(target, count, static_cast<std::uint64_t>(av_rescale_rnd(
              static_cast<std::int64_t>(total), target, rate, AV_ROUND_DOWN)))
    {
        AVChannelLayout layout{};
        av_channel_layout_default(&layout, count);
        const int allocated = swr_alloc_set_opts2(&swr, &layout, AV_SAMPLE_FMT_DBL, target,
                                                  &layout, AV_SAMPLE_FMT_DBL, rate, 0, nullptr);
        av_channel_layout_uninit(&layout);
        // Preserve transform evidence near the candidate Nyquist boundary.
        // These fixed analysis-only settings do not affect playback resampling.
        if (allocated < 0 || !swr
            || av_opt_set_int(swr, "filter_size", 128, 0) < 0
            || av_opt_set_double(swr, "cutoff", 1.0, 0) < 0
            || swr_init(swr) < 0) {
            swr_free(&swr);
            throw std::runtime_error("Cannot initialize inverse-rate MDCT analysis");
        }
    }
    ~Impl() { swr_free(&swr); }
    int convert(const double* samples, int count, const std::atomic_bool& cancel)
    {
        const auto* input = reinterpret_cast<const std::uint8_t*>(samples);
        auto* destination = reinterpret_cast<std::uint8_t*>(output.data());
        const int produced = swr_convert(swr, &destination, chunkFrames,
                                         samples ? &input : nullptr, count);
        if (produced < 0) throw std::runtime_error("Inverse-rate MDCT conversion failed");
        if (!cancel.load(std::memory_order_relaxed))
            probe.consume(output.data(), static_cast<std::size_t>(produced), cancel);
        return produced;
    }
};
ResampledMdctProbe::ResampledMdctProbe(int rate, int channels, std::uint64_t total, int target)
{
    if (rate < 88200 || rate > maximumInputRate || channels < 1 || channels > 64
        || (target != 44100 && target != 48000) || target >= rate
        || total > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return;
    impl_ = std::make_unique<Impl>(rate, channels, total, target);
}
ResampledMdctProbe::~ResampledMdctProbe() = default;
bool ResampledMdctProbe::available() const noexcept { return impl_ != nullptr; }
void ResampledMdctProbe::consume(const double* samples, std::size_t frames,
                                const std::atomic_bool& cancel)
{
    if (!impl_ || !samples || cancel.load(std::memory_order_relaxed)) return;
    if (impl_->finished) throw std::logic_error("Inverse-rate MDCT input after finish");
    if (frames > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(impl_->channels))
        throw std::overflow_error("Inverse-rate MDCT input size overflow");
    while (frames > 0 && !cancel.load(std::memory_order_relaxed)) {
        const auto count = std::min(frames, static_cast<std::size_t>(chunkFrames));
        const auto values = count * static_cast<std::size_t>(impl_->channels);
        for (std::size_t i = 0; i < values; ++i)
            if (!std::isfinite(samples[i]) || std::abs(samples[i]) > maximumSampleMagnitude)
                throw std::runtime_error("Invalid PCM in inverse-rate MDCT analysis");
        impl_->convert(samples, static_cast<int>(count), cancel);
        samples += values;
        frames -= count;
    }
}
void ResampledMdctProbe::finish(const std::atomic_bool& cancel)
{
    if (!impl_ || impl_->finished || cancel.load(std::memory_order_relaxed)) return;
    while (!cancel.load(std::memory_order_relaxed)) {
        if (impl_->convert(nullptr, 0, cancel) == 0) { impl_->finished = true; break; }
    }
}
std::array<MdctFrameEvidence, 4> ResampledMdctProbe::result() const noexcept
{
    return impl_ ? impl_->probe.result() : std::array<MdctFrameEvidence, 4>{};
}
}

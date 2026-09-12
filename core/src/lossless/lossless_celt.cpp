#include "lossless_celt.hpp"

extern "C" {
#include <libavutil/tx.h>
}
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace agplayer::lossless {
namespace {
// Low-overlap window and de-emphasis reversal: RFC 6716 sections 4.3.6-4.3.7.
// https://www.rfc-editor.org/rfc/rfc6716.html#section-4.3.7
// Xiph celt_synthesis uses shortMdctSize << LM, with fixed 120-sample overlap.
// Nominal non-transient 2.5/5/10/20 ms frames use 120/240/480/960 bins.
// Phase-curve gates are local engineering criteria, not RFC accuracy guarantees.
constexpr double pi = 3.14159265358979323846;
constexpr std::size_t maximumHop = 960, overlap = 120;
constexpr std::size_t maximumFramesPerAnchor = 768, anchorCount = 4;
constexpr double preemphasis = 0.8500061035;
constexpr double minimumAnchorPower = 1e-8;
constexpr double normalizationFloor = 1e-15;
constexpr double normalizedCoefficientFloor = 1e-12;
constexpr double minimumMadScale = 1e-12;
constexpr double normalMadScale = 1.4826;
constexpr std::size_t alignmentTolerance = 2;
using Curve = std::array<double, maximumHop>;

template<std::size_t Capacity>
double median(std::array<double, Capacity> values, std::size_t count = Capacity) noexcept
{
    if (!count) return 0.0;
    std::sort(values.begin(), values.begin() + count);
    return count % 2 ? values[count / 2]
                     : 0.5 * (values[count / 2 - 1] + values[count / 2]);
}

std::size_t phaseDistance(std::size_t a, std::size_t b, std::size_t hop) noexcept
{
    const auto distance = a > b ? a - b : b - a;
    return std::min(distance, hop - distance);
}

CeltBandEvidence summarize(const std::array<Curve, anchorCount>& curves,
                           std::size_t count, std::size_t hop) noexcept
{
    CeltBandEvidence result;
    result.activeBlocks = count;
    if (!count) return result;
    Curve combined{};
    std::array<std::size_t, anchorCount> phases{};
    double real = 0.0, imaginary = 0.0;
    for (std::size_t anchor = 0; anchor < count; ++anchor) {
        phases[anchor] = static_cast<std::size_t>(std::max_element(
            curves[anchor].begin(), curves[anchor].begin() + hop) - curves[anchor].begin());
        const double angle = 2.0 * pi * phases[anchor] / hop;
        real += std::cos(angle); imaginary += std::sin(angle);
    }
    for (std::size_t phase = 0; phase < hop; ++phase) {
        std::array<double, anchorCount> values{};
        for (std::size_t anchor = 0; anchor < count; ++anchor)
            values[anchor] = curves[anchor][phase];
        combined[phase] = median(values, count);
    }
    result.peakPhase = static_cast<std::size_t>(std::max_element(
        combined.begin(), combined.begin() + hop) - combined.begin());
    const double center = median(combined, hop);
    Curve deviations{};
    for (std::size_t phase = 0; phase < hop; ++phase)
        deviations[phase] = std::abs(combined[phase] - center);
    result.peakProminence = std::max(0.0, combined[result.peakPhase] - center);
    result.robustZ = result.peakProminence / std::max(normalMadScale * median(deviations, hop), minimumMadScale);
    const double halfHeight = center + 0.5 * result.peakProminence;
    result.halfProminenceWidth = static_cast<std::size_t>(std::count_if(
        combined.begin(), combined.begin() + hop, [halfHeight](double value) { return value >= halfHeight; }));
    result.anchorPeakCoherence = std::clamp(std::hypot(real, imaginary) / count, 0.0, 1.0);
    for (std::size_t anchor = 0; anchor < count; ++anchor)
        if (phaseDistance(phases[anchor], result.peakPhase, hop) <= alignmentTolerance) ++result.alignedBlocks;
    return result;
}
} // namespace

struct CeltFramingProbe::Impl final {
    const std::size_t hop, size, excerptSize, lowBins, framesPerAnchor;
    int channels;
    std::uint64_t cursor = 0;
    std::array<std::uint64_t, anchorCount> starts{};
    std::size_t current = 0, measured = 0, refined = 0;
    std::array<std::vector<double>, anchorCount> deferredMono{};
    std::vector<double> captured;
    std::array<double, 1920> window{};
    std::array<AVComplexDouble, 1920> pre{};
    std::array<AVComplexDouble, 960> post{};
    std::array<Curve, anchorCount> fullCurves{}, lowCurves{};
    std::vector<AVComplexDouble> input, output;
    AVTXContext* context = nullptr;
    av_tx_fn transform = nullptr;

    Impl(int channelCount, std::uint64_t total, std::size_t frameSamples, std::size_t frameCount)
        : hop(frameSamples), size(2 * hop), excerptSize(size + frameCount * hop),
          lowBins(hop * 3 / 4), framesPerAnchor(frameCount), channels(channelCount), input(size), output(size)
    {
        for (std::size_t anchor = 0; anchor < anchorCount; ++anchor) {
            if (total > 13 * 48000) {
                // Spread long-file evidence across the recording instead of
                // inspecting only its introduction.
                starts[anchor] = total / 5 * (anchor + 1);
            } else if (total != 0 && total < anchorCount * excerptSize) {
                starts[anchor] = anchor * excerptSize;
            } else if (total != 0 && total < 8 * 48000 + excerptSize
                    && total >= anchorCount * excerptSize) {
                const auto first = std::min<std::uint64_t>(48000, total - anchorCount * excerptSize);
                starts[anchor] = first + (total - excerptSize - first) * anchor / (anchorCount - 1);
            } else {
                starts[anchor] = (anchor + 1) * 2 * 48000;
            }
        }
        if (framesPerAnchor == 12) captured.reserve(excerptSize * static_cast<std::size_t>(channels));
        const auto zero = (hop - overlap) / 2;
        for (std::size_t n = 0; n < overlap; ++n) {
            const double inner = std::sin(0.5 * pi * (n + 0.5) / overlap);
            const double value = std::sin(0.5 * pi * inner * inner);
            window[zero + n] = value;
            window[size - zero - n - 1] = value;
        }
        std::fill(window.begin() + zero + overlap, window.begin() + size - zero - overlap, 1.0);
        for (std::size_t n = 0; n < size; ++n)
            pre[n] = {std::cos(-pi * n / size), std::sin(-pi * n / size)};
        for (std::size_t k = 0; k < hop; ++k) {
            const double angle = -2.0 * pi / size * (0.5 + size / 4.0) * (k + 0.5);
            post[k] = {std::cos(angle), std::sin(angle)};
        }
        if (av_tx_init(&context, &transform, AV_TX_DOUBLE_FFT, 0,
                       static_cast<int>(size), nullptr, AV_TX_UNALIGNED) < 0) {
            av_tx_uninit(&context);
            throw std::runtime_error("CELT framing FFT initialization failed");
        }
    }
    ~Impl() { av_tx_uninit(&context); }

    void capture(const std::atomic_bool& cancel)
    {
        std::size_t selected = 0;
        double maximumPower = 0.0;
        for (int channel = 0; channel < channels; ++channel) {
            double power = 0.0;
            for (std::size_t n = 0; n < excerptSize; ++n) {
                const auto value = captured[n * channels + channel];
                if (!std::isfinite(value)) return;
                power += value * value;
            }
            if (!std::isfinite(power)) return;
            if (power > maximumPower) { maximumPower = power; selected = channel; }
        }
        if (maximumPower / excerptSize < minimumAnchorPower) return;
        std::vector<double> mono(excerptSize);
        // A block-wide scale prevents squared MDCT coefficients overflowing;
        // per-frame RMS normalization removes this scale from every metric.
        const double scale = std::sqrt(maximumPower / excerptSize);
        double previous = 0.0;
        for (std::size_t n = 0; n < excerptSize; ++n) {
            const double value = captured[n * channels + selected] / scale;
            mono[n] = value - preemphasis * previous;
            previous = value;
        }
        if (framesPerAnchor > 12) deferredMono[current] = std::move(mono);
        else measure(mono, current, cancel);
    }

    bool measure(const std::vector<double>& mono, std::size_t anchor,
                 const std::atomic_bool& cancel)
    {
        Curve full{}, low{};
        for (std::size_t shift = 0; shift < hop; ++shift) {
            if (cancel.load(std::memory_order_relaxed)) return false;
            std::array<double, maximumFramesPerAnchor> fullFrames{}, lowFrames{};
            for (std::size_t frame = 0; frame < framesPerAnchor; ++frame) {
                const auto start = shift + frame * hop;
                for (std::size_t n = 0; n < size; ++n) {
                    const double value = mono[start + n] * window[n];
                    input[n] = {value * pre[n].re, value * pre[n].im};
                }
                transform(context, output.data(), input.data(), sizeof(AVComplexDouble));
                std::array<double, 960> coefficients{};
                double fullPower = 0.0, lowPower = 0.0;
                for (std::size_t k = 0; k < hop; ++k) {
                    const double value = output[k].re * post[k].re - output[k].im * post[k].im;
                    if (!std::isfinite(value)) return false;
                    coefficients[k] = value;
                    fullPower += value * value;
                    if (k < lowBins) lowPower += value * value;
                }
                const double fullRms = std::max(std::sqrt(fullPower / hop), normalizationFloor);
                const double lowRms = std::max(std::sqrt(lowPower / lowBins), normalizationFloor);
                const double fullLogRms = std::log10(fullRms);
                const double lowLogRms = std::log10(lowRms);
                const double coefficientFloorDb = std::log10(normalizedCoefficientFloor);
                double fullDb = 0.0, lowDb = 0.0;
                for (std::size_t k = 0; k < hop; ++k) {
                    // Both bands share the same coefficient. Apply the original
                    // normalized floor in log space and compute its log once.
                    const double magnitude = std::abs(coefficients[k]);
                    const double logMagnitude = magnitude > 0.0
                        ? std::log10(magnitude) : -std::numeric_limits<double>::infinity();
                    fullDb -= 20.0 * std::max(logMagnitude - fullLogRms, coefficientFloorDb);
                    if (k < lowBins)
                        lowDb -= 20.0 * std::max(logMagnitude - lowLogRms, coefficientFloorDb);
                }
                fullFrames[frame] = fullDb / hop;
                lowFrames[frame] = lowDb / lowBins;
            }
            const auto phase = (starts[anchor] + shift) % hop;
            full[phase] = median(fullFrames, framesPerAnchor);
            low[phase] = median(lowFrames, framesPerAnchor);
        }
        // Publish only complete anchors; cancellation never contributes a
        // partly measured phase curve to later evidence.
        fullCurves[measured] = full;
        lowCurves[measured] = low;
        ++measured;
        return true;
    }
};

CeltFramingProbe::CeltFramingProbe(int rate, int channels, std::uint64_t total, std::size_t frameSamples, std::size_t framesPerAnchor)
{
    if (rate == 48000 && channels > 0 && channels <= 64
            && (framesPerAnchor == 12 || framesPerAnchor == 48
                || (frameSamples == 120 && (framesPerAnchor == 192 || framesPerAnchor == 768)))
            && (frameSamples == 120 || frameSamples == 240 || frameSamples == 480 || frameSamples == 960))
        impl_ = std::make_unique<Impl>(channels, total, frameSamples, framesPerAnchor);
}
CeltFramingProbe::~CeltFramingProbe() = default;

void CeltFramingProbe::consume(const double* samples, std::size_t frames,
                              const std::atomic_bool& cancel)
{
    if (!impl_ || !samples || cancel.load(std::memory_order_relaxed)) return;
    auto& p = *impl_;
    if (frames > std::numeric_limits<std::uint64_t>::max() - p.cursor) return;
    const auto end = p.cursor + frames;
    while (p.current < anchorCount && p.starts[p.current] < end) {
        const auto begin = std::max(p.cursor, p.starts[p.current]);
        const auto stop = std::min(end, p.starts[p.current] + p.excerptSize);
        if (stop > begin && p.captured.capacity() == 0)
            p.captured.reserve(p.excerptSize * static_cast<std::size_t>(p.channels));
        if (stop > begin)
            p.captured.insert(p.captured.end(), samples + (begin - p.cursor) * p.channels,
                              samples + (stop - p.cursor) * p.channels);
        if (p.captured.size() != p.excerptSize * static_cast<std::size_t>(p.channels)) break;
        p.capture(cancel);
        if (p.framesPerAnchor > 12) std::vector<double>().swap(p.captured);
        else p.captured.clear();
        ++p.current;
        // No later anchor can reuse this capture once all four are complete.
        // Release ordinary-mode capacity before other probes finish capturing.
        if (p.current == anchorCount) std::vector<double>().swap(p.captured);
        if (cancel.load(std::memory_order_relaxed)) break;
    }
    p.cursor = end;
}

void CeltFramingProbe::refine(const std::atomic_bool& cancel)
{
    if (!impl_ || impl_->framesPerAnchor == 12 || cancel.load(std::memory_order_relaxed)) return;
    auto& p = *impl_;
    while (p.refined < p.current) {
        auto& mono = p.deferredMono[p.refined];
        if (!mono.empty() && !p.measure(mono, p.refined, cancel)) return;
        std::vector<double>().swap(mono);
        ++p.refined;
        if (cancel.load(std::memory_order_relaxed)) return;
    }
}

CeltFrameEvidence CeltFramingProbe::result() const noexcept
{
    CeltFrameEvidence result;
    if (!impl_) return result;
    result.fullBand = summarize(impl_->fullCurves, impl_->measured, impl_->hop);
    result.lowBand = summarize(impl_->lowCurves, impl_->measured, impl_->hop);
    result.phaseAgreementSamples = phaseDistance(result.fullBand.peakPhase, result.lowBand.peakPhase, impl_->hop);
    return result;
}
} // namespace agplayer::lossless

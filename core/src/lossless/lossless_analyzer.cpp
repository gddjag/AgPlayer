#include "lossless_analyzer.hpp"

#include "sacd_reader.hpp"
#include "lossless_mdct.hpp"
#include "lossless_celt.hpp"
#include "lossless_resampled_mdct.hpp"
#include "lossless_mp3_hybrid.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

extern "C" {
#include <libavutil/tx.h>
}

namespace agplayer::lossless {
namespace {

constexpr std::array<std::size_t, 3> kStftSizes{4'096U, 8'192U, 16'384U};
constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kSpectrumFloorDb = -120.0;
constexpr double kActiveRms = 1.0e-5;
constexpr double kCutoffRelativeDb = -60.0;
constexpr std::size_t kMaxSpectrumBins = 2'048U;
// Measurement resolution is versioned and independent of plot dimensions.
constexpr std::size_t kAnalysisSpectrumBins = 256U;
constexpr std::size_t kMaxSpectrogramTimeBins = 256U;
constexpr std::size_t kMaxSpectrogramFrequencyBins = 256U;

// Conservative support thresholds. These are named/versioned engineering
// parameters awaiting corpus calibration; confidence is evidence strength,
// never a statistical probability.
struct AnalysisThresholds final {
    static constexpr std::uint64_t minimumActiveWindows = 3U;
    static constexpr double minimumActiveWindowRatio = 0.05;
    static constexpr double lowInformationConfidence = 20.0;
    static constexpr double stableCutoffRatio = 0.72;
    static constexpr double cutoffStabilityNeighborhoodHz = 500.0;
    static constexpr double stableCutoffMinimumConsistency = 0.70;
    static constexpr double spectralEdgeMinimumFrequencyHz = 8'000.0;
    static constexpr double spectralEdgeMaximumFrequencyHz = 30'000.0;
    static constexpr double spectralEdgeNyquistGuardHz = 2'500.0;
    static constexpr double spectralEdgeSpanHz = 1'200.0;
    static constexpr double spectralEdgeGuardHz = 200.0;
    static constexpr std::size_t spectralEdgeStrideBins = 4U;
    static constexpr double spectralEdgeMinimumPassbandRelativeDb = -105.0;
    static constexpr double spectralEdgeMinimumDepthDb = 24.0;
    static constexpr double sharpDigitalEdgeMinimumDepthDb = 45.0;
    static constexpr double spectralEdgeMinimumStability = 0.80;
    // Low-level edge observation only. Absolute powers below the UI's -120 dB
    // floor remain meaningful; do not use this feature to infer source history.
    static constexpr double lowLevelEdgeMinimumPassbandDb = -180.0;
    static constexpr double lowLevelEdgeMaximumPassbandDb = -100.0;
    static constexpr double lowLevelEdgeNumericalFloorPower = 1.0e-24;
    static constexpr double lowLevelEdgeMinimumDepthDb = 24.0;
    static constexpr double spectralEdgeCutoffToleranceHz = 2'500.0;
    static constexpr double codecHoleScore = 0.10;
    static constexpr double codecHoleNeighborFloorDb = -55.0;
    static constexpr double codecHoleMinimumDepthDb = 18.0;
    static constexpr double codecHoleMinimumFrequencyHz = 1'000.0;
    static constexpr double codecHoleMaximumNyquistRatio = 0.95;
    static constexpr std::size_t codecHoleNeighborhoodRadiusBins = 8U;
    static constexpr std::size_t codecHoleGuardBins = 1U;
    static constexpr std::uint64_t codecHoleMinimumObservations = 8U;
    static constexpr double codecHoleObservationRatio = 0.50;
    static constexpr double codecHoleTemporalQuantile = 0.90;
    static constexpr double codecHoleMinimumContentEntropy = 0.20;
    static constexpr double codecHoleMinimumContentFlatness = 0.02;
    static constexpr double pcmHighFrequencyStartRatio = 0.75;
    static constexpr double resamplingMirrorScore = 0.72;
    static constexpr int resamplingMinimumSampleRate = 88'200;
    static constexpr double resamplingFamily44BoundaryHz = 22'050.0;
    static constexpr double resamplingFamily48BoundaryHz = 24'000.0;
    static constexpr double resamplingComparisonSpanHz = 8'000.0;
    static constexpr std::size_t resamplingMinimumPairs = 8U;
    static constexpr double resamplingMirrorMinimumVariationDb = 1.5;
    static constexpr double resamplingMirrorMinimumEnergyRatio = 0.01;
    static constexpr double resamplingPhaseMinimumStrength = 0.02;
    static constexpr double resamplingPhaseMinimumCoherence = 0.995;
    static constexpr double resamplingPhaseMinimumResidualEntropy = 0.45;
    static constexpr double resamplingPhaseMinimumBandSuppressionDb = 35.0;
    // Engineering comparison bands relative to the candidate source rate:
    // passband below its 0.5*rate Nyquist; stopband well above it. Numerical
    // floors preserve original low-level powers rather than the display floor.
    static constexpr double resamplingPassbandLowerSourceRatio = 0.35;
    static constexpr double resamplingPassbandUpperSourceRatio = 0.45;
    static constexpr double resamplingStopbandLowerSourceRatio = 0.75;
    static constexpr double resamplingStopbandUpperSourceRatio = 0.99;
    static constexpr double resamplingMinimumPassbandPower = 1.0e-18;
    static constexpr double resamplingStopbandNumericalFloorPower = 1.0e-24;
    static constexpr double mdctMinimumCoherentPeakDb = 6.0;
    static constexpr double mdctMinimumPeakZ = 12.0;
    static constexpr std::uint64_t mdctMinimumBlocks = 3U;
    static constexpr double mdctHybridMinimumCoherentPeakDb = 2.0;
    static constexpr double mdctHybridMinimumPeakZ = 5.0;
    static constexpr std::uint64_t mdctHybridMinimumAlignedBlocks = 3U;
    static constexpr double celtMinimumBandZ = 8.0;
    static constexpr double celtMinimumAnchorCoherence = 0.90;
    static constexpr std::uint64_t celtMinimumBlocks = 3U;
    static constexpr std::uint64_t celtMaximumPeakWidth = 8U;
    static constexpr std::uint64_t celtMaximumBandPhaseDifference = 2U;
    static constexpr int bitExpansionMinimumUnusedBits = 6;
    static constexpr double bitExpansionMaximumLowBitUsage = 0.02;
    static constexpr double bitExpansionMinimumSpectralEntropy = 0.20;
    static constexpr double bitExpansionMinimumSpectralFlatness = 0.02;
    static constexpr double credibleMinimumBandwidthRatio = 0.95;
    static constexpr double dsdMinimumUltrasonicEnergyRatio = 0.02;
    static constexpr double dsdUltrasonicLowerHz = 20'000.0;
    static constexpr double dsdUltrasonicSplitHz = 40'000.0;
    static constexpr double dsdUltrasonicUpperHz = 80'000.0;
    static constexpr double dsdSlopeStabilityDeviationDb = 6.0;
    static constexpr double dsdMinimumSlopeDbPerOctave = 3.0;
    static constexpr double dsdMinimumSlopeStability = 0.55;
    static constexpr double dsdPcmBandwidthHz = 35'000.0;
    static constexpr double dsdMaximumPcmUltrasonicEnergyRatio = 0.002;
    static constexpr double dsdMinimumRawByteEntropy = 0.60;
    static constexpr double dsdMaximumPeriodTwoRepeatRatio = 0.35;
};

// Named evidence support levels shown on a 0-100 UI scale. They are ordinal
// uncalibrated engineering support levels, not probabilities or measured accuracy rates.
struct AnalysisSupport final {
    static constexpr int emptyContent = 10;
    static constexpr int pcmToDsd = 58;
    static constexpr int dsdInconclusive = 35;
    static constexpr int knownLossyCodec = 82;
    static constexpr int lossyUpsample = 78;
    static constexpr int lossyTranscode = 64;
    static constexpr int bitDepthExpansion = 68;
    static constexpr int credibleLossless = 64;
    static constexpr int cutoffInconclusive = 40;
    static constexpr int defaultInconclusive = 32;
};

bool supports_mdct_framing(std::string_view window, double peak_db, double peak_z,
                          std::uint64_t blocks, std::uint64_t aligned_blocks) noexcept
{
    if (window == "sine1152" || window == "mp3_hybrid_36") {
        return peak_db >= AnalysisThresholds::mdctHybridMinimumCoherentPeakDb
            && peak_z >= AnalysisThresholds::mdctHybridMinimumPeakZ
            && aligned_blocks >= AnalysisThresholds::mdctHybridMinimumAlignedBlocks;
    }
    return peak_db >= AnalysisThresholds::mdctMinimumCoherentPeakDb
        && peak_z >= AnalysisThresholds::mdctMinimumPeakZ
        && blocks >= AnalysisThresholds::mdctMinimumBlocks;
}

bool supports_celt_framing(const AnalysisMeasurements& m) noexcept
{
    return m.celtFrameBlocks >= AnalysisThresholds::celtMinimumBlocks
        && m.celtFrameMinimumBandZ >= AnalysisThresholds::celtMinimumBandZ
        && m.celtFrameMinimumAnchorCoherence >= AnalysisThresholds::celtMinimumAnchorCoherence
        && m.celtFrameMaximumPeakWidth <= AnalysisThresholds::celtMaximumPeakWidth
        && m.celtFrameBandPhaseDifference <= AnalysisThresholds::celtMaximumBandPhaseDifference;
}

double clamp01(const double value) noexcept
{
    return std::clamp(value, 0.0, 1.0);
}

double power_to_db(const double power) noexcept
{
    return power > 1.0e-12 ? 10.0 * std::log10(power) : kSpectrumFloorDb;
}

class FftPlan final
{
public:
    explicit FftPlan(const std::size_t size)
    {
        if (size > static_cast<std::size_t>((std::numeric_limits<int>::max)())
            || av_tx_init(&context_, &transform_, AV_TX_DOUBLE_FFT, 0,
                          static_cast<int>(size), nullptr,
                          AV_TX_UNALIGNED) < 0) {
            throw std::runtime_error("FFmpeg FFT initialization failed");
        }
    }

    ~FftPlan() { av_tx_uninit(&context_); }
    FftPlan(const FftPlan&) = delete;
    FftPlan& operator=(const FftPlan&) = delete;

    FftPlan(FftPlan&& other) noexcept
        : context_(std::exchange(other.context_, nullptr)),
          transform_(std::exchange(other.transform_, nullptr))
    {
    }

    FftPlan& operator=(FftPlan&& other) noexcept
    {
        if (this != &other) {
            av_tx_uninit(&context_);
            context_ = std::exchange(other.context_, nullptr);
            transform_ = std::exchange(other.transform_, nullptr);
        }
        return *this;
    }

    void execute(std::vector<AVComplexDouble>& output,
                 std::vector<AVComplexDouble>& input) noexcept
    {
        transform_(context_, output.data(), input.data(),
                   static_cast<std::ptrdiff_t>(sizeof(AVComplexDouble)));
    }

private:
    AVTXContext* context_ = nullptr;
    av_tx_fn transform_ = nullptr;
};

struct SpectrumFrame final {
    std::vector<double> power;
    double rms = 0.0;
    double cutoffHz = 0.0;
    double spectralEdgeFrequencyHz = 0.0;
    double spectralEdgeDepthDb = 0.0;
};

struct HannWindow final {
    explicit HannWindow(const std::size_t size)
        : window(size)
    {
        double window_sum = 0.0;
        for (std::size_t index = 0U; index < size; ++index) {
            window[index] = 0.5 - 0.5 * std::cos(
                2.0 * kPi * static_cast<double>(index)
                / static_cast<double>(size - 1U));
            window_sum += window[index];
        }
        amplitudeScale = window_sum > 0.0 ? 2.0 / window_sum : 0.0;
    }

    void release() noexcept
    {
        std::vector<double>().swap(window);
    }

    std::vector<double> window;
    double amplitudeScale = 0.0;
};

SpectrumFrame analyze_window(const double* samples,
                             const std::size_t size,
                             const int sample_rate,
                             const std::size_t channels,
                             FftPlan& fft_plan,
                             const HannWindow& cached)
{
    SpectrumFrame result;
    result.power.resize(size / 2U + 1U);
    std::vector<AVComplexDouble> input(size);
    std::vector<AVComplexDouble> transformed(size);
    const auto& window = cached.window;
    const double amplitude_scale = cached.amplitudeScale;
    std::size_t active_channels = 0U;
    // Combine powers, never signed samples. Phase inversion and silent channels
    // cannot erase another channel's spectrum. Every channel stays continuous.
    for (std::size_t channel = 0U; channel < channels; ++channel) {
        double square_sum = 0.0;
        for (std::size_t index = 0U; index < size; ++index) {
            const double value = samples[index * channels + channel];
            const double sample = std::isfinite(value) ? value : 0.0;
            input[index] = {sample * window[index], 0.0};
            square_sum += sample * sample;
        }
        const double rms = std::sqrt(square_sum / static_cast<double>(size));
        result.rms = std::max(result.rms, rms);
        if (rms < kActiveRms) continue;
        ++active_channels;
        fft_plan.execute(transformed, input);
        for (std::size_t bin = 0U; bin < result.power.size(); ++bin) {
            const double amplitude = std::hypot(transformed[bin].re, transformed[bin].im)
                * amplitude_scale;
            result.power[bin] += amplitude * amplitude;
        }
    }
    double maximum_power = 0.0;
    for (std::size_t bin = 0U; bin < result.power.size(); ++bin) {
        if (active_channels > 0U) result.power[bin] /= static_cast<double>(active_channels);
        maximum_power = std::max(maximum_power, result.power[bin]);
    }
    if (maximum_power <= 1.0e-12) return result;
    const double threshold = maximum_power * std::pow(10.0, kCutoffRelativeDb / 10.0);
    for (std::size_t bin = result.power.size(); bin-- > 1U;) {
        if (result.power[bin] >= threshold
            && power_to_db(result.power[bin]) > -100.0) {
            result.cutoffHz = static_cast<double>(bin)
                * static_cast<double>(sample_rate) / static_cast<double>(size);
            break;
        }
    }

    const double hz_per_bin = static_cast<double>(sample_rate)
        / static_cast<double>(size);
    const std::size_t span_bins = std::max<std::size_t>(
        2U, static_cast<std::size_t>(std::llround(
            AnalysisThresholds::spectralEdgeSpanHz / hz_per_bin)));
    const std::size_t guard_bins = std::clamp<std::size_t>(
        static_cast<std::size_t>(std::llround(
            AnalysisThresholds::spectralEdgeGuardHz / hz_per_bin)),
        1U, span_bins - 1U);
    const double maximum_edge_frequency = std::min(
        AnalysisThresholds::spectralEdgeMaximumFrequencyHz,
        static_cast<double>(sample_rate) / 2.0
            - AnalysisThresholds::spectralEdgeNyquistGuardHz);
    const std::size_t first_center = std::max(
        span_bins, static_cast<std::size_t>(std::ceil(
            AnalysisThresholds::spectralEdgeMinimumFrequencyHz / hz_per_bin)));
    const std::size_t last_center = std::min(
        result.power.size() > span_bins
            ? result.power.size() - span_bins - 1U : 0U,
        maximum_edge_frequency > 0.0
            ? static_cast<std::size_t>(std::floor(
                maximum_edge_frequency / hz_per_bin)) : 0U);
    if (first_center <= last_center) {
        std::vector<double> relative_db_prefix(result.power.size() + 1U, 0.0);
        for (std::size_t bin = 0U; bin < result.power.size(); ++bin) {
            const double relative_db = std::max(kSpectrumFloorDb,
                power_to_db(result.power[bin] / maximum_power));
            relative_db_prefix[bin + 1U] = relative_db_prefix[bin]
                + relative_db;
        }
        const double count = static_cast<double>(span_bins - guard_bins);
        for (std::size_t center = first_center; center <= last_center;
             center += AnalysisThresholds::spectralEdgeStrideBins) {
            const double lower_db =
                (relative_db_prefix[center - guard_bins]
                 - relative_db_prefix[center - span_bins]) / count;
            if (lower_db
                < AnalysisThresholds::spectralEdgeMinimumPassbandRelativeDb) {
                continue;
            }
            const double upper_db =
                (relative_db_prefix[center + span_bins]
                 - relative_db_prefix[center + guard_bins]) / count;
            const double depth_db = lower_db - upper_db;
            if (depth_db > result.spectralEdgeDepthDb) {
                result.spectralEdgeDepthDb = depth_db;
                result.spectralEdgeFrequencyHz = static_cast<double>(center)
                    * hz_per_bin;
            }
        }
    }
    return result;
}

// Engineering observation windows, not trained codec probabilities. Fixed 1 ms
// energy bins avoid FFT pre-ringing and channel cancellation. The 3 ms guard
// excludes the immediate attack; 30-50 ms estimates the local noise background.
// Quiet attacks (< -26 dBFS RMS) and slow attacks are deliberately not measured.
class TransientAccumulator final {
public:
    explicit TransientAccumulator(int sample_rate) : sample_rate_(sample_rate) {}
    void add(double mean_channel_power)
    {
        power_ += mean_channel_power;
        ++samples_;
        phase_ += 1'000;
        if (phase_ < sample_rate_) return;
        phase_ -= sample_rate_;
        const double energy = power_ / static_cast<double>(samples_);
        power_ = 0.0;
        samples_ = 0U;
        if (filled_ == history_.size() && cooldown_ == 0U) {
            const double recent = mean(1U, 2U);
            if (energy >= minimumAttackPower && energy > recent * minimumRisePowerRatio) {
                const double background = mean(30U, 50U);
                const double preceding = mean(3U, 20U);
                score_sum_ += clamp01((preceding - background) / energy);
                ++count_;
                cooldown_ = refractoryMilliseconds;
            }
        }
        history_[cursor_] = energy;
        cursor_ = (cursor_ + 1U) % history_.size();
        filled_ = std::min(filled_ + 1U, history_.size());
        if (cooldown_ > 0U) --cooldown_;
    }
    void finish(AnalysisMeasurements& result) const
    {
        result.transientCount = count_;
        result.transientPreEchoMeasured = count_ > 0U;
        result.transientPreEchoScore = count_ ? score_sum_ / static_cast<double>(count_) : 0.0;
    }
private:
    double mean(std::size_t first, std::size_t last) const
    {
        double sum = 0.0;
        for (auto lag = first; lag <= last; ++lag)
            sum += history_[(cursor_ + history_.size() - lag) % history_.size()];
        return sum / static_cast<double>(last - first + 1U);
    }
    static constexpr double minimumAttackPower = 0.0025;
    static constexpr double minimumRisePowerRatio = 16.0;
    static constexpr std::size_t refractoryMilliseconds = 50U;
    int sample_rate_;
    int phase_ = 0;
    std::size_t samples_ = 0U, cursor_ = 0U, filled_ = 0U, cooldown_ = 0U;
    std::uint64_t count_ = 0U;
    double power_ = 0.0, score_sum_ = 0.0;
    std::array<double, 50U> history_{};
};

// Cyclic energy in a high-order residual can expose the polyphase grid left by
// a resampler. Phase itself is deliberately unconstrained: arbitrary trimming
// rotates it. Only across-segment agreement is measured. This is one heuristic,
// not a mathematical proof of sampling provenance.
class ResamplingPhaseAccumulator final {
    struct Candidate {
        int source_rate = 0;
        std::vector<double> cosine, sine;
        double real = 0.0, imaginary = 0.0, energy = 0.0;
        double unit_real = 0.0, unit_imaginary = 0.0, strength = 0.0;
        std::uint64_t hits = 0U, segments = 0U;
    };
public:
    ResamplingPhaseAccumulator(int sample_rate, std::size_t channels, bool enabled)
        : channels_(channels), segment_frames_(static_cast<std::size_t>(sample_rate / 2))
    {
        if (!enabled || sample_rate < 88'200) return;
        history_.resize(channels * 8U, 0.0);
        for (const int source : {44'100, 48'000}) {
            if (source >= sample_rate) continue;
            const int divisor = std::gcd(source, sample_rate);
            const int period = sample_rate / divisor;
            if (period > 2'048) continue;
            Candidate candidate;
            candidate.source_rate = source;
            for (int index = 0; index < period; ++index) {
                const double angle = 2.0 * kPi * index * (source / divisor) / period;
                candidate.cosine.push_back(std::cos(angle));
                candidate.sine.push_back(std::sin(angle));
            }
            candidates_.push_back(std::move(candidate));
        }
    }
    void add(const double* samples)
    {
        if (candidates_.empty()) return;
        constexpr std::array<double, 9> coefficients{1, -8, 28, -56, 70, -56, 28, -8, 1};
        double energy = 0.0;
        for (std::size_t channel = 0; channel < channels_; ++channel) {
            double residual = samples[channel];
            for (std::size_t lag = 1; lag <= 8U; ++lag)
                residual += coefficients[lag] * history_[channel * 8U + (cursor_ + 8U - lag) % 8U];
            history_[channel * 8U + cursor_] = samples[channel];
            energy += residual * residual;
        }
        cursor_ = (cursor_ + 1U) % 8U;
        ++frames_;
        if (frames_ <= 8U) return;
        energy /= static_cast<double>(channels_);
        for (auto& candidate : candidates_) {
            const auto phase = static_cast<std::size_t>((frames_ - 1U) % candidate.cosine.size());
            candidate.real += energy * candidate.cosine[phase];
            candidate.imaginary += energy * candidate.sine[phase];
            candidate.energy += energy;
        }
        if (++segment_count_ < segment_frames_) return;
        for (auto& candidate : candidates_) {
            // The first half-second can contain file-boundary/filter startup
            // transients. It is not an independent stationary phase segment.
            if (!settled_) {
                candidate.real = candidate.imaginary = candidate.energy = 0.0;
                continue;
            }
            ++candidate.segments;
            const double magnitude = std::hypot(candidate.real, candidate.imaginary);
            const double strength = candidate.energy > 0.0 ? magnitude / candidate.energy : 0.0;
            if (candidate.energy / segment_count_ >= minimumResidualPower
                && strength >= minimumSegmentStrength) {
                candidate.unit_real += candidate.real / magnitude;
                candidate.unit_imaginary += candidate.imaginary / magnitude;
                candidate.strength += strength;
                ++candidate.hits;
            }
            candidate.real = candidate.imaginary = candidate.energy = 0.0;
        }
        segment_count_ = 0U;
        settled_ = true;
    }
    void finish(AnalysisMeasurements& measurements) const
    {
        double best = 0.0;
        for (const auto& candidate : candidates_) {
            if (candidate.hits < minimumSegments || candidate.hits * 2U < candidate.segments) continue;
            const double coherence = std::hypot(candidate.unit_real, candidate.unit_imaginary) / candidate.hits;
            const double strength = candidate.strength / candidate.hits;
            if (strength * coherence <= best) continue;
            best = strength * coherence;
            measurements.resamplingPhaseSourceRate = candidate.source_rate;
            measurements.resamplingPhaseStrength = strength;
            measurements.resamplingPhaseCoherence = coherence;
            measurements.resamplingPhaseSegments = candidate.hits;
        }
    }
private:
    // Named engineering gates; independent negative controls are mandatory.
    static constexpr double minimumResidualPower = 1.0e-18;
    static constexpr double minimumSegmentStrength = 0.005;
    static constexpr std::uint64_t minimumSegments = 4U;
    std::size_t channels_, segment_frames_, segment_count_ = 0U, cursor_ = 0U;
    std::uint64_t frames_ = 0U;
    std::vector<double> history_;
    std::vector<Candidate> candidates_;
    bool settled_ = false;
};

class StftAccumulator final {
public:
    StftAccumulator(const std::size_t size,
                    const int sample_rate,
                    const std::size_t channels,
                    const std::size_t spectrum_bins,
                    const bool capture_spectrogram,
                    const std::size_t spectrogram_time_bins,
                    const std::size_t spectrogram_frequency_bins,
                    const std::int64_t duration_ms)
        : size_(size), hop_(size / 2U), sample_rate_(sample_rate), channels_(channels),
          fft_plan_(size), hann_window_(size),
          spectrum_power_(spectrum_bins, 0.0),
          raw_spectrum_power_(size == kStftSizes.back() ? size / 2U + 1U : 0U, 0.0),
          cutoff_histogram_(spectrum_bins, 0U),
          edge_frequency_histogram_(spectrum_bins, 0U),
          hole_hits_(size == kStftSizes.back() ? size / 2U + 1U : 0U, 0U),
          hole_observations_(hole_hits_.size(), 0U),
          power_prefix_(hole_hits_.empty() ? 0U : size / 2U + 2U, 0.0),
          capture_spectrogram_(capture_spectrogram),
          spectrogram_time_bins_(spectrogram_time_bins),
          spectrogram_frequency_bins_(spectrogram_frequency_bins)
    {
        buffer_.reserve(size * channels_);
        if (capture_spectrogram_ && spectrogram_time_bins_ > 0U
            && spectrogram_frequency_bins_ > 0U && duration_ms > 0) {
            const long double expected_samples =
                static_cast<long double>(duration_ms)
                * static_cast<long double>(sample_rate_) / 1'000.0L;
            const auto expected_windows = static_cast<std::uint64_t>(
                std::max<long double>(1.0L,
                    (expected_samples - static_cast<long double>(size_))
                    / static_cast<long double>(hop_) + 1.0L));
            spectrogram_stride_ = static_cast<std::size_t>(
                std::max<std::uint64_t>(1U,
                    (expected_windows + spectrogram_time_bins_ - 1U)
                    / spectrogram_time_bins_));
        }
    }

    bool add(const std::vector<double>& samples, const std::atomic_bool& cancelled)
    {
        std::size_t offset = 0U;
        while (offset < samples.size()) {
            if (cancelled.load(std::memory_order_relaxed)) return false;
            const std::size_t take = std::min(samples.size() - offset,
                                             size_ * channels_ - buffer_.size());
            buffer_.insert(buffer_.end(), samples.begin() + static_cast<std::ptrdiff_t>(offset),
                           samples.begin() + static_cast<std::ptrdiff_t>(offset + take));
            offset += take;
            if (buffer_.size() == size_ * channels_) {
                consume(buffer_.data());
                buffer_.erase(buffer_.begin(), buffer_.begin()
                              + static_cast<std::ptrdiff_t>(hop_ * channels_));
            }
        }
        return true;
    }

    bool finish(const std::atomic_bool& cancelled)
    {
        if (window_count_ == 0U && buffer_.size() >= 512U * channels_) {
            if (cancelled.load(std::memory_order_relaxed)) return false;
            buffer_.resize(size_ * channels_, 0.0);
            consume(buffer_.data());
        }
        buffer_.clear();
        buffer_.shrink_to_fit();
        // The Hann window is no longer needed during the deferred probes.
        hann_window_.release();
        return true;
    }

    std::uint64_t window_count() const noexcept { return window_count_; }
    std::uint64_t active_count() const noexcept { return active_count_; }

    void low_level_edge(AnalysisMeasurements& measurements) const
    {
        if (raw_spectrum_power_.empty()
            || active_count_ < AnalysisThresholds::minimumActiveWindows) return;
        double residual_sum = 0.0, weighted_log_sum = 0.0;
        for (std::size_t index = 1U; index < raw_spectrum_power_.size(); ++index) {
            const double weight = raw_spectrum_power_[index]
                * std::pow(2.0 * std::sin(kPi * index / size_), 16.0);
            residual_sum += weight;
            if (weight > 0.0) weighted_log_sum += weight * std::log(weight);
        }
        if (residual_sum > 0.0) {
            measurements.resamplingResidualEntropyMeasured = true;
            measurements.resamplingResidualEntropy = clamp01(
                (std::log(residual_sum) - weighted_log_sum / residual_sum)
                / std::log(static_cast<double>(raw_spectrum_power_.size() - 1U)));
        }
        std::vector<double> prefix(raw_spectrum_power_.size() + 1U, 0.0);
        for (std::size_t index = 0U; index < raw_spectrum_power_.size(); ++index) {
            const double average = raw_spectrum_power_[index] / static_cast<double>(active_count_);
            prefix[index + 1U] = prefix[index] + 10.0 * std::log10(std::max(
                AnalysisThresholds::lowLevelEdgeNumericalFloorPower, average));
        }
        const double hz_per_bin = static_cast<double>(sample_rate_) / size_;
        const auto span = std::max<std::size_t>(2U, static_cast<std::size_t>(
            std::llround(AnalysisThresholds::spectralEdgeSpanHz / hz_per_bin)));
        const auto guard = std::clamp<std::size_t>(static_cast<std::size_t>(
            std::llround(AnalysisThresholds::spectralEdgeGuardHz / hz_per_bin)), 1U, span - 1U);
        double maximum_depth = AnalysisThresholds::lowLevelEdgeMinimumDepthDb;
        for (std::size_t center = span; center + span < raw_spectrum_power_.size();
             center += AnalysisThresholds::spectralEdgeStrideBins) {
            const double frequency = center * hz_per_bin;
            if (frequency < AnalysisThresholds::spectralEdgeMinimumFrequencyHz
                || frequency > AnalysisThresholds::spectralEdgeMaximumFrequencyHz) continue;
            const double lower = (prefix[center - guard] - prefix[center - span]) / (span - guard);
            const double upper = (prefix[center + span] - prefix[center + guard]) / (span - guard);
            const double depth = lower - upper;
            if (lower < AnalysisThresholds::lowLevelEdgeMinimumPassbandDb
                || lower > AnalysisThresholds::lowLevelEdgeMaximumPassbandDb
                || depth <= maximum_depth) continue;
            maximum_depth = depth;
            measurements.lowLevelSpectralEdgeMeasured = true;
            measurements.lowLevelSpectralEdgeHz = frequency;
            measurements.lowLevelSpectralEdgeDepthDb = depth;
        }
    }

    void resampling_band_suppression(AnalysisMeasurements& measurements) const
    {
        const double source = measurements.resamplingPhaseSourceRate;
        if (source <= 0.0 || active_count_ == 0U) return;
        double pass = 0.0, stop = 0.0;
        std::size_t pass_count = 0U, stop_count = 0U;
        for (std::size_t index = 1U; index < raw_spectrum_power_.size(); ++index) {
            const double frequency = static_cast<double>(index) * sample_rate_ / size_;
            if (frequency >= source * AnalysisThresholds::resamplingPassbandLowerSourceRatio
                && frequency < source * AnalysisThresholds::resamplingPassbandUpperSourceRatio) {
                pass += raw_spectrum_power_[index]; ++pass_count;
            }
            if (frequency >= source * AnalysisThresholds::resamplingStopbandLowerSourceRatio
                && frequency < source * AnalysisThresholds::resamplingStopbandUpperSourceRatio) {
                stop += raw_spectrum_power_[index]; ++stop_count;
            }
        }
        if (pass_count == 0U || stop_count == 0U) return;
        pass /= static_cast<double>(pass_count) * active_count_;
        stop /= static_cast<double>(stop_count) * active_count_;
        if (pass < AnalysisThresholds::resamplingMinimumPassbandPower) return;
        measurements.resamplingBandSuppressionMeasured = true;
        measurements.resamplingBandSuppressionDb = std::max(0.0,
            10.0 * std::log10(pass / std::max(stop,
                AnalysisThresholds::resamplingStopbandNumericalFloorPower)));
    }

    std::vector<double> spectrum_db() const
    {
        std::vector<double> result(spectrum_power_.size(), kSpectrumFloorDb);
        if (active_count_ == 0U) return result;
        for (std::size_t index = 0U; index < result.size(); ++index) {
            result[index] = std::max(kSpectrumFloorDb,
                power_to_db(spectrum_power_[index]
                            / static_cast<double>(active_count_)));
        }
        return result;
    }

    double cutoff_median() const noexcept
    {
        const std::uint64_t total = std::accumulate(
            cutoff_histogram_.begin(), cutoff_histogram_.end(),
            std::uint64_t{0});
        if (total == 0U) return 0.0;
        const std::uint64_t target = (total + 1U) / 2U;
        std::uint64_t sum = 0U;
        for (std::size_t index = 0U; index < cutoff_histogram_.size(); ++index) {
            sum += cutoff_histogram_[index];
            if (sum >= target) {
                return static_cast<double>(index)
                    * (static_cast<double>(sample_rate_) / 2.0)
                    / static_cast<double>(cutoff_histogram_.size() - 1U);
            }
        }
        return static_cast<double>(sample_rate_) / 2.0;
    }

    double cutoff_stability() const noexcept
    {
        const std::uint64_t total = std::accumulate(
            cutoff_histogram_.begin(), cutoff_histogram_.end(),
            std::uint64_t{0});
        if (total == 0U) return 0.0;
        return frequency_cluster_ratio(cutoff_histogram_, total,
            AnalysisThresholds::cutoffStabilityNeighborhoodHz);
    }

    double spectral_edge_frequency_median() const noexcept
    {
        const std::uint64_t total = std::accumulate(
            edge_frequency_histogram_.begin(), edge_frequency_histogram_.end(),
            std::uint64_t{0});
        if (total == 0U) return 0.0;
        const std::uint64_t target = (total + 1U) / 2U;
        std::uint64_t sum = 0U;
        for (std::size_t index = 0U;
             index < edge_frequency_histogram_.size(); ++index) {
            sum += edge_frequency_histogram_[index];
            if (sum >= target) {
                return static_cast<double>(index)
                    * (static_cast<double>(sample_rate_) / 2.0)
                    / static_cast<double>(edge_frequency_histogram_.size() - 1U);
            }
        }
        return 0.0;
    }

    double spectral_edge_depth_lower_decile() const noexcept
    {
        const std::uint64_t total = std::accumulate(
            edge_depth_histogram_.begin(), edge_depth_histogram_.end(),
            std::uint64_t{0});
        if (total == 0U) return 0.0;
        const std::uint64_t target = std::max<std::uint64_t>(1U,
            static_cast<std::uint64_t>(std::ceil(
                static_cast<double>(total) * 0.10)));
        std::uint64_t sum = 0U;
        for (std::size_t index = 0U; index < edge_depth_histogram_.size();
             ++index) {
            sum += edge_depth_histogram_[index];
            if (sum >= target) return static_cast<double>(index);
        }
        return static_cast<double>(edge_depth_histogram_.size() - 1U);
    }

    double spectral_edge_stability() const noexcept
    {
        const std::uint64_t total = std::accumulate(
            edge_frequency_histogram_.begin(), edge_frequency_histogram_.end(),
            std::uint64_t{0});
        if (total == 0U) return 0.0;
        return frequency_cluster_ratio(edge_frequency_histogram_, total,
            AnalysisThresholds::cutoffStabilityNeighborhoodHz);
    }

    double temporal_codec_hole_score() const
    {
        const std::uint64_t minimum_observations = std::max(
            AnalysisThresholds::codecHoleMinimumObservations,
            static_cast<std::uint64_t>(std::ceil(
                static_cast<double>(active_count_)
                * AnalysisThresholds::codecHoleObservationRatio)));
        std::vector<double> rates;
        rates.reserve(hole_hits_.size());
        for (std::size_t index = 0U; index < hole_hits_.size(); ++index) {
            if (hole_observations_[index] < minimum_observations) continue;
            rates.push_back(static_cast<double>(hole_hits_[index])
                            / static_cast<double>(hole_observations_[index]));
        }
        if (rates.empty()) return 0.0;
        std::sort(rates.begin(), rates.end());
        const auto quantile_index = static_cast<std::size_t>(std::floor(
            AnalysisThresholds::codecHoleTemporalQuantile
            * static_cast<double>(rates.size() - 1U)));
        return clamp01(rates[quantile_index]);
    }

    SpectrogramSummary spectrogram() const
    {
        SpectrogramSummary result;
        result.frequencyBins = spectrogram_frequency_bins_;
        result.nyquistHz = static_cast<double>(sample_rate_) / 2.0;
        std::vector<std::pair<std::uint64_t, std::vector<float>>> ordered =
            spectrogram_rows_;
        std::sort(ordered.begin(), ordered.end(),
                  [](const auto& left, const auto& right) {
                      return left.first < right.first;
                  });
        result.timeBins = ordered.size();
        result.db.reserve(result.timeBins * result.frequencyBins);
        for (const auto& item : ordered) {
            result.db.insert(result.db.end(), item.second.begin(), item.second.end());
        }
        return result;
    }

private:
    double frequency_cluster_ratio(
        const std::vector<std::uint64_t>& histogram,
        const std::uint64_t total,
        const double radius_hz) const noexcept
    {
        if (histogram.size() < 2U || total == 0U) return 0.0;
        const double bin_width_hz = (static_cast<double>(sample_rate_) / 2.0)
            / static_cast<double>(histogram.size() - 1U);
        const std::size_t radius_bins = std::max<std::size_t>(1U,
            static_cast<std::size_t>(std::ceil(radius_hz / bin_width_hz)));
        std::uint64_t cluster = 0U;
        std::uint64_t maximum = 0U;
        std::size_t left = 0U;
        std::size_t right = 0U;
        for (; right < histogram.size(); ++right) {
            cluster += histogram[right];
            while (right - left > radius_bins * 2U) {
                cluster -= histogram[left++];
            }
            maximum = std::max(maximum, cluster);
        }
        return static_cast<double>(maximum) / static_cast<double>(total);
    }

    void consume(const double* samples)
    {
        const SpectrumFrame frame = analyze_window(samples, size_, sample_rate_, channels_,
                                                   fft_plan_, hann_window_);
        ++window_count_;
        accumulate_spectrogram(frame);
        if (frame.rms < kActiveRms) return;
        ++active_count_;
        for (std::size_t index = 0U; index < raw_spectrum_power_.size(); ++index)
            raw_spectrum_power_[index] += frame.power[index];
        const double nyquist = static_cast<double>(sample_rate_) / 2.0;
        if (!hole_hits_.empty()) {
            const double maximum_power = *std::max_element(
                frame.power.begin() + 1U, frame.power.end());
            if (maximum_power > 1.0e-12) {
                power_prefix_[0] = 0.0;
                for (std::size_t index = 0U; index < frame.power.size();
                     ++index) {
                    power_prefix_[index + 1U] = power_prefix_[index]
                        + frame.power[index];
                }
                constexpr std::size_t radius =
                    AnalysisThresholds::codecHoleNeighborhoodRadiusBins;
                constexpr std::size_t guard =
                    AnalysisThresholds::codecHoleGuardBins;
                const double frame_max_db = power_to_db(maximum_power);
                const double maximum_frequency = nyquist
                    * AnalysisThresholds::codecHoleMaximumNyquistRatio;
                for (std::size_t index = radius;
                     index + radius < frame.power.size(); ++index) {
                    const double frequency = static_cast<double>(index)
                        * static_cast<double>(sample_rate_)
                        / static_cast<double>(size_);
                    if (frequency
                            < AnalysisThresholds::codecHoleMinimumFrequencyHz
                        || frequency > maximum_frequency) {
                        continue;
                    }
                    const std::size_t left_end = index - guard;
                    const std::size_t right_begin = index + guard + 1U;
                    const double neighbor_sum =
                        power_prefix_[left_end] - power_prefix_[index - radius]
                        + power_prefix_[index + radius + 1U]
                        - power_prefix_[right_begin];
                    const auto neighbor_count = static_cast<double>(
                        2U * (radius - guard));
                    const double neighbor_db = power_to_db(
                        neighbor_sum / neighbor_count);
                    if (neighbor_db < frame_max_db
                            + AnalysisThresholds::codecHoleNeighborFloorDb) {
                        continue;
                    }
                    ++hole_observations_[index];
                    if (power_to_db(frame.power[index])
                        < neighbor_db
                            - AnalysisThresholds::codecHoleMinimumDepthDb) {
                        ++hole_hits_[index];
                    }
                }
            }
        }
        for (std::size_t index = 0U; index < spectrum_power_.size(); ++index) {
            const double source_scale = static_cast<double>(frame.power.size() - 1U)
                / static_cast<double>(spectrum_power_.size() - 1U);
            const auto source_begin = static_cast<std::size_t>(std::max(
                0.0, std::floor((static_cast<double>(index) - 0.5)
                                * source_scale)));
            const auto source_end = std::min(
                frame.power.size() - 1U,
                static_cast<std::size_t>(std::ceil(
                    (static_cast<double>(index) + 0.5) * source_scale)));
            double band_peak = 0.0;
            for (std::size_t source_bin = source_begin;
                 source_bin <= source_end; ++source_bin) {
                band_peak = std::max(band_peak, frame.power[source_bin]);
            }
            spectrum_power_[index] += band_peak;
        }
        if (frame.cutoffHz > 0.0) {
            const std::size_t bin = std::min(
                cutoff_histogram_.size() - 1U,
                static_cast<std::size_t>(std::llround(
                    frame.cutoffHz / nyquist
                    * static_cast<double>(cutoff_histogram_.size() - 1U))));
            ++cutoff_histogram_[bin];
        }
        if (frame.spectralEdgeFrequencyHz > 0.0) {
            const std::size_t frequency_bin = std::min(
                edge_frequency_histogram_.size() - 1U,
                static_cast<std::size_t>(std::llround(
                    frame.spectralEdgeFrequencyHz / nyquist
                    * static_cast<double>(edge_frequency_histogram_.size() - 1U))));
            ++edge_frequency_histogram_[frequency_bin];
            const std::size_t depth_bin = std::min(
                edge_depth_histogram_.size() - 1U,
                static_cast<std::size_t>(std::max(0.0,
                    std::floor(frame.spectralEdgeDepthDb))));
            ++edge_depth_histogram_[depth_bin];
        }
    }

    void accumulate_spectrogram(const SpectrumFrame& frame)
    {
        if (!capture_spectrogram_ || spectrogram_time_bins_ == 0U
            || spectrogram_frequency_bins_ == 0U || frame.power.empty()) {
            return;
        }
        const std::size_t time_bin = static_cast<std::size_t>(
            (window_count_ - 1U) / spectrogram_stride_);
        if (time_bin >= spectrogram_time_bins_) return;

        while (spectrogram_rows_.size() <= time_bin) {
            const std::uint64_t first_window =
                static_cast<std::uint64_t>(spectrogram_rows_.size())
                * static_cast<std::uint64_t>(spectrogram_stride_);
            spectrogram_rows_.emplace_back(
                first_window,
                std::vector<float>(spectrogram_frequency_bins_,
                                   static_cast<float>(kSpectrumFloorDb)));
        }
        if (frame.rms < kActiveRms) return;

        std::vector<float>& row = spectrogram_rows_[time_bin].second;
        for (std::size_t index = 0U; index < row.size(); ++index) {
            const std::size_t source_begin =
                index * frame.power.size() / row.size();
            const std::size_t source_end = std::max(
                source_begin + 1U,
                (index + 1U) * frame.power.size() / row.size());
            double band_peak = 0.0;
            for (std::size_t source_bin = source_begin;
                 source_bin < source_end; ++source_bin) {
                band_peak = std::max(band_peak, frame.power[source_bin]);
            }
            row[index] = std::max(row[index], static_cast<float>(std::max(
                kSpectrumFloorDb, power_to_db(band_peak))));
        }
    }

    std::size_t size_ = 0U;
    std::size_t hop_ = 0U;
    int sample_rate_ = 0;
    std::size_t channels_ = 1U;
    FftPlan fft_plan_;
    HannWindow hann_window_;
    std::vector<double> buffer_;
    std::vector<double> spectrum_power_;
    std::vector<double> raw_spectrum_power_;
    std::vector<std::uint64_t> cutoff_histogram_;
    std::vector<std::uint64_t> edge_frequency_histogram_;
    std::array<std::uint64_t, 121U> edge_depth_histogram_{};
    std::vector<std::uint64_t> hole_hits_;
    std::vector<std::uint64_t> hole_observations_;
    std::vector<double> power_prefix_;
    std::uint64_t window_count_ = 0U;
    std::uint64_t active_count_ = 0U;
    bool capture_spectrogram_ = false;
    std::size_t spectrogram_time_bins_ = 0U;
    std::size_t spectrogram_frequency_bins_ = 0U;
    std::size_t spectrogram_stride_ = 1U;
    std::vector<std::pair<std::uint64_t, std::vector<float>>> spectrogram_rows_;
};

struct InterruptContext final {
    const std::atomic_bool* cancelled = nullptr;
    DecoderInterruptCallback upstream = nullptr;
    void* upstream_context = nullptr;
};

struct PacketEvidenceContext final {
    DecoderPacketCallback upstream = nullptr;
    void* upstream_context = nullptr;
    bool collect_raw_dsd = false;
    std::array<std::uint64_t, 256> byte_histogram{};
    std::uint64_t bytes = 0U;
    std::uint64_t one_bits = 0U;
    std::uint64_t period_two_repeats = 0U;
    std::array<std::uint8_t, 2> previous{};
};

bool decoder_interrupted(void* context) noexcept
{
    const auto& state = *static_cast<InterruptContext*>(context);
    return (state.cancelled != nullptr
            && state.cancelled->load(std::memory_order_relaxed))
        || (state.upstream != nullptr && state.upstream(state.upstream_context));
}

unsigned int bit_count(std::uint8_t value) noexcept
{
    unsigned int count = 0U;
    while (value != 0U) {
        count += value & 1U;
        value >>= 1U;
    }
    return count;
}

void observe_packet_bytes(void* context,
                          const std::uint8_t* bytes,
                          const int size) noexcept
{
    auto& evidence = *static_cast<PacketEvidenceContext*>(context);
    if (evidence.upstream != nullptr) {
        evidence.upstream(evidence.upstream_context, bytes, size);
    }
    if (!evidence.collect_raw_dsd || bytes == nullptr || size <= 0) return;
    for (int index = 0; index < size; ++index) {
        const std::uint8_t value = bytes[index];
        ++evidence.byte_histogram[value];
        evidence.one_bits += bit_count(value);
        if (evidence.bytes >= 2U
            && value == evidence.previous[evidence.bytes % 2U]) {
            ++evidence.period_two_repeats;
        }
        evidence.previous[evidence.bytes % 2U] = value;
        ++evidence.bytes;
    }
}

void report_progress(const ProgressCallback& callback, const float value)
{
    if (!callback) return;
    try {
        callback(std::clamp(value, 0.0F, 1.0F));
    } catch (...) {
        // Progress observers do not control analysis success.
    }
}

bool is_lossy_codec(const std::string_view codec) noexcept
{
    constexpr std::array<std::string_view, 10> codecs{
        "mp3", "aac", "opus", "vorbis", "ac3", "eac3", "wmav1", "wmav2",
        "atrac3", "mp2",
    };
    return std::find(codecs.begin(), codecs.end(), codec) != codecs.end();
}

double percentile_from_histogram(const std::array<std::uint64_t, 121>& histogram,
                                 const double percentile) noexcept
{
    const std::uint64_t total = std::accumulate(histogram.begin(), histogram.end(),
                                                std::uint64_t{0});
    if (total == 0U) return -120.0;
    const auto target = static_cast<std::uint64_t>(std::ceil(
        percentile * static_cast<double>(total)));
    std::uint64_t sum = 0U;
    for (std::size_t index = 0U; index < histogram.size(); ++index) {
        sum += histogram[index];
        if (sum >= target) return static_cast<double>(index) - 120.0;
    }
    return 0.0;
}

void derive_spectrum_measurements(AnalysisResult& result)
{
    const auto& db = result.spectrum.db;
    if (db.size() < 4U) return;
    std::vector<double> power(db.size(), 0.0);
    double power_sum = 0.0;
    double log_sum = 0.0;
    for (std::size_t index = 1U; index < db.size(); ++index) {
        power[index] = std::pow(10.0, db[index] / 10.0);
        power_sum += power[index];
        // Geometric and arithmetic means must describe the SAME bin set.
        // Excluding only geometric floor bins makes a tone appear broadband.
        log_sum += std::log(std::max(power[index], 1.0e-12));
    }
    if (power_sum <= 0.0) return;
    double entropy = 0.0;
    for (std::size_t index = 1U; index < power.size(); ++index) {
        const double probability = power[index] / power_sum;
        if (probability > 0.0) entropy -= probability * std::log(probability);
    }
    result.measurements.spectralEntropy = entropy
        / std::log(static_cast<double>(power.size() - 1U));
    if (db.size() > 1U) {
        const double geometric = std::exp(log_sum / static_cast<double>(db.size() - 1U));
        const double arithmetic = power_sum / static_cast<double>(db.size() - 1U);
        result.measurements.spectralFlatness = clamp01(geometric / arithmetic);
    }

    std::size_t dominant = 1U;
    for (std::size_t index = 2U; index + 2U < db.size(); ++index) {
        if (db[index] > db[dominant]) dominant = index;
    }
    result.measurements.dominantFrequencyHz = static_cast<double>(dominant)
        * result.spectrum.nyquistHz / static_cast<double>(db.size() - 1U);

    double high_power = 0.0;
    const double high_start = result.source.kind == SourceKind::Dsd
        || result.source.kind == SourceKind::Dst
        ? AnalysisThresholds::dsdUltrasonicSplitHz
        : result.spectrum.nyquistHz
            * AnalysisThresholds::pcmHighFrequencyStartRatio;
    for (std::size_t index = 1U; index < power.size(); ++index) {
        const double frequency = static_cast<double>(index)
            * result.spectrum.nyquistHz / static_cast<double>(power.size() - 1U);
        if (frequency >= high_start) high_power += power[index];
    }
    result.measurements.highFrequencyEnergyRatio = high_power / power_sum;

    if (result.source.sampleRate
        >= AnalysisThresholds::resamplingMinimumSampleRate) {
        double lower_sum = 0.0;
        double upper_sum = 0.0;
        double lower_square_sum = 0.0;
        double upper_square_sum = 0.0;
        double product_sum = 0.0;
        double lower_power = 0.0;
        double upper_power = 0.0;
        std::size_t pair_count = 0U;
        const double boundary = result.source.sampleRate % 44'100 == 0
            ? AnalysisThresholds::resamplingFamily44BoundaryHz
            : AnalysisThresholds::resamplingFamily48BoundaryHz;
        const double span = std::min(
            AnalysisThresholds::resamplingComparisonSpanHz,
            result.spectrum.nyquistHz - boundary);
        for (std::size_t index = 1U; index < power.size(); ++index) {
            const double frequency = static_cast<double>(index)
                * result.spectrum.nyquistHz / static_cast<double>(power.size() - 1U);
            if (frequency < boundary - span || frequency >= boundary) continue;
            const double mirror_frequency = 2.0 * boundary - frequency;
            const std::size_t mirror = std::min(
                power.size() - 1U,
                static_cast<std::size_t>(std::llround(
                    mirror_frequency / result.spectrum.nyquistHz
                    * static_cast<double>(power.size() - 1U))));
            const double lower_db = db[index];
            const double upper_db = db[mirror];
            lower_sum += lower_db;
            upper_sum += upper_db;
            lower_square_sum += lower_db * lower_db;
            upper_square_sum += upper_db * upper_db;
            product_sum += lower_db * upper_db;
            lower_power += power[index];
            upper_power += power[mirror];
            ++pair_count;
        }
        if (pair_count >= AnalysisThresholds::resamplingMinimumPairs
            && lower_power > 1.0e-18) {
            const double count = static_cast<double>(pair_count);
            const double lower_variance = lower_square_sum
                - lower_sum * lower_sum / count;
            const double upper_variance = upper_square_sum
                - upper_sum * upper_sum / count;
            const double lower_deviation = std::sqrt(
                std::max(0.0, lower_variance / count));
            const double upper_deviation = std::sqrt(
                std::max(0.0, upper_variance / count));
            const double energy_ratio = upper_power / lower_power;
            if (lower_deviation
                    >= AnalysisThresholds::resamplingMirrorMinimumVariationDb
                && upper_deviation
                    >= AnalysisThresholds::resamplingMirrorMinimumVariationDb
                && energy_ratio
                    >= AnalysisThresholds::resamplingMirrorMinimumEnergyRatio
                && lower_variance > 1.0e-18 && upper_variance > 1.0e-18) {
                const double covariance = product_sum
                    - lower_sum * upper_sum / count;
                result.measurements.resamplingMirrorScore = clamp01(
                    covariance / std::sqrt(lower_variance * upper_variance));
            }
        }
    }
}

void add_evidence(AnalysisResult& result,
                  std::string code,
                  const EvidenceFamily family,
                  const EvidenceDirection direction,
                  const double value,
                  std::string unit,
                  std::string reference,
                  const int severity,
                  std::string explanation)
{
    Evidence evidence;
    evidence.code = std::move(code);
    evidence.family = family;
    evidence.direction = direction;
    evidence.value = value;
    evidence.unit = std::move(unit);
    evidence.reference = std::move(reference);
    evidence.severity = severity;
    evidence.coverageEndSeconds = static_cast<double>(result.source.durationMs) / 1'000.0;
    evidence.explanation = std::move(explanation);
    result.evidence.push_back(std::move(evidence));
}

void aggregate_verdict(AnalysisResult& result)
{
    if (result.coverage.activeWindows < AnalysisThresholds::minimumActiveWindows
        || result.coverage.activeWindowRatio
               < AnalysisThresholds::minimumActiveWindowRatio) {
        result.verdict = Verdict::Inconclusive;
        result.confidence = result.coverage.activeWindows == 0U
            ? AnalysisSupport::emptyContent
            : static_cast<int>(AnalysisThresholds::lowInformationConfidence);
        add_evidence(result, "insufficient_active_content",
                     EvidenceFamily::ContentQuality,
                     EvidenceDirection::ContradictsConclusion,
                     result.coverage.activeWindowRatio, "ratio", ">= 0.05", 2,
                     "有效声音窗口不足，无法形成可靠取证结论。");
        return;
    }

    const bool dsd = result.source.kind == SourceKind::Dsd
                     || result.source.kind == SourceKind::Dst;
    if (dsd) {
        const bool shaped_noise = result.measurements.highFrequencyEnergyRatio
                > AnalysisThresholds::dsdMinimumUltrasonicEnergyRatio
            && result.measurements.dsdUltrasonicSlopeDbPerOctave
                > AnalysisThresholds::dsdMinimumSlopeDbPerOctave;
        const bool stable_noise = result.measurements.dsdNoiseShapingStability
            > AnalysisThresholds::dsdMinimumSlopeStability;
        const bool raw_stream_evidence = result.measurements.dsdRawPacketBytes > 0U
            && result.measurements.dsdRawByteEntropy
                > AnalysisThresholds::dsdMinimumRawByteEntropy
            && result.measurements.dsdRepeatedPatternRatio
                < AnalysisThresholds::dsdMaximumPeriodTwoRepeatRatio;
        const bool pcm_bandwidth = result.measurements.cutoffHz > 0.0
            && result.measurements.cutoffHz
                < AnalysisThresholds::dsdPcmBandwidthHz
            && result.measurements.cutoffStability
                > AnalysisThresholds::dsdMinimumSlopeStability;
        if (pcm_bandwidth
                   && result.measurements.highFrequencyEnergyRatio
                        < AnalysisThresholds::dsdMaximumPcmUltrasonicEnergyRatio) {
            result.verdict = Verdict::SuspectedPcmToDsd;
            result.confidence = AnalysisSupport::pcmToDsd;
            result.chain = {"PCM（推测）", "DSD"};
        } else {
            result.verdict = Verdict::Inconclusive;
            result.confidence = AnalysisSupport::dsdInconclusive;
            if (shaped_noise && stable_noise && raw_stream_evidence) {
                result.warnings.push_back(
                    "噪声整形与原始1-bit统计只能确认有效DSD表示，无法证明录音为原生DSD采集。");
            }
        }
        return;
    }

    const bool known_lossy = is_lossy_codec(result.source.codec);
    const bool stable_cutoff = result.measurements.cutoffHz > 0.0
        && result.measurements.cutoffHz < result.spectrum.nyquistHz
               * AnalysisThresholds::stableCutoffRatio
        && result.measurements.cutoffStability
               >= AnalysisThresholds::stableCutoffMinimumConsistency;
    const bool edge_matches_cutoff = result.measurements.cutoffHz > 0.0
        && result.measurements.spectralEdgeFrequencyHz > 0.0
        && std::abs(result.measurements.spectralEdgeFrequencyHz
                    - result.measurements.cutoffHz)
            <= AnalysisThresholds::spectralEdgeCutoffToleranceHz;
    const bool persistent_spectral_edge = edge_matches_cutoff
        && result.measurements.cutoffStability
            >= AnalysisThresholds::stableCutoffMinimumConsistency
        && result.measurements.spectralEdgeDepthDb
            >= AnalysisThresholds::spectralEdgeMinimumDepthDb
        && result.measurements.spectralEdgeStability
            >= AnalysisThresholds::spectralEdgeMinimumStability;
    const bool sharp_spectral_edge = persistent_spectral_edge
        && result.measurements.spectralEdgeDepthDb
            >= AnalysisThresholds::sharpDigitalEdgeMinimumDepthDb;
    const bool temporal_codec_holes = result.measurements.codecHoleScore
        > AnalysisThresholds::codecHoleScore;
    const bool codec_structure = stable_cutoff && temporal_codec_holes;
    const bool mirrored = result.source.sampleRate
            >= AnalysisThresholds::resamplingMinimumSampleRate
        && result.measurements.resamplingMirrorScore
               > AnalysisThresholds::resamplingMirrorScore;
    const bool resampling_edge = result.source.sampleRate
            >= AnalysisThresholds::resamplingMinimumSampleRate
        && stable_cutoff && persistent_spectral_edge;
    const bool resampling_structure = mirrored;
    const bool polyphase_structure = result.measurements.resamplingPhaseSegments >= 4U
        && result.measurements.resamplingResidualEntropyMeasured
        && result.measurements.resamplingBandSuppressionMeasured
        && result.measurements.resamplingPhaseStrength >= AnalysisThresholds::resamplingPhaseMinimumStrength
        && result.measurements.resamplingPhaseCoherence >= AnalysisThresholds::resamplingPhaseMinimumCoherence
        && result.measurements.resamplingResidualEntropy >= AnalysisThresholds::resamplingPhaseMinimumResidualEntropy
        && result.measurements.resamplingBandSuppressionDb >= AnalysisThresholds::resamplingPhaseMinimumBandSuppressionDb;
    const bool bit_expansion = result.source.bitsPerSample >= 24
        && result.measurements.effectiveBits > 0.0
        && result.measurements.effectiveBits
               <= static_cast<double>(result.source.bitsPerSample
                                      - AnalysisThresholds::bitExpansionMinimumUnusedBits)
        && result.measurements.lowBitUsageRatio
               < AnalysisThresholds::bitExpansionMaximumLowBitUsage
        && (result.measurements.spectralEntropy
                >= AnalysisThresholds::bitExpansionMinimumSpectralEntropy
            || result.measurements.spectralFlatness
                >= AnalysisThresholds::bitExpansionMinimumSpectralFlatness);

    if (polyphase_structure) {
        add_evidence(result, "resampling_polyphase_grid", EvidenceFamily::Resampling,
                     EvidenceDirection::Neutral,
                     result.measurements.resamplingPhaseSourceRate, "Hz",
                     "相位一致性>=0.995、强度>=0.02、>=4段、残差熵>=0.45、带外抑制>=35dB", 1,
                     "检测到稳定周期结构；重采样与周期调制均可形成，不能单独确定升频历史。");
        result.candidates.push_back({"重采样或周期调制", 33,
            "周期、镜像和带外抑制仍可由同一种调制处理形成，候选不构成来源判定。"});
    }
    if (known_lossy) {
        result.verdict = Verdict::SuspectedLossyTranscode;
        result.confidence = AnalysisSupport::knownLossyCodec;
        result.candidates.push_back({result.source.codec, 100,
            "当前编解码器是文件事实；更早的编码历史仍无法恢复。"});
        result.chain = {result.source.codec, result.source.container};
        add_evidence(result, "known_lossy_codec", EvidenceFamily::CodecStructure,
                     EvidenceDirection::SupportsLossySource, 1.0, "boolean",
                     "当前流编解码器", 3,
                     "当前音频流使用有损编解码器；这不等同于识别出更早的源文件。");
        return;
    }
    if (supports_mdct_framing(result.measurements.mdctFrameWindow,
        result.measurements.mdctFrameCoherentPeakDb, result.measurements.mdctFramePeakZ,
        result.measurements.mdctFrameBlocks, result.measurements.mdctFrameAlignedBlocks)) {
        result.verdict = Verdict::SuspectedLossyTranscode;
        result.confidence = AnalysisSupport::lossyTranscode;
        const bool hybrid = result.measurements.mdctFrameWindow == "mp3_hybrid_36";
        result.chain = {hybrid ? "MP3长块编码（推测）" : "MDCT编码（推测）", result.source.container};
        result.candidates.push_back({hybrid ? "MP3混合变换结构" : "MDCT变换编码", AnalysisSupport::lossyTranscode,
            hybrid ? "检测到与MP3长块混合滤波器一致的帧结构；尚未验证短块、混合块及完整编码历史。"
                   : "帧结构证据不能确定具体编码器、码率或完整历史；1152采样帧仅是混合编码结构的近似观测。"});
        add_evidence(result, "mdct_framing_structure", EvidenceFamily::CodecStructure,
                     EvidenceDirection::SupportsLossySource,
                     result.measurements.mdctFrameCoherentPeakDb, "dB",
                     (result.measurements.mdctFrameWindow == "sine1152" || hybrid)
                         ? ">=2dB、峰值z>=5、>=3段帧相位在±2采样内一致"
                         : ">=6dB、峰值z>=12、>=3个不重叠片段", 2,
                     "多个不重叠片段保留变换帧栅格，支持历史压缩推断；特殊变换处理仍可能形成类似结构。");
        return;
    }
    if (sharp_spectral_edge) {
        add_evidence(result, "persistent_spectral_edge",
                     EvidenceFamily::Bandwidth,
                     EvidenceDirection::Neutral,
                     result.measurements.spectralEdgeDepthDb, "dB",
                     ">= 45 dB且需跨窗稳定截止、边缘位置一致", 2,
                     "多数有效窗口出现与截止位置相符的深陡频谱边缘；有损编码、升频抗镜像滤波和母带数字低通均可能形成此特征。");
    }
    if (supports_celt_framing(result.measurements)) {
        result.verdict = Verdict::SuspectedLossyTranscode;
        result.confidence = AnalysisSupport::lossyTranscode;
        result.chain = {"CELT变换帧（推测）", result.source.container};
        result.candidates.push_back({"CELT帧结构", AnalysisSupport::lossyTranscode,
            "仅覆盖48kHz下的部分变换帧结构，不能确定具体编码器、码率或完整编码历史。"});
        add_evidence(result, "celt_framing_structure", EvidenceFamily::CodecStructure,
                     EvidenceDirection::SupportsLossySource,
                     result.measurements.celtFrameMinimumBandZ, "z",
                     "两频带z>=8、峰宽<=8采样、跨段一致性>=0.9、>=3段、频带相位差<=2采样", 2,
                     "全频与18kHz以下频带共同保留窄变换帧峰，支持历史压缩推断；无峰不证明未压缩。");
        return;
    }
    if (polyphase_structure) {
        add_evidence(result, "resampling_polyphase_grid", EvidenceFamily::Resampling,
                     EvidenceDirection::Neutral,
                     result.measurements.resamplingPhaseSourceRate, "Hz",
                     "相位一致性>=0.995、强度>=0.02、>=4段、残差熵>=0.45、带外抑制>=35dB", 2,
                     "多相残差与较低采样栅格一致；原生采样率的幅度调制也能形成，不能单独确定重采样历史。");
    }
    if (resampling_edge) {
        add_evidence(result, "persistent_resampling_edge",
                     EvidenceFamily::Resampling,
                     EvidenceDirection::Neutral,
                     result.measurements.spectralEdgeStability, "ratio",
                     ">= 0.80且需高采样率、稳定低带宽及>=24 dB边缘", 2,
                     "高采样率信号保留低带宽陡峭边缘；重采样与当前采样率数字低通均可形成，来源暂不确定。");
        if (!mirrored) result.candidates.push_back({"较低采样率PCM或数字低通母带", 33,
            "频谱边缘不能区分重采样与原生采样率滤波，候选不构成来源判定。"});
    }
    if (stable_cutoff && resampling_structure && !polyphase_structure) {
        add_evidence(result, "resampling_mirror_unattributed", EvidenceFamily::Resampling,
                     EvidenceDirection::Neutral, result.measurements.resamplingMirrorScore,
                     "score", "镜像相关不能区分重采样与周期调制", 1,
                     "频移副本也可能来自周期调制，不能单独确定升频历史。");
    }
    if (codec_structure) {
        // Both are spectral-shape observations, not independent codec evidence.
        // Native filtering and harmonic gaps can produce both simultaneously.
        add_evidence(result, "spectral_notches_unattributed", EvidenceFamily::Bandwidth,
                     EvidenceDirection::Neutral, result.measurements.codecHoleScore, "score",
                     "局部频谱凹口的跨窗出现比例", 1,
                     "频谱凹口和截止可由同一滤波或谐波结构形成，不能单独证明有损编码历史。");
    }
    if (bit_expansion) {
        result.verdict = Verdict::SuspectedBitDepthExpansion;
        result.confidence = AnalysisSupport::bitDepthExpansion;
        result.chain = {"较低位深 PCM（推测）", result.source.container};
        return;
    }
    const bool broad_band = result.measurements.cutoffHz
        >= result.spectrum.nyquistHz
               * AnalysisThresholds::credibleMinimumBandwidthRatio;
    const bool quantization_healthy = result.source.bitsPerSample <= 0
        || result.measurements.effectiveBits <= 0.0
        || result.measurements.effectiveBits
               >= static_cast<double>(std::max(12, result.source.bitsPerSample - 4));
    if (result.source.codecIsLossless && broad_band && quantization_healthy
        && !codec_structure) {
        add_evidence(result, "broadband_quantization_compatible", EvidenceFamily::ContentQuality,
                     EvidenceDirection::Neutral, result.measurements.cutoffHz, "Hz",
                     "宽频带与量化统计正常不等于来源认证", 1,
                     "有损解码后添加噪声也能形成宽频带与有效低位，未发现压缩痕迹不能证明原始无损。" );
    }
    result.verdict = Verdict::Inconclusive;
    result.confidence = stable_cutoff
        ? AnalysisSupport::cutoffInconclusive
        : AnalysisSupport::defaultInconclusive;
    add_evidence(result, "insufficient_source_evidence", EvidenceFamily::ContentQuality,
                 EvidenceDirection::Neutral, 0.0, "", "需要相互支持的来源证据", 1,
                 "当前频谱与量化特征不足以确定来源；低带宽、前置能量或单一频谱边缘均不能单独证明转码。");
}

AnalysisResult cancelled_result(AnalysisResult result)
{
    result.verdict = Verdict::Cancelled;
    result.confidence = 0;
    result.cancelled = true;
    result.error.clear();
    return result;
}

AnalysisResult failed_result(AnalysisResult result, std::string error)
{
    result.verdict = Verdict::AnalysisFailed;
    result.confidence = 0;
    result.error = std::move(error);
    return result;
}

} // namespace

AnalysisResult analyzeFile(const std::string& utf8Path,
                           const AnalysisOptions& options,
                           const std::atomic_bool& cancelled,
                           ProgressCallback progressCallback)
{
    AnalysisResult result;
    result.analysisStartedUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    result.file.path = utf8Path;
    result.spectrum.minDb = kSpectrumFloorDb;
    try {
        if (utf8Path.empty()) return failed_result(std::move(result), "文件路径为空。");
        if (options.spectrumBins < 16U || options.spectrumBins > kMaxSpectrumBins
            || options.maxSpectrogramTimeBins > kMaxSpectrogramTimeBins
            || options.maxSpectrogramFrequencyBins
                   > kMaxSpectrogramFrequencyBins) {
            return failed_result(std::move(result), "分析参数超出有界范围。");
        }
        if (cancelled.load(std::memory_order_relaxed)) {
            return cancelled_result(std::move(result));
        }
        report_progress(progressCallback, 0.0F);

        std::error_code file_error;
        if (std::filesystem::is_regular_file(
                std::filesystem::u8path(utf8Path), file_error)) {
            result.file.fileSize = std::filesystem::file_size(
                std::filesystem::u8path(utf8Path), file_error);
            if (!file_error) {
                const auto modified = std::filesystem::last_write_time(
                    std::filesystem::u8path(utf8Path), file_error);
                if (!file_error) {
                    // C++17 file_clock has an implementation-defined epoch.
                    // Translate it through near-simultaneous clock readings.
                    const auto system_modified =
                        std::chrono::system_clock::now()
                        + (modified
                           - std::filesystem::file_time_type::clock::now());
                    result.file.modifiedUnixMs =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            system_modified.time_since_epoch()).count();
                }
            }
        }

        DecoderOpenOptions decoder_options = options.decoderOpenOptions;
        InterruptContext interrupt_context{&cancelled,
                                           decoder_options.interrupt_callback,
                                           decoder_options.interrupt_context};
        decoder_options.interrupt_callback = &decoder_interrupted;
        decoder_options.interrupt_context = &interrupt_context;
        PacketEvidenceContext packet_evidence;
        packet_evidence.upstream = decoder_options.packet_callback;
        packet_evidence.upstream_context = decoder_options.packet_context;
        decoder_options.packet_callback = &observe_packet_bytes;
        decoder_options.packet_context = &packet_evidence;
        std::optional<SacdTrackStream> sacd_stream;
        if (options.isoTrackIndex >= 0) {
            sacd_stream.emplace();
            std::string sacd_error;
            if (!sacd_stream->open(utf8Path,
                                   static_cast<std::size_t>(options.isoTrackIndex),
                                   sacd_error, &cancelled)) {
                if (cancelled.load(std::memory_order_relaxed)) {
                    return cancelled_result(std::move(result));
                }
                return failed_result(std::move(result), std::move(sacd_error));
            }
            decoder_options.custom_read = &SacdTrackStream::readCallback;
            decoder_options.custom_seek = &SacdTrackStream::seekCallback;
            decoder_options.custom_io_context = &*sacd_stream;
            decoder_options.input_format_hint = "dsdiff";
        }

        Decoder decoder;
        const ag_result open_status = decoder.open(utf8Path, decoder_options);
        if (open_status == AG_CANCELLED
            || cancelled.load(std::memory_order_relaxed)) {
            return cancelled_result(std::move(result));
        }
        if (open_status != AG_OK) {
            return failed_result(std::move(result), "无法打开或解码音频流。");
        }
        const MediaMetadata metadata = decoder.metadata();
        const DecodedNativeFormat native = decoder.native_format();
        packet_evidence.collect_raw_dsd = native.is_dsd && !native.is_dst;
        result.source.container = options.isoTrackIndex >= 0
            ? "sacd_iso" : metadata.format;
        result.source.codec = metadata.codec;
        result.source.sampleRate = native.sample_rate;
        result.source.decodedSampleRate = native.sample_rate;
        result.source.rawDsdSampleRate = native.raw_dsd_sample_rate;
        result.source.bitsPerSample = native.valid_bits > 0
            ? native.valid_bits : metadata.bits_per_sample;
        result.source.channels = native.channels;
        result.source.channelLayout = native.channel_layout;
        result.source.durationMs = metadata.duration_ms;
        result.source.codecIsLossless = native.codec_is_lossless;
        result.source.dstDecoded = native.is_dst;
        result.source.kind = native.is_dst ? SourceKind::Dst
            : native.is_dsd ? SourceKind::Dsd
            : native.representation == NativeSampleRepresentation::SignedInteger
                ? SourceKind::PcmInteger
            : native.representation == NativeSampleRepresentation::FloatingPoint
                ? SourceKind::PcmFloatingPoint : SourceKind::Unknown;
        if (native.is_dsd || native.is_dst) result.source.bitsPerSample = 1;
        result.spectrum.nyquistHz = static_cast<double>(native.sample_rate) / 2.0;
        if (native.sample_rate <= 0 || native.channels <= 0 || native.channels > 64) {
            return failed_result(std::move(result), "音频流参数无效。");
        }

        std::vector<StftAccumulator> stft;
        stft.reserve(kStftSizes.size());
        for (const std::size_t size : kStftSizes) {
            const bool capture = options.includeSpectrogram
                && size == kStftSizes[1];
            stft.emplace_back(size, native.sample_rate, static_cast<std::size_t>(native.channels), kAnalysisSpectrumBins,
                              capture, options.maxSpectrogramTimeBins,
                              options.maxSpectrogramFrequencyBins,
                              metadata.duration_ms);
            result.measurements.stftWindowSizes.push_back(size);
        }

        TransientAccumulator transients(native.sample_rate);
        ResamplingPhaseAccumulator resampling_phase(native.sample_rate,
            static_cast<std::size_t>(native.channels), !native.is_dsd && !native.is_dst);
        long double sum_squares = 0.0L;
        double peak = 0.0;
        std::uint64_t sample_count = 0U;
        std::uint64_t invalid_sample_count = 0U;
        std::uint64_t nonzero_integer_count = 0U;
        std::uint64_t low_bit_used_count = 0U;
        int minimum_trailing_zeros = result.source.bitsPerSample > 0
            ? result.source.bitsPerSample : native.storage_bits;
        long double channel_x = 0.0L;
        long double channel_y = 0.0L;
        long double channel_xy = 0.0L;
        long double channel_x2 = 0.0L;
        long double channel_y2 = 0.0L;
        std::uint64_t correlation_frames = 0U;
        std::array<std::uint64_t, 121> rms_histogram{};
        const int padding_bits = native.representation
                == NativeSampleRepresentation::SignedInteger
            ? std::max(0, native.storage_bits - result.source.bitsPerSample) : 0;
        const int low_bit_count = std::clamp(result.source.bitsPerSample, 1, 8);
        const std::uint64_t low_bit_mask = (std::uint64_t{1} << low_bit_count) - 1U;
        const std::uint64_t expected_frames = metadata.duration_ms > 0
            ? static_cast<std::uint64_t>(metadata.duration_ms)
                * static_cast<std::uint64_t>(native.sample_rate) / 1'000U : 0U;
        std::unique_ptr<MdctFramingProbe> mdct_probe;
        constexpr std::array<int, 2> inverse_rates{44100, 48000};
        std::array<std::unique_ptr<ResampledMdctProbe>, 2> inverse_probes;
        if (!native.is_dsd && !native.is_dst && !is_lossy_codec(result.source.codec)
            && native.sample_rate >= 88200 && native.sample_rate <= 768000) {
            for (std::size_t i = 0; i < inverse_probes.size(); ++i)
                inverse_probes[i] = std::make_unique<ResampledMdctProbe>(native.sample_rate,
                    native.channels, expected_frames, inverse_rates[i]);
        }
        constexpr std::array<std::size_t, 4> celt_frame_sizes{960, 480, 240, 120};
        std::array<std::unique_ptr<CeltFramingProbe>, 4> celt_probes, celt_deep_probes;
        constexpr std::array<std::size_t, 2> extended_depths{192, 768};
        std::array<std::unique_ptr<CeltFramingProbe>, 2> celt_extended_probes;
        std::unique_ptr<Mp3HybridProbe> hybrid_probe;
        if (!native.is_dsd && !native.is_dst && !is_lossy_codec(result.source.codec)
            && native.sample_rate >= 32'000 && native.sample_rate <= 48'000) {
            mdct_probe = std::make_unique<MdctFramingProbe>(native.sample_rate, native.channels,
                                                         expected_frames);
            hybrid_probe = std::make_unique<Mp3HybridProbe>(native.sample_rate, native.channels, expected_frames);
            if (native.sample_rate == 48'000) {
                for (std::size_t i = 0; i < celt_probes.size(); ++i) {
                    celt_probes[i] = std::make_unique<CeltFramingProbe>(native.sample_rate, native.channels,
                                                                     expected_frames, celt_frame_sizes[i]);
                    celt_deep_probes[i] = std::make_unique<CeltFramingProbe>(native.sample_rate, native.channels,
                        expected_frames, celt_frame_sizes[i], 48);
                }
                for (std::size_t i = 0; i < extended_depths.size(); ++i) {
                    // Bound the extra interleaved capture for high channel counts.
                    if (native.channels > 32) continue;
                    if (extended_depths[i] == 768 && native.channels > 8) continue;
                    if (expected_frames > 0 && expected_frames < 3 * (extended_depths[i] + 2) * 120) continue;
                    celt_extended_probes[i] = std::make_unique<CeltFramingProbe>(48000,
                        native.channels, expected_frames, 120, extended_depths[i]);
                }
            }
        }

        DecodedAnalysisBlock block;
        do {
            if (cancelled.load(std::memory_order_relaxed)) {
                return cancelled_result(std::move(result));
            }
            const ag_result read_status = decoder.readAnalysis(block);
            if (read_status == AG_CANCELLED
                || cancelled.load(std::memory_order_relaxed)) {
                return cancelled_result(std::move(result));
            }
            if (read_status != AG_OK) {
                return failed_result(std::move(result), "解码过程中出现错误。");
            }
            if (block.frames == 0U) continue;
            invalid_sample_count += block.invalid_samples;
            const std::size_t channels = static_cast<std::size_t>(native.channels);
            long double block_square_sum = 0.0L;
            for (std::size_t frame = 0U; frame < block.frames; ++frame) {
                double channel_power = 0.0;
                for (std::size_t channel = 0U; channel < channels; ++channel) {
                    const std::size_t index = frame * channels + channel;
                    const double sample = std::isfinite(block.samples[index])
                        ? std::clamp(block.samples[index], -1.0, 1.0) : 0.0;
                    block.samples[index] = sample;
                    channel_power += sample * sample;
                    sum_squares += static_cast<long double>(sample) * sample;
                    block_square_sum += static_cast<long double>(sample) * sample;
                    peak = std::max(peak, std::abs(sample));
                    ++sample_count;
                    if (!block.integer_samples.empty()) {
                        const std::int64_t raw = block.integer_samples[index];
                        const std::int64_t aligned = padding_bits > 0
                            ? raw / (std::int64_t{1} << padding_bits) : raw;
                        if (aligned != 0) {
                            ++nonzero_integer_count;
                            const std::uint64_t magnitude = aligned < 0
                                ? static_cast<std::uint64_t>(-(aligned + 1)) + 1U
                                : static_cast<std::uint64_t>(aligned);
                            if ((magnitude & low_bit_mask) != 0U) {
                                ++low_bit_used_count;
                            }
                            int trailing = 0;
                            std::uint64_t bits = magnitude;
                            while (trailing < result.source.bitsPerSample
                                   && (bits & 1U) == 0U) {
                                ++trailing;
                                bits >>= 1U;
                            }
                            minimum_trailing_zeros = std::min(
                                minimum_trailing_zeros, trailing);
                        }
                    }
                }
                transients.add(channel_power / static_cast<double>(channels));
                resampling_phase.add(block.samples.data() + frame * channels);
                if (channels >= 2U) {
                    const double left = block.samples[frame * channels];
                    const double right = block.samples[frame * channels + 1U];
                    channel_x += left;
                    channel_y += right;
                    channel_xy += left * right;
                    channel_x2 += left * left;
                    channel_y2 += right * right;
                    ++correlation_frames;
                }
            }
            const double block_rms = std::sqrt(static_cast<double>(
                block_square_sum / static_cast<long double>(
                    block.frames * channels)));
            const double block_db = block_rms > 1.0e-6
                ? 20.0 * std::log10(block_rms) : -120.0;
            const std::size_t histogram_index = static_cast<std::size_t>(
                std::clamp(std::lround(block_db + 120.0), 0L, 120L));
            ++rms_histogram[histogram_index];
            if (mdct_probe) mdct_probe->consume(block.samples.data(), block.frames, cancelled);
            if (hybrid_probe) hybrid_probe->consume(block.samples.data(), block.frames, cancelled);
            for (auto& probe : inverse_probes)
                if (probe) probe->consume(block.samples.data(), block.frames, cancelled);
            for (auto& probe : celt_probes)
                if (probe) probe->consume(block.samples.data(), block.frames, cancelled);
            for (auto& probe : celt_deep_probes)
                if (probe) probe->consume(block.samples.data(), block.frames, cancelled);
            for (auto& probe : celt_extended_probes)
                if (probe) probe->consume(block.samples.data(), block.frames, cancelled);
            for (auto& accumulator : stft) {
                if (!accumulator.add(block.samples, cancelled)) {
                    return cancelled_result(std::move(result));
                }
            }
            result.coverage.decodedFrames += block.frames;
            if (expected_frames > 0U) {
                const double ratio = std::min(1.0,
                    static_cast<double>(result.coverage.decodedFrames)
                    / static_cast<double>(expected_frames));
                report_progress(progressCallback,
                                static_cast<float>(0.05 + ratio * 0.94));
            }
        } while (!block.end_of_stream);

        for (auto& probe : inverse_probes)
            if (probe) probe->finish(cancelled);
        for (auto& accumulator : stft) {
            if (!accumulator.finish(cancelled)) {
                return cancelled_result(std::move(result));
            }
        }
        if (cancelled.load(std::memory_order_relaxed)) {
            return cancelled_result(std::move(result));
        }

        const StftAccumulator& primary = stft.back();
        const auto select_celt = [&](const CeltFrameEvidence& measured, std::size_t i, std::size_t frames) {
            AnalysisMeasurements candidate;
            candidate.celtFrameBlocks = std::min(measured.fullBand.activeBlocks, measured.lowBand.activeBlocks);
            candidate.celtFrameMinimumBandZ = std::min(measured.fullBand.robustZ, measured.lowBand.robustZ);
            candidate.celtFrameMinimumAnchorCoherence = std::min(measured.fullBand.anchorPeakCoherence, measured.lowBand.anchorPeakCoherence);
            candidate.celtFrameMaximumPeakWidth = std::max(measured.fullBand.halfProminenceWidth, measured.lowBand.halfProminenceWidth);
            candidate.celtFrameBandPhaseDifference = measured.phaseAgreementSamples;
            const bool qualifies = supports_celt_framing(candidate);
            const bool current_qualifies = supports_celt_framing(result.measurements);
            if ((qualifies && !current_qualifies) || (qualifies == current_qualifies
                && candidate.celtFrameBlocks > 0 && candidate.celtFrameMinimumBandZ > result.measurements.celtFrameMinimumBandZ)) {
                result.measurements.celtFrameBlocks = candidate.celtFrameBlocks;
                result.measurements.celtFrameSamples = celt_frame_sizes[i];
                result.measurements.celtFramesPerAnchor = frames;
                result.measurements.celtFrameMinimumBandZ = candidate.celtFrameMinimumBandZ;
                result.measurements.celtFrameMinimumAnchorCoherence = candidate.celtFrameMinimumAnchorCoherence;
                result.measurements.celtFrameMaximumPeakWidth = candidate.celtFrameMaximumPeakWidth;
                result.measurements.celtFrameBandPhaseDifference = candidate.celtFrameBandPhaseDifference;
            }
        };
        for (std::size_t i = 0; i < celt_probes.size(); ++i)
            if (celt_probes[i]) select_celt(celt_probes[i]->result(), i, 12);
        const auto select_mdct = [&](const auto& candidates, int rate) {
            for (const auto& measured : candidates) {
                if (measured.activeBlocks == 0U) continue;
                // The hybrid 1152 approximation has not been validated through
                // this analysis resampler. Keep it limited to native-rate PCM.
                if (rate != native.sample_rate && std::string_view(measured.window) == "sine1152") continue;
                const bool supports = supports_mdct_framing(measured.window, measured.coherentPeakDb,
                    measured.meanPeakZ, measured.activeBlocks, measured.alignedBlocks);
                const bool current_supports = supports_mdct_framing(result.measurements.mdctFrameWindow,
                    result.measurements.mdctFrameCoherentPeakDb, result.measurements.mdctFramePeakZ,
                    result.measurements.mdctFrameBlocks, result.measurements.mdctFrameAlignedBlocks);
                if ((supports && !current_supports) || (supports == current_supports
                    && measured.coherentPeakDb >= result.measurements.mdctFrameCoherentPeakDb)) {
                    result.measurements.mdctFrameCoherentPeakDb = measured.coherentPeakDb;
                    result.measurements.mdctFramePeakZ = measured.meanPeakZ;
                    result.measurements.mdctFrameBlocks = measured.activeBlocks;
                    result.measurements.mdctFrameAlignedBlocks = measured.alignedBlocks;
                    result.measurements.mdctFrameWindow = measured.window;
                    result.measurements.mdctAnalysisSampleRate = rate;
                }
            }
        };
        if (mdct_probe) select_mdct(mdct_probe->result(), native.sample_rate);
        for (std::size_t i = 0; i < inverse_probes.size(); ++i)
            if (inverse_probes[i]) select_mdct(inverse_probes[i]->result(), inverse_rates[i]);
        if (hybrid_probe && !supports_celt_framing(result.measurements)
            && !supports_mdct_framing(result.measurements.mdctFrameWindow,
                result.measurements.mdctFrameCoherentPeakDb, result.measurements.mdctFramePeakZ,
                result.measurements.mdctFrameBlocks, result.measurements.mdctFrameAlignedBlocks)) {
            hybrid_probe->refine(cancelled);
            if (cancelled.load(std::memory_order_relaxed)) return cancelled_result(std::move(result));
            select_mdct(std::array<MdctFrameEvidence, 1>{hybrid_probe->result()}, native.sample_rate);
            if (!supports_mdct_framing(result.measurements.mdctFrameWindow,
                    result.measurements.mdctFrameCoherentPeakDb, result.measurements.mdctFramePeakZ,
                    result.measurements.mdctFrameBlocks, result.measurements.mdctFrameAlignedBlocks)) {
                // Stereo channels can retain different coding evidence. Keep
                // each complete measurement separate; do not combine scores.
                hybrid_probe->refineAlternate(cancelled);
                if (cancelled.load(std::memory_order_relaxed)) return cancelled_result(std::move(result));
                select_mdct(std::array<MdctFrameEvidence, 1>{hybrid_probe->alternateResult()}, native.sample_rate);
            }
        }
        // Dense transforms are deferred until both ordinary framing probes
        // abstain. Four bounded mono excerpts were retained during decoding.
        if (!supports_celt_framing(result.measurements)
            && !supports_mdct_framing(result.measurements.mdctFrameWindow,
                result.measurements.mdctFrameCoherentPeakDb, result.measurements.mdctFramePeakZ,
                result.measurements.mdctFrameBlocks, result.measurements.mdctFrameAlignedBlocks)) {
            for (std::size_t i = 0; i < celt_deep_probes.size(); ++i) {
                if (!celt_deep_probes[i]) continue;
                celt_deep_probes[i]->refine(cancelled);
                if (cancelled.load(std::memory_order_relaxed)) return cancelled_result(std::move(result));
                select_celt(celt_deep_probes[i]->result(), i, 48);
                if (supports_celt_framing(result.measurements)) break;
            }
        }
        for (std::size_t i = 0; i < celt_extended_probes.size(); ++i) {
            if (supports_celt_framing(result.measurements)
                || supports_mdct_framing(result.measurements.mdctFrameWindow,
                    result.measurements.mdctFrameCoherentPeakDb, result.measurements.mdctFramePeakZ,
                    result.measurements.mdctFrameBlocks, result.measurements.mdctFrameAlignedBlocks)) break;
            if (!celt_extended_probes[i]) continue;
            celt_extended_probes[i]->refine(cancelled);
            if (cancelled.load(std::memory_order_relaxed)) return cancelled_result(std::move(result));
            select_celt(celt_extended_probes[i]->result(), 3, extended_depths[i]);
        }
        {
            if (result.measurements.celtFrameBlocks > 0U) {
                add_evidence(result, "celt_framing_peak", EvidenceFamily::CodecStructure,
                             EvidenceDirection::Neutral, result.measurements.celtFrameMinimumBandZ,
                             "z", "全频与18kHz以下频带中较低的稳健峰值显著性", 1,
                             "固定数量独立片段的低重叠变换帧扫描；仅适用于48kHz，缺失证据不证明无损。");
                add_evidence(result, "celt_frame_samples", EvidenceFamily::CodecStructure,
                             EvidenceDirection::Neutral, static_cast<double>(result.measurements.celtFrameSamples),
                             "samples", "120 / 240 / 480 / 960", 1,
                             "候选CELT帧长仅描述所检验的变换栅格，不等于编码器或码率认证。");
            }
        }
        {
            if (result.measurements.mdctFrameBlocks > 0U) {
                add_evidence(result, "mdct_framing_peak", EvidenceFamily::CodecStructure,
                             EvidenceDirection::Neutral, result.measurements.mdctFrameCoherentPeakDb,
                             "dB", result.measurements.mdctFrameWindow, 1,
                             "固定数量独立片段的MDCT邻移能量差分，需结合峰值显著性；无峰不证明未压缩。");
                if (result.measurements.mdctAnalysisSampleRate != native.sample_rate)
                    add_evidence(result, "mdct_inverse_analysis_rate", EvidenceFamily::CodecStructure,
                                 EvidenceDirection::Neutral, result.measurements.mdctAnalysisSampleRate,
                                 "Hz", "44100 / 48000", 1,
                                 "此帧证据来自独立降采样分析视图；原始格式与量化测量未改变，不单独证明完整升频链。");
            }
        }
        result.coverage.analyzedWindows = primary.window_count();
        result.coverage.activeWindows = primary.active_count();
        result.coverage.activeWindowRatio = primary.window_count() > 0U
            ? static_cast<double>(primary.active_count())
                / static_cast<double>(primary.window_count()) : 0.0;
        result.coverage.decodedRatio = expected_frames > 0U
            ? std::min(1.0, static_cast<double>(result.coverage.decodedFrames)
                              / static_cast<double>(expected_frames))
            : block.end_of_stream ? 1.0 : 0.0;
        result.spectrum.db = primary.spectrum_db();
        if (!native.is_dsd && !native.is_dst) {
            transients.finish(result.measurements);
            primary.low_level_edge(result.measurements);
            resampling_phase.finish(result.measurements);
            primary.resampling_band_suppression(result.measurements);
        }
        result.measurements.cutoffHz = primary.cutoff_median();
        result.measurements.cutoffStability = primary.cutoff_stability();
        result.measurements.spectralEdgeFrequencyHz =
            primary.spectral_edge_frequency_median();
        result.measurements.spectralEdgeDepthDb =
            primary.spectral_edge_depth_lower_decile();
        result.measurements.spectralEdgeStability =
            primary.spectral_edge_stability();
        result.measurements.peak = peak;
        result.measurements.rms = sample_count > 0U
            ? std::sqrt(static_cast<double>(sum_squares
                  / static_cast<long double>(sample_count))) : 0.0;
        result.measurements.dynamicRangeDb = std::max(
            0.0, percentile_from_histogram(rms_histogram, 0.95)
                    - percentile_from_histogram(rms_histogram, 0.10));
        if (nonzero_integer_count > 0U) {
            result.measurements.lowBitUsageRatio =
                static_cast<double>(low_bit_used_count)
                / static_cast<double>(nonzero_integer_count);
            result.measurements.effectiveBits = static_cast<double>(
                std::max(0, result.source.bitsPerSample - minimum_trailing_zeros));
        }
        if (correlation_frames > 1U) {
            const long double count = static_cast<long double>(correlation_frames);
            const long double covariance = channel_xy - channel_x * channel_y / count;
            const long double variance_x = channel_x2 - channel_x * channel_x / count;
            const long double variance_y = channel_y2 - channel_y * channel_y / count;
            if (variance_x > 0.0L && variance_y > 0.0L) {
                result.measurements.channelCorrelation = static_cast<double>(
                    covariance / std::sqrt(variance_x * variance_y));
            }
        }
        if (options.includeSpectrogram) result.spectrogram = stft[1].spectrogram();
        derive_spectrum_measurements(result);
        const bool codec_hole_content_supported =
            result.measurements.spectralEntropy
                >= AnalysisThresholds::codecHoleMinimumContentEntropy
            || result.measurements.spectralFlatness
                >= AnalysisThresholds::codecHoleMinimumContentFlatness;
        result.measurements.codecHoleScore = codec_hole_content_supported
            ? primary.temporal_codec_hole_score() : 0.0;

        if (packet_evidence.bytes > 0U) {
            result.measurements.dsdRawPacketBytes = packet_evidence.bytes;
            result.measurements.dsdRawBitOneRatio =
                static_cast<double>(packet_evidence.one_bits)
                / static_cast<double>(packet_evidence.bytes * 8U);
            result.measurements.dsdRepeatedPatternRatio =
                static_cast<double>(packet_evidence.period_two_repeats)
                / static_cast<double>(packet_evidence.bytes);
            double entropy = 0.0;
            for (const std::uint64_t count : packet_evidence.byte_histogram) {
                if (count == 0U) continue;
                const double probability = static_cast<double>(count)
                    / static_cast<double>(packet_evidence.bytes);
                entropy -= probability * std::log2(probability);
            }
            result.measurements.dsdRawByteEntropy = entropy / 8.0;
        }
        if (invalid_sample_count > 0U) {
            result.warnings.push_back(
                "检测到非有限浮点样本；已按静音处理并降低证据可信度。");
            add_evidence(result, "nonfinite_samples",
                         EvidenceFamily::ContentQuality,
                         EvidenceDirection::ContradictsConclusion,
                         static_cast<double>(invalid_sample_count), "samples",
                         "expected 0", 2,
                         "NaN或Inf样本不参与频谱与统计计算。");
        }

        if (result.source.kind == SourceKind::Dsd
            || result.source.kind == SourceKind::Dst) {
            const auto ultrasonic_slope = [&](const std::vector<double>& db) {
                const auto band_power = [&](const double low, const double high) {
                double sum = 0.0;
                for (std::size_t index = 1U; index < db.size(); ++index) {
                    const double frequency = static_cast<double>(index)
                        * result.spectrum.nyquistHz
                        / static_cast<double>(db.size() - 1U);
                    if (frequency >= low && frequency < high) {
                        sum += std::pow(10.0, db[index] / 10.0);
                    }
                }
                return sum;
                };
                const double lower = band_power(
                    AnalysisThresholds::dsdUltrasonicLowerHz,
                    AnalysisThresholds::dsdUltrasonicSplitHz);
                const double upper = band_power(
                    AnalysisThresholds::dsdUltrasonicSplitHz,
                    std::min(AnalysisThresholds::dsdUltrasonicUpperHz,
                             result.spectrum.nyquistHz));
                return lower > 1.0e-12 && upper > 1.0e-12
                    ? 10.0 * std::log10(upper / lower) : 0.0;
            };
            std::array<double, kStftSizes.size()> slopes{};
            for (std::size_t index = 0U; index < stft.size(); ++index) {
                slopes[index] = ultrasonic_slope(stft[index].spectrum_db());
            }
            const double mean = std::accumulate(slopes.begin(), slopes.end(), 0.0)
                / static_cast<double>(slopes.size());
            double squared_deviation = 0.0;
            for (const double slope : slopes) {
                const double difference = slope - mean;
                squared_deviation += difference * difference;
            }
            const double deviation = std::sqrt(
                squared_deviation / static_cast<double>(slopes.size()));
            result.measurements.dsdUltrasonicSlopeDbPerOctave = mean;
            result.measurements.dsdNoiseShapingStability = clamp01(
                1.0 - deviation
                    / AnalysisThresholds::dsdSlopeStabilityDeviationDb);
            add_evidence(result, "dsd_ultrasonic_noise_shape",
                         EvidenceFamily::Dsd, EvidenceDirection::Neutral,
                         result.measurements.dsdUltrasonicSlopeDbPerOctave,
                         "dB/oct", "需与稳定性及带宽交叉判断", 1,
                         "DSD使用独立的超声噪声整形证据，不套用PCM固定截止规则。");
            if (packet_evidence.bytes > 0U) {
                add_evidence(result, "dsd_raw_bitstream_statistics",
                             EvidenceFamily::Dsd, EvidenceDirection::Neutral,
                             result.measurements.dsdRawByteEntropy, "normalized_entropy",
                             "需与解码频谱交叉判断", 1,
                             "原始1-bit包统计用于与解码后的超声频谱交叉比对。");
            } else if (native.is_dst) {
                result.warnings.push_back(
                    "DST压缩包不能当作原始1-bit统计，可信原生DSD结论被保守禁用。");
            }
        } else {
            if (result.measurements.resamplingPhaseSegments > 0U) {
                add_evidence(result, "resampling_phase_coherence", EvidenceFamily::Resampling,
                             EvidenceDirection::Neutral, result.measurements.resamplingPhaseCoherence,
                             "ratio", "8阶残差能量的跨段采样栅格相位一致性", 1,
                             "周期残差可能来自多相重采样，也可能由周期信号或处理形成；需结合带宽和残差谱分布。");
                add_evidence(result, "resampling_phase_strength", EvidenceFamily::Resampling,
                             EvidenceDirection::Neutral, result.measurements.resamplingPhaseStrength,
                             "ratio", "候选采样栅格循环能量相关", 1,
                             "增益和任意起始相位不改变跨段一致性的定义。");
            }
            if (result.measurements.lowLevelSpectralEdgeMeasured) {
                add_evidence(result, "low_level_spectral_edge", EvidenceFamily::Bandwidth,
                             EvidenceDirection::Neutral,
                             result.measurements.lowLevelSpectralEdgeHz, "Hz",
                             "原始FFT功率时间均值，独立于绘图下限", 1,
                             "低电平频谱存在陡峭衰减；数字低通、重采样和编码均可能形成，不能单独确定来源。");
                add_evidence(result, "low_level_spectral_edge_depth", EvidenceFamily::Bandwidth,
                             EvidenceDirection::Neutral,
                             result.measurements.lowLevelSpectralEdgeDepthDb, "dB",
                             "边缘两侧1.2kHz范围，排除中心200Hz", 1,
                             "测量保留低于绘图显示下限的原始功率，频谱图可能无法显示这部分细节。");
            }
            if (result.measurements.transientPreEchoMeasured) {
                add_evidence(result, "transient_precursor_energy", EvidenceFamily::ContentQuality,
                             EvidenceDirection::Neutral,
                             result.measurements.transientPreEchoScore, "ratio",
                             "瞬态前3-20ms能量减去30-50ms背景，再除以瞬态能量", 1,
                             "前置能量也可来自声学起音、混响或编辑，不单独证明编码前回声。");
            }
            add_evidence(result, "bandwidth_cutoff", EvidenceFamily::Bandwidth,
                         EvidenceDirection::Neutral,
                         result.measurements.cutoffHz, "Hz",
                         "跨有效窗口-60 dB相对阈值中位数", 1,
                         "截止仅描述跨时间频谱，不单独证明有损来源。");
            if (result.source.kind == SourceKind::PcmInteger) {
                add_evidence(result, "integer_low_bit_usage",
                             EvidenceFamily::Quantization,
                             EvidenceDirection::Neutral,
                             result.measurements.lowBitUsageRatio, "ratio",
                             "按源有效位对齐后的最低8位", 1,
                             "统计来自解码器原始整数表示，未先降低为float32。");
            }
            if (result.measurements.resamplingMirrorScore > 0.0) {
                add_evidence(result, "resampling_mirror",
                             EvidenceFamily::Resampling,
                             EvidenceDirection::Neutral,
                             result.measurements.resamplingMirrorScore, "score",
                             "> 0.72且需稳定截止共同支持", 1,
                             "比较常见源Nyquist两侧的镜像相关性。");
            }
        }
        aggregate_verdict(result);
        // Only after all evidence and verdict work may the display be resized.
        if (options.spectrumBins != result.spectrum.db.size()) {
            const auto analysis_db = std::move(result.spectrum.db);
            result.spectrum.db.resize(options.spectrumBins);
            for (std::size_t index = 0; index < options.spectrumBins; ++index) {
                const double position = static_cast<double>(index) * (analysis_db.size() - 1U)
                    / static_cast<double>(options.spectrumBins - 1U);
                const auto low = static_cast<std::size_t>(position);
                const auto high = std::min(low + 1U, analysis_db.size() - 1U);
                result.spectrum.db[index] = analysis_db[low]
                    + (analysis_db[high] - analysis_db[low]) * (position - low);
            }
        }
        const bool strong_counterevidence = std::any_of(
            result.evidence.begin(), result.evidence.end(),
            [](const Evidence& item) {
                return item.direction == EvidenceDirection::ContradictsConclusion
                    && item.severity >= 2;
            });
        if (strong_counterevidence) {
            if (result.verdict == Verdict::CredibleLossless
                || result.verdict == Verdict::CredibleNativeDsd) {
                result.verdict = Verdict::Inconclusive;
                result.confidence = std::min(result.confidence, 35);
                result.chain.clear();
            } else {
                result.confidence = std::max(0, result.confidence - 15);
            }
        }
        report_progress(progressCallback, 1.0F);
        decoder.clearInterruptCallback();
        return result;
    } catch (const std::bad_alloc&) {
        return failed_result(std::move(result), "分析内存不足。");
    } catch (...) {
        return failed_result(std::move(result), "分析过程中发生内部错误。");
    }
}

} // namespace agplayer::lossless

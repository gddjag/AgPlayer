#include "bpm_analyzer.hpp"

#include "decoder.hpp"
#include "audio_stream_source.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace agplayer {
namespace {

constexpr int kTargetSampleRate = 16000;
constexpr int kFrameSize = 512;   // 32 ms at 16 kHz
constexpr int kHopSize = 256;     // 16 ms hop
constexpr double kMinBpm = 45.0;
constexpr double kMaxBpm = 210.0;
constexpr std::size_t kMinFrames = 2 * kTargetSampleRate; // 2 seconds

class LowPassFilter {
public:
    explicit LowPassFilter(double alpha) noexcept
        : alpha_(alpha)
    {
    }

    float process(float input) noexcept
    {
        state_ = static_cast<float>((1.0 - alpha_) * state_ + alpha_ * input);
        return state_;
    }

private:
    double alpha_;
    float state_ = 0.0f;
};

std::vector<float> compute_onset_envelope(const std::vector<float>& pcm)
{
    LowPassFilter envelope_lp(0.15);
    std::vector<float> envelope;
    envelope.reserve(pcm.size() / kHopSize + 1);

    float prev_envelope = 0.0f;
    for (std::size_t i = 0; i + kFrameSize <= pcm.size(); i += kHopSize) {
        float frame_energy = 0.0f;
        for (int j = 0; j < kFrameSize; ++j) {
            frame_energy += std::abs(pcm[i + j]);
        }
        const float smoothed = envelope_lp.process(frame_energy);
        float novelty = smoothed - prev_envelope;
        if (novelty < 0.0f) {
            novelty = 0.0f;
        }
        envelope.push_back(novelty);
        prev_envelope = smoothed;
    }

    if (envelope.empty()) {
        return envelope;
    }

    // Remove DC.
    const float mean = std::accumulate(envelope.begin(), envelope.end(), 0.0f)
                       / static_cast<float>(envelope.size());
    for (float& v : envelope) {
        v = std::max(0.0f, v - mean);
    }

    // Normalize.
    const float max_val = *std::max_element(envelope.begin(), envelope.end());
    if (max_val > 0.0f) {
        for (float& v : envelope) {
            v /= max_val;
        }
    }

    return envelope;
}

std::vector<double> autocorrelate(
    const std::vector<float>& signal, const std::atomic_bool* cancelled)
{
    const std::size_t n = signal.size();
    std::vector<double> ac(n, 0.0);
    for (std::size_t lag = 0; lag < n; ++lag) {
        if (cancelled != nullptr
            && cancelled->load(std::memory_order_relaxed)) return {};
        double sum = 0.0;
        for (std::size_t i = 0; i + lag < n; ++i) {
            sum += static_cast<double>(signal[i]) * static_cast<double>(signal[i + lag]);
        }
        ac[lag] = sum;
    }
    return ac;
}

ag_result analyze_pcm(const std::vector<float>& pcm,
                      const std::atomic_bool* cancelled,
                      BpmAnalyzeOutput* out)
{
    if (pcm.size() < kMinFrames) return AG_DECODE_ERROR;
    if (cancelled != nullptr
        && cancelled->load(std::memory_order_relaxed)) return AG_CANCELLED;
    const std::vector<float> envelope = compute_onset_envelope(pcm);
    if (envelope.size() < 10) return AG_DECODE_ERROR;
    const std::vector<double> ac = autocorrelate(envelope, cancelled);
    if (ac.empty()) return AG_CANCELLED;

    const double frames_per_second = static_cast<double>(kTargetSampleRate)
                                     / static_cast<double>(kHopSize);
    const std::size_t min_lag = static_cast<std::size_t>(
        frames_per_second * 60.0 / kMaxBpm);
    const std::size_t max_lag = static_cast<std::size_t>(
        frames_per_second * 60.0 / kMinBpm);
    if (max_lag >= ac.size() || min_lag >= max_lag) return AG_DECODE_ERROR;

    std::size_t best_lag = min_lag;
    double best_value = ac[min_lag];
    for (std::size_t lag = min_lag + 1; lag <= max_lag; ++lag) {
        if (ac[lag] > best_value) {
            best_value = ac[lag];
            best_lag = lag;
        }
    }
    double interpolated_lag = static_cast<double>(best_lag);
    if (best_lag > min_lag && best_lag + 1 < ac.size()) {
        const double y0 = ac[best_lag - 1];
        const double y1 = ac[best_lag];
        const double y2 = ac[best_lag + 1];
        const double denom = 2.0 * (2.0 * y1 - y0 - y2);
        if (denom != 0.0) interpolated_lag += (y2 - y0) / denom;
    }
    out->bpm = std::clamp(frames_per_second * 60.0 / interpolated_lag,
                          kMinBpm, kMaxBpm);
    double sum = 0.0;
    for (std::size_t lag = min_lag; lag <= max_lag; ++lag) sum += ac[lag];
    const double mean = sum / static_cast<double>(max_lag - min_lag + 1);
    if (mean > 0.0) out->confidence = std::min(100.0,
                                               (best_value / mean) * 8.0);
    return AG_OK;
}

} // namespace

ag_result analyze_bpm(const BpmAnalyzeInput& input, BpmAnalyzeOutput* out)
{
    if (out == nullptr || input.file_path == nullptr || input.file_path[0] == '\0') {
        return AG_INVALID_ARGUMENT;
    }
    if (input.max_duration_seconds <= 0) {
        return AG_INVALID_ARGUMENT;
    }
    *out = {0.0, 0.0};

    Decoder decoder;
    const ag_result open_result = decoder.open(input.file_path, kTargetSampleRate, 1);
    if (open_result != AG_OK) {
        return open_result;
    }
    if (input.cancelled && input.cancelled->load(std::memory_order_relaxed)) {
        return AG_CANCELLED;
    }

    std::vector<float> pcm;
    pcm.reserve(static_cast<std::size_t>(input.max_duration_seconds) * kTargetSampleRate);

    DecodedAudioBlock block;
    const std::int64_t max_frames = static_cast<std::int64_t>(input.max_duration_seconds)
                                    * kTargetSampleRate;
    while (decoder.read(block) == AG_OK) {
        if (input.cancelled && input.cancelled->load(std::memory_order_relaxed)) {
            return AG_CANCELLED;
        }
        if (block.end_of_stream) {
            break;
        }
        for (float sample : block.samples) {
            if (!std::isfinite(sample)) {
                return AG_DECODE_ERROR;
            }
            pcm.push_back(sample);
            if (static_cast<std::int64_t>(pcm.size()) >= max_frames) {
                break;
            }
        }
        if (static_cast<std::int64_t>(pcm.size()) >= max_frames) {
            break;
        }
    }

    return analyze_pcm(pcm, input.cancelled, out);
}

ag_result analyze_bpm(IAudioStreamSource& stream,
                      const int max_duration_seconds,
                      const std::atomic_bool* cancelled,
                      BpmAnalyzeOutput* out)
{
    const MediaMetadata& metadata = stream.metadata();
    if (out == nullptr || max_duration_seconds <= 0
        || metadata.sample_rate <= 0 || metadata.channels <= 0) {
        return AG_INVALID_ARGUMENT;
    }
    *out = {};
    std::vector<float> pcm;
    pcm.reserve(static_cast<std::size_t>(max_duration_seconds)
                * kTargetSampleRate);
    const std::size_t maximum = static_cast<std::size_t>(max_duration_seconds)
        * kTargetSampleRate;
    const double sourceStep = static_cast<double>(metadata.sample_rate)
        / kTargetSampleRate;
    double nextSourceFrame = 0.0;
    std::uint64_t sourceFrame = 0;
    DecodedAudioBlock block;
    while (pcm.size() < maximum) {
        if (cancelled != nullptr
            && cancelled->load(std::memory_order_relaxed)) return AG_CANCELLED;
        const ag_result result = stream.read(block);
        if (result != AG_OK) return result;
        for (std::size_t frame = 0; frame < block.frames
             && pcm.size() < maximum; ++frame, ++sourceFrame) {
            while (nextSourceFrame <= static_cast<double>(sourceFrame)
                   && pcm.size() < maximum) {
                double mono = 0.0;
                for (int channel = 0; channel < metadata.channels; ++channel) {
                    mono += block.samples[
                        frame * static_cast<std::size_t>(metadata.channels)
                        + static_cast<std::size_t>(channel)];
                }
                mono /= metadata.channels;
                if (!std::isfinite(mono)) return AG_DECODE_ERROR;
                pcm.push_back(static_cast<float>(mono));
                nextSourceFrame += sourceStep;
            }
        }
        if (block.end_of_stream) break;
    }
    return analyze_pcm(pcm, cancelled, out);
}

} // namespace agplayer

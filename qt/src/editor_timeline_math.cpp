#include "editor_timeline_math.hpp"

#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

BeatGridEstimate estimateBeatGrid(const QVariantList& peaks,
                                 const qint64 durationMs, const double bpm)
{
    if (peaks.size() < 2 || durationMs <= 0 || !std::isfinite(bpm)
        || bpm < 20.0 || bpm > 400.0) return {};
    // Waveform buckets cover intervals, not inclusive endpoint samples.
    const double bucketMs = double(durationMs) / peaks.size();
    if (bucketMs > 10.0) return {};
    const auto count = std::min(peaks.size(), qsizetype(120000.0 / bucketMs));
    struct Onset { double time; double weight; };
    std::vector<Onset> onsets;
    double maximum = 0.0;
    for (qsizetype i = 0; i < count; ++i) {
        const double level = std::abs(peaks[i].toDouble());
        if (!std::isfinite(level)) return {};
        maximum = std::max(maximum, level);
    }
    if (maximum < 0.01) return {};
    double previous = 0.0;
    for (qsizetype i = 0; i < count; ++i) {
        const double level = std::abs(peaks[i].toDouble());
        const double rise = level - previous;
        previous = level;
        if (rise < maximum * 0.08) continue;
        const Onset onset{double(i) * bucketMs, rise};
        // One attack can occupy several buckets. Keep its strongest rise.
        if (!onsets.empty() && onset.time - onsets.back().time < 40.0) {
            if (rise > onsets.back().weight) onsets.back() = onset;
        } else {
            onsets.push_back(onset);
        }
    }
    if (onsets.size() < 6) return {};

    constexpr int bins = 256;
    double totalWeight = 0.0;
    for (const auto& onset : onsets) totalWeight += onset.weight;
    double bestScore = -1.0, period = 0.0, phase = 0.0;
    // Integer BPM tags drift over a song. A narrow search corrects rounding
    // without reinterpreting the user's tempo as half/double time.
    for (int step = -20; step <= 20; ++step) {
        const double candidate = 60000.0 / (bpm * (1.0 + step * 0.0005));
        std::array<double, bins> histogram{};
        const int radius = std::max(1, int(std::ceil(20.0 * bins / candidate)));
        for (const auto& onset : onsets) {
            const double bin = std::fmod(onset.time, candidate) * bins / candidate;
            const int center = int(std::floor(bin));
            for (int delta = -radius; delta <= radius + 1; ++delta) {
                const double distanceMs = std::abs(center + delta - bin) * candidate / bins;
                if (distanceMs >= 20.0) continue;
                histogram[(center + delta + bins) % bins] +=
                    onset.weight * (1.0 - distanceMs / 20.0);
            }
        }
        const auto best = std::max_element(histogram.begin(), histogram.end());
        const double score = *best * (1.0 - std::abs(step) * 0.00001);
        if (score > bestScore) {
            bestScore = score;
            period = candidate;
            phase = double(best - histogram.begin()) * candidate / bins;
        }
    }
    if (bestScore < totalWeight * 0.30) return {};

    // Regress source timestamps against integer beat indices, preserving
    // fractional BPM instead of accumulating an integer millisecond period.
    for (int iteration = 0; iteration < 2; ++iteration) {
        double weight = 0, sumN = 0, sumT = 0, sumNN = 0, sumNT = 0;
        int matched = 0;
        for (const auto& onset : onsets) {
            const double n = std::round((onset.time - phase) / period);
            if (std::abs(onset.time - phase - n * period) > 25.0) continue;
            weight += onset.weight;
            sumN += onset.weight * n;
            sumT += onset.weight * onset.time;
            sumNN += onset.weight * n * n;
            sumNT += onset.weight * n * onset.time;
            ++matched;
        }
        const double denominator = weight * sumNN - sumN * sumN;
        if (matched < 6 || weight < totalWeight * 0.35 || denominator <= 0) return {};
        period = (weight * sumNT - sumN * sumT) / denominator;
        phase = (sumT - period * sumN) / weight;
    }
    const double fittedBpm = 60000.0 / period;
    if (!std::isfinite(fittedBpm) || fittedBpm < 20 || fittedBpm > 400
        || std::abs(fittedBpm / bpm - 1.0) > 0.012) return {};
    double first = -1, last = -1, error = 0, weight = 0;
    for (const auto& onset : onsets) {
        const double predicted = phase + std::round((onset.time - phase) / period) * period;
        const double residual = onset.time - predicted;
        if (std::abs(residual) > 25.0) continue;
        if (first < 0) first = predicted;
        last = predicted;
        error += onset.weight * residual * residual;
        weight += onset.weight;
    }
    if (weight <= 0 || last - first < period * 4
        || std::sqrt(error / weight) > std::min(20.0, period * 0.04)) return {};
    return {fittedBpm, qRound64(std::max(0.0, first)), true};
}

qint64 beatGridMs(double bpm, int denominator)
{
    if (bpm < 40.0 || bpm > 300.0 || denominator <= 0) {
        return 0;
    }
    return qRound64(240000.0 / (bpm * denominator));
}

qint64 snapMs(qint64 value, double bpm, int denominator)
{
    const qint64 grid = beatGridMs(bpm, denominator);
    if (grid <= 0) {
        return std::max<qint64>(0, value);
    }
    return std::max<qint64>(0, qRound64(static_cast<double>(value) / grid) * grid);
}

qint64 estimateFirstBeatOffsetMs(const QVariantList& peaks,
                                 qint64 durationMs,
                                 double bpm)
{
    if (peaks.size() < 2 || durationMs <= 0 || beatGridMs(bpm, 4) <= 0) {
        return 0;
    }

    double maximum = 0.0;
    for (const QVariant& peak : peaks) {
        maximum = std::max(maximum, std::abs(peak.toDouble()));
    }
    if (maximum < 0.05) {
        return 0;
    }

    const double levelThreshold = maximum * 0.35;
    const double riseThreshold = maximum * 0.15;
    double previous = std::abs(peaks.front().toDouble());
    if (previous >= levelThreshold) {
        return 0;
    }

    for (int index = 1; index < peaks.size(); ++index) {
        const double current = std::abs(peaks.at(index).toDouble());
        if (current >= levelThreshold
            && current - previous >= riseThreshold) {
            return qRound64(
                static_cast<double>(index) * durationMs
                / static_cast<double>(peaks.size() - 1));
        }
        previous = current;
    }
    return 0;
}

ClipBounds normalizeClip(ClipBounds clip, qint64 sourceDurationMs)
{
    constexpr qint64 minimumClipMs = 200;
    const qint64 duration = std::max<qint64>(0, sourceDurationMs);

    clip.timelineStartMs = std::max<qint64>(0, clip.timelineStartMs);
    clip.inMs = std::clamp<qint64>(clip.inMs, 0, duration);
    clip.outMs = std::clamp<qint64>(clip.outMs, 0, duration);

    if (duration <= minimumClipMs) {
        clip.inMs = 0;
        clip.outMs = duration;
    } else if (clip.outMs - clip.inMs < minimumClipMs) {
        clip.outMs = std::min(duration, std::max(clip.outMs, clip.inMs + minimumClipMs));
        clip.inMs = std::max<qint64>(0, clip.outMs - minimumClipMs);
    }

    return clip;
}

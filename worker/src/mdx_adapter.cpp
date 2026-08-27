#include "mdx_adapter.hpp"

extern "C" {
#include <libavutil/tx.h>
}

#include <algorithm>
#include <cmath>
#include <limits>

namespace agplayer::separation {
namespace {

constexpr double kPi = 3.14159265358979323846;

qsizetype spectrumIndex(int channel, int bin, int frame, int bins, int frames)
{
    return (static_cast<qsizetype>(channel) * bins + bin) * frames + frame;
}

float chunkWeight(qint64 localFrame, qint64 chunkFrames, bool first, bool last,
                  qint64 fadeIn, qint64 fadeOut)
{
    float weight = 1.0F;
    if (!first && fadeIn > 1 && localFrame < fadeIn) {
        weight = std::min(weight, static_cast<float>(localFrame)
                                     / static_cast<float>(fadeIn - 1));
    }
    if (!last && fadeOut > 1 && localFrame >= chunkFrames - fadeOut) {
        weight = std::min(weight,
                          static_cast<float>(chunkFrames - 1 - localFrame)
                              / static_cast<float>(fadeOut - 1));
    }
    return std::max(0.0F, weight);
}

} // namespace

MdxProfile MdxProfile::kara()
{
    MdxProfile profile;
    profile.frequencyBins = 2048;
    profile.primaryStem = QStringLiteral("vocals");
    profile.compensation = 1.035F;
    return profile;
}

MdxProfile MdxProfile::hq3()
{
    MdxProfile profile;
    profile.frequencyBins = 3072;
    profile.primaryStem = QStringLiteral("instrumental");
    profile.compensation = 1.022F;
    return profile;
}

float MdxSpectrogram::at(int channel, int bin, int frame) const
{
    return values.at(spectrumIndex(channel, bin, frame, bins, frames));
}

QVector<float> periodicHann(int size)
{
    QVector<float> window(std::max(0, size));
    for (int index = 0; index < size; ++index) {
        window[index] = static_cast<float>(
            0.5 - 0.5 * std::cos(2.0 * kPi * index / size));
    }
    return window;
}

MdxSpectrogram mdxStftPack(const QVector<float>& interleavedStereo,
                           const MdxProfile& profile)
{
    MdxSpectrogram result;
    result.shape = {1, 4, profile.frequencyBins, profile.frames};
    result.bins = profile.frequencyBins;
    result.frames = profile.frames;
    result.values.resize(4 * profile.frequencyBins * profile.frames);

    AVTXContext* context = nullptr;
    av_tx_fn transform = nullptr;
    if (av_tx_init(&context, &transform, AV_TX_FLOAT_RDFT, 0,
                   profile.fftSize, nullptr, AV_TX_UNALIGNED) < 0) {
        result.values.clear();
        return result;
    }
    const QVector<float> window = periodicHann(profile.fftSize);
    QVector<float> input(profile.fftSize);
    QVector<AVComplexFloat> frequency(profile.fftSize / 2 + 1);
    const int sourceFrames = static_cast<int>(interleavedStereo.size() / 2);
    for (int frame = 0; frame < profile.frames; ++frame) {
        const int sourceStart = frame * profile.hopSize - profile.trimSamples;
        for (int channel = 0; channel < 2; ++channel) {
            for (int sample = 0; sample < profile.fftSize; ++sample) {
                const int source = sourceStart + sample;
                input[sample] = (source >= 0 && source < sourceFrames)
                    ? interleavedStereo.at(source * 2 + channel) * window.at(sample)
                    : 0.0F;
            }
            transform(context, frequency.data(), input.data(), sizeof(float));
            for (int bin = 0; bin < profile.frequencyBins; ++bin) {
                const float real = bin < 3 ? 0.0F : frequency.at(bin).re;
                const float imaginary = bin < 3 ? 0.0F : frequency.at(bin).im;
                result.values[spectrumIndex(channel * 2, bin, frame,
                                            profile.frequencyBins,
                                            profile.frames)] = real;
                result.values[spectrumIndex(channel * 2 + 1, bin, frame,
                                            profile.frequencyBins,
                                            profile.frames)] = imaginary;
            }
        }
    }
    av_tx_uninit(&context);
    return result;
}

QVector<float> mdxIstftUnpack(const MdxSpectrogram& spectrum,
                              const MdxProfile& profile, int outputFrames)
{
    if (spectrum.values.size() != 4 * profile.frequencyBins * profile.frames
        || outputFrames < 0) {
        return {};
    }
    const float scale = 1.0F / profile.fftSize;
    AVTXContext* context = nullptr;
    av_tx_fn transform = nullptr;
    if (av_tx_init(&context, &transform, AV_TX_FLOAT_RDFT, 1,
                   profile.fftSize, &scale, AV_TX_UNALIGNED) < 0) {
        return {};
    }
    const QVector<float> window = periodicHann(profile.fftSize);
    const int paddedFrames = outputFrames + profile.trimSamples * 2;
    QVector<float> accumulated(paddedFrames * 2);
    QVector<float> normalization(paddedFrames);
    QVector<AVComplexFloat> frequency(profile.fftSize / 2 + 1);
    QVector<float> output(profile.fftSize);
    for (int frame = 0; frame < profile.frames; ++frame) {
        const int destinationStart = frame * profile.hopSize;
        for (int channel = 0; channel < 2; ++channel) {
            std::fill(frequency.begin(), frequency.end(), AVComplexFloat{});
            for (int bin = 0; bin < profile.frequencyBins; ++bin) {
                frequency[bin].re = spectrum.at(channel * 2, bin, frame);
                frequency[bin].im = spectrum.at(channel * 2 + 1, bin, frame);
            }
            transform(context, output.data(), frequency.data(),
                      sizeof(AVComplexFloat));
            for (int sample = 0; sample < profile.fftSize; ++sample) {
                const int destination = destinationStart + sample;
                if (destination >= paddedFrames) break;
                accumulated[destination * 2 + channel] +=
                    output.at(sample) * window.at(sample);
            }
        }
        for (int sample = 0; sample < profile.fftSize; ++sample) {
            const int destination = destinationStart + sample;
            if (destination >= paddedFrames) break;
            normalization[destination] += window.at(sample) * window.at(sample);
        }
    }
    av_tx_uninit(&context);

    QVector<float> result(outputFrames * 2);
    for (int frame = 0; frame < outputFrames; ++frame) {
        const int padded = frame + profile.trimSamples;
        const float divisor = normalization.at(padded);
        if (divisor > std::numeric_limits<float>::epsilon()) {
            result[frame * 2] = accumulated.at(padded * 2) / divisor;
            result[frame * 2 + 1] = accumulated.at(padded * 2 + 1) / divisor;
        }
    }
    return result;
}

QVector<qint64> mdxChunkStarts(qint64 totalFrames)
{
    constexpr qint64 chunk = 261120;
    constexpr qint64 stride = 195840;
    if (totalFrames <= 0) return {};
    QVector<qint64> starts{0};
    while (starts.back() + chunk < totalFrames) {
        starts.push_back(starts.back() + stride);
    }
    return starts;
}

QVector<float> mdxWeightedOverlapAdd(const QVector<PositionedAudioChunk>& chunks,
                                     qint64 totalFrames)
{
    if (totalFrames <= 0) return {};
    QVector<float> output(totalFrames * 2);
    QVector<float> weights(totalFrames);
    for (qsizetype index = 0; index < chunks.size(); ++index) {
        const PositionedAudioChunk& chunk = chunks.at(index);
        const qint64 chunkFrames = chunk.interleavedStereo.size() / 2;
        const qint64 fadeIn = index == 0
            ? 0 : std::max<qint64>(0, chunks.at(index - 1).startFrame
                                         + chunks.at(index - 1).interleavedStereo.size() / 2
                                         - chunk.startFrame);
        const qint64 fadeOut = index + 1 >= chunks.size()
            ? 0 : std::max<qint64>(0, chunk.startFrame + chunkFrames
                                         - chunks.at(index + 1).startFrame);
        for (qint64 frame = 0; frame < chunkFrames; ++frame) {
            const qint64 destination = chunk.startFrame + frame;
            if (destination < 0 || destination >= totalFrames) continue;
            const float weight = chunkWeight(frame, chunkFrames, index == 0,
                                             index + 1 == chunks.size(),
                                             fadeIn, fadeOut);
            output[destination * 2] += chunk.interleavedStereo.at(frame * 2) * weight;
            output[destination * 2 + 1] += chunk.interleavedStereo.at(frame * 2 + 1) * weight;
            weights[destination] += weight;
        }
    }
    for (qint64 frame = 0; frame < totalFrames; ++frame) {
        if (weights.at(frame) > 0.0F) {
            output[frame * 2] /= weights.at(frame);
            output[frame * 2 + 1] /= weights.at(frame);
        }
    }
    return output;
}

QVector<float> complementaryStem(const QVector<float>& mix,
                                 const QVector<float>& primary)
{
    if (mix.size() != primary.size()) return {};
    QVector<float> result(mix.size());
    for (qsizetype index = 0; index < mix.size(); ++index) {
        result[index] = mix.at(index) - primary.at(index);
    }
    return result;
}

} // namespace agplayer::separation

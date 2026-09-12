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

MdxProfile MdxProfile::forModel(const QString& profileId)
{
    if (profileId == QStringLiteral("uvr-mdxnet-kara")) return kara();
    MdxProfile profile = hq3();
    // Parameters verified against the UVR publisher's hash-indexed model_data:
    // https://github.com/TRvlvr/application_data/blob/main/mdx_model_data/model_data.json
    if (profileId == QStringLiteral("uvr-mdx-net-inst-hq1")) {
        profile.compensation = 1.035F;
    } else if (profileId == QStringLiteral("kim-vocal-2")
               || profileId == QStringLiteral("uvr-mdx-net-voc-ft")) {
        profile.fftSize = 7680;
        profile.trimSamples = profile.fftSize / 2;
        profile.primaryStem = QStringLiteral("vocals");
        profile.compensation = profileId == QStringLiteral("kim-vocal-2")
            ? 1.009F : 1.021F;
    }
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

QVector<float> overlapWeights(const QVector<qint64>& starts, qsizetype index,
                              int chunkFrames)
{
    QVector<float> weights(chunkFrames, 1.0F);
    if (index > 0) {
        const qint64 overlap = starts.at(index - 1) + chunkFrames - starts.at(index);
        for (qint64 sample = 0; sample < overlap; ++sample) {
            weights[static_cast<qsizetype>(sample)] = overlap > 1
                ? static_cast<float>(sample) / static_cast<float>(overlap - 1)
                : 1.0F;
        }
    }
    if (index + 1 < starts.size()) {
        const qint64 overlap = starts.at(index) + chunkFrames - starts.at(index + 1);
        for (qint64 sample = 0; sample < overlap; ++sample) {
            const qsizetype position = static_cast<qsizetype>(chunkFrames - overlap + sample);
            const float fade = overlap > 1
                ? 1.0F - static_cast<float>(sample) / static_cast<float>(overlap - 1)
                : 1.0F;
            weights[position] = std::min(weights.at(position), fade);
        }
    }
    return weights;
}

StreamingOverlapAdd::StreamingOverlapAdd(Sink sink) : sink_(std::move(sink)) {}

bool StreamingOverlapAdd::add(qint64 start, const QVector<float>& samples,
                              const QVector<float>& weights)
{
    if (!takeBefore(start)) return false;
    const qint64 frames = samples.size() / 2;
    if (weights.size() != frames || start < base_) return false;
    const qint64 offset = start - base_;
    const qint64 required = offset + frames;
    if (accumulated_.size() < required * 2) accumulated_.resize(required * 2);
    if (normalization_.size() < required) normalization_.resize(required);
    for (qint64 frame = 0; frame < frames; ++frame) {
        const qint64 destination = offset + frame;
        const float weight = weights.at(static_cast<qsizetype>(frame));
        accumulated_[destination * 2] += samples.at(frame * 2) * weight;
        accumulated_[destination * 2 + 1] += samples.at(frame * 2 + 1) * weight;
        normalization_[destination] += weight;
    }
    return true;
}

bool StreamingOverlapAdd::finish(qint64 totalFrames)
{
    return takeBefore(totalFrames);
}

bool StreamingOverlapAdd::takeBefore(qint64 end)
{
    const qint64 frames = std::clamp<qint64>(end - base_, 0,
                                             normalization_.size());
    if (frames == 0) return true;
    QVector<float> published(frames * 2);
    for (qint64 frame = 0; frame < frames; ++frame) {
        const float weight = normalization_.at(frame);
        if (weight > 0.0F) {
            published[frame * 2] = accumulated_.at(frame * 2) / weight;
            published[frame * 2 + 1] = accumulated_.at(frame * 2 + 1) / weight;
        }
    }
    if (!sink_(published)) return false;
    accumulated_.remove(0, static_cast<qsizetype>(frames * 2));
    normalization_.remove(0, static_cast<qsizetype>(frames));
    base_ += frames;
    return true;
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

#pragma once

#include <QString>
#include <QVector>

namespace agplayer::separation {

struct MdxProfile {
    int sampleRate = 44100;
    int fftSize = 6144;
    int hopSize = 1024;
    int frequencyBins = 0;
    int frames = 256;
    int chunkSamples = 261120;
    int trimSamples = 3072;
    int strideSamples = 195840;
    QString primaryStem;
    float compensation = 1.0F;

    [[nodiscard]] static MdxProfile kara();
    [[nodiscard]] static MdxProfile hq3();
};

struct MdxSpectrogram {
    QVector<qint64> shape;
    QVector<float> values;
    int bins = 0;
    int frames = 0;

    [[nodiscard]] float at(int channel, int bin, int frame) const;
};

struct PositionedAudioChunk {
    qint64 startFrame = 0;
    QVector<float> interleavedStereo;
};

[[nodiscard]] QVector<float> periodicHann(int size);
[[nodiscard]] MdxSpectrogram mdxStftPack(const QVector<float>& interleavedStereo,
                                        const MdxProfile& profile);
[[nodiscard]] QVector<float> mdxIstftUnpack(const MdxSpectrogram& spectrum,
                                            const MdxProfile& profile,
                                            int outputFrames);
[[nodiscard]] QVector<qint64> mdxChunkStarts(qint64 totalFrames);
[[nodiscard]] QVector<float>
mdxWeightedOverlapAdd(const QVector<PositionedAudioChunk>& chunks,
                      qint64 totalFrames);
[[nodiscard]] QVector<float> complementaryStem(const QVector<float>& mix,
                                               const QVector<float>& primary);

} // namespace agplayer::separation

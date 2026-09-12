#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace agplayer::separation {

struct DemucsProfile {
    QVector<qint64> inputShape;
    QVector<qint64> outputShape;
    int chunkSamples = 343980;
    int overlapSamples = 85995;
    int strideSamples = 257985;
    QStringList rows;

    [[nodiscard]] static DemucsProfile trusted();
};

[[nodiscard]] QVector<qint64> demucsChunkStarts(qint64 totalFrames);
[[nodiscard]] QVector<float> demucsPublisherWeights(int chunkIndex,
                                                    int chunkCount);
[[nodiscard]] QVector<float> selectDemucsRow(const QVector<float>& modelOutput,
                                             int row);
[[nodiscard]] float accompanimentPeak(const QVector<float>& drums,
                                      const QVector<float>& bass,
                                      const QVector<float>& other);
[[nodiscard]] QVector<float> sumAccompaniment(const QVector<float>& drums,
                                              const QVector<float>& bass,
                                              const QVector<float>& other,
                                              float scale);

} // namespace agplayer::separation

#include "demucs_adapter.hpp"

#include <algorithm>
#include <cmath>

namespace agplayer::separation {

DemucsProfile DemucsProfile::trusted()
{
    return {{1, 2, 343980}, {1, 4, 2, 343980}, 343980, 85995, 257985,
            {QStringLiteral("drums"), QStringLiteral("bass"),
             QStringLiteral("other"), QStringLiteral("vocals")}};
}

QVector<qint64> demucsChunkStarts(qint64 totalFrames)
{
    constexpr qint64 chunk = 343980;
    constexpr qint64 stride = 257985;
    if (totalFrames <= 0) return {};
    QVector<qint64> starts{0};
    while (starts.back() + chunk < totalFrames) {
        starts.push_back(starts.back() + stride);
    }
    return starts;
}

QVector<float> demucsPublisherWeights(int chunkIndex, int chunkCount)
{
    constexpr int chunk = 343980;
    constexpr int overlap = 85995;
    QVector<float> weights(chunk, 1.0F);
    if (chunkIndex > 0) {
        for (int index = 0; index < overlap; ++index) {
            weights[index] = static_cast<float>(index)
                / static_cast<float>(overlap - 1);
        }
    }
    if (chunkIndex + 1 < chunkCount) {
        for (int index = 0; index < overlap; ++index) {
            weights[chunk - overlap + index] = std::min(
                weights.at(chunk - overlap + index),
                1.0F - static_cast<float>(index)
                    / static_cast<float>(overlap - 1));
        }
    }
    return weights;
}

int demucsRowForModelFile(const QString& fileName)
{
    const QString name = fileName.toLower();
    if (name.contains(QStringLiteral("drums"))) return 0;
    if (name.contains(QStringLiteral("bass"))) return 1;
    if (name.contains(QStringLiteral("other"))) return 2;
    if (name.contains(QStringLiteral("vocals"))) return 3;
    return -1;
}

QVector<float> selectDemucsRow(const QVector<float>& modelOutput, int row)
{
    constexpr int rowSamples = 2 * 343980;
    if (row < 0 || row >= 4 || modelOutput.size() != 4 * rowSamples) return {};
    return QVector<float>(modelOutput.cbegin() + row * rowSamples,
                          modelOutput.cbegin() + (row + 1) * rowSamples);
}

QVector<float> deriveAccompaniment(const QVector<float>& drums,
                                   const QVector<float>& bass,
                                   const QVector<float>& other)
{
    if (drums.size() != bass.size() || drums.size() != other.size()) return {};
    QVector<float> output(drums.size());
    float peak = 0.0F;
    for (qsizetype index = 0; index < output.size(); ++index) {
        output[index] = drums.at(index) + bass.at(index) + other.at(index);
        peak = std::max(peak, std::abs(output.at(index)));
    }
    if (peak > 1.0F) {
        for (float& sample : output) sample /= peak;
    }
    return output;
}

} // namespace agplayer::separation

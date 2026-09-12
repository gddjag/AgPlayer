#include "demucs_adapter.hpp"
#include "mdx_adapter.hpp"

#include <QTest>

#include <algorithm>
#include <cmath>

using namespace agplayer::separation;

class SeparationDspTest final : public QObject {
    Q_OBJECT

private slots:
    void periodicHannAndMdxGeometryAreExact();
    void mdxPackingClearsLowBinsAndRoundTripsLowFrequencyAudio();
    void mdxChunkingUsesTwentyFivePercentOverlapAndWeightedOla();
    void demucsGeometryFadesAndNamedRowsAreExact();
    void derivedAccompanimentUsesOnlyThreeRowsAndProtectsFromClipping();
};

void SeparationDspTest::periodicHannAndMdxGeometryAreExact()
{
    const QVector<float> window = periodicHann(6144);
    QCOMPARE(window.size(), 6144);
    QVERIFY(std::abs(window.at(0)) < 1.0e-7F);
    QCOMPARE(window.at(3072), 1.0F);
    QVERIFY(window.at(6143) > 0.0F);

    QCOMPARE(MdxProfile::kara().sampleRate, 44100);
    QCOMPARE(MdxProfile::kara().fftSize, 6144);
    QCOMPARE(MdxProfile::kara().hopSize, 1024);
    QCOMPARE(MdxProfile::kara().frequencyBins, 2048);
    QCOMPARE(MdxProfile::kara().frames, 256);
    QCOMPARE(MdxProfile::kara().chunkSamples, 261120);
    QCOMPARE(MdxProfile::kara().trimSamples, 3072);
    QCOMPARE(MdxProfile::kara().strideSamples, 195840);
    QCOMPARE(MdxProfile::kara().primaryStem, QStringLiteral("vocals"));
    QCOMPARE(MdxProfile::kara().compensation, 1.035F);
    QCOMPARE(MdxProfile::hq3().frequencyBins, 3072);
    QCOMPARE(MdxProfile::hq3().primaryStem, QStringLiteral("instrumental"));
    QCOMPARE(MdxProfile::hq3().compensation, 1.022F);
    const MdxProfile kim = MdxProfile::forModel(QStringLiteral("kim-vocal-2"));
    QCOMPARE(kim.fftSize, 7680);
    QCOMPARE(kim.trimSamples, 3840);
    QCOMPARE(kim.primaryStem, QStringLiteral("vocals"));
    QCOMPARE(kim.compensation, 1.009F);
    QCOMPARE(MdxProfile::forModel(QStringLiteral("uvr-mdx-net-inst-hq1")).compensation, 1.035F);
}

void SeparationDspTest::mdxPackingClearsLowBinsAndRoundTripsLowFrequencyAudio()
{
    QVector<float> stereo(MdxProfile::hq3().chunkSamples * 2);
    for (int frame = 0; frame < MdxProfile::hq3().chunkSamples; ++frame) {
        const float value = 0.25F * std::sin(
            2.0 * 3.14159265358979323846 * 440.0 * frame / 44100.0);
        stereo[frame * 2] = value;
        stereo[frame * 2 + 1] = -value;
    }

    const MdxSpectrogram spectrum = mdxStftPack(stereo, MdxProfile::hq3());
    QCOMPARE(spectrum.shape, (QVector<qint64>{1, 4, 3072, 256}));
    QCOMPARE(spectrum.values.size(), 4 * 3072 * 256);
    for (int channel = 0; channel < 4; ++channel) {
        for (int bin = 0; bin < 3; ++bin) {
            for (int frame = 0; frame < 256; ++frame) {
                QCOMPARE(spectrum.at(channel, bin, frame), 0.0F);
            }
        }
    }

    const QVector<float> reconstructed = mdxIstftUnpack(
        spectrum, MdxProfile::hq3(), MdxProfile::hq3().chunkSamples);
    QCOMPARE(reconstructed.size(), stereo.size());
    double error = 0.0;
    double signal = 0.0;
    for (qsizetype i = 0; i < stereo.size(); ++i) {
        const double delta = reconstructed.at(i) - stereo.at(i);
        error += delta * delta;
        signal += stereo.at(i) * stereo.at(i);
    }
    QVERIFY2(error / signal < 1.0e-4, "STFT/ISTFT changed a low-frequency signal");
}

void SeparationDspTest::mdxChunkingUsesTwentyFivePercentOverlapAndWeightedOla()
{
    QCOMPARE(mdxChunkStarts(261120), (QVector<qint64>{0}));
    QCOMPARE(mdxChunkStarts(456960), (QVector<qint64>{0, 195840}));
    QCOMPARE(mdxChunkStarts(500000), (QVector<qint64>{0, 195840, 391680}));

    const QVector<float> first(261120 * 2, 0.25F);
    const QVector<float> second(261120 * 2, 0.75F);
    QVector<float> joined;
    StreamingOverlapAdd publisher(
        [&](const QVector<float>& block) {
            joined += block;
            return true;
        });
    const QVector<qint64> starts{0, 195840};
    QVERIFY(publisher.add(0, first, overlapWeights(starts, 0, 261120)));
    QVERIFY(publisher.add(195840, second,
                          overlapWeights(starts, 1, 261120)));
    QVERIFY(publisher.finish(456960));
    QCOMPARE(joined.size(), 456960 * 2);
    QCOMPARE(joined.at(1000 * 2), 0.25F);
    QVERIFY(joined.at(228480 * 2) > 0.49F && joined.at(228480 * 2) < 0.51F);
    QCOMPARE(joined.at(400000 * 2), 0.75F);
}

void SeparationDspTest::demucsGeometryFadesAndNamedRowsAreExact()
{
    const DemucsProfile profile = DemucsProfile::trusted();
    QCOMPARE(profile.inputShape, (QVector<qint64>{1, 2, 343980}));
    QCOMPARE(profile.outputShape, (QVector<qint64>{1, 4, 2, 343980}));
    QCOMPARE(profile.overlapSamples, 85995);
    QCOMPARE(profile.strideSamples, 257985);
    QCOMPARE(profile.rows, (QStringList{QStringLiteral("drums"),
                                        QStringLiteral("bass"),
                                        QStringLiteral("other"),
                                        QStringLiteral("vocals")}));
    QCOMPARE(demucsChunkStarts(601965), (QVector<qint64>{0, 257985}));
    QCOMPARE(demucsChunkStarts(700000),
             (QVector<qint64>{0, 257985, 515970}));

    const QVector<float> first = demucsPublisherWeights(0, 2);
    const QVector<float> second = demucsPublisherWeights(1, 2);
    QCOMPARE(first.size(), 343980);
    QCOMPARE(second.size(), 343980);
    QCOMPARE(first.at(0), 1.0F);
    QCOMPARE(second.at(343979), 1.0F);
    QVERIFY(std::abs(first.at(300982) + second.at(42997) - 1.0F) < 2.0e-5F);

    QVector<float> output(4 * 2 * 343980);
    for (int row = 0; row < 4; ++row) {
        std::fill(output.begin() + row * 2 * 343980,
                  output.begin() + (row + 1) * 2 * 343980,
                  static_cast<float>(row + 1));
    }
    const QVector<float> vocals = selectDemucsRow(output, 3);
    QCOMPARE(vocals.size(), 2 * 343980);
    QCOMPARE(vocals.front(), 4.0F);
    QCOMPARE(vocals.back(), 4.0F);
}

void SeparationDspTest::derivedAccompanimentUsesOnlyThreeRowsAndProtectsFromClipping()
{
    const QVector<float> drums{0.5F, -0.7F, 0.2F};
    const QVector<float> bass{0.5F, -0.4F, 0.2F};
    const QVector<float> other{0.5F, -0.1F, 0.2F};
    const float peak = accompanimentPeak(drums, bass, other);
    QCOMPARE(peak, 1.5F);
    const QVector<float> accompaniment = sumAccompaniment(
        drums, bass, other, 1.0F / peak);
    QCOMPARE(accompaniment.size(), 3);
    QCOMPARE(accompaniment.at(0), 1.0F);
    QCOMPARE(accompaniment.at(1), -0.8F);
    QCOMPARE(accompaniment.at(2), 0.4F);
}

QTEST_GUILESS_MAIN(SeparationDspTest)
#include "separation_dsp_test.moc"

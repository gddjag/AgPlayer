#include "audio_visual_feature_controller.hpp"

#include <QSignalSpy>
#include <QTest>

class AudioVisualFeatureControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void reliableBpmEmitsOnceEveryEightBeats();
    void seeksAndTrackChangesDoNotEmitDuplicateImpacts();
    void unreliableTimingUsesDebouncedTransientFallbackOnly();
};

namespace {
QVariantList spectrum(double value, bool lowOnly = false)
{
    QVariantList values;
    values.reserve(128);
    for (int index = 0; index < 128; ++index) {
        values.append(!lowOnly || index < 32 ? value : 0.0);
    }
    return values;
}
}

void AudioVisualFeatureControllerTest::reliableBpmEmitsOnceEveryEightBeats()
{
    AudioVisualFeatureController features;
    QSignalSpy impactSpy(&features, &AudioVisualFeatureController::featuresChanged);
    QVERIFY(impactSpy.isValid());
    features.setActive(true);
    features.setWaveformTiming(QStringLiteral("track-a"), 120.0, 120000,
                               QVariantList{0.2, 0.6, 0.4});
    QVERIFY(features.beatReliable());

    features.processPlaybackPosition(0);
    features.processPlaybackPosition(3999);
    QCOMPARE(features.impactRevision(), 0);
    features.processPlaybackPosition(4000);
    QCOMPARE(features.impactRevision(), 1);
    features.processPlaybackPosition(4017);
    QCOMPARE(features.impactRevision(), 1);
    features.processPlaybackPosition(7999);
    features.processPlaybackPosition(8000);
    QCOMPARE(features.impactRevision(), 2);
    QCOMPARE(impactSpy.count(), 2);
    QVERIFY(features.impactStrength() > 0.0);
}

void AudioVisualFeatureControllerTest::seeksAndTrackChangesDoNotEmitDuplicateImpacts()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.setWaveformTiming(QStringLiteral("track-a"), 120.0, 120000,
                               QVariantList{0.2, 0.6, 0.4});
    features.processPlaybackPosition(0);
    features.processPlaybackPosition(3999);
    features.processPlaybackPosition(4000);
    QCOMPARE(features.impactRevision(), 1);

    features.processPlaybackPosition(20000);
    QCOMPARE(features.impactRevision(), 1);
    features.processPlaybackPosition(4000);
    QCOMPARE(features.impactRevision(), 1);

    features.setWaveformTiming(QStringLiteral("track-b"), 120.0, 120000,
                               QVariantList{0.4, 0.3});
    features.processPlaybackPosition(4000);
    QCOMPARE(features.impactRevision(), 1);

    features.setWaveformTiming(QStringLiteral("track-c"), 120.0, 120000,
                               QVariantList{0.4, 0.3});
    features.processPlaybackPosition(3200);
    features.processPlaybackPosition(4000);
    QCOMPARE(features.impactRevision(), 1);
}

void AudioVisualFeatureControllerTest::unreliableTimingUsesDebouncedTransientFallbackOnly()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.setWaveformTiming(QStringLiteral("track-a"), 120.0, 120000, {});
    QVERIFY(!features.beatReliable());

    features.processSpectrum(spectrum(0.0));
    features.processSpectrum(spectrum(0.25, true));
    QCOMPARE(features.impactRevision(), 1);

    features.processSpectrum(spectrum(0.0));
    features.processSpectrum(spectrum(0.25, true));
    QCOMPARE(features.impactRevision(), 1);
    QTest::qWait(190);
    features.processSpectrum(spectrum(0.0));
    features.processSpectrum(spectrum(0.25, true));
    QCOMPARE(features.impactRevision(), 2);

    features.setWaveformTiming(QStringLiteral("track-a"), 120.0, 120000,
                               QVariantList{0.2, 0.6, 0.4});
    features.processSpectrum(spectrum(0.0));
    QTest::qWait(190);
    features.processSpectrum(spectrum(0.25, true));
    QCOMPARE(features.impactRevision(), 2);
}

QTEST_GUILESS_MAIN(AudioVisualFeatureControllerTest)

#include "audio_visual_feature_controller_test.moc"

#include "audio_visual_feature_controller.hpp"
#include "playback_controller.hpp"

#include <agplayer/c_api.h>

#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <limits>
#include <memory>

class AudioVisualFeatureControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void reliableBpmEmitsOnceEveryEightBeats();
    void seeksAndTrackChangesDoNotEmitDuplicateImpacts();
    void unreliableTimingUsesDebouncedTransientFallbackOnly();
    void outputLevelsHaveFastAttackAndVisibleDecay();
    void outputLevelsPollCoreAtPlaybackCadence();
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

void AudioVisualFeatureControllerTest::outputLevelsHaveFastAttackAndVisibleDecay()
{
    AudioVisualFeatureController features;
    QVERIFY(features.metaObject()->indexOfProperty("leftPeak") >= 0);
    QVERIFY(features.metaObject()->indexOfProperty("rightPeak") >= 0);
    QVERIFY(features.metaObject()->indexOfProperty("leftRms") >= 0);
    QVERIFY(features.metaObject()->indexOfProperty("rightRms") >= 0);
    QSignalSpy levelsSpy(
        &features, &AudioVisualFeatureController::outputLevelsChanged);
    QVERIFY(levelsSpy.isValid());
    features.setActive(true);

    features.applyOutputLevels(0.80, 0.40, 0.50, 0.25);
    QCOMPARE(features.leftPeak(), 0.80);
    QCOMPARE(features.rightPeak(), 0.40);
    QCOMPARE(features.leftRms(), 0.50);
    QCOMPARE(features.rightRms(), 0.25);

    features.applyOutputLevels(0.0, 0.0, 0.0, 0.0);
    QVERIFY(features.leftPeak() > 0.0);
    QVERIFY(features.leftPeak() < 0.80);
    QVERIFY(features.rightRms() > 0.0);
    QVERIFY(features.rightRms() < 0.25);

    for (int tick = 0; tick < 100; ++tick) {
        features.applyOutputLevels(0.0, 0.0, 0.0, 0.0);
    }
    QCOMPARE(features.leftPeak(), 0.0);
    QCOMPARE(features.rightPeak(), 0.0);
    QCOMPARE(features.leftRms(), 0.0);
    QCOMPARE(features.rightRms(), 0.0);

    features.applyOutputLevels(
        std::numeric_limits<double>::quiet_NaN(), 2.0, -1.0, 0.4);
    QCOMPARE(features.leftPeak(), 0.0);
    QCOMPARE(features.rightPeak(), 1.0);
    QCOMPARE(features.leftRms(), 0.0);
    QCOMPARE(features.rightRms(), 0.4);
    QVERIFY(levelsSpy.count() > 0);

    features.setActive(false);
    QCOMPARE(features.leftPeak(), 0.0);
    QCOMPARE(features.rightPeak(), 0.0);
    QCOMPARE(features.leftRms(), 0.0);
    QCOMPARE(features.rightRms(), 0.0);

    PlaybackController playback;
    AudioVisualFeatureController detached(&playback);
    detached.setActive(true);
    detached.applyOutputLevels(0.6, 0.5, 0.4, 0.3);
    detached.setPlaybackController(nullptr);
    QCOMPARE(detached.leftPeak(), 0.0);
    QCOMPARE(detached.rightPeak(), 0.0);

    PlaybackController replacement;
    detached.setPlaybackController(&playback);
    detached.applyOutputLevels(0.7, 0.6, 0.5, 0.4);
    detached.setPlaybackController(&replacement);
    QCOMPARE(detached.leftPeak(), 0.0);
    QCOMPARE(detached.rightPeak(), 0.0);

    auto ownedPlayback = std::make_unique<PlaybackController>();
    AudioVisualFeatureController destroyed(ownedPlayback.get());
    destroyed.setActive(true);
    destroyed.applyOutputLevels(0.7, 0.6, 0.5, 0.4);
    QVERIFY(destroyed.outputLevelTimer_.isActive());
    ownedPlayback.reset();
    QVERIFY(destroyed.playback_.isNull());
    QVERIFY(!destroyed.outputLevelTimer_.isActive());
    QCOMPARE(destroyed.leftPeak(), 0.0);
    QCOMPARE(destroyed.rightPeak(), 0.0);
}

void AudioVisualFeatureControllerTest::outputLevelsPollCoreAtPlaybackCadence()
{
    const QByteArray fixture = qgetenv("AGPLAYER_TEST_AUDIO");
    QVERIFY2(!fixture.isEmpty(), "AGPLAYER_TEST_AUDIO is required");
    ag_player* player = nullptr;
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    QVERIFY(player != nullptr);
    {
        PlaybackController playback(player);
        AudioVisualFeatureController features(&playback);
        features.setActive(true);
        QCOMPARE(ag_player_load(player, fixture.constData()), AG_OK);
        QCOMPARE(ag_player_play(player), AG_OK);
        QTRY_VERIFY_WITH_TIMEOUT(features.leftPeak() > 0.0, 3'000);
        QTRY_VERIFY_WITH_TIMEOUT(features.rightRms() > 0.0, 3'000);

        QCOMPARE(ag_player_set_muted(player, 1), AG_OK);
        const double beforeDecay = features.leftPeak();
        QTRY_VERIFY_WITH_TIMEOUT(features.leftPeak() < beforeDecay, 500);
        QTRY_COMPARE_WITH_TIMEOUT(features.leftPeak(), 0.0, 2'000);
        QTRY_COMPARE_WITH_TIMEOUT(features.rightPeak(), 0.0, 2'000);
    }
    ag_player_destroy(player);
}

QTEST_GUILESS_MAIN(AudioVisualFeatureControllerTest)

#include "audio_visual_feature_controller_test.moc"

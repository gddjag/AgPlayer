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
    void reliableBpmEmitsRegularBeatPulseAndEightBeatImpact();
    void seeksAndTrackChangesDoNotEmitDuplicateImpacts();
    void unreliableTimingUsesDebouncedTransientFallbackOnly();
    void outputLevelsHaveFastAttackAndVisibleDecay();
    void outputLevelsPollCoreAtPlaybackCadence();
    void adaptiveTransientFloorRejectsRepeatedBackgroundPulses();
    void perceptualBandsKeepNarrowBassEnergyLocalized();
    void narrowBassTransientsTriggerKickWithoutMidrangeLeakage();
    void perceptualBandsUseFastAttackAndProgressiveRelease();
    void perceptualBandsPreserveSparseMidAndHighTones();
    void perceptualBandsReleaseWithinEightFrames();
    void silentWaveformDoesNotEnableSyntheticBeatGrid();
    void reliableBeatGridKeepsCountingWithoutLightingSilentPassages();
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
    features.processSpectrum(spectrum(0.3));
    impactSpy.clear();

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

void AudioVisualFeatureControllerTest::reliableBpmEmitsRegularBeatPulseAndEightBeatImpact()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.setWaveformTiming(QStringLiteral("track-beat"), 120.0, 120000,
                               QVariantList{0.2, 0.6, 0.4});
    features.processSpectrum(spectrum(0.3));

    features.processPlaybackPosition(0);
    features.processPlaybackPosition(499);
    QCOMPARE(features.beatRevision(), 0);
    QCOMPARE(features.impactRevision(), 0);

    features.processPlaybackPosition(500);
    QCOMPARE(features.beatRevision(), 1);
    QCOMPARE(features.impactRevision(), 0);
    QVERIFY(features.beatStrength() >= 0.35);
    QVERIFY(features.beatStrength() < 0.8);

    for (qint64 position = 1000; position <= 4000; position += 500) {
        features.processPlaybackPosition(position);
    }
    QCOMPARE(features.beatRevision(), 8);
    QCOMPARE(features.impactRevision(), 1);
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
    QCOMPARE(features.beatRevision(), 1);
    QCOMPARE(features.impactRevision(), 0);

    features.processSpectrum(spectrum(0.0));
    features.processSpectrum(spectrum(0.25, true));
    QCOMPARE(features.beatRevision(), 1);
    QCOMPARE(features.impactRevision(), 0);
    QTest::qWait(190);
    features.processSpectrum(spectrum(0.0));
    features.processSpectrum(spectrum(0.25, true));
    QCOMPARE(features.beatRevision(), 2);
    QCOMPARE(features.impactRevision(), 0);

    features.setWaveformTiming(QStringLiteral("track-a"), 120.0, 120000,
                               QVariantList{0.2, 0.6, 0.4});
    features.processSpectrum(spectrum(0.0));
    QTest::qWait(190);
    features.processSpectrum(spectrum(0.25, true));
    QCOMPARE(features.beatRevision(), 2);
    QCOMPARE(features.impactRevision(), 0);
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

void AudioVisualFeatureControllerTest::adaptiveTransientFloorRejectsRepeatedBackgroundPulses()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.setWaveformTiming(QStringLiteral("track-noisy"), 0.0, 120000, {});
    QVERIFY(!features.beatReliable());

    for (int cycle = 0; cycle < 3; ++cycle) {
        features.processSpectrum(spectrum(0.10, true));
        features.processSpectrum(spectrum(0.18, true));
        QTest::qWait(190);
    }
    QVERIFY2(features.beatRevision() <= 1,
             "A repeating low-level noise floor must not be classified as a new beat");

    const quint64 beforeAccent = features.beatRevision();
    features.processSpectrum(spectrum(0.10, true));
    features.processSpectrum(spectrum(0.82, true));
    QCOMPARE(features.beatRevision(), beforeAccent + 1);
    QCOMPARE(features.impactRevision(), 0);
}

void AudioVisualFeatureControllerTest::perceptualBandsKeepNarrowBassEnergyLocalized()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.processSpectrum(spectrum(0.0));

    QVariantList narrowBass(128, 0.0);
    for (int index = 0; index < 3; ++index) narrowBass[index] = 1.0;
    features.processSpectrum(narrowBass);

    const QVariantList bands = features.bands();
    QCOMPARE(bands.size(), 8);
    QVERIFY2(bands.at(0).toDouble() > 0.70,
             "A narrow bass transient must retain useful visual energy");
    for (int band = 1; band < bands.size(); ++band) {
        QVERIFY2(bands.at(band).toDouble() < 0.05,
                 "A narrow bass transient leaked into another visual band");
    }
    QVERIFY(features.energy() > 0.12);
}

void AudioVisualFeatureControllerTest::narrowBassTransientsTriggerKickWithoutMidrangeLeakage()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.processSpectrum(spectrum(0.0));

    QVariantList bass(128, 0.0);
    for (int index = 0; index < 3; ++index) bass[index] = 0.30;
    features.processSpectrum(bass);
    QVERIFY2(features.kickPulse(),
             "A localized audible bass onset must not be diluted by silent midrange bins");
    QCOMPARE(features.beatRevision(), 1);
    QCOMPARE(features.impactRevision(), 0);

    // A sustained bass bed plus a new midrange note is not another kick.
    QVariantList bassAndMid = bass;
    for (int index = 13; index < 22; ++index) bassAndMid[index] = 0.8;
    features.processSpectrum(bassAndMid);
    QVERIFY2(!features.kickPulse(),
             "Midrange flux must not trigger a kick over sustained bass");
}

void AudioVisualFeatureControllerTest::perceptualBandsUseFastAttackAndProgressiveRelease()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.processSpectrum(spectrum(0.0));

    QVariantList midPulse(128, 0.0);
    for (int index = 13; index < 22; ++index) midPulse[index] = 1.0;
    features.processSpectrum(midPulse);
    const double attack = features.bands().at(3).toDouble();
    QVERIFY2(attack > 0.70, "Visual bands must rise within one spectrum frame");

    features.processSpectrum(spectrum(0.0));
    const double release = features.bands().at(3).toDouble();
    QVERIFY2(release > 0.25 && release < attack,
             "Visual bands must decay instead of snapping to zero");
}

void AudioVisualFeatureControllerTest::perceptualBandsPreserveSparseMidAndHighTones()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.processSpectrum(spectrum(0.0));

    QVariantList sparseMid(128, 0.0);
    sparseMid[14] = 1.0;
    features.processSpectrum(sparseMid);
    QVERIFY2(features.bands().at(3).toDouble() > 0.20,
             "A single active mid bin must remain visible after band reduction");

    features.processSpectrum(spectrum(0.0));
    QVariantList sparseHigh(128, 0.0);
    sparseHigh[40] = 1.0;
    features.processSpectrum(sparseHigh);
    QVERIFY2(features.bands().at(5).toDouble() > 0.15,
             "A single active high bin must remain visible after band reduction");
}

void AudioVisualFeatureControllerTest::perceptualBandsReleaseWithinEightFrames()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.processSpectrum(spectrum(0.0));

    QVariantList midPulse(128, 0.0);
    for (int index = 13; index < 22; ++index) midPulse[index] = 1.0;
    features.processSpectrum(midPulse);
    const double attack = features.bands().at(3).toDouble();
    QVERIFY(attack > 0.70);

    for (int frame = 0; frame < 8; ++frame) {
        features.processSpectrum(spectrum(0.0));
    }
    QVERIFY2(features.bands().at(3).toDouble() < 0.10,
             "A stopped tone must settle within roughly eight spectrum frames");
}

void AudioVisualFeatureControllerTest::silentWaveformDoesNotEnableSyntheticBeatGrid()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.setWaveformTiming(QStringLiteral("silent-track"), 120.0, 120000,
                               QVariantList{0.0, 0.0, 0.0});

    QVERIFY(!features.beatReliable());
    features.processPlaybackPosition(0);
    features.processPlaybackPosition(4000);
    QCOMPARE(features.beatRevision(), 0);
    QCOMPARE(features.impactRevision(), 0);
}

void AudioVisualFeatureControllerTest::reliableBeatGridKeepsCountingWithoutLightingSilentPassages()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.setWaveformTiming(QStringLiteral("track-with-silence"), 120.0, 120000,
                               QVariantList{0.2, 0.6, 0.4});
    features.processSpectrum(spectrum(0.3));
    features.processPlaybackPosition(0);
    features.processPlaybackPosition(500);
    QVERIFY(features.beatStrength() > 0.0);

    // The envelope still has a release tail, but the current audio is silent.
    features.processSpectrum(spectrum(0.0));
    QVERIFY(features.energy() > 0.0);
    for (qint64 position = 1000; position <= 4000; position += 500)
        features.processPlaybackPosition(position);
    QCOMPARE(features.beatRevision(), 8);
    QCOMPARE(features.impactRevision(), 1);
    QCOMPARE(features.beatStrength(), 0.0);
    QCOMPARE(features.impactStrength(), 0.0);

    features.processSpectrum(spectrum(0.3));
    for (qint64 position = 4500; position <= 8000; position += 500)
        features.processPlaybackPosition(position);
    QCOMPARE(features.beatRevision(), 16);
    QCOMPARE(features.impactRevision(), 2);
    QVERIFY(features.beatStrength() > 0.0);
    QVERIFY(features.impactStrength() > 0.0);
}

QTEST_GUILESS_MAIN(AudioVisualFeatureControllerTest)

#include "audio_visual_feature_controller_test.moc"

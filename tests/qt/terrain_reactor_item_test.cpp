#include "audio_visual_feature_controller.hpp"
#include "player_experience_controller.hpp"
#include "terrain_reactor_item.hpp"
#include "immersive_theme_catalog.hpp"
#include "playback_controller.hpp"
#include <agplayer/c_api.h>

#include <QSignalSpy>
#include <QScopeGuard>
#include <QImage>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include <array>
#include <cmath>
#include <limits>

using namespace agplayer::terrain;

class FeatureSourceProbe final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList bands READ bands NOTIFY featuresChanged)
    Q_PROPERTY(double energy READ energy NOTIFY featuresChanged)
    Q_PROPERTY(double spectralFlux READ spectralFlux NOTIFY featuresChanged)
    Q_PROPERTY(bool kickPulse READ kickPulse NOTIFY featuresChanged)
    Q_PROPERTY(bool snarePulse READ snarePulse NOTIFY featuresChanged)
    Q_PROPERTY(quint64 beatRevision READ beatRevision NOTIFY featuresChanged)
    Q_PROPERTY(double beatStrength READ beatStrength NOTIFY featuresChanged)
    Q_PROPERTY(quint64 impactRevision READ impactRevision NOTIFY featuresChanged)
    Q_PROPERTY(double impactStrength READ impactStrength NOTIFY featuresChanged)

public:
    QVariantList bands() const { return bands_; }
    double energy() const noexcept { return energy_; }
    double spectralFlux() const noexcept { return spectralFlux_; }
    bool kickPulse() const noexcept { return kick_; }
    bool snarePulse() const noexcept { return snare_; }
    quint64 beatRevision() const noexcept { return beatRevision_; }
    double beatStrength() const noexcept { return beatStrength_; }
    quint64 impactRevision() const noexcept { return impactRevision_; }
    double impactStrength() const noexcept { return impactStrength_; }

    void publishBeat(quint64 revision, double strength)
    {
        beatRevision_ = revision;
        beatStrength_ = strength;
        emit featuresChanged();
    }

    void publishImpact(quint64 revision, double strength)
    {
        impactRevision_ = revision;
        impactStrength_ = strength;
        emit featuresChanged();
    }

signals:
    void featuresChanged();

private:
    QVariantList bands_{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    double energy_ = 0.0;
    double spectralFlux_ = 0.0;
    bool kick_ = false;
    bool snare_ = false;
    quint64 beatRevision_ = 0;
    double beatStrength_ = 0.0;
    quint64 impactRevision_ = 0;
    double impactStrength_ = 0.0;
};

class TerrainReactorItemTest final : public QObject {
    Q_OBJECT

private slots:
    void visualEqSwitchesReachRealAudioResponse()
    {
        PlayerExperienceController style;
        style.setVisualEqEnabled({true,true,true,true,true,true,true,true});
        TerrainReactorItem item;
        item.setStyleSource(&style);
        TerrainReactorItem::RenderSnapshot snapshot;
        snapshot.pcm.valid = true; snapshot.pcm.sampleRate = 48000; snapshot.pcm.epoch = 1;
        for (std::size_t i = 0; i < snapshot.pcm.pcm.size(); ++i)
            snapshot.pcm.pcm[i] = float(.6 * std::sin(2 * 3.141592653589793 * 80 * i / 48000));
        agplayer::VisualAudioFrameAnalyzer analyzer;
        agplayer::VisualTerrainResponse response;
        agplayer::VisualSnareTrigger snare;
        snapshot.style = item.renderStyleSnapshot();
        const auto active = TerrainReactorItem::advanceReferenceAudioFrame(analyzer, response, snapshot, .016, snare).terrain;
        QVERIFY(active.bands[0] > 0);
        style.setVisualEqEnabled({false,false,false,false,false,false,false,false});
        snapshot.style = item.renderStyleSnapshot();
        analyzer.reset(); response.reset(); snare.reset();
        const auto inactive = TerrainReactorItem::advanceReferenceAudioFrame(analyzer, response, snapshot, .016, snare).terrain;
        for (double band : inactive.bands) QCOMPARE(band, 0.0);
    }
    void delayedPcmBatchPreservesIntermediateKick()
    {
        TerrainReactorItem::RenderSnapshot snapshot;
        snapshot.style.rhythmSensitivity = 1.0F;
        snapshot.pcmBatch.count = 3;
        for (std::size_t frame = 0; frame < snapshot.pcmBatch.count; ++frame) {
            auto& pcm = snapshot.pcmBatch.frames[frame];
            pcm.sampleRate = 48000;
            pcm.epoch = 1;
            pcm.firstSampleIndex = frame * pcm.pcm.size();
            pcm.sequence = frame + 1;
            pcm.valid = true;
        }
        constexpr double tau = 6.2831853071795864769;
        auto& kick = snapshot.pcmBatch.frames[1];
        for (std::size_t sample = 0; sample < kick.pcm.size(); ++sample)
            kick.pcm[sample] = float(0.85 * std::sin(
                tau * 80.0 * double(kick.firstSampleIndex + sample) / 48000.0));
        auto& mixedTail = snapshot.pcmBatch.frames[2];
        for (std::size_t sample = 0; sample < mixedTail.pcm.size(); ++sample)
            mixedTail.pcm[sample] = float(0.25 * std::sin(
                tau * 1200.0 * double(mixedTail.firstSampleIndex + sample) / 48000.0));
        snapshot.pcm = mixedTail;

        for (const int fps : {30, 45, 60}) {
            agplayer::VisualAudioFrameAnalyzer analyzer;
            agplayer::VisualTerrainResponse response;
            agplayer::VisualSnareTrigger snare;
            std::uint64_t consumedSequence = 0;
            const auto result = TerrainReactorItem::advanceReferenceAudioFrames(
                analyzer, response, snapshot, 1.0 / double(fps), snare, consumedSequence);
            QCOMPARE(consumedSequence, std::uint64_t(3));
            QCOMPARE(result.beatCount, 1);
            QVERIFY2(result.terrain.bands[0] > 0.0,
                     "An intermediate kick must still lift the final rendered terrain");
            const auto heldTerrain = result.terrain;
            const auto repeated = TerrainReactorItem::advanceReferenceAudioFrames(
                analyzer, response, snapshot, 1.0 / double(fps), snare, consumedSequence);
            QCOMPARE(repeated.beatCount, 0);
            QCOMPARE(repeated.terrain.bands, heldTerrain.bands);
        }
    }
    void canonicalAmplitudeUsesOriginalCurve()
    {
        PlayerExperienceController style;
        QVERIFY(style.applyTheme(QStringLiteral("nocturnal")));
        TerrainReactorItem item;
        item.setStyleSource(&style);
        const int percentages[] = {0, 50, 75, 100};
        const float halfMultipliers[] = {0, .5F, 2.25F, 7.5F};
        for (int i = 0; i < 4; ++i) {
            style.setTerrainAmplitude(percentages[i]);
            QCOMPARE(item.renderStyleSnapshot().terrainAmplitude, halfMultipliers[i]);
            QCOMPARE(style.terrainAmplitude(), percentages[i]);
        }
        style.setCoolColor(QStringLiteral("#123456"));
        QVERIFY(style.themeId().isEmpty());
        for (const int percentage : percentages) {
            style.setTerrainAmplitude(percentage);
            QCOMPARE(item.renderStyleSnapshot().terrainAmplitude, percentage / 100.0F);
        }
    }
    void snareWhiteWaveUsesSharedPresentationGate();
    void referenceFrameRoutesOriginalSnareEvents();
    void firstTerrainFrameUsesActualDelta();
    void renderEventsSurviveDiscardedGuiReportsAndRendererRecreation();
    void queuedRenderReportCannotOverwriteManualMode();
    void referenceSceneRetainsSharedPcmForPauseRelease();
    void pauseResumeValidityDoesNotResetContinuousPcm();
    void defaultsDoNotScheduleRendering();
    void compensatesRasterDirectionAtPresentation()
    {
        TerrainReactorItem item;
        QVERIFY(item.isMirrorVerticallyEnabled());
    }
    void consumesTaskOneFeaturesWithoutSpectrumAnalysis();
    void syntheticFeaturesAreDeterministicAndClamped();
    void visibilityAndExposureGateRendering();
    void windowEventsGateShowMinimizeAndRestore();
    void softwareBackendFailsClosedWithoutSchedulingWork();
    void taskOneStyleIsCopiedIntoImmutableSnapshot();
    void v46DynamicsAreCopiedAndRemainBounded();
    void featureSourceImpactRevisionIsConsumedWithoutAnotherDecoder();
    void trackIdentitySelectsStablePaletteWithoutThemeCycling();
    void highDpiInternalScaleUsesPhysicalPixels();
    void adaptiveRenderScaleChangesItemSampleCount();
    void duplicateRendererIsRejectedBySharedLifecycle();
    void cameraPropertiesSupportTaskFourInput();
    void nonFiniteCameraInvokablesPreserveExposedState();
    void referenceThemesReachRendererWithoutHexQuantization();
    void independentCustomColorsKeepCanonicalRendering()
    {
        PlayerExperienceController style;
        QVERIFY(style.applyTheme(QStringLiteral("nocturnal")));
        style.setTerrainAmplitude(76);
        style.setTopographyDensity(23);
        TerrainReactorItem item;
        item.setStyleSource(&style);
        const auto original = item.renderStyleSnapshot();
        for (const auto& key : {"coolColor", "warmColor", "accentColor", "peakColor", "baseColor"}) {
            bool accepted = false;
            QVERIFY(QMetaObject::invokeMethod(&style, "setCustomColor", Qt::DirectConnection,
                Q_RETURN_ARG(bool, accepted), Q_ARG(QString, QString::fromLatin1(key)),
                Q_ARG(QString, QStringLiteral("#C020F0"))));
            QVERIFY(accepted);
            QCOMPARE(style.themeId(), QStringLiteral("custom"));
            const auto custom = item.renderStyleSnapshot();
            QCOMPARE(custom.bodyColor.w(), 1.0F);
            QCOMPARE(custom.rippleColor.w(), 1.0F);
            QCOMPARE(custom.terrainAmplitude, original.terrainAmplitude);
            QCOMPARE(custom.topographyDensity, original.topographyDensity);
            QCOMPARE(style.property("customColors").toMap().value(QString::fromLatin1(key)).toString(),
                     QStringLiteral("#C020F0"));
        }
        const auto custom = item.renderStyleSnapshot();
        for (const auto color : {custom.colors[1], custom.colors[2], custom.colors[4],
                                 custom.rippleColor, custom.atmosphereColor}) {
            QVERIFY(std::abs(color.x() - 192.0F / 255.0F) < .0001F);
            QVERIFY(std::abs(color.y() - 32.0F / 255.0F) < .0001F);
            QVERIFY(std::abs(color.z() - 240.0F / 255.0F) < .0001F);
        }
        QVERIFY(style.applyTheme(QStringLiteral("nocturnal")));
        QCOMPARE(item.renderStyleSnapshot().colors, original.colors);
        QCOMPARE(style.property("customColors").toMap().value("coolColor").toString(),
                 QStringLiteral("#C020F0"));
    }
};

void TerrainReactorItemTest::referenceFrameRoutesOriginalSnareEvents()
{
    const auto path = qEnvironmentVariable("AGPLAYER_REFERENCE_AUDIO_TRACE");
    if (path.isEmpty()) QSKIP("Optional original real PCM trace not provided");
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    const auto frames = root["frames"].toArray();
    QCOMPARE(frames.size(), 126);
    TerrainReactorItem::RenderSnapshot snapshot;
    snapshot.pcm.sampleRate = root["sampleRate"].toInt();
    snapshot.pcm.valid = true;
    snapshot.pcm.epoch = 1;
    agplayer::VisualAudioFrameAnalyzer analyzer;
    agplayer::VisualTerrainResponse response;
    agplayer::VisualSnareTrigger snare;
    for (int i = 0; i < frames.size(); ++i) {
        const auto samples = frames[i].toObject()["timeDomain"].toArray();
        QCOMPARE(samples.size(), 1024);
        for (int sample = 0; sample < 1024; ++sample)
            snapshot.pcm.pcm[sample] = float(samples[sample].toDouble());
        const auto actual = TerrainReactorItem::advanceReferenceAudioFrame(
            analyzer, response, snapshot, .016, snare);
        const bool expected = i == 12 || i == 123;
        QVERIFY2(actual.snare.triggered == expected, qPrintable(QString("PCM frame %1").arg(i)));
        if (expected) {
            const double strength = i == 12 ? .14751430206677263 : .192378863459133;
            QVERIFY(std::abs(actual.snare.strength - strength) < 1e-12);
        }
    }
    snapshot.pcm.valid = false;
    const auto invalid = TerrainReactorItem::advanceReferenceAudioFrame(
        analyzer, response, snapshot, .016, snare);
    QVERIFY(!invalid.snare.triggered);
    QCOMPARE(invalid.snare.strength, 0.0);
}

void TerrainReactorItemTest::snareWhiteWaveUsesSharedPresentationGate()
{
    TravelingWaveGate gate;
    // Actual original Snare callback strength from real trace frame 12.
    const double strength = .14751430206677263;
    const auto wave = consumeSnareWave(gate, 0, strength, 42, true, false);
    QVERIFY(wave.w() < 0); // Reference shader's white-wave encoding.
    QVERIFY(std::abs(wave.w() + strength * 3) < 1e-7);
    const double radius = std::hypot(wave.x(), wave.y());
    QVERIFY(radius >= 10 && radius <= 45);
    QCOMPARE(consumeSnareWave(gate, .05F, 1, 43, true, false), QVector4D());
    QVERIFY(!gate.consume(3, 1)); // Snare blocks subsequent ordinary beat/meteor.
    QVERIFY(gate.consume(6, 1));
    // Landing must anchor to display time, not an earlier launch reservation.
    gate.anchor(6.5F, 1);
    QCOMPARE(consumeSnareWave(gate, 9.49F, 1, 44, true, false), QVector4D());
    const auto afterLanding = consumeSnareWave(gate, 9.5F, 2, 44, true, false);
    QCOMPARE(afterLanding.w(), -3.0F);
    for (quint32 seed = 1; seed <= 64; ++seed) {
        TravelingWaveGate fresh;
        QCOMPARE(consumeSnareWave(fresh, 0, 1, seed, true, true), QVector4D());
        QCOMPARE(consumeSnareWave(fresh, 0, 1, seed, false, false), QVector4D());
        QCOMPARE(consumeSnareWave(fresh, 0, 0, seed, true, false), QVector4D());
        const auto allowed = consumeSnareWave(fresh, 0, 1, seed, true, false);
        const double distance = std::hypot(allowed.x(), allowed.y());
        QVERIFY(distance >= 10 && distance <= 45);
        QCOMPARE(allowed.w(), -3.0F);
    }
}

void TerrainReactorItemTest::firstTerrainFrameUsesActualDelta()
{
    TerrainReactorItem::RenderSnapshot snapshot;
    snapshot.pcm.valid = true;
    snapshot.pcm.sampleRate = 48000;
    snapshot.pcm.epoch = 1;
    for (std::size_t i = 0; i < snapshot.pcm.pcm.size(); ++i)
        snapshot.pcm.pcm[i] = float(.6 * std::sin(2 * 3.141592653589793 * 80 * i / 48000));
    snapshot.style.visualEqGains.fill(.5F);
    snapshot.style.motionResponse = .5F;
    snapshot.style.rhythmSensitivity = 1;
    agplayer::VisualAudioFrameAnalyzer analyzer;
    agplayer::VisualSnareTrigger snare;
    agplayer::VisualTerrainResponse response, oracle;
    agplayer::VisualTerrainResponse::EqBands eq;
    eq.fill(.5);
    for (const double dt : {.016, .008, .5}) {
        const auto frame = TerrainReactorItem::advanceReferenceAudioFrame(
            analyzer, response, snapshot, dt, snare);
        QVERIFY(frame.audio.valid);
        QVERIFY(frame.audio.descriptors.bands[0] > 0);
        const auto expected = oracle.update(frame.audio.descriptors, frame.audio.kick.envelope,
            eq, {true,true,true,true,true,true,true,true}, dt, 50);
        for (std::size_t i = 0; i < expected.bands.size(); ++i)
            QCOMPARE(frame.terrain.bands[i], expected.bands[i]);
    }
    analyzer.reset(); response.reset(); oracle.reset();
    ++snapshot.pcm.epoch;
    const auto resumed = TerrainReactorItem::advanceReferenceAudioFrame(
        analyzer, response, snapshot, .008, snare);
    const auto expected = oracle.update(resumed.audio.descriptors, resumed.audio.kick.envelope,
        eq, {true,true,true,true,true,true,true,true}, .008, 50);
    QCOMPARE(resumed.terrain.bands[0], expected.bands[0]);
    agplayer::visual::KickResponse kick(agplayer::visual::KickResponse::Mode::Reference);
    agplayer::VisualSpectrumAnalyzer spectrum;
    const auto display = spectrum.process(snapshot.pcm.pcm);
    const auto expectedKick = kick.process(display, 1.0 / 60.0, 100);
    QCOMPARE(resumed.audio.kick.envelope, expectedKick.envelope);
    QCOMPARE(resumed.audio.kick.level, expectedKick.level);
}

void TerrainReactorItemTest::renderEventsSurviveDiscardedGuiReportsAndRendererRecreation()
{
    RendererResourceState resources;
    BeatEventConsumer consumer(resources);
    ImpactEventConsumer impacts(resources);
    QVERIFY(consumer.consume({.8F,12},0));
    QVERIFY(impacts.consume({.9F,8},0));
    TerrainReactorItem::RenderSnapshot staleGui;
    staleGui.beatEvent = {.1F,2};
    staleGui.impactEvent = {.1F,1};
    BeatEvent beat{.8F,12}; ImpactEvent impact{.9F,8};
    TerrainReactorItem::restoreRenderAudioEvents(beat,impact,staleGui,resources);
    ++beat.revision; ++impact.revision;
    QVERIFY2(consumer.consume(beat,1),"Resume must not lose onset when GUI report was discarded");
    QVERIFY(impacts.consume(impact,1));
    BeatEvent recreatedBeat; ImpactEvent recreatedImpact;
    BeatEventConsumer recreatedConsumer(resources);
    ImpactEventConsumer recreatedImpacts(resources);
    TerrainReactorItem::restoreRenderAudioEvents(recreatedBeat,recreatedImpact,staleGui,resources);
    ++recreatedBeat.revision; ++recreatedImpact.revision;
    QVERIFY2(recreatedConsumer.consume(recreatedBeat,2),"New renderer must exceed persistent consumed revision");
    QVERIFY(recreatedImpacts.consume(recreatedImpact,2));
}

void TerrainReactorItemTest::queuedRenderReportCannotOverwriteManualMode()
{
    AudioVisualFeatureController source;
    PlayerExperienceController style;
    QVERIFY(style.applyTheme(QStringLiteral("nocturnal")));
    TerrainReactorItem item;
    item.setStyleSource(&style); item.setFeatureSource(&source); item.setActive(true);
    const auto snapshot=item.snapshotForRenderer();
    const TerrainReactorItem::AudioFrameOrigin origin{snapshot.visualResetRevision,
        snapshot.activityRevision,snapshot.styleRevision};
    AudioFeatures oldFrame; oldFrame.energy=.9F;
    QMetaObject::invokeMethod(&item,[&item,oldFrame,origin] {
        item.applyRenderAudioFrame(oldFrame,origin,{.8F,4},{.8F,1});
    },Qt::QueuedConnection);
    style.setBaseColor(QStringLiteral("#102030"));
    QVERIFY(style.themeId().isEmpty());
    QCoreApplication::sendPostedEvents(&item,QEvent::MetaCall);
    QCOMPARE(item.featureEnergy(),0.0);
}

void TerrainReactorItemTest::referenceSceneRetainsSharedPcmForPauseRelease()
{
    ag_player* raw = nullptr;
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4096U};
    QCOMPARE(ag_player_create_with_config(&config, &raw), AG_OK);
    const auto cleanup = qScopeGuard([&] { ag_player_destroy(raw); });
    PlaybackController playback(raw);
    AudioVisualFeatureController features(&playback);
    PlayerExperienceController style;
    QVERIFY(style.applyTheme(QStringLiteral("neon-tokyo")));
    TerrainReactorItem item;
    item.setStyleSource(&style);
    item.setFeatureSource(&features);
    features.setActive(true);
    // The legacy spectrum remains silent, isolating the actual PCM consumer.
    QCOMPARE(ag_player_set_muted(raw, 1), AG_OK);
    QCOMPARE(ag_player_load(raw, AGPLAYER_TEST_WAV), AG_OK);
    QCOMPARE(ag_player_play(raw), AG_OK);
    QTest::qWait(200);
    // Attached terrain owns analysis cadence. Without an actual render frame,
    // the GUI FIFO drain must not run FFT/features/kick on its own timer.
    QCOMPARE(features.visualSpectrumUpdateCount(), quint64{0});
    QCOMPARE(item.featureEnergy(), 0.0);
    const auto before = item.featureRevision();
    QCOMPARE(ag_player_pause(raw), AG_OK);
    QTRY_VERIFY_WITH_TIMEOUT(item.snapshotForRenderer().pcm.paused, 1000);
    const auto paused = item.snapshotForRenderer();
    QVERIFY(paused.pcm.paused);
    QVERIFY(paused.pcm.releasing);
    QVERIFY(!paused.pcm.valid);
    QCOMPARE(item.featureRevision(), before);
    item.setFeatureSource(nullptr);
    QCOMPARE(item.featureEnergy(), 0.0);
    for (const auto& band : item.featureBands()) QCOMPARE(band.toDouble(), 0.0);
}

void TerrainReactorItemTest::pauseResumeValidityDoesNotResetContinuousPcm()
{
    agplayer::VisualAudioFrameAnalyzer::Snapshot playing;
    playing.valid = true;
    playing.sampleRate = 48000;
    playing.epoch = 7;

    auto paused = playing;
    paused.valid = false;
    paused.paused = true;
    QVERIFY(!TerrainReactorItem::hasVisualPcmDiscontinuity(playing, paused));
    QVERIFY(!TerrainReactorItem::hasVisualPcmDiscontinuity(paused, playing));

    auto seeked = playing;
    ++seeked.epoch;
    QVERIFY(TerrainReactorItem::hasVisualPcmDiscontinuity(playing, seeked));

    auto reconfigured = playing;
    reconfigured.sampleRate = 44100;
    QVERIFY(TerrainReactorItem::hasVisualPcmDiscontinuity(playing, reconfigured));
}

void TerrainReactorItemTest::referenceThemesReachRendererWithoutHexQuantization()
{
    using namespace agplayer::immersive;
    PlayerExperienceController style;
    style.setTopographyDensity(46);
    TerrainReactorItem item;
    item.setStyleSource(&style);
    const std::array<ThemeColorRole, 5> roles{
        ThemeColorRole::BasePrimary, ThemeColorRole::CoolCore,
        ThemeColorRole::WarmCore, ThemeColorRole::CoolEdge,
        ThemeColorRole::WarmEdge};
    for (const auto& theme : builtInThemes()) {
        bool accepted = false;
        const QString id = QString::fromUtf8(theme.id.data(), int(theme.id.size()));
        QVERIFY(QMetaObject::invokeMethod(&style, "applyTheme", Qt::DirectConnection,
                                         Q_RETURN_ARG(bool, accepted), Q_ARG(QString, id)));
        QVERIFY(accepted);
        const auto snapshot = item.renderStyleSnapshot();
        QCOMPARE(snapshot.topographyDensity, 46);
        for (std::size_t index = 0; index < roles.size(); ++index) {
            const auto encoded = workingLinearToSrgb(toWorkingLinear(
                theme.colors[themeColorIndex(roles[index])]));
            QVERIFY(std::abs(snapshot.colors[index].x() - encoded.red) < 0.00001F);
            QVERIFY(std::abs(snapshot.colors[index].y() - encoded.green) < 0.00001F);
            QVERIFY(std::abs(snapshot.colors[index].z() - encoded.blue) < 0.00001F);
        }
        QVERIFY(std::abs(snapshot.glowIntensity - theme.glowIntensity) < 0.00001F);
        const std::array<ThemeColorRole, 3> separateRoles{
            ThemeColorRole::BaseSecondary, ThemeColorRole::Fog, ThemeColorRole::Ripple};
        const std::array<QVector4D, 3> separateValues{snapshot.bodyColor,
                                                   snapshot.atmosphereColor, snapshot.rippleColor};
        for (std::size_t index = 0; index < separateRoles.size(); ++index) {
            const auto encoded = workingLinearToSrgb(toWorkingLinear(
                theme.colors[themeColorIndex(separateRoles[index])]));
            QVERIFY(std::abs(separateValues[index].x() - encoded.red) < 0.00001F);
            QVERIFY(std::abs(separateValues[index].y() - encoded.green) < 0.00001F);
            QVERIFY(std::abs(separateValues[index].z() - encoded.blue) < 0.00001F);
            // Only Violet Heart localizes the warm palette to the raised core.
            QCOMPARE(separateValues[index].w(),
                     index == 0 && theme.id == "violet-heart" ? 2.0F : 1.0F);
        }
    }
}

class TestableTerrainReactorItem final : public TerrainReactorItem {
public:
    using TerrainReactorItem::TerrainReactorItem;
    using TerrainReactorItem::applyInternalScale;
    using TerrainReactorItem::createRenderer;
};

void TerrainReactorItemTest::defaultsDoNotScheduleRendering()
{
    TerrainReactorItem item;
    QVERIFY(!item.active());
    QVERIFY(!item.renderingRequested());
    QCOMPARE(item.deterministicSeed(), quint32{0x5eedU});
    QCOMPARE(item.frameCount(), quint64{0});
    QCOMPARE(item.animationCount(), quint64{0});
    QCOMPARE(item.uploadCount(), quint64{0});
}

void TerrainReactorItemTest::trackIdentitySelectsStablePaletteWithoutThemeCycling()
{
    TerrainReactorItem item;
    QSignalSpy changed(&item, &TerrainReactorItem::trackIdentityChanged);
    QCOMPARE(item.trackIdentity(), QString{});
    QCOMPARE(item.trackPaletteSeed(), quint32{0});

    item.setTrackIdentity(QStringLiteral("album/track-a.flac"));
    const quint32 firstSeed = item.trackPaletteSeed();
    QVERIFY(firstSeed != 0U);
    QCOMPARE(changed.count(), 1);
    QVERIFY(!item.renderStyleSnapshot().themeCycleEnabled);

    item.setTrackIdentity(QStringLiteral("album/track-a.flac"));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(item.trackPaletteSeed(), firstSeed);
    item.setTrackIdentity(QStringLiteral("album/track-b.flac"));
    QCOMPARE(changed.count(), 2);
    QVERIFY(item.trackPaletteSeed() != firstSeed);
}

void TerrainReactorItemTest::consumesTaskOneFeaturesWithoutSpectrumAnalysis()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    TerrainReactorItem item;
    item.setFeatureSource(&features);
    item.setActive(true);
    const quint64 revisionBeforeUpdate = item.featureRevision();
    QCOMPARE(revisionBeforeUpdate, quint64{1}); // initial immutable source snapshot
    const quint64 punchBeforeUpdate = item.punchRevision();

    QVariantList spectrum(128, 0.0);
    for (int index = 0; index < 32; ++index) spectrum[index] = 1.0;
    features.processSpectrum(spectrum);

    QCOMPARE(item.featureRevision(), revisionBeforeUpdate + 1);
    QCOMPARE(item.punchRevision(), punchBeforeUpdate + 1);
    const QVariantList bands = item.featureBands();
    QCOMPARE(bands.size(), 8);
    QCOMPARE(bands.at(0).toDouble(), 1.0);
    QCOMPARE(bands.at(7).toDouble(), 0.0);
    QVERIFY(item.featureEnergy() > 0.0);
}

void TerrainReactorItemTest::syntheticFeaturesAreDeterministicAndClamped()
{
    TerrainReactorItem item;
    item.setUseSyntheticFeatures(true);
    item.setSyntheticFeatures({-1.0, 0.1, 0.2, 0.3, 0.4,
                               0.5, 0.6, 2.0}, 1.4, 2.0, true, false);
    const QVariantList bands = item.featureBands();
    const std::array<double, 8> expected{0.0, 0.1, 0.2, 0.3,
                                          0.4, 0.5, 0.6, 1.0};
    QCOMPARE(bands.size(), int(expected.size()));
    for (int index = 0; index < bands.size(); ++index) {
        QVERIFY(std::abs(bands.at(index).toDouble()
                         - expected.at(std::size_t(index))) < 0.000001);
    }
    QCOMPARE(item.featureEnergy(), 1.0);
    QCOMPARE(item.featureSpectralFlux(), 1.0);
    QVERIFY(item.featureKick());
    QVERIFY(!item.featureSnare());
}

void TerrainReactorItemTest::visibilityAndExposureGateRendering()
{
    TerrainReactorItem item;
    item.setActive(true);
    item.setHostExposed(true);
    QVERIFY(item.renderingRequested());
    item.setVisible(false);
    QVERIFY(!item.renderingRequested());
    item.setVisible(true);
    item.setHostExposed(false);
    QVERIFY(!item.renderingRequested());
}

void TerrainReactorItemTest::windowEventsGateShowMinimizeAndRestore()
{
    QQuickWindow window;
    TerrainReactorItem item(window.contentItem());
    item.setActive(true);
    window.show();
    QTRY_VERIFY(item.renderStatus()
                != TerrainReactorItem::RenderStatus::Inactive);
    if (item.renderStatus() != TerrainReactorItem::RenderStatus::Ready) {
        QVERIFY(!item.renderingRequested());
        return;
    }
    QTRY_VERIFY(item.renderingRequested());
    window.hide();
    QTRY_VERIFY(!item.renderingRequested());
    window.show();
    QTRY_VERIFY(item.renderingRequested());
    window.showMinimized();
    QTRY_VERIFY(!item.renderingRequested());
    window.showNormal();
    QTRY_VERIFY(item.renderingRequested());
}

void TerrainReactorItemTest::softwareBackendFailsClosedWithoutSchedulingWork()
{
    QQuickWindow window;
    window.resize(320, 180);
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(320, 180));
    item.setActive(true);
    window.show();

    QTRY_COMPARE(item.renderStatus(),
                 TerrainReactorItem::RenderStatus::SoftwareBackend);
    QVERIFY(!item.renderingRequested());
    const quint64 frames = item.frameCount();
    const quint64 uploads = item.uploadCount();
    QTest::qWait(40);
    QCOMPARE(item.frameCount(), frames);
    QCOMPARE(item.uploadCount(), uploads);
    QVERIFY(!item.diagnostic().isEmpty());
}

void TerrainReactorItemTest::taskOneStyleIsCopiedIntoImmutableSnapshot()
{
    PlayerExperienceController style;
    style.setColorMode(PlayerExperienceController::RgbSweep);
    style.setCoolColor(QStringLiteral("#123456"));
    style.setTerrainAmplitude(81);
    style.setMotionResponse(37);
    style.setGlowIntensity(72);
    style.setCinemaShake(1.2);
    style.setAutoRotate(43);
    style.setPeakBoost(67);
    style.setMeteorsEnabled(false);
    style.setVisualEqGains({10, 20, 30, 40, 50, 60, 70, 80});

    TerrainReactorItem item;
    item.setStyleSource(&style);
    // Initial binding publishes one complete immutable style snapshot.
    QCOMPARE(item.styleRevision(), quint64{1});
    const RenderStyleSnapshot snapshot = item.renderStyleSnapshot();
    QCOMPARE(snapshot.colorMode, RenderColorMode::RgbSweep);
    QCOMPARE(snapshot.rippleColor.w(), 0.0F); // Custom theme retains palette/event encoding.
    QVERIFY(std::abs(snapshot.colors[1].x() - 0.070588) < 0.00001);
    QVERIFY(std::abs(snapshot.terrainAmplitude - 0.81F) < 0.00001F);
    QVERIFY(std::abs(snapshot.motionResponse - 0.37F) < 0.00001F);
    QVERIFY(std::abs(snapshot.glowIntensity - 0.72F) < 0.00001F);
    QVERIFY(std::abs(snapshot.cinemaShake - 1.2F) < 0.00001F);
    QVERIFY(std::abs(snapshot.autoRotate - 0.43F) < 0.00001F);
    QVERIFY(std::abs(snapshot.peakBoost - 0.67F) < 0.00001F);
    QVERIFY(!snapshot.meteorsEnabled);
    QVERIFY(std::abs(snapshot.visualEqGains.front() - 0.1F) < 0.00001F);
    QVERIFY(std::abs(snapshot.visualEqGains.back() - 0.8F) < 0.00001F);

    style.setRipplesEnabled(false);
    QCOMPARE(item.styleRevision(), quint64{2});
    QVERIFY(!item.renderStyleSnapshot().ripplesEnabled);
}

void TerrainReactorItemTest::v46DynamicsAreCopiedAndRemainBounded()
{
    PlayerExperienceController style;
    style.setInputCompression(150);
    style.setAudioResponse(200);
    style.setResponseRange(220);
    style.setCenterHighlight(100);
    style.setRhythmStrength(140);
    style.setDepthOfField(150);
    style.setSubjectClarity(140);
    style.setAutoRotateSpeed(100);
    style.setRhythmSensitivity(100);

    TerrainReactorItem item;
    item.setStyleSource(&style);
    const RenderStyleSnapshot snapshot = item.renderStyleSnapshot();
    QCOMPARE(snapshot.inputCompression, 1.5F);
    QCOMPARE(snapshot.audioResponse, 2.0F);
    QCOMPARE(snapshot.responseRange, 2.2F);
    QCOMPARE(snapshot.centerHighlight, 1.0F);
    QCOMPARE(snapshot.rhythmStrength, 1.4F);
    QCOMPARE(snapshot.depthOfField, 1.5F);
    QCOMPARE(snapshot.subjectClarity, 1.4F);
    QCOMPARE(snapshot.autoRotateSpeed, 1.0F);
    QCOMPARE(snapshot.rhythmSensitivity, 1.0F);

    const quint64 revision = item.styleRevision();
    style.setRhythmStrength(-100);
    QCOMPARE(item.styleRevision(), revision + 1);
    QCOMPARE(item.renderStyleSnapshot().rhythmStrength, 0.0F);
}

void TerrainReactorItemTest::featureSourceImpactRevisionIsConsumedWithoutAnotherDecoder()
{
    FeatureSourceProbe features;
    features.publishBeat(17, 0.62);
    features.publishImpact(41, 0.73);

    TerrainReactorItem item;
    item.setFeatureSource(&features);
    QCOMPARE(item.beatRevision(), quint64{17});
    QVERIFY(std::abs(item.beatStrength() - 0.62) < 0.00001);
    QCOMPARE(item.impactRevision(), quint64{41});
    QVERIFY(std::abs(item.impactStrength() - 0.73) < 0.00001);

    const quint64 explicitRevision = item.impactRevision();
    emit features.featuresChanged();
    QCOMPARE(item.impactRevision(), explicitRevision);

    features.publishImpact(42, 5.0);
    QCOMPARE(item.impactRevision(), quint64{42});
    QCOMPARE(item.impactStrength(), 1.0);

    AudioVisualFeatureController fallback;
    fallback.setActive(true);
    TerrainReactorItem fallbackItem;
    fallbackItem.setFeatureSource(&fallback);
    const quint64 beatBefore = fallbackItem.beatRevision();
    const quint64 fallbackBefore = fallbackItem.impactRevision();
    QVariantList spectrum(128, 0.0);
    for (int index = 0; index < 32; ++index) spectrum[index] = 1.0;
    fallback.processSpectrum(spectrum);
    QVERIFY(fallbackItem.featureKick());
    QVERIFY(fallbackItem.punchRevision() > 0);
    QCOMPARE(fallbackItem.beatRevision(), beatBefore + 1);
    QVERIFY(fallbackItem.beatStrength() > 0.0);
    QCOMPARE(fallbackItem.impactRevision(), fallbackBefore);
}

void TerrainReactorItemTest::highDpiInternalScaleUsesPhysicalPixels()
{
    QQuickWindow window;
    QImage image(400, 200, QImage::Format_RGBA8888_Premultiplied);
    QQuickRenderTarget target = QQuickRenderTarget::fromPaintDevice(&image);
    target.setDevicePixelRatio(2.0);
    window.setRenderTarget(target);
    TestableTerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(100, 50));
    item.applyInternalScale(0.75F, 4);
    QCOMPARE(window.effectiveDevicePixelRatio(), 2.0);
    QCOMPARE(item.fixedColorBufferWidth(), 150);
    QCOMPARE(item.fixedColorBufferHeight(), 75);
}

void TerrainReactorItemTest::adaptiveRenderScaleChangesItemSampleCount()
{
    TestableTerrainReactorItem item;
    QCOMPARE(item.sampleCount(), 1);

    item.setQuality(TerrainReactorItem::Quality::Balanced);
    QCOMPARE(item.sampleCount(), 4);
    item.setQuality(TerrainReactorItem::Quality::High);
    QCOMPARE(item.sampleCount(), 4);
    item.setQuality(TerrainReactorItem::Quality::Eco);
    QCOMPARE(item.sampleCount(), 1);

    item.applyInternalScale(0.70F, 1);
    QCOMPARE(item.sampleCount(), 1);

    item.applyInternalScale(1.0F, 4);
    QCOMPARE(item.sampleCount(), 4);
}

void TerrainReactorItemTest::duplicateRendererIsRejectedBySharedLifecycle()
{
    TestableTerrainReactorItem item;
    QQuickRhiItemRenderer* first = item.createRenderer();
    QCOMPARE(item.liveRendererCount(), 1);
    QQuickRhiItemRenderer* duplicate = item.createRenderer();
    QCOMPARE(item.liveRendererCount(), 1);
    QTRY_COMPARE(item.renderStatus(), TerrainReactorItem::RenderStatus::ResourceError);
    QTRY_VERIFY(item.diagnostic().contains(QStringLiteral("renderer"),
                                           Qt::CaseInsensitive));
    delete duplicate;
    QCOMPARE(item.liveRendererCount(), 1);
    delete first;
    QCOMPARE(item.liveRendererCount(), 0);
}

void TerrainReactorItemTest::cameraPropertiesSupportTaskFourInput()
{
    TerrainReactorItem item;
    const qreal originalYaw = item.cameraYaw();
    const quint64 originalPunchRevision = item.punchRevision();
    item.triggerCameraPunch(0.7);
    QCOMPARE(item.punchRevision(), originalPunchRevision + 1);
    const quint64 firstPunchRevision = item.punchRevision();
    item.orbitBy(0.25, -0.1, 1.0);
    item.zoomBy(10000.0, 1.0);
    QCOMPARE(item.punchRevision(), firstPunchRevision);
    item.triggerCameraPunch(0.7);
    QCOMPARE(item.punchRevision(), firstPunchRevision + 1);
    item.triggerCameraPunch(0.2);
    QCOMPARE(item.punchRevision(), firstPunchRevision + 2);
    QCOMPARE(item.cameraYaw(), originalYaw + 0.25);
    QCOMPARE(item.cameraDistance(), 5.0);
    QVERIFY(item.cameraPunch() >= 0.19);
}

void TerrainReactorItemTest::nonFiniteCameraInvokablesPreserveExposedState()
{
    const std::array nonFinite{
        std::numeric_limits<qreal>::quiet_NaN(),
        std::numeric_limits<qreal>::infinity(),
        -std::numeric_limits<qreal>::infinity(),
    };
    TerrainReactorItem item;
    item.orbitBy(0.2, -0.05, 1.0);
    item.zoomBy(-800.0, 1.0);
    const qreal yaw = item.cameraYaw();
    const qreal pitch = item.cameraPitch();
    const qreal distance = item.cameraDistance();
    QSignalSpy cameraChanges(&item, &TerrainReactorItem::cameraChanged);

    for (const qreal invalid : nonFinite) {
        const int changesBefore = cameraChanges.count();
        item.orbitBy(invalid, 0.0, invalid);
        item.orbitBy(0.0, invalid, invalid);
        item.zoomBy(invalid, invalid);
        item.orbitBy(0.1, -0.02, invalid);
        item.zoomBy(-20.0, invalid);
        QVERIFY(std::isfinite(double(item.cameraYaw())));
        QVERIFY(std::isfinite(double(item.cameraPitch())));
        QVERIFY(item.cameraPitch() >= 0.12 && item.cameraPitch() <= 1.15);
        QVERIFY(std::isfinite(double(item.cameraDistance())));
        QVERIFY(item.cameraDistance() >= 42.0 && item.cameraDistance() <= 220.0);
        QCOMPARE(item.cameraYaw(), yaw);
        QCOMPARE(item.cameraPitch(), pitch);
        QCOMPARE(item.cameraDistance(), distance);
        QCOMPARE(cameraChanges.count(), changesBefore);
    }
}

QTEST_MAIN(TerrainReactorItemTest)

#include "terrain_reactor_item_test.moc"

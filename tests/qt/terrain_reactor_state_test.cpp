#include "terrain_reactor_state.hpp"

#include <QTest>

#include <algorithm>
#include <array>
#include <thread>

using namespace agplayer::terrain;

class TerrainReactorStateTest final : public QObject {
    Q_OBJECT

private slots:
    void fixedSeedProducesStableLayoutAndColorZones();
    void meteorsHaveFiniteTrailsAndCollisionEffects();
    void meteorGroupsKeepOneDeterministicPrimaryImpact();
    void audioFeaturesDriveBoundedVisualParameters();
    void automaticQualityUsesHysteresisCooldownAndEffectFirstOrder();
    void qualityFeedbackSeparatesPacerDelayFromWorkCost();
    void inactiveOrOccludedGateFreezesAllWorkCounters();
    void rendererOwnershipHasOneLiveResourceGeneration();
    void manualCameraControlRecoversAfterFourSeconds();
    void manualCameraDeltaPreservesRendererMotion();
    void punchEventsAreConsumedOnceByRevision();
    void punchRevisionClaimSurvivesRendererRebuild();
    void immersiveStyleControlsMapToBoundedDistinctDynamics();
    void impactEventsProduceOneBoundedPulsePerRevision();
    void explicitImpactRaisesCenterAndTravelingRing();
    void steadyMusicKeepsCenterVisiblyFocused();
    void nearbyRandomnessKeepsTerrainSoftWithoutThresholdSpikes();
    void idleTerrainKeepsFineVisibleReliefWithoutMusic();
    void idleTerrainFadesOutsideResponseField();
    void trackIdentityProducesStableBoundedDistinctPalette();
    void ecoFramePacerLimitsWorkToThirtyFrames();
};

void TerrainReactorStateTest::fixedSeedProducesStableLayoutAndColorZones()
{
    const SceneLayout first = makeSceneLayout(0x5eedU, 9, 12, 4, 16);
    const SceneLayout repeated = makeSceneLayout(0x5eedU, 9, 12, 4, 16);
    const SceneLayout different = makeSceneLayout(0x5eeeU, 9, 12, 4, 16);

    QCOMPARE(first.terrain, repeated.terrain);
    QCOMPARE(first.floating, repeated.floating);
    QCOMPARE(first.meteors, repeated.meteors);
    QCOMPARE(first.particles, repeated.particles);
    QCOMPARE(first.terrain.size(), 81);
    QCOMPARE(first.floating.size(), 12);
    QCOMPARE(first.meteors.size(), 4);
    QCOMPARE(first.particles.size(), 16);
    QVERIFY(first.floating != different.floating);

    const auto hasZone = [&first](ColorZone zone) {
        return std::any_of(first.terrain.cbegin(), first.terrain.cend(),
                           [zone](const SceneInstance& instance) {
                               return instance.zone == zone;
                           });
    };
    QVERIFY(hasZone(ColorZone::Cool));
    QVERIFY(hasZone(ColorZone::Warm));
    QVERIFY(hasZone(ColorZone::Accent));
    QVERIFY(hasZone(ColorZone::Peak));
    QCOMPARE(first.terrain.at(40).zone, ColorZone::Peak);
}

void TerrainReactorStateTest::trackIdentityProducesStableBoundedDistinctPalette()
{
    const quint32 firstSeed = stableTrackPaletteSeed(
        QStringView(u"album/track-a.flac"));
    QCOMPARE(firstSeed, stableTrackPaletteSeed(
        QStringView(u"album/track-a.flac")));
    QVERIFY(firstSeed != stableTrackPaletteSeed(
        QStringView(u"album/track-b.flac")));

    const TrackPalette first = trackPalette(firstSeed);
    QCOMPARE(first, trackPalette(firstSeed));
    const TrackPalette second = trackPalette(stableTrackPaletteSeed(
        QStringView(u"album/track-b.flac")));
    QVERIFY(first != second);
    for (const QVector4D& color : first) {
        QVERIFY(color.x() >= 0.0F && color.x() <= 1.0F);
        QVERIFY(color.y() >= 0.0F && color.y() <= 1.0F);
        QVERIFY(color.z() >= 0.0F && color.z() <= 1.0F);
        QCOMPARE(color.w(), 1.0F);
    }
    QCOMPARE(blendTrackPalettes(first, first, -1.0F), first);
    QCOMPARE(blendTrackPalettes(first, first, 2.0F), first);
    const TrackPalette midpoint = blendTrackPalettes(first, second, 0.5F);
    QVERIFY(midpoint != first);
    QVERIFY(midpoint != second);
    for (std::size_t index = 0; index < midpoint.size(); ++index) {
        for (int channel = 0; channel < 3; ++channel) {
            const float minimum = std::min(first[index][channel],
                                           second[index][channel]);
            const float maximum = std::max(first[index][channel],
                                           second[index][channel]);
            QVERIFY(midpoint[index][channel] >= minimum);
            QVERIFY(midpoint[index][channel] <= maximum);
        }
    }
}

void TerrainReactorStateTest::meteorsHaveFiniteTrailsAndCollisionEffects()
{
    const SceneLayout layout = makeSceneLayout(0x5eedU, 9, 3, 4, 8);
    QCOMPARE(layout.meteorTrails.size(), 12);
    QCOMPARE(layout.collisionRipples.size(), 64);
    QCOMPARE(layout.collisionParticles.size(), 48);

    const MeteorPhase flight = meteorPhase(0.25F, 0.5F);
    const MeteorPhase repeated = meteorPhase(0.25F, 0.5F);
    QCOMPARE(flight, repeated);
    QVERIFY(flight.flightActive);
    QVERIFY(!flight.collisionActive);
    const MeteorPhase collision = meteorPhase(0.25F, 3.0F);
    QVERIFY(!collision.flightActive);
    QVERIFY(collision.collisionActive);
    QVERIFY(collision.collisionProgress > 0.0F);
    const MeteorPhase later = meteorPhase(0.25F, 20.0F);
    QVERIFY(later.normalizedAge >= 0.0F && later.normalizedAge < 1.0F);
    QVERIFY(later.fallDistance >= 0.0F && later.fallDistance <= 1.0F);
    QVERIFY(later.collisionProgress >= 0.0F
            && later.collisionProgress <= 1.0F);
}

void TerrainReactorStateTest::meteorGroupsKeepOneDeterministicPrimaryImpact()
{
    const SceneLayout layout = makeSceneLayout(0x5eedU, 9, 0, 4, 0);
    QCOMPARE(layout.meteors.at(0).aux, 0.0F);
    QCOMPARE(layout.meteors.at(1).aux, 1.0F);
    QCOMPARE(int(std::floor(layout.meteorTrails.at(6).aux)), 2);
    QCOMPARE(int(std::floor(layout.collisionRipples.at(16).aux)), 1);
    QCOMPARE(int(std::floor(layout.collisionParticles.at(24).aux)), 2);
}

void TerrainReactorStateTest::audioFeaturesDriveBoundedVisualParameters()
{
    AudioFeatures features;
    features.bands = {-1.0F, 1.8F, 0.25F, 0.4F,
                      0.7F, 0.8F, 0.9F, 2.0F};
    features.energy = 1.5F;
    features.spectralFlux = 1.4F;
    features.kick = 1.0F;
    features.snare = 0.75F;

    const VisualParameters visual = mapVisualParameters(features, 2.0F);
    QCOMPARE(visual.bands.front(), 0.0F);
    QCOMPARE(visual.bands.back(), 1.0F);
    QCOMPARE(visual.energy, 1.0F);
    QVERIFY(visual.rippleStrength >= 0.2F && visual.rippleStrength <= 0.4F);
    QVERIFY(visual.particleActivity > 0.6F && visual.particleActivity <= 1.0F);
    QVERIFY(visual.meteorActivity > 0.6F && visual.meteorActivity <= 1.0F);
    QVERIFY(visual.cameraPunch > 0.2F && visual.cameraPunch <= 0.4F);

    SceneInstance center;
    center.position = QVector3D(0.0F, 0.0F, 0.0F);
    center.random = 0.5F;
    center.zone = ColorZone::Peak;
    SceneInstance edge = center;
    edge.position = QVector3D(84.0F, 0.0F, 84.0F);
    edge.zone = ColorZone::Cool;
    const float centerHeight = terrainHeight(center, visual, 2.0F);
    const float edgeHeight = terrainHeight(edge, visual, 2.0F);
    QVERIFY(centerHeight > edgeHeight);
    QVERIFY(centerHeight <= 30.0F);
    QVERIFY(edgeHeight >= 0.035F);
}

void TerrainReactorStateTest::immersiveStyleControlsMapToBoundedDistinctDynamics()
{
    RenderStyleSnapshot restrained;
    restrained.inputCompression = -2.0F;
    restrained.audioResponse = -1.0F;
    restrained.responseRange = 0.0F;
    restrained.centerHighlight = -1.0F;
    restrained.rhythmStrength = -1.0F;
    restrained.depthOfField = -1.0F;
    restrained.subjectClarity = -1.0F;
    restrained.autoRotateSpeed = -1.0F;
    restrained.rhythmSensitivity = -1.0F;
    const RenderDynamics low = mapRenderDynamics(restrained);

    RenderStyleSnapshot vivid;
    vivid.inputCompression = 4.0F;
    vivid.audioResponse = 4.0F;
    vivid.responseRange = 4.0F;
    vivid.centerHighlight = 4.0F;
    vivid.rhythmStrength = 4.0F;
    vivid.depthOfField = 4.0F;
    vivid.subjectClarity = 4.0F;
    vivid.autoRotate = 1.0F;
    vivid.autoRotateSpeed = 4.0F;
    vivid.rhythmSensitivity = 4.0F;
    const RenderDynamics high = mapRenderDynamics(vivid);

    QCOMPARE(low.inputCompression, 0.2F);
    QCOMPARE(high.inputCompression, 1.5F);
    QCOMPARE(low.audioResponse, 0.2F);
    QCOMPARE(high.audioResponse, 2.0F);
    QCOMPARE(low.responseRadius, 36.0F);
    QCOMPARE(high.responseRadius, 158.4F);
    QCOMPARE(low.centerHighlight, 0.0F);
    QCOMPARE(high.centerHighlight, 1.0F);
    QCOMPARE(low.rhythmStrength, 0.0F);
    QCOMPARE(high.rhythmStrength, 1.4F);
    QCOMPARE(low.depthOfField, 0.0F);
    QCOMPARE(high.depthOfField, 1.5F);
    QCOMPARE(low.subjectClarity, 0.2F);
    QCOMPARE(high.subjectClarity, 1.4F);
    QCOMPARE(low.autoRotateSpeed, 0.0F);
    QCOMPARE(high.autoRotateSpeed, 2.0F);
    QCOMPARE(low.rhythmSensitivity, 0.0F);
    QCOMPARE(high.rhythmSensitivity, 1.0F);

    AudioFeatures features;
    features.bands.fill(0.25F);
    features.energy = 0.25F;
    features.spectralFlux = 0.25F;
    features.kick = 0.5F;
    const VisualParameters quiet = mapVisualParameters(features, 1.0F, restrained);
    const VisualParameters reactive = mapVisualParameters(features, 1.0F, vivid);
    QVERIFY(reactive.energy > quiet.energy);
    QVERIFY(reactive.bands.front() > quiet.bands.front());
    QVERIFY(reactive.rippleStrength > quiet.rippleStrength);
    QVERIFY(reactive.cameraPunch > quiet.cameraPunch);
    QVERIFY(reactive.energy <= 1.0F);
    QVERIFY(reactive.rippleStrength <= 1.0F);
}

void TerrainReactorStateTest::impactEventsProduceOneBoundedPulsePerRevision()
{
    RendererResourceState lifecycle;
    ImpactEventConsumer consumer(lifecycle);
    const ImpactEvent first{0.8F, 7};
    QVERIFY(consumer.consume(first, 10.0F));
    QVERIFY(!consumer.consume(first, 10.1F));

    const ImpactPulseSnapshot start = consumer.snapshot(10.0F);
    QVERIFY(start.active);
    QCOMPARE(start.strength, 0.8F);
    QCOMPARE(start.age, 0.0F);
    const ImpactPulseSnapshot moving = consumer.snapshot(10.3F);
    QVERIFY(moving.active);
    QVERIFY(moving.age > 0.0F && moving.age < 1.0F);
    QVERIFY(moving.strength < start.strength);
    QVERIFY(!consumer.snapshot(11.3F).active);

    const ImpactEvent clipped{9.0F, 8};
    QVERIFY(consumer.consume(clipped, 12.0F));
    QCOMPARE(consumer.snapshot(12.0F).strength, 1.0F);

    ImpactEventConsumer rebuilt(lifecycle);
    QVERIFY(!rebuilt.consume(clipped, 12.0F));
    QVERIFY(!rebuilt.snapshot(12.0F).active);
}

void TerrainReactorStateTest::explicitImpactRaisesCenterAndTravelingRing()
{
    AudioFeatures features;
    features.bands.fill(0.3F);
    features.energy = 0.3F;
    RenderStyleSnapshot style;
    style.responseRange = 1.0F;
    style.centerHighlight = 0.8F;
    style.rhythmStrength = 1.0F;
    VisualParameters baseline = mapVisualParameters(features, 2.0F, style);
    VisualParameters impacted = baseline;
    impacted.impactStrength = 0.9F;
    impacted.impactAge = 0.22F;

    SceneInstance center;
    center.position = QVector3D(0.0F, 0.0F, 0.0F);
    center.random = 0.5F;
    center.zone = ColorZone::Peak;
    SceneInstance ring = center;
    ring.position = QVector3D(28.0F, 0.0F, 0.0F);

    QVERIFY(terrainHeight(center, impacted, 2.0F, style)
            > terrainHeight(center, baseline, 2.0F, style));
    QVERIFY(terrainHeight(ring, impacted, 2.0F, style)
            > terrainHeight(ring, baseline, 2.0F, style));
    QVERIFY(terrainHeight(center, impacted, 2.0F, style) <= 36.0F);
}

void TerrainReactorStateTest::steadyMusicKeepsCenterVisiblyFocused()
{
    AudioFeatures features;
    features.bands.fill(0.36F);
    features.energy = 0.62F;

    RenderStyleSnapshot dimStyle;
    dimStyle.centerHighlight = 0.0F;
    RenderStyleSnapshot focusedStyle = dimStyle;
    focusedStyle.centerHighlight = 1.0F;

    const VisualParameters dim = mapVisualParameters(features, 2.0F, dimStyle);
    const VisualParameters focused = mapVisualParameters(
        features, 2.0F, focusedStyle);
    SceneInstance center;
    center.position = QVector3D(0.0F, 0.0F, 0.0F);
    center.random = 0.5F;
    center.zone = ColorZone::Peak;

    QVERIFY(terrainHeight(center, focused, 2.0F, focusedStyle)
            > terrainHeight(center, dim, 2.0F, dimStyle) + 1.0F);
}

void TerrainReactorStateTest::nearbyRandomnessKeepsTerrainSoftWithoutThresholdSpikes()
{
    AudioFeatures features;
    features.bands.fill(0.42F);
    features.energy = 0.62F;
    RenderStyleSnapshot style;
    style.centerHighlight = 0.72F;
    const VisualParameters visual = mapVisualParameters(features, 2.0F, style);

    SceneInstance lower;
    lower.position = QVector3D(8.0F, 0.0F, 8.0F);
    lower.random = 0.779F;
    lower.zone = ColorZone::Peak;
    SceneInstance upper = lower;
    upper.random = 0.781F;

    const float lowerHeight = terrainHeight(lower, visual, 2.0F, style);
    const float upperHeight = terrainHeight(upper, visual, 2.0F, style);
    QVERIFY2(std::abs(upperHeight - lowerHeight) < 0.35F,
             "Near-identical neighbouring seeds must not create a hard spike");
}

void TerrainReactorStateTest::idleTerrainKeepsFineVisibleReliefWithoutMusic()
{
    const VisualParameters silent;
    const RenderStyleSnapshot style;
    SceneInstance sample;
    sample.position = QVector3D(12.0F, 0.0F, -7.0F);
    sample.random = 0.52F;

    const float first = terrainHeight(sample, silent, 0.0F, style);
    const float second = terrainHeight(sample, silent, 1.5F, style);
    QVERIFY2(first > 0.65F && second > 0.65F,
             "Idle terrain must remain visibly textured instead of collapsing flat");
    QVERIFY(first < 3.0F && second < 3.0F);
    QVERIFY(std::abs(first - second) < 0.35F);
}

void TerrainReactorStateTest::idleTerrainFadesOutsideResponseField()
{
    const VisualParameters silent;
    const RenderStyleSnapshot style;
    SceneInstance inner;
    inner.position = QVector3D(12.0F, 0.0F, -7.0F);
    inner.random = 0.46F;
    inner.zone = ColorZone::Cool;
    SceneInstance outer = inner;
    outer.position = QVector3D(88.0F, 0.0F, 0.0F);

    const float innerHeight = terrainHeight(inner, silent, 0.75F, style);
    const float outerHeight = terrainHeight(outer, silent, 0.75F, style);
    QVERIFY(innerHeight > 0.65F);
    QVERIFY2(outerHeight < 0.16F,
             "Idle relief must fade to a fine dark floor outside the reactor field");
}

void TerrainReactorStateTest::automaticQualityUsesHysteresisCooldownAndEffectFirstOrder()
{
    AutomaticQualityController quality;
    const auto observe = [&quality](double workMilliseconds,
                                    double elapsedSeconds) {
        quality.observeWorkSample(workMilliseconds);
        quality.advanceWallClock(elapsedSeconds);
    };
    QCOMPARE(quality.stage(), DegradationStage::Full);

    observe(40.0, 1.99);
    QCOMPARE(quality.stage(), DegradationStage::Full);
    observe(40.0, 0.01);
    QCOMPARE(quality.stage(), DegradationStage::ReducedParticles);
    const QualityConfiguration particlesReduced = quality.configuration();
    QVERIFY(particlesReduced.particleCount < 140);
    QCOMPARE(particlesReduced.floatingCount, 80);
    QCOMPARE(particlesReduced.meteorCount, 20);
    QCOMPARE(particlesReduced.rippleCount, 10);
    QCOMPARE(particlesReduced.gridSize, 160);

    observe(40.0, 4.99);
    QCOMPARE(quality.stage(), DegradationStage::ReducedParticles);
    observe(40.0, 0.01);
    observe(40.0, 2.0);
    QCOMPARE(quality.stage(), DegradationStage::ReducedMeteors);
    const QualityConfiguration meteorsReduced = quality.configuration();
    QCOMPARE(meteorsReduced.particleCount, particlesReduced.particleCount);
    QVERIFY(meteorsReduced.meteorCount < particlesReduced.meteorCount);
    QCOMPARE(meteorsReduced.rippleCount, particlesReduced.rippleCount);
    QCOMPARE(meteorsReduced.gridSize, particlesReduced.gridSize);
    observe(40.0, 5.0);
    observe(40.0, 2.0);
    QCOMPARE(quality.stage(), DegradationStage::ReducedRipples);
    observe(40.0, 5.0);
    observe(40.0, 2.0);
    QCOMPARE(quality.stage(), DegradationStage::ReducedGrid);
    observe(40.0, 5.0);
    observe(40.0, 2.0);
    QCOMPARE(quality.stage(), DegradationStage::ReducedResolution);

    observe(10.0, 7.99);
    QCOMPARE(quality.stage(), DegradationStage::ReducedResolution);
    observe(10.0, 0.01);
    QCOMPARE(quality.stage(), DegradationStage::ReducedGrid);
}

void TerrainReactorStateTest::qualityFeedbackSeparatesPacerDelayFromWorkCost()
{
    const auto simulateIdleDisplay = [](double refreshRate) {
        AutomaticQualityController quality;
        FramePacer pacer;
        double previousAllowed = 0.0;
        const int ticks = qRound(refreshRate * 3.0);
        for (int tick = 0; tick <= ticks; ++tick) {
            const double now = double(tick) / refreshRate;
            if (!pacer.shouldRender(now, 30.0)) continue;
            quality.observeWorkSample(2.0);
            quality.advanceWallClock(now - previousAllowed);
            previousAllowed = now;
        }
        return quality.stage();
    };

    QCOMPARE(simulateIdleDisplay(75.0), DegradationStage::Full);
    QCOMPARE(simulateIdleDisplay(165.0), DegradationStage::Full);

    AutomaticQualityController overloaded;
    overloaded.observeWorkSample(40.0);
    overloaded.advanceWallClock(1.99);
    QCOMPARE(overloaded.stage(), DegradationStage::Full);
    overloaded.advanceWallClock(0.01);
    QCOMPARE(overloaded.stage(), DegradationStage::ReducedParticles);

    overloaded.observeWorkSample(2.0);
    overloaded.advanceWallClock(7.99);
    QCOMPARE(overloaded.stage(), DegradationStage::ReducedParticles);
    overloaded.advanceWallClock(0.01);
    QCOMPARE(overloaded.stage(), DegradationStage::Full);
}

void TerrainReactorStateTest::inactiveOrOccludedGateFreezesAllWorkCounters()
{
    RenderWorkGate gate;
    gate.setActive(true);
    gate.setVisible(true);
    gate.setExposed(true);
    QVERIFY(gate.advance(true));
    QVERIFY((gate.counters() == WorkCounters{1, 1, 1}));

    gate.setActive(false);
    QVERIFY(!gate.advance(true));
    gate.setActive(true);
    gate.setVisible(false);
    QVERIFY(!gate.advance(true));
    gate.setVisible(true);
    gate.setExposed(false);
    QVERIFY(!gate.advance(true));
    QVERIFY((gate.counters() == WorkCounters{1, 1, 1}));

    gate.setExposed(true);
    QVERIFY(gate.advance(false));
    QVERIFY((gate.counters() == WorkCounters{2, 2, 1}));
}

void TerrainReactorStateTest::rendererOwnershipHasOneLiveResourceGeneration()
{
    RendererResourceState resources;
    QVERIFY(resources.acquireRenderer(41));
    QVERIFY(!resources.acquireRenderer(42));
    QCOMPARE(resources.liveRendererCount(), 1);
    const quint64 firstGeneration = resources.initializeResources();
    QVERIFY(firstGeneration > 0);
    QCOMPARE(resources.initializeResources(), firstGeneration);

    resources.invalidateResources();
    QVERIFY(!resources.resourcesReady());
    const quint64 rebuiltGeneration = resources.initializeResources();
    QVERIFY(rebuiltGeneration > firstGeneration);
    resources.releaseRenderer(41);
    QCOMPARE(resources.liveRendererCount(), 0);
    QVERIFY(resources.acquireRenderer(42));

    RendererResourceState otherItem;
    QVERIFY(otherItem.acquireRenderer(77));
    const quint64 otherGeneration = otherItem.initializeResources();
    QVERIFY(otherGeneration > resources.generation());
}

void TerrainReactorStateTest::manualCameraDeltaPreservesRendererMotion()
{
    CameraMotion renderer;
    renderer.advance(5.0, 5.0F, 1.0F);
    const float automaticallyRotated = renderer.snapshot().yaw;

    CameraSnapshot previousGui;
    CameraSnapshot nextGui = previousGui;
    nextGui.yaw += 0.25F;
    nextGui.pitch -= 0.1F;
    nextGui.distance = 64.0F;
    renderer.applyManualDelta(previousGui, nextGui, 5.0);
    QCOMPARE(renderer.snapshot().yaw, automaticallyRotated + 0.25F);
    QCOMPARE(renderer.snapshot().distance, 64.0F);

    renderer.advance(8.99, 1.0F, 1.0F);
    QCOMPARE(renderer.snapshot().yaw, automaticallyRotated + 0.25F);
    renderer.advance(9.01, 1.0F, 1.0F);
    QVERIFY(renderer.snapshot().yaw > automaticallyRotated + 0.25F);
}

void TerrainReactorStateTest::punchEventsAreConsumedOnceByRevision()
{
    RendererResourceState lifecycle;
    CameraMotion camera;
    PunchEventConsumer consumer(lifecycle);
    const PunchEvent first{0.7F, 1};
    QVERIFY(consumer.consume(first, camera));
    const float initialPunch = camera.snapshot().punch;
    QVERIFY(initialPunch >= 0.69F);

    camera.advance(0.1, 0.1F, 0.0F);
    const float decayedPunch = camera.snapshot().punch;
    QVERIFY(decayedPunch < initialPunch);

    CameraSnapshot previousGui;
    previousGui.punch = first.strength;
    CameraSnapshot nextGui = previousGui;
    nextGui.yaw += 0.1F;
    nextGui.distance = 64.0F;
    camera.applyManualDelta(previousGui, nextGui, 0.1);
    QCOMPARE(camera.snapshot().punch, decayedPunch);

    QVERIFY(!consumer.consume(first, camera));
    QCOMPARE(camera.snapshot().punch, decayedPunch);

    const PunchEvent sameStrength{0.7F, 2};
    QVERIFY(consumer.consume(sameStrength, camera));
    QVERIFY(camera.snapshot().punch > decayedPunch);
    camera.advance(0.2, 0.1F, 0.0F);
    const float beforeWeaker = camera.snapshot().punch;
    const PunchEvent weaker{0.2F, 3};
    QVERIFY(consumer.consume(weaker, camera));
    QVERIFY(camera.snapshot().punch > beforeWeaker);
}

void TerrainReactorStateTest::punchRevisionClaimSurvivesRendererRebuild()
{
    RendererResourceState itemLifecycle;
    const PunchEvent existing{0.6F, 41};

    CameraMotion firstCamera;
    PunchEventConsumer firstRenderer(itemLifecycle);
    QVERIFY(firstRenderer.consume(existing, firstCamera));

    CameraMotion rebuiltCamera;
    PunchEventConsumer rebuiltRenderer(itemLifecycle);
    QVERIFY(!rebuiltRenderer.consume(existing, rebuiltCamera));
    QCOMPARE(rebuiltCamera.snapshot().punch, 0.0F);

    const PunchEvent next{0.6F, 42};
    QVERIFY(rebuiltRenderer.consume(next, rebuiltCamera));
    QVERIFY(rebuiltCamera.snapshot().punch >= 0.59F);

    RendererResourceState otherItemLifecycle;
    CameraMotion otherItemCamera;
    PunchEventConsumer otherItemRenderer(otherItemLifecycle);
    QVERIFY(otherItemRenderer.consume(existing, otherItemCamera));

    RendererResourceState concurrentLifecycle;
    std::atomic<int> claims{0};
    const PunchEvent concurrentEvent{0.5F, 90};
    const auto claim = [&] {
        CameraMotion localCamera;
        PunchEventConsumer renderer(concurrentLifecycle);
        if (renderer.consume(concurrentEvent, localCamera)) ++claims;
    };
    std::thread firstClaim(claim);
    std::thread secondClaim(claim);
    firstClaim.join();
    secondClaim.join();
    QCOMPARE(claims.load(), 1);
}

void TerrainReactorStateTest::ecoFramePacerLimitsWorkToThirtyFrames()
{
    FramePacer pacer;
    int rendered = 0;
    for (int tick = 0; tick <= 120; ++tick) {
        if (pacer.shouldRender(double(tick) / 120.0, 30.0)) ++rendered;
    }
    QVERIFY(rendered >= 30 && rendered <= 31);
    QVERIFY(pacer.shouldRender(2.0, 60.0));
}

void TerrainReactorStateTest::manualCameraControlRecoversAfterFourSeconds()
{
    CameraMotion camera;
    const CameraSnapshot initial = camera.snapshot();
    camera.orbitBy(0.4F, -0.2F, 1.0);
    camera.zoomBy(-10000.0F, 1.0);
    camera.applyBeatPunch(0.8F);
    const CameraSnapshot manual = camera.snapshot();
    QVERIFY(manual.yaw != initial.yaw);
    QVERIFY(manual.pitch != initial.pitch);
    QCOMPARE(manual.distance, 42.0F);
    QVERIFY(manual.punch > 0.0F);

    camera.advance(4.99, 0.5F, 1.0F);
    QCOMPARE(camera.snapshot().yaw, manual.yaw);
    camera.advance(5.01, 0.5F, 1.0F);
    QVERIFY(camera.snapshot().yaw > manual.yaw);
    QVERIFY(camera.snapshot().punch < manual.punch);
}

QTEST_APPLESS_MAIN(TerrainReactorStateTest)

#include "terrain_reactor_state_test.moc"

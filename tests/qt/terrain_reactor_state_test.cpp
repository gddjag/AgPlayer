#include "terrain_reactor_state.hpp"

#include <QTest>

#include <algorithm>
#include <array>
#include <limits>
#include <thread>

using namespace agplayer::terrain;

class TerrainReactorStateTest final : public QObject {
    Q_OBJECT

private slots:
    void fixedSeedProducesStableLayoutAndColorZones();
    void terrainLayoutFormsCircularStageAndStarsStayOutsideCore();
    void meteorsHaveFiniteTrailsAndCollisionEffects();
    void meteorGroupsKeepOneDeterministicPrimaryImpact();
    void audioFeaturesDriveBoundedVisualParameters();
    void bassEnvelopeUsesFastAttackAndSlowRelease();
    void multiWaveSourcesAreStableDistributedAndBounded();
    void automaticQualityUsesHysteresisCooldownAndEffectFirstOrder();
    void qualityFeedbackSeparatesPacerDelayFromWorkCost();
    void presentedFrameDelayTriggersAdaptiveDowngrade();
    void inactiveOrOccludedGateFreezesAllWorkCounters();
    void rendererOwnershipHasOneLiveResourceGeneration();
    void manualCameraControlRecoversAfterFourSeconds();
    void manualCameraDeltaPreservesRendererMotion();
    void nonFiniteCameraInputsPreserveFiniteBoundedState();
    void punchEventsAreConsumedOnceByRevision();
    void punchRevisionClaimSurvivesRendererRebuild();
    void immersiveStyleControlsMapToBoundedDistinctDynamics();
    void impactEventsProduceOneBoundedPulsePerRevision();
    void explicitImpactRaisesCenterAndTravelingRing();
    void steadyMusicKeepsCenterVisiblyFocused();
    void nearbyRandomnessKeepsTerrainSoftWithoutThresholdSpikes();
    void finalReferenceProfileKeepsCoreBroadAndSpikesSubordinate();
    void equalStrengthHighsRemainHeightSubordinateToLowMids();
    void representativeGridHasBroadCoreWithoutIsolatedTowers();
    void floatingCubesAreDeterministicAndVisuallySubordinate();
    void continuousAndEventControlsStayDistinctBoundedAndLive();
    void nonFiniteInputsUseFiniteBoundedFallbacks();
    void idleTerrainKeepsFineVisibleReliefWithoutMusic();
    void idleTerrainFadesOutsideResponseField();
    void trackIdentityProducesStableBoundedDistinctPalette();
    void ecoFramePacerLimitsWorkToThirtyFrames();
    void balancedFramePacerDoesNotCollapseToThirtyOnSixtyHertz();
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
    QVERIFY(first.terrain.size() < 81);
    QVERIFY(first.terrain.size() > 48);
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
    const auto center = std::find_if(first.terrain.cbegin(), first.terrain.cend(),
                                     [](const SceneInstance& instance) {
        return qFuzzyIsNull(instance.position.x())
            && qFuzzyIsNull(instance.position.z());
    });
    QVERIFY(center != first.terrain.cend());
    QCOMPARE(center->zone, ColorZone::Peak);
}

void TerrainReactorStateTest::terrainLayoutFormsCircularStageAndStarsStayOutsideCore()
{
    const SceneLayout layout = makeSceneLayout(0x5eedU, 32, 8, 4, 48);
    QVERIFY(!layout.terrain.isEmpty());
    QVERIFY(!layout.particles.isEmpty());

    for (const SceneInstance& instance : layout.terrain) {
        const float radius = std::hypot(instance.position.x(),
                                        instance.position.z());
        QVERIFY2(radius <= 84.01F, "terrain cell escaped the circular stage");
    }
    for (const SceneInstance& star : layout.particles) {
        const float radius = std::hypot(star.position.x(), star.position.z());
        QVERIFY2(radius >= 72.0F, "deep-space star leaked into the reactor core");
        QVERIFY2(star.position.y() >= 10.0F,
                 "deep-space star is too low to read as environment");
    }
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
    QCOMPARE(low.responseRadius, 28.0F);
    QCOMPARE(high.responseRadius, 123.2F);
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

void TerrainReactorStateTest::finalReferenceProfileKeepsCoreBroadAndSpikesSubordinate()
{
    AudioFeatures features;
    features.bands.fill(0.48F);
    features.energy = 0.68F;
    RenderStyleSnapshot style;
    style.centerHighlight = 0.74F;
    const VisualParameters visual = mapVisualParameters(features, 2.0F, style);

    SceneInstance quietCore;
    quietCore.position = QVector3D(0.0F, 0.0F, 0.0F);
    quietCore.random = 0.08F;
    quietCore.zone = ColorZone::Peak;
    SceneInstance spikyCore = quietCore;
    spikyCore.random = 0.96F;
    SceneInstance shoulder = quietCore;
    shoulder.position = QVector3D(19.0F, 0.0F, 0.0F);
    shoulder.random = 0.52F;
    SceneInstance outer = shoulder;
    outer.position = QVector3D(62.0F, 0.0F, 0.0F);

    const float quietCoreHeight = terrainHeight(quietCore, visual, 2.0F, style);
    const float spikyCoreHeight = terrainHeight(spikyCore, visual, 2.0F, style);
    const float shoulderHeight = terrainHeight(shoulder, visual, 2.0F, style);
    const float outerHeight = terrainHeight(outer, visual, 2.0F, style);

    QVERIFY2(std::abs(spikyCoreHeight - quietCoreHeight) < 4.0F,
             "Random seeds must texture the core without creating isolated towers");
    QVERIFY2((quietCoreHeight + spikyCoreHeight) * 0.5F > outerHeight + 5.0F,
             "The luminous center must remain dominant over the outer field");
    QVERIFY2(shoulderHeight > outerHeight + 0.45F,
             "The reactor must retain a layered falloff instead of a flat noisy field");
    QVERIFY(std::max(quietCoreHeight, spikyCoreHeight) < 22.0F);
}

void TerrainReactorStateTest::equalStrengthHighsRemainHeightSubordinateToLowMids()
{
    AudioFeatures lowMidFeatures;
    lowMidFeatures.bands = {0.72F, 0.72F, 0.72F, 0.72F,
                            0.0F, 0.0F, 0.0F, 0.0F};
    lowMidFeatures.energy = 0.72F;
    AudioFeatures highFeatures;
    highFeatures.bands = {0.0F, 0.0F, 0.0F, 0.0F,
                          0.72F, 0.72F, 0.72F, 0.72F};
    highFeatures.energy = 0.72F;
    RenderStyleSnapshot style;
    style.peakBoost = 1.0F;
    const VisualParameters lowMids = mapVisualParameters(
        lowMidFeatures, 2.25F, style);
    const VisualParameters highs = mapVisualParameters(
        highFeatures, 2.25F, style);
    const SceneLayout layout = makeSceneLayout(0x4b1dU, 25, 0, 0, 0);

    float tallestLowMid = 0.0F;
    float tallestHigh = 0.0F;
    for (const SceneInstance& instance : layout.terrain) {
        if (std::hypot(instance.position.x(), instance.position.z()) > 56.0F) {
            continue;
        }
        tallestLowMid = std::max(tallestLowMid,
            terrainHeight(instance, lowMids, 2.25F, style));
        tallestHigh = std::max(tallestHigh,
            terrainHeight(instance, highs, 2.25F, style));
    }
    qInfo() << "equal-strength relief low/mid vs high:"
            << tallestLowMid << tallestHigh;
    QVERIFY2(tallestHigh <= tallestLowMid,
             "High-frequency detail must not become taller terrain relief than low/mids");
}

void TerrainReactorStateTest::representativeGridHasBroadCoreWithoutIsolatedTowers()
{
    AudioFeatures features;
    features.bands.fill(0.58F);
    features.energy = 0.68F;
    RenderStyleSnapshot style;
    style.terrainAmplitude = 0.74F;
    style.centerHighlight = 0.70F;
    style.peakBoost = 0.82F;
    const VisualParameters visual = mapVisualParameters(features, 1.75F, style);
    const SceneLayout layout = makeSceneLayout(0x8a31U, 25, 0, 0, 0);

    struct Sample { QVector3D position; float height; };
    QVector<Sample> samples;
    QVector<float> ordered;
    for (const SceneInstance& instance : layout.terrain) {
        if (std::hypot(instance.position.x(), instance.position.z()) > 42.0F) {
            continue;
        }
        const float height = terrainHeight(instance, visual, 1.75F, style);
        samples.append({instance.position, height});
        ordered.append(height);
    }
    QVERIFY(ordered.size() > 80);
    std::sort(ordered.begin(), ordered.end());
    const float median = ordered.at(ordered.size() / 2);
    const Sample peak = *std::max_element(samples.cbegin(), samples.cend(),
        [](const Sample& left, const Sample& right) {
            return left.height < right.height;
        });
    float neighbourTotal = 0.0F;
    int neighbourCount = 0;
    for (const Sample& sample : samples) {
        const float separation = (sample.position - peak.position).length();
        if (separation > 0.01F && separation < 10.0F) {
            neighbourTotal += sample.height;
            ++neighbourCount;
        }
    }
    QVERIFY(neighbourCount >= 4);
    const float neighbourMean = neighbourTotal / float(neighbourCount);
    qInfo() << "representative core peak/median/neighbour:"
            << peak.height << median << neighbourMean;
    // Ratios are intentionally perceptual bounds: a 65% median excursion
    // retains layered relief, while a 40% local excursion prevents a lone tower.
    QVERIFY2(peak.height <= median * 1.65F,
             "Representative core peak is too isolated from the field median");
    QVERIFY2(peak.height <= neighbourMean * 1.40F,
             "Representative core peak is too isolated from its neighbours");
}

void TerrainReactorStateTest::floatingCubesAreDeterministicAndVisuallySubordinate()
{
    const SceneLayout first = makeSceneLayout(0x71c3U, 25, 64, 0, 0);
    const SceneLayout repeated = makeSceneLayout(0x71c3U, 25, 64, 0, 0);
    QCOMPARE(first.floating, repeated.floating);
    QCOMPARE(first.floating.size(), 64);

    QVector<float> sizes;
    for (const SceneInstance& cube : first.floating) {
        QCOMPARE(cube.scale.x(), cube.scale.y());
        QCOMPARE(cube.scale.x(), cube.scale.z());
        sizes.append(cube.scale.x());
    }
    std::sort(sizes.begin(), sizes.end());
    const float median = sizes.at(sizes.size() / 2);
    qInfo() << "floating cube median/max size:" << median << sizes.back();
    // At the 25x25 representative grid, terrain cells span 6.72 world units;
    // sub-unit cubes remain atmosphere instead of competing terrain masses.
    QVERIFY2(sizes.back() <= 0.85F,
             "Floating environment cubes are too large to remain subordinate");
    QVERIFY2(median <= 0.68F,
             "The floating cube population is visually too heavy");
}

void TerrainReactorStateTest::continuousAndEventControlsStayDistinctBoundedAndLive()
{
    AudioFeatures continuous;
    continuous.bands.fill(0.44F);
    continuous.energy = 0.46F;
    continuous.spectralFlux = 0.18F;
    const VisualParameters baseline = mapVisualParameters(continuous, 1.0F);
    AudioFeatures eventful = continuous;
    eventful.kick = 1.0F;
    eventful.snare = 1.0F;
    const VisualParameters event = mapVisualParameters(eventful, 1.0F);
    QCOMPARE(event.bands, baseline.bands);
    QCOMPARE(event.energy, baseline.energy);
    QVERIFY(event.rippleStrength > baseline.rippleStrength);
    QVERIFY(event.cameraPunch > baseline.cameraPunch);
    QVERIFY(event.rippleStrength <= 1.0F);
    QVERIFY(event.cameraPunch <= 1.0F);

    SceneInstance detail;
    detail.position = QVector3D(9.0F, 0.0F, -6.0F);
    detail.random = 0.92F;
    detail.zone = ColorZone::Peak;
    AudioFeatures highFeatures;
    highFeatures.bands[4] = 0.72F;
    highFeatures.energy = 0.42F;
    RenderStyleSnapshot restrained;
    restrained.peakBoost = 0.0F;
    RenderStyleSnapshot emphasized = restrained;
    emphasized.peakBoost = 1.0F;
    const float restrainedHeight = terrainHeight(detail,
        mapVisualParameters(highFeatures, 1.0F, restrained),
        1.0F, restrained);
    const float emphasizedHeight = terrainHeight(detail,
        mapVisualParameters(highFeatures, 1.0F, emphasized),
        1.0F, emphasized);
    QVERIFY2(emphasizedHeight > restrainedHeight + 0.10F,
             "Peak boost must retain a visible bounded terrain-detail effect");
    QVERIFY2(emphasizedHeight <= restrainedHeight * 1.30F + 0.20F,
             "Peak boost must not turn restrained detail into a tower");

    VisualParameters pulse;
    pulse.rippleStrength = 1.0F;
    pulse.timeSeconds = 1.0F;
    SceneInstance ring = detail;
    ring.position = QVector3D(13.5F, 0.0F, 0.0F);
    RenderStyleSnapshot ripplesOn;
    ripplesOn.idleBreathingEnabled = false;
    RenderStyleSnapshot ripplesOff = ripplesOn;
    ripplesOff.ripplesEnabled = false;
    QVERIFY2(terrainHeight(ring, pulse, 1.0F, ripplesOn)
                 > terrainHeight(ring, pulse, 1.0F, ripplesOff) + 0.20F,
             "Ripple toggle must disable discrete ring relief");
}

void TerrainReactorStateTest::nonFiniteInputsUseFiniteBoundedFallbacks()
{
    const std::array nonFinite{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    const auto boundedUnit = [](float value) {
        return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
    };

    for (const float invalid : nonFinite) {
        RenderStyleSnapshot style;
        style.terrainAmplitude = invalid;
        style.motionResponse = invalid;
        style.gradientLayers = invalid;
        style.glowIntensity = invalid;
        style.cinemaShake = invalid;
        style.autoRotate = invalid;
        style.peakBoost = invalid;
        style.inputCompression = invalid;
        style.audioResponse = invalid;
        style.responseRange = invalid;
        style.centerHighlight = invalid;
        style.rhythmStrength = invalid;
        style.depthOfField = invalid;
        style.subjectClarity = invalid;
        style.autoRotateSpeed = invalid;
        style.rhythmSensitivity = invalid;
        const RenderDynamics dynamics = mapRenderDynamics(style);
        QVERIFY(std::isfinite(dynamics.inputCompression));
        QVERIFY(std::isfinite(dynamics.audioResponse));
        QVERIFY(std::isfinite(dynamics.responseRadius));
        QVERIFY(std::isfinite(dynamics.centerHighlight));
        QVERIFY(std::isfinite(dynamics.rhythmStrength));
        QVERIFY(std::isfinite(dynamics.depthOfField));
        QVERIFY(std::isfinite(dynamics.subjectClarity));
        QVERIFY(std::isfinite(dynamics.autoRotateSpeed));
        QVERIFY(std::isfinite(dynamics.rhythmSensitivity));
        QVERIFY(dynamics.inputCompression >= 0.2F
                && dynamics.inputCompression <= 1.5F);
        QVERIFY(dynamics.audioResponse >= 0.2F
                && dynamics.audioResponse <= 2.0F);
        QVERIFY(dynamics.responseRadius >= 28.0F
                && dynamics.responseRadius <= 123.2F);
        QVERIFY(dynamics.centerHighlight >= 0.0F
                && dynamics.centerHighlight <= 1.0F);
        QVERIFY(dynamics.rhythmStrength >= 0.0F
                && dynamics.rhythmStrength <= 1.4F);
        QVERIFY(dynamics.depthOfField >= 0.0F
                && dynamics.depthOfField <= 1.5F);
        QVERIFY(dynamics.subjectClarity >= 0.2F
                && dynamics.subjectClarity <= 1.4F);
        QVERIFY(dynamics.autoRotateSpeed >= 0.0F
                && dynamics.autoRotateSpeed <= 2.0F);
        QVERIFY(dynamics.rhythmSensitivity >= 0.0F
                && dynamics.rhythmSensitivity <= 1.0F);

        AudioFeatures features;
        features.bands.fill(invalid);
        features.energy = invalid;
        features.spectralFlux = invalid;
        features.kick = invalid;
        features.snare = invalid;
        const VisualParameters visual = mapVisualParameters(
            features, invalid, style);
        for (const float band : visual.bands) QVERIFY(boundedUnit(band));
        QVERIFY(boundedUnit(visual.energy));
        QVERIFY(boundedUnit(visual.spectralFlux));
        QVERIFY(boundedUnit(visual.rippleStrength));
        QVERIFY(boundedUnit(visual.particleActivity));
        QVERIFY(boundedUnit(visual.meteorActivity));
        QVERIFY(boundedUnit(visual.cameraPunch));
        QVERIFY(boundedUnit(visual.impactStrength));
        QVERIFY(boundedUnit(visual.impactAge));
        QVERIFY(std::isfinite(visual.timeSeconds));
        QVERIFY(visual.timeSeconds >= 0.0F);

        SceneInstance instance;
        instance.position = QVector3D(invalid, 0.0F, invalid);
        instance.random = invalid;
        VisualParameters unsafe;
        unsafe.bands.fill(invalid);
        unsafe.energy = invalid;
        unsafe.spectralFlux = invalid;
        unsafe.rippleStrength = invalid;
        unsafe.particleActivity = invalid;
        unsafe.meteorActivity = invalid;
        unsafe.cameraPunch = invalid;
        unsafe.impactStrength = invalid;
        unsafe.impactAge = invalid;
        unsafe.timeSeconds = invalid;
        const float height = terrainHeight(instance, unsafe, invalid, style);
        QVERIFY2(std::isfinite(height),
                 "terrainHeight must never send a non-finite scale to uniforms");
        QVERIFY(height >= 0.035F && height <= 36.0F);
    }
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
    QVERIFY(particlesReduced.particleCount < 96);
    QCOMPARE(particlesReduced.floatingCount, 52);
    QCOMPARE(particlesReduced.meteorCount, 10);
    QCOMPARE(particlesReduced.rippleCount, 8);
    QCOMPARE(particlesReduced.gridSize, 128);

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

void TerrainReactorStateTest::bassEnvelopeUsesFastAttackAndSlowRelease()
{
    BassEnvelopeFollower envelope;
    const BassEnvelopeSnapshot idle = envelope.advance(0.0F, 1.0F / 60.0F);
    QCOMPARE(idle.fast, 0.0F);
    QCOMPARE(idle.slow, 0.0F);

    const BassEnvelopeSnapshot attack = envelope.advance(1.0F, 0.033F);
    QVERIFY(attack.fast > 0.55F);
    QVERIFY(attack.fast > attack.slow);

    const BassEnvelopeSnapshot release = envelope.advance(0.0F, 0.22F);
    QVERIFY(release.fast < attack.fast);
    QVERIFY(release.slow > release.fast);
    QVERIFY(release.slow > 0.10F);
}

void TerrainReactorStateTest::multiWaveSourcesAreStableDistributedAndBounded()
{
    const auto first = multiWaveSources(0x5eedU);
    const auto again = multiWaveSources(0x5eedU);
    const auto other = multiWaveSources(0x5eeeU);
    QCOMPARE(first, again);
    QVERIFY(first != other);
    QCOMPARE(first.size(), std::size_t(8));

    bool hasOffCenterSource = false;
    for (const QVector4D& source : first) {
        QVERIFY(std::hypot(source.x(), source.y()) <= 62.0F);
        QVERIFY(source.z() >= 0.0F && source.z() < 1.0F);
        QVERIFY(source.w() >= 0.45F && source.w() <= 1.0F);
        hasOffCenterSource = hasOffCenterSource
            || std::hypot(source.x(), source.y()) > 12.0F;
    }
    QVERIFY(hasOffCenterSource);
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

    AutomaticQualityController recoveringFromBalancedLoad;
    recoveringFromBalancedLoad.observeFrameSample(40.0, 40.0, 22.222);
    recoveringFromBalancedLoad.advanceWallClock(2.0);
    QCOMPARE(recoveringFromBalancedLoad.stage(),
             DegradationStage::ReducedParticles);
    for (int cycle = 0; cycle < 121; ++cycle) {
        for (const double elapsed : {33.333, 16.667, 16.667}) {
            recoveringFromBalancedLoad.observeFrameSample(
                2.0, elapsed, 22.222);
            recoveringFromBalancedLoad.advanceWallClock(elapsed / 1000.0);
        }
    }
    QCOMPARE(recoveringFromBalancedLoad.stage(), DegradationStage::Full);
}

void TerrainReactorStateTest::presentedFrameDelayTriggersAdaptiveDowngrade()
{
    AutomaticQualityController quality;

    // GPU saturation can leave command submission cheap while actual frame
    // cadence misses the budget. The adaptive controller must observe both.
    quality.observeFrameSample(2.0, 52.0, 33.333);
    quality.advanceWallClock(1.99);
    QCOMPARE(quality.stage(), DegradationStage::Full);
    quality.advanceWallClock(0.01);
    QCOMPARE(quality.stage(), DegradationStage::ReducedParticles);

    AutomaticQualityController onBudget;
    onBudget.observeFrameSample(2.0, 33.333, 33.333);
    onBudget.advanceWallClock(3.0);
    QCOMPARE(onBudget.stage(), DegradationStage::Full);

    AutomaticQualityController sustainedThirtyFps;
    for (int frame = 0; frame < 70; ++frame) {
        sustainedThirtyFps.observeFrameSample(2.0, 33.333, 22.222);
        sustainedThirtyFps.advanceWallClock(0.033333);
    }
    QCOMPARE(sustainedThirtyFps.stage(),
             DegradationStage::ReducedParticles);
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

void TerrainReactorStateTest::nonFiniteCameraInputsPreserveFiniteBoundedState()
{
    const std::array nonFinite{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    CameraMotion validCamera;
    validCamera.orbitBy(0.25F, -0.08F, 1.0);
    validCamera.zoomBy(-900.0F, 1.0);
    validCamera.applyBeatPunch(0.4F);
    const CameraSnapshot valid = validCamera.snapshot();
    const double validManualUntil = validCamera.manualUntilSeconds();
    const auto isFiniteBounded = [](const CameraSnapshot& snapshot) {
        return std::isfinite(snapshot.yaw)
            && std::isfinite(snapshot.pitch)
            && snapshot.pitch >= 0.12F && snapshot.pitch <= 1.15F
            && std::isfinite(snapshot.distance)
            && snapshot.distance >= 42.0F && snapshot.distance <= 220.0F
            && std::isfinite(snapshot.punch)
            && snapshot.punch >= 0.0F && snapshot.punch <= 1.0F;
    };
    const auto preservesValid = [&valid](const CameraSnapshot& snapshot) {
        return snapshot.yaw == valid.yaw
            && snapshot.pitch == valid.pitch
            && snapshot.distance == valid.distance
            && snapshot.punch == valid.punch;
    };

    for (const float invalid : nonFinite) {
        CameraMotion orbitYaw = validCamera;
        orbitYaw.orbitBy(invalid, 0.0F, 2.0);
        QVERIFY(isFiniteBounded(orbitYaw.snapshot()));
        QVERIFY(preservesValid(orbitYaw.snapshot()));
        QCOMPARE(orbitYaw.manualUntilSeconds(), validManualUntil);

        CameraMotion orbitPitch = validCamera;
        orbitPitch.orbitBy(0.0F, invalid, 2.0);
        QVERIFY(isFiniteBounded(orbitPitch.snapshot()));
        QVERIFY(preservesValid(orbitPitch.snapshot()));
        QCOMPARE(orbitPitch.manualUntilSeconds(), validManualUntil);

        CameraMotion zoom = validCamera;
        zoom.zoomBy(invalid, 2.0);
        QVERIFY(isFiniteBounded(zoom.snapshot()));
        QVERIFY(preservesValid(zoom.snapshot()));
        QCOMPARE(zoom.manualUntilSeconds(), validManualUntil);

        CameraMotion orbitTime = validCamera;
        orbitTime.orbitBy(0.1F, -0.02F, double(invalid));
        QVERIFY(isFiniteBounded(orbitTime.snapshot()));
        QVERIFY(preservesValid(orbitTime.snapshot()));
        QCOMPARE(orbitTime.manualUntilSeconds(), validManualUntil);

        CameraMotion zoomTime = validCamera;
        zoomTime.zoomBy(-20.0F, double(invalid));
        QVERIFY(isFiniteBounded(zoomTime.snapshot()));
        QVERIFY(preservesValid(zoomTime.snapshot()));
        QCOMPARE(zoomTime.manualUntilSeconds(), validManualUntil);

        CameraMotion synchronized = validCamera;
        CameraSnapshot poisoned = valid;
        poisoned.yaw = invalid;
        poisoned.pitch = invalid;
        poisoned.distance = invalid;
        poisoned.punch = invalid;
        synchronized.synchronize(poisoned, double(invalid));
        QVERIFY(isFiniteBounded(synchronized.snapshot()));
        QVERIFY(preservesValid(synchronized.snapshot()));
        QCOMPARE(synchronized.manualUntilSeconds(), validManualUntil);

        CameraMotion manualDelta = validCamera;
        CameraSnapshot next = valid;
        next.yaw = invalid;
        next.pitch = invalid;
        next.distance = invalid;
        manualDelta.applyManualDelta(valid, next, double(invalid));
        QVERIFY(isFiniteBounded(manualDelta.snapshot()));
        QVERIFY(preservesValid(manualDelta.snapshot()));
        QCOMPARE(manualDelta.manualUntilSeconds(), validManualUntil);

        CameraMotion poisonedPrevious = validCamera;
        CameraSnapshot previous = valid;
        previous.yaw = invalid;
        previous.pitch = invalid;
        previous.distance = invalid;
        poisonedPrevious.applyManualDelta(previous, valid, double(invalid));
        QVERIFY(isFiniteBounded(poisonedPrevious.snapshot()));
        QVERIFY(preservesValid(poisonedPrevious.snapshot()));
        QCOMPARE(poisonedPrevious.manualUntilSeconds(), validManualUntil);

        CameraMotion manualTime = validCamera;
        CameraSnapshot moved = valid;
        moved.yaw += 0.1F;
        moved.pitch += 0.02F;
        moved.distance += 1.0F;
        manualTime.applyManualDelta(valid, moved, double(invalid));
        QVERIFY(isFiniteBounded(manualTime.snapshot()));
        QVERIFY(preservesValid(manualTime.snapshot()));
        QCOMPARE(manualTime.manualUntilSeconds(), validManualUntil);
    }
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

void TerrainReactorStateTest::balancedFramePacerDoesNotCollapseToThirtyOnSixtyHertz()
{
    FramePacer pacer;
    int rendered = 0;
    for (int tick = 0; tick <= 60; ++tick) {
        if (pacer.shouldRender(double(tick) / 60.0, 45.0)) ++rendered;
    }
    QVERIFY2(rendered >= 44 && rendered <= 46,
             qPrintable(QStringLiteral("45 FPS pacing rendered %1 frames")
                            .arg(rendered)));
}

void TerrainReactorStateTest::manualCameraControlRecoversAfterFourSeconds()
{
    CameraMotion camera;
    const CameraSnapshot initial = camera.snapshot();
    QCOMPARE(initial.distance, 160.0F);
    CameraMotion zoomedOut;
    zoomedOut.zoomBy(10000.0F, 1.0);
    QCOMPARE(zoomedOut.snapshot().distance, 220.0F);
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

#include "terrain_reactor_state.hpp"

#include <QTest>

#include <algorithm>
#include <array>

using namespace agplayer::terrain;

class TerrainReactorStateTest final : public QObject {
    Q_OBJECT

private slots:
    void fixedSeedProducesStableLayoutAndColorZones();
    void audioFeaturesDriveBoundedVisualParameters();
    void automaticQualityUsesHysteresisCooldownAndEffectFirstOrder();
    void inactiveOrOccludedGateFreezesAllWorkCounters();
    void rendererOwnershipHasOneLiveResourceGeneration();
    void manualCameraControlRecoversAfterFourSeconds();
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
    QVERIFY(visual.rippleStrength >= 0.9F && visual.rippleStrength <= 1.0F);
    QVERIFY(visual.particleActivity > 0.6F && visual.particleActivity <= 1.0F);
    QVERIFY(visual.meteorActivity > 0.6F && visual.meteorActivity <= 1.0F);
    QVERIFY(visual.cameraPunch > 0.7F && visual.cameraPunch <= 1.0F);

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
    QVERIFY(centerHeight <= 18.0F);
    QVERIFY(edgeHeight >= 0.035F);
}

void TerrainReactorStateTest::automaticQualityUsesHysteresisCooldownAndEffectFirstOrder()
{
    AutomaticQualityController quality;
    QCOMPARE(quality.stage(), DegradationStage::Full);

    quality.observe(40.0, 1.99);
    QCOMPARE(quality.stage(), DegradationStage::Full);
    quality.observe(40.0, 0.01);
    QCOMPARE(quality.stage(), DegradationStage::ReducedParticles);
    const QualityConfiguration particlesReduced = quality.configuration();
    QVERIFY(particlesReduced.particleCount < 180);
    QCOMPARE(particlesReduced.floatingCount, 120);
    QCOMPARE(particlesReduced.meteorCount, 28);
    QCOMPARE(particlesReduced.rippleCount, 10);
    QCOMPARE(particlesReduced.gridSize, 160);

    quality.observe(40.0, 4.99);
    QCOMPARE(quality.stage(), DegradationStage::ReducedParticles);
    quality.observe(40.0, 0.01);
    quality.observe(40.0, 2.0);
    QCOMPARE(quality.stage(), DegradationStage::ReducedMeteors);
    const QualityConfiguration meteorsReduced = quality.configuration();
    QCOMPARE(meteorsReduced.particleCount, particlesReduced.particleCount);
    QVERIFY(meteorsReduced.meteorCount < particlesReduced.meteorCount);
    QCOMPARE(meteorsReduced.rippleCount, particlesReduced.rippleCount);
    QCOMPARE(meteorsReduced.gridSize, particlesReduced.gridSize);
    quality.observe(40.0, 5.0);
    quality.observe(40.0, 2.0);
    QCOMPARE(quality.stage(), DegradationStage::ReducedRipples);
    quality.observe(40.0, 5.0);
    quality.observe(40.0, 2.0);
    QCOMPARE(quality.stage(), DegradationStage::ReducedGrid);
    quality.observe(40.0, 5.0);
    quality.observe(40.0, 2.0);
    QCOMPARE(quality.stage(), DegradationStage::ReducedResolution);

    quality.observe(10.0, 7.99);
    QCOMPARE(quality.stage(), DegradationStage::ReducedResolution);
    quality.observe(10.0, 0.01);
    QCOMPARE(quality.stage(), DegradationStage::ReducedGrid);
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
    QCOMPARE(resources.initializeResources(), quint64{1});
    QCOMPARE(resources.initializeResources(), quint64{1});

    resources.invalidateResources();
    QVERIFY(!resources.resourcesReady());
    QCOMPARE(resources.initializeResources(), quint64{2});
    resources.releaseRenderer(41);
    QCOMPARE(resources.liveRendererCount(), 0);
    QVERIFY(resources.acquireRenderer(42));
}

void TerrainReactorStateTest::manualCameraControlRecoversAfterFourSeconds()
{
    CameraMotion camera;
    const CameraSnapshot initial = camera.snapshot();
    camera.orbitBy(0.4F, -0.2F, 1.0);
    camera.zoomBy(-1000.0F, 1.0);
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

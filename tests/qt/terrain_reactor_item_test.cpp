#include "audio_visual_feature_controller.hpp"
#include "player_experience_controller.hpp"
#include "terrain_reactor_item.hpp"

#include <QSignalSpy>
#include <QImage>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QTest>

#include <array>
#include <cmath>

using namespace agplayer::terrain;

class TerrainReactorItemTest final : public QObject {
    Q_OBJECT

private slots:
    void defaultsDoNotScheduleRendering();
    void consumesTaskOneFeaturesWithoutSpectrumAnalysis();
    void syntheticFeaturesAreDeterministicAndClamped();
    void visibilityAndExposureGateRendering();
    void windowEventsGateShowMinimizeAndRestore();
    void softwareBackendFailsClosedWithoutSchedulingWork();
    void taskOneStyleIsCopiedIntoImmutableSnapshot();
    void highDpiInternalScaleUsesPhysicalPixels();
    void duplicateRendererIsRejectedBySharedLifecycle();
    void cameraPropertiesSupportTaskFourInput();
};

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

void TerrainReactorItemTest::consumesTaskOneFeaturesWithoutSpectrumAnalysis()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    TerrainReactorItem item;
    item.setFeatureSource(&features);
    item.setActive(true);
    const quint64 revisionBeforeUpdate = item.featureRevision();
    QCOMPARE(revisionBeforeUpdate, quint64{1}); // initial immutable source snapshot

    QVariantList spectrum(128, 0.0);
    for (int index = 0; index < 16; ++index) spectrum[index] = 1.0;
    features.processSpectrum(spectrum);

    QCOMPARE(item.featureRevision(), revisionBeforeUpdate + 1);
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

void TerrainReactorItemTest::highDpiInternalScaleUsesPhysicalPixels()
{
    QQuickWindow window;
    QImage image(400, 200, QImage::Format_RGBA8888_Premultiplied);
    QQuickRenderTarget target = QQuickRenderTarget::fromPaintDevice(&image);
    target.setDevicePixelRatio(2.0);
    window.setRenderTarget(target);
    TestableTerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(100, 50));
    item.applyInternalScale(0.75F);
    QCOMPARE(window.effectiveDevicePixelRatio(), 2.0);
    QCOMPARE(item.fixedColorBufferWidth(), 150);
    QCOMPARE(item.fixedColorBufferHeight(), 75);
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
    item.zoomBy(-1000.0, 1.0);
    QCOMPARE(item.punchRevision(), firstPunchRevision);
    item.triggerCameraPunch(0.7);
    QCOMPARE(item.punchRevision(), firstPunchRevision + 1);
    item.triggerCameraPunch(0.2);
    QCOMPARE(item.punchRevision(), firstPunchRevision + 2);
    QCOMPARE(item.cameraYaw(), originalYaw + 0.25);
    QCOMPARE(item.cameraDistance(), 42.0);
    QVERIFY(item.cameraPunch() >= 0.19);
}

QTEST_MAIN(TerrainReactorItemTest)

#include "terrain_reactor_item_test.moc"

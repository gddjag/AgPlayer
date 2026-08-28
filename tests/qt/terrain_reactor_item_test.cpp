#include "audio_visual_feature_controller.hpp"
#include "terrain_reactor_item.hpp"

#include <QSignalSpy>
#include <QQuickWindow>
#include <QTest>

#include <array>
#include <cmath>

class TerrainReactorItemTest final : public QObject {
    Q_OBJECT

private slots:
    void defaultsDoNotScheduleRendering();
    void consumesTaskOneFeaturesWithoutSpectrumAnalysis();
    void syntheticFeaturesAreDeterministicAndClamped();
    void visibilityAndExposureGateRendering();
    void softwareBackendFailsClosedWithoutSchedulingWork();
    void cameraPropertiesSupportTaskFourInput();
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

void TerrainReactorItemTest::cameraPropertiesSupportTaskFourInput()
{
    TerrainReactorItem item;
    const qreal originalYaw = item.cameraYaw();
    item.orbitBy(0.25, -0.1, 1.0);
    item.zoomBy(-1000.0, 1.0);
    item.triggerCameraPunch(0.7);
    QCOMPARE(item.cameraYaw(), originalYaw + 0.25);
    QCOMPARE(item.cameraDistance(), 42.0);
    QVERIFY(item.cameraPunch() >= 0.69);
}

QTEST_MAIN(TerrainReactorItemTest)

#include "terrain_reactor_item_test.moc"

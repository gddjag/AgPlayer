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
#include <limits>

using namespace agplayer::terrain;

class FeatureSourceProbe final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList bands READ bands NOTIFY featuresChanged)
    Q_PROPERTY(double energy READ energy NOTIFY featuresChanged)
    Q_PROPERTY(double spectralFlux READ spectralFlux NOTIFY featuresChanged)
    Q_PROPERTY(bool kickPulse READ kickPulse NOTIFY featuresChanged)
    Q_PROPERTY(bool snarePulse READ snarePulse NOTIFY featuresChanged)
    Q_PROPERTY(quint64 impactRevision READ impactRevision NOTIFY featuresChanged)
    Q_PROPERTY(double impactStrength READ impactStrength NOTIFY featuresChanged)

public:
    QVariantList bands() const { return bands_; }
    double energy() const noexcept { return energy_; }
    double spectralFlux() const noexcept { return spectralFlux_; }
    bool kickPulse() const noexcept { return kick_; }
    bool snarePulse() const noexcept { return snare_; }
    quint64 impactRevision() const noexcept { return impactRevision_; }
    double impactStrength() const noexcept { return impactStrength_; }

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
    quint64 impactRevision_ = 0;
    double impactStrength_ = 0.0;
};

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
    void v46DynamicsAreCopiedAndRemainBounded();
    void featureSourceImpactRevisionIsConsumedWithoutAnotherDecoder();
    void trackIdentitySelectsStablePaletteWithoutThemeCycling();
    void highDpiInternalScaleUsesPhysicalPixels();
    void duplicateRendererIsRejectedBySharedLifecycle();
    void cameraPropertiesSupportTaskFourInput();
    void nonFiniteCameraInvokablesPreserveExposedState();
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
    features.publishImpact(41, 0.73);

    TerrainReactorItem item;
    item.setFeatureSource(&features);
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
    const quint64 fallbackBefore = fallbackItem.impactRevision();
    QVariantList spectrum(128, 0.0);
    for (int index = 0; index < 32; ++index) spectrum[index] = 1.0;
    fallback.processSpectrum(spectrum);
    QVERIFY(fallbackItem.featureKick());
    QVERIFY(fallbackItem.punchRevision() > 0);
    QCOMPARE(fallbackItem.impactRevision(), fallbackBefore + 1);
    QVERIFY(fallbackItem.impactStrength() > 0.0);
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
    item.zoomBy(-10000.0, 1.0);
    QCOMPARE(item.punchRevision(), firstPunchRevision);
    item.triggerCameraPunch(0.7);
    QCOMPARE(item.punchRevision(), firstPunchRevision + 1);
    item.triggerCameraPunch(0.2);
    QCOMPARE(item.punchRevision(), firstPunchRevision + 2);
    QCOMPARE(item.cameraYaw(), originalYaw + 0.25);
    QCOMPARE(item.cameraDistance(), 42.0);
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

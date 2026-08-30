#include "terrain_reactor_item.hpp"
#include "player_experience_controller.hpp"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTest>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>

class TerrainReactorGpuSmokeTest final : public QObject {
    Q_OBJECT

private slots:
    void firstActiveCreatesResourcesAndRendersStaticFeatures();
    void explicitImpactBrightensAStableTerrainFrame();
    void highFrequencySheenStaysLocalizedAndHeightSubordinate();
    void nonFiniteFeatureInputsAreSanitizedBeforeExposure();
    void nonFiniteCameraControlsRemainRenderable();
};

class MutableFeatureSource final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList bands READ bands NOTIFY featuresChanged)
    Q_PROPERTY(double energy READ energy NOTIFY featuresChanged)
    Q_PROPERTY(double spectralFlux READ spectralFlux NOTIFY featuresChanged)
    Q_PROPERTY(bool kickPulse READ kickPulse NOTIFY featuresChanged)
    Q_PROPERTY(bool snarePulse READ snarePulse NOTIFY featuresChanged)
    Q_PROPERTY(double impactStrength READ impactStrength NOTIFY featuresChanged)
    Q_PROPERTY(quint64 impactRevision READ impactRevision NOTIFY featuresChanged)

public:
    QVariantList bands() const { return bands_; }
    double energy() const noexcept { return energy_; }
    double spectralFlux() const noexcept { return spectralFlux_; }
    bool kickPulse() const noexcept { return false; }
    bool snarePulse() const noexcept { return false; }
    double impactStrength() const noexcept { return impactStrength_; }
    quint64 impactRevision() const noexcept { return impactRevision_; }
    void publish(double value)
    {
        bands_ = QVariantList(8, value);
        energy_ = value;
        spectralFlux_ = value;
        impactStrength_ = value;
        ++impactRevision_;
        emit featuresChanged();
    }

signals:
    void featuresChanged();

private:
    QVariantList bands_{8, 0.0};
    double energy_ = 0.0;
    double spectralFlux_ = 0.0;
    double impactStrength_ = 0.0;
    quint64 impactRevision_ = 0;
};

class StableImpactSource final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList bands READ bands NOTIFY featuresChanged)
    Q_PROPERTY(double energy READ energy NOTIFY featuresChanged)
    Q_PROPERTY(double spectralFlux READ spectralFlux NOTIFY featuresChanged)
    Q_PROPERTY(bool kickPulse READ kickPulse NOTIFY featuresChanged)
    Q_PROPERTY(bool snarePulse READ snarePulse NOTIFY featuresChanged)
    Q_PROPERTY(quint64 impactRevision READ impactRevision NOTIFY impactChanged)
    Q_PROPERTY(double impactStrength READ impactStrength NOTIFY impactChanged)

public:
    QVariantList bands() const { return QVariantList(8, 0.0); }
    double energy() const noexcept { return 0.0; }
    double spectralFlux() const noexcept { return 0.0; }
    bool kickPulse() const noexcept { return false; }
    bool snarePulse() const noexcept { return false; }
    quint64 impactRevision() const noexcept { return revision_; }
    double impactStrength() const noexcept { return strength_; }
    void publishImpact(double strength)
    {
        strength_ = strength;
        ++revision_;
        emit impactChanged();
    }

signals:
    void featuresChanged();
    void impactChanged();

private:
    quint64 revision_ = 0;
    double strength_ = 0.0;
};

void TerrainReactorGpuSmokeTest::firstActiveCreatesResourcesAndRendersStaticFeatures()
{
    QQuickWindow window;
    window.resize(480, 270);
    TerrainReactorItem item(window.contentItem());
    QCOMPARE(item.liveRendererCount(), 0);
    item.setSize(QSizeF(480, 270));
    QCOMPARE(item.liveRendererCount(), 0);
    item.setUseSyntheticFeatures(true);
    item.setQuality(TerrainReactorItem::Quality::High);
    QCOMPARE(item.liveRendererCount(), 0);
    item.setSyntheticFeatures({0.8, 0.7, 0.6, 0.5,
                               0.4, 0.3, 0.2, 0.1}, 0.7, 0.4, true, false);
    QCOMPARE(item.liveRendererCount(), 0);
    window.show();

    QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
    const auto api = window.rendererInterface()->graphicsApi();
    if (api != QSGRendererInterface::Direct3D11
        && api != QSGRendererInterface::OpenGL
        && api != QSGRendererInterface::Vulkan
        && api != QSGRendererInterface::Metal) {
        QSKIP("No accelerated Qt Quick backend is available");
    }
    const char* backendName = api == QSGRendererInterface::Direct3D11
        ? "Direct3D11" : api == QSGRendererInterface::OpenGL
        ? "OpenGL" : api == QSGRendererInterface::Vulkan
        ? "Vulkan" : "Metal";
    qInfo() << "Terrain Reactor accelerated backend:" << backendName;

    item.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderStatus(),
                              TerrainReactorItem::RenderStatus::Ready, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(item.resourceGeneration() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() > 0, 5000);
    QVERIFY(item.uploadCount() > 0);
    QCOMPARE(item.liveRendererCount(), 1);

    item.setActive(false);
    QTest::qWait(100);
    const quint64 inactiveFrames = item.frameCount();
    const quint64 inactiveUploads = item.uploadCount();
    QTest::qWait(150);
    QCOMPARE(item.frameCount(), inactiveFrames);
    QCOMPARE(item.uploadCount(), inactiveUploads);

    const quint64 generation = item.resourceGeneration();
    item.setActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() > inactiveFrames, 5000);
    QCOMPARE(item.resourceGeneration(), generation);
    QCOMPARE(item.liveRendererCount(), 1);

    window.showMinimized();
    QTRY_VERIFY_WITH_TIMEOUT(!item.renderingRequested(), 3000);
    QTest::qWait(100);
    const quint64 minimizedFrames = item.frameCount();
    const quint64 minimizedUploads = item.uploadCount();
    QTest::qWait(150);
    QCOMPARE(item.frameCount(), minimizedFrames);
    QCOMPARE(item.uploadCount(), minimizedUploads);

    window.showNormal();
    QTRY_VERIFY_WITH_TIMEOUT(item.renderingRequested(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() > minimizedFrames, 5000);
    QVERIFY(item.resourceGeneration() >= generation);
    QCOMPARE(item.liveRendererCount(), 1);
}

void TerrainReactorGpuSmokeTest::explicitImpactBrightensAStableTerrainFrame()
{
    QQuickWindow window;
    window.resize(480, 270);
    PlayerExperienceController style;
    style.setAutoRotate(0);
    style.setAutoRotateSpeed(0);
    style.setMotionResponse(0);
    style.setIdleBreathingEnabled(false);
    style.setFloatingCubesEnabled(false);
    style.setMeteorsEnabled(false);
    StableImpactSource source;
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(480, 270));
    item.setStyleSource(&style);
    item.setFeatureSource(&source);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
    const auto api = window.rendererInterface()->graphicsApi();
    if (api != QSGRendererInterface::Direct3D11
        && api != QSGRendererInterface::OpenGL
        && api != QSGRendererInterface::Vulkan
        && api != QSGRendererInterface::Metal) {
        QSKIP("No accelerated Qt Quick backend is available");
    }

    item.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderStatus(),
                              TerrainReactorItem::RenderStatus::Ready, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() > 0, 5000);
    const QImage baseline = window.grabWindow().convertToFormat(
        QImage::Format_RGBA8888);
    QVERIFY(!baseline.isNull());

    const quint64 before = item.frameCount();
    source.publishImpact(1.0);
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() > before, 3000);
    const QImage impacted = window.grabWindow().convertToFormat(
        QImage::Format_RGBA8888);
    QCOMPARE(impacted.size(), baseline.size());

    quint64 baselineLight = 0;
    quint64 impactedLight = 0;
    int brighterPixels = 0;
    const QRect center(baseline.width() / 4, baseline.height() / 4,
                       baseline.width() / 2, baseline.height() / 2);
    for (int y = center.top(); y <= center.bottom(); ++y) {
        for (int x = center.left(); x <= center.right(); ++x) {
            const QColor beforeColor = baseline.pixelColor(x, y);
            const QColor afterColor = impacted.pixelColor(x, y);
            const int beforeValue = beforeColor.red() + beforeColor.green()
                + beforeColor.blue();
            const int afterValue = afterColor.red() + afterColor.green()
                + afterColor.blue();
            baselineLight += quint64(beforeValue);
            impactedLight += quint64(afterValue);
            if (afterValue > beforeValue + 9) ++brighterPixels;
        }
    }
    const bool centerBrightened = impactedLight > baselineLight * 105 / 100;
    const bool broadPulseVisible = brighterPixels
        > center.width() * center.height() / 30;
    qInfo() << "Terrain Reactor impact luminance:" << baselineLight
            << "->" << impactedLight << "brighter pixels:" << brighterPixels;
    item.setActive(false);
    QTest::qWait(100);
    QVERIFY(centerBrightened);
    QVERIFY(broadPulseVisible);
}

void TerrainReactorGpuSmokeTest::highFrequencySheenStaysLocalizedAndHeightSubordinate()
{
    QQuickWindow window;
    window.resize(480, 270);
    window.setColor(QColor(4, 6, 11));
    PlayerExperienceController style;
    style.setAutoRotate(0);
    style.setAutoRotateSpeed(0);
    style.setMotionResponse(0);
    style.setCinemaShake(0.0);
    style.setIdleBreathingEnabled(false);
    style.setRipplesEnabled(false);
    style.setFloatingCubesEnabled(false);
    style.setMeteorsEnabled(false);
    style.setBurstEnabled(false);
    style.setStreamHighlightEnabled(false);
    style.setPeakBoost(100);
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(480, 270));
    item.setStyleSource(&style);
    item.setUseSyntheticFeatures(true);
    item.setQuality(TerrainReactorItem::Quality::High);
    item.setSyntheticFeatures({0.0, 0.0, 0.0, 0.0,
                               0.0, 0.0, 0.0, 0.0},
                              0.0, 0.0, false, false);
    const auto presentedFrames = std::make_shared<std::atomic<int>>(0);
    QObject::connect(&window, &QQuickWindow::frameSwapped, &window,
                     [presentedFrames] {
        presentedFrames->fetch_add(1, std::memory_order_release);
    }, Qt::DirectConnection);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
    const auto api = window.rendererInterface()->graphicsApi();
    if (api != QSGRendererInterface::Direct3D11
        && api != QSGRendererInterface::OpenGL
        && api != QSGRendererInterface::Vulkan
        && api != QSGRendererInterface::Metal) {
        QSKIP("No accelerated Qt Quick backend is available");
    }
    const char* backendName = api == QSGRendererInterface::Direct3D11
        ? "Direct3D11" : api == QSGRendererInterface::OpenGL
        ? "OpenGL" : api == QSGRendererInterface::Vulkan
        ? "Vulkan" : "Metal";
    qInfo() << "Terrain Reactor sheen backend:" << backendName;

    const auto waitForStableRevisions = [&item, presentedFrames] {
        const quint64 expectedFeatureRevision = item.featureRevision();
        const quint64 expectedStyleRevision = item.styleRevision();
        const QVariant renderedFeature = item.property("renderedFeatureRevision");
        const QVariant renderedStyle = item.property("renderedStyleRevision");
        const QVariant stableFrames = item.property("stableRenderedFrameCount");
        QVERIFY2(renderedFeature.isValid(),
                 "renderer must expose the feature revision used for uniforms");
        QVERIFY2(renderedStyle.isValid(),
                 "renderer must expose the style revision used for uniforms");
        QVERIFY2(stableFrames.isValid(),
                 "renderer must expose consecutive frames for the revision pair");
        QTRY_VERIFY_WITH_TIMEOUT(([&item, expectedFeatureRevision,
                                   expectedStyleRevision] {
            item.update();
            return item.property("renderedFeatureRevision").toULongLong()
                    == expectedFeatureRevision
                && item.property("renderedStyleRevision").toULongLong()
                    == expectedStyleRevision;
        }()), 5000);
        const int presentedBaseline = presentedFrames->load(
            std::memory_order_acquire);
        QTRY_VERIFY_WITH_TIMEOUT(([&item, presentedFrames, presentedBaseline,
                                   expectedFeatureRevision,
                                   expectedStyleRevision] {
            item.update();
            return item.property("renderedFeatureRevision").toULongLong()
                    == expectedFeatureRevision
                && item.property("renderedStyleRevision").toULongLong()
                    == expectedStyleRevision
                && item.property("stableRenderedFrameCount").toULongLong() >= 3
                && presentedFrames->load(std::memory_order_acquire)
                    >= presentedBaseline + 3;
        }()), 5000);
    };

    item.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderStatus(),
                              TerrainReactorItem::RenderStatus::Ready, 5000);
    waitForStableRevisions();
    const QImage neutral = window.grabWindow().convertToFormat(
        QImage::Format_RGBA8888);
    QVERIFY(!neutral.isNull());

    item.setSyntheticFeatures({0.72, 0.72, 0.72, 0.72,
                               0.0, 0.0, 0.0, 0.0},
                              0.0, 0.0, false, false);
    waitForStableRevisions();
    const QImage lowMids = window.grabWindow().convertToFormat(
        QImage::Format_RGBA8888);
    QCOMPARE(lowMids.size(), neutral.size());

    item.setSyntheticFeatures({0.0, 0.0, 0.0, 0.0,
                               0.72, 0.72, 0.72, 0.72},
                              0.0, 0.0, false, false);
    waitForStableRevisions();
    const QImage highPlain = window.grabWindow().convertToFormat(
        QImage::Format_RGBA8888);
    QCOMPARE(highPlain.size(), lowMids.size());

    style.setStreamHighlightEnabled(true);
    waitForStableRevisions();
    const QImage highSheen = window.grabWindow().convertToFormat(
        QImage::Format_RGBA8888);
    QCOMPARE(highSheen.size(), highPlain.size());

    QVector<int> lowMidResponse;
    QVector<int> highResponse;
    lowMidResponse.reserve(neutral.width() * neutral.height());
    highResponse.reserve(neutral.width() * neutral.height());
    int sheenPixels = 0;
    quint64 plainLight = 0;
    quint64 sheenLight = 0;
    for (int y = 0; y < lowMids.height(); ++y) {
        for (int x = 0; x < lowMids.width(); ++x) {
            const QColor lowColor = lowMids.pixelColor(x, y);
            const QColor neutralColor = neutral.pixelColor(x, y);
            const QColor plainColor = highPlain.pixelColor(x, y);
            const QColor sheenColor = highSheen.pixelColor(x, y);
            const int lowValue = lowColor.red() + lowColor.green()
                + lowColor.blue();
            const int neutralValue = neutralColor.red() + neutralColor.green()
                + neutralColor.blue();
            const int plainValue = plainColor.red() + plainColor.green()
                + plainColor.blue();
            const int sheenValue = sheenColor.red() + sheenColor.green()
                + sheenColor.blue();
            lowMidResponse.append(std::abs(lowValue - neutralValue));
            highResponse.append(std::abs(plainValue - neutralValue));
            if (sheenValue > plainValue + 12) ++sheenPixels;
            plainLight += quint64(plainValue);
            sheenLight += quint64(sheenValue);
        }
    }
    const int pixels = lowMids.width() * lowMids.height();
    std::sort(lowMidResponse.begin(), lowMidResponse.end());
    std::sort(highResponse.begin(), highResponse.end());
    const int strongestCount = std::max(1, pixels / 100);
    quint64 lowMidStrongest = 0;
    quint64 highStrongest = 0;
    for (int index = 0; index < strongestCount; ++index) {
        lowMidStrongest += quint64(lowMidResponse.at(pixels - 1 - index));
        highStrongest += quint64(highResponse.at(pixels - 1 - index));
    }
    qInfo() << "Terrain Reactor strongest 1% high vs low/mid response:"
            << highStrongest << lowMidStrongest
            << "localized sheen pixels:" << sheenPixels
            << "light:" << plainLight << sheenLight;
    // The strongest one percent is large enough to absorb raster edge changes
    // across backends while directly measuring whether sparse highs dominate
    // the most visible relief. High detail must retain a 10% perceptual margin.
    QVERIFY2(highStrongest * 100 <= lowMidStrongest * 90,
             "Strong high-frequency relief is not subordinate to low/mids");
    QVERIFY2(sheenPixels > pixels / 3000,
             "High frequencies did not create an observable top-face sheen");
    QVERIFY2(sheenPixels < pixels / 8,
             "High-frequency sheen spread broadly instead of staying localized");
    QVERIFY2(sheenLight < plainLight * 108 / 100,
             "High-frequency sheen lifted the whole frame instead of the top faces");
    item.setActive(false);
    QTest::qWait(100);
}

void TerrainReactorGpuSmokeTest::nonFiniteFeatureInputsAreSanitizedBeforeExposure()
{
    const std::array nonFinite{
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    const auto verifyExposed = [](const TerrainReactorItem& item) {
        const QVariantList bands = item.featureBands();
        QCOMPARE(bands.size(), 8);
        for (const QVariant& band : bands) {
            const double value = band.toDouble();
            QVERIFY(std::isfinite(value));
            QVERIFY(value >= 0.0 && value <= 1.0);
        }
        QVERIFY(std::isfinite(double(item.featureEnergy())));
        QVERIFY(item.featureEnergy() >= 0.0 && item.featureEnergy() <= 1.0);
        QVERIFY(std::isfinite(double(item.featureSpectralFlux())));
        QVERIFY(item.featureSpectralFlux() >= 0.0
                && item.featureSpectralFlux() <= 1.0);
        QVERIFY(std::isfinite(double(item.impactStrength())));
        QVERIFY(item.impactStrength() >= 0.0 && item.impactStrength() <= 1.0);
    };

    TerrainReactorItem item;
    item.setUseSyntheticFeatures(true);
    for (const double invalid : nonFinite) {
        item.setSyntheticFeatures(QVariantList(8, invalid), invalid, invalid,
                                  false, false);
        item.triggerCameraPunch(invalid);
        verifyExposed(item);
        QVERIFY(std::isfinite(double(item.cameraPunch())));
        QVERIFY(item.cameraPunch() >= 0.0 && item.cameraPunch() <= 1.0);
    }

    PlayerExperienceController style;
    item.setStyleSource(&style);
    for (const double invalid : nonFinite) {
        style.setCinemaShake(invalid);
        const auto snapshot = item.renderStyleSnapshot();
        QVERIFY(std::isfinite(snapshot.cinemaShake));
        QVERIFY(snapshot.cinemaShake >= 0.0F && snapshot.cinemaShake <= 1.8F);
        for (const float gain : snapshot.visualEqGains) {
            QVERIFY(std::isfinite(gain));
            QVERIFY(gain >= 0.0F && gain <= 1.0F);
        }
    }

    MutableFeatureSource source;
    item.setUseSyntheticFeatures(false);
    item.setFeatureSource(&source);
    for (const double invalid : nonFinite) {
        source.publish(invalid);
        verifyExposed(item);
    }
}

void TerrainReactorGpuSmokeTest::nonFiniteCameraControlsRemainRenderable()
{
    const std::array nonFinite{
        std::numeric_limits<qreal>::quiet_NaN(),
        std::numeric_limits<qreal>::infinity(),
        -std::numeric_limits<qreal>::infinity(),
    };
    QQuickWindow window;
    window.resize(480, 270);
    window.setColor(QColor(4, 6, 11));
    PlayerExperienceController style;
    style.setAutoRotate(0);
    style.setAutoRotateSpeed(0);
    style.setMotionResponse(0);
    style.setCinemaShake(0.0);
    style.setIdleBreathingEnabled(false);
    style.setRipplesEnabled(false);
    style.setFloatingCubesEnabled(false);
    style.setMeteorsEnabled(false);
    style.setBurstEnabled(false);
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(480, 270));
    item.setStyleSource(&style);
    item.setUseSyntheticFeatures(true);
    item.setSyntheticFeatures({0.65, 0.55, 0.35, 0.25,
                               0.0, 0.0, 0.0, 0.0},
                              0.0, 0.0, false, false);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
    const auto api = window.rendererInterface()->graphicsApi();
    if (api != QSGRendererInterface::Direct3D11
        && api != QSGRendererInterface::OpenGL
        && api != QSGRendererInterface::Vulkan
        && api != QSGRendererInterface::Metal) {
        QSKIP("No accelerated Qt Quick backend is available");
    }
    item.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderStatus(),
                              TerrainReactorItem::RenderStatus::Ready, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() > 0, 5000);
    const QImage baseline = window.grabWindow().convertToFormat(
        QImage::Format_RGBA8888);
    QVERIFY(!baseline.isNull());
    const qreal yaw = item.cameraYaw();
    const qreal pitch = item.cameraPitch();
    const qreal distance = item.cameraDistance();

    for (const qreal invalid : nonFinite) {
        item.orbitBy(invalid, 0.0, invalid);
        item.orbitBy(0.0, invalid, invalid);
        item.zoomBy(invalid, invalid);
        item.orbitBy(0.1, -0.02, invalid);
        item.zoomBy(-20.0, invalid);
    }
    QVERIFY(std::isfinite(double(item.cameraYaw())));
    QVERIFY(std::isfinite(double(item.cameraPitch())));
    QVERIFY(std::isfinite(double(item.cameraDistance())));
    QVERIFY(item.cameraPitch() >= 0.12 && item.cameraPitch() <= 1.15);
    QVERIFY(item.cameraDistance() >= 42.0 && item.cameraDistance() <= 220.0);
    QCOMPARE(item.cameraYaw(), yaw);
    QCOMPARE(item.cameraPitch(), pitch);
    QCOMPARE(item.cameraDistance(), distance);

    const quint64 before = item.frameCount();
    item.update();
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() > before, 5000);
    QCOMPARE(item.renderStatus(), TerrainReactorItem::RenderStatus::Ready);
    const QImage after = window.grabWindow().convertToFormat(
        QImage::Format_RGBA8888);
    QCOMPARE(after.size(), baseline.size());
    quint64 baselineLight = 0;
    quint64 afterLight = 0;
    for (int y = 0; y < baseline.height(); ++y) {
        for (int x = 0; x < baseline.width(); ++x) {
            const QColor beforeColor = baseline.pixelColor(x, y);
            const QColor afterColor = after.pixelColor(x, y);
            baselineLight += quint64(beforeColor.red() + beforeColor.green()
                                     + beforeColor.blue());
            afterLight += quint64(afterColor.red() + afterColor.green()
                                  + afterColor.blue());
        }
    }
    QVERIFY2(afterLight * 100 >= baselineLight * 90,
             "Non-finite camera input removed the finite rendered scene");
    item.setActive(false);
    QTest::qWait(100);
}

int main(int argc, char** argv)
{
#ifdef Q_OS_WIN
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#endif
    QGuiApplication application(argc, argv);
    TerrainReactorGpuSmokeTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "terrain_reactor_gpu_smoke_test.moc"

#include "terrain_reactor_item.hpp"
#include "player_experience_controller.hpp"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTest>

class TerrainReactorGpuSmokeTest final : public QObject {
    Q_OBJECT

private slots:
    void firstActiveCreatesResourcesAndRendersStaticFeatures();
    void explicitImpactBrightensAStableTerrainFrame();
    void highFrequencySheenStaysLocalizedAndHeightSubordinate();
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
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 3000);
    const auto api = window.rendererInterface()->graphicsApi();
    if (api != QSGRendererInterface::Direct3D11
        && api != QSGRendererInterface::OpenGL
        && api != QSGRendererInterface::Vulkan
        && api != QSGRendererInterface::Metal) {
        QSKIP("No accelerated Qt Quick backend is available");
    }

    item.setSyntheticFeatures({0.72, 0.72, 0.72, 0.72,
                               0.0, 0.0, 0.0, 0.0},
                              0.72, 0.0, false, false);
    item.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderStatus(),
                              TerrainReactorItem::RenderStatus::Ready, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() > 0, 5000);
    const QImage lowMids = window.grabWindow().convertToFormat(
        QImage::Format_RGBA8888);
    QVERIFY(!lowMids.isNull());

    quint64 before = item.frameCount();
    item.setSyntheticFeatures({0.0, 0.0, 0.0, 0.0,
                               0.72, 0.72, 0.72, 0.72},
                              0.72, 0.0, false, false);
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() > before, 3000);
    const QImage highPlain = window.grabWindow().convertToFormat(
        QImage::Format_RGBA8888);
    QCOMPARE(highPlain.size(), lowMids.size());

    before = item.frameCount();
    style.setStreamHighlightEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() > before, 3000);
    const QImage highSheen = window.grabWindow().convertToFormat(
        QImage::Format_RGBA8888);
    QCOMPARE(highSheen.size(), highPlain.size());

    int lowMidOccupied = 0;
    int highOccupied = 0;
    int sheenPixels = 0;
    quint64 plainLight = 0;
    quint64 sheenLight = 0;
    for (int y = 0; y < lowMids.height(); ++y) {
        for (int x = 0; x < lowMids.width(); ++x) {
            const QColor lowColor = lowMids.pixelColor(x, y);
            const QColor plainColor = highPlain.pixelColor(x, y);
            const QColor sheenColor = highSheen.pixelColor(x, y);
            const int lowValue = lowColor.red() + lowColor.green()
                + lowColor.blue();
            const int plainValue = plainColor.red() + plainColor.green()
                + plainColor.blue();
            const int sheenValue = sheenColor.red() + sheenColor.green()
                + sheenColor.blue();
            if (lowValue > 42) ++lowMidOccupied;
            if (plainValue > 42) ++highOccupied;
            if (sheenValue > plainValue + 12) ++sheenPixels;
            plainLight += quint64(plainValue);
            sheenLight += quint64(sheenValue);
        }
    }
    const int pixels = lowMids.width() * lowMids.height();
    qInfo() << "Terrain Reactor high occupancy vs low/mid:"
            << highOccupied << lowMidOccupied
            << "localized sheen pixels:" << sheenPixels
            << "light:" << plainLight << sheenLight;
    // Eight edge pixels tolerate backend rasterization without allowing the
    // high-frequency silhouette to become observably broader.
    QVERIFY2(highOccupied <= lowMidOccupied + 8,
             "Equal-strength highs grew a broader terrain silhouette than low/mids");
    QVERIFY2(sheenPixels > pixels / 3000,
             "High frequencies did not create an observable top-face sheen");
    QVERIFY2(sheenPixels < pixels / 8,
             "High-frequency sheen spread broadly instead of staying localized");
    QVERIFY2(sheenLight < plainLight * 108 / 100,
             "High-frequency sheen lifted the whole frame instead of the top faces");
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

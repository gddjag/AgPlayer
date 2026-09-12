#include "terrain_reactor_item.hpp"
#include "player_experience_controller.hpp"
#include "audio_visual_feature_controller.hpp"
#include "playback_controller.hpp"
#include <agplayer/c_api.h>

#include <QGuiApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QQuickWindow>
#include <QScreen>
#include <QSGRendererInterface>
#include <QScopeGuard>
#include <QTest>
#include <QDataStream>
#include <QTemporaryFile>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>

namespace {
bool waitForGpuWindow(QQuickWindow& window)
{
    if (QTest::qWaitForWindowExposed(&window, 3000)) return true;
    window.hide();
    QTest::qWait(50);
    window.show();
    return QTest::qWaitForWindowExposed(&window, 5000);
}
} // namespace

class TerrainReactorGpuSmokeTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void spatialLyricsAndPointerRippleRender();
    void referencePcmAnalysisFollowsActualFramesAndStopsWhenHidden();
    void independentCustomColorsChangeGpuPixels();
    void violetHeartKeepsPurpleSurfacesAndLocalizedWarmInterior();
    void densityAndQualityChangesKeepDrawingCompleteFrames();
    void cameraPunchDoesNotMoveTheGroundProjection();
    void denseMaterialFrameBudgetProbe_data();
    void denseMaterialFrameBudgetProbe();
    void columnLayeringReferenceFixture_data();
    void columnLayeringReferenceFixture();
    void shaderFalloffsKeepSmoothstepEdgesAscending();
    void referenceSharpnessReachesSideGlowUnclipped();
    void firstActiveCreatesResourcesAndRendersStaticFeatures();
    void wideGroundFadesBeforeCircularBoundary();
    void staticFeaturesKeepRenderingWithoutGuiFeatureUpdates();
    void explicitImpactBrightensAStableTerrainFrame();
    void regularBeatBrieflyBrightensThenReturns();
    void meteorTouchdownProducesReferenceWhiteRipple();
    void meteorParticlePoolUploadsAndClearsWithoutResourceRebuild();
    void ordinaryBeatProducesRippleWithMeteorsEnabled();
    void floatingControlsReachGpu_data();
    void floatingControlsReachGpu();
    void materialControlsChangeRenderedSurface_data();
    void materialControlsChangeRenderedSurface();
    void subjectClarityPreservesBrightThemeExposure();
    void beatMaterialControlsChangeRenderedSurface_data();
    void beatMaterialControlsChangeRenderedSurface();
    void highFrequencySheenStaysLocalizedAndHeightSubordinate();
    void highFrequencyHeightControlChangesActualRelief();
    void silentTerrainDoesNotGenerateTopFlashes();
    void silentTerrainGeometryStaysFixedWithIdleReliefEnabled();
    void steadyCorePreservesHighlightDetailWithoutWhitePlateau();
    void nonFiniteFeatureInputsAreSanitizedBeforeExposure();
    void nonFiniteCameraControlsRemainRenderable();
};

void TerrainReactorGpuSmokeTest::spatialLyricsAndPointerRippleRender()
{
    PlayerExperienceController style;
    QVERIFY(style.applyTheme(QStringLiteral("nocturnal")));
    style.restoreDynamicDefaults();
    style.setAutoRotate(0);
    style.setIdleBreathingEnabled(false);
    style.setFloatingCubesEnabled(false);
    style.setMeteorsEnabled(false);
    style.setTopographyDensity(20);
    QQuickWindow window;
    window.resize(960, 640);
    window.setColor(style.themeBackground());
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(960, 640));
    item.setStyleSource(&style);
    item.setUseSyntheticFeatures(true);
    item.setSyntheticFeatures({0.,0.,0.,0.,0.,0.,0.,0.}, 0, 0, false, false);
    window.show();
    QVERIFY(waitForGpuWindow(window));
    item.setActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() >= 4, 5000);
    QCOMPARE(item.renderStatus(), TerrainReactorItem::RenderStatus::Ready);
    const QImage baseline = window.grabWindow();
    item.setSpatialLyrics({QStringLiteral("上一句"), QStringLiteral("让音乐在空间中流动"), QStringLiteral("下一句")});
    QTest::qWait(450);
    const QImage lyrics = window.grabWindow();
    QVERIFY(!lyrics.isNull());
    int changed = 0;
    for (int y = 0; y < lyrics.height(); ++y)
        for (int x = 0; x < lyrics.width(); ++x)
            if (lyrics.pixelColor(x,y).lightness() > baseline.pixelColor(x,y).lightness() + 30) ++changed;
    QVERIFY2(changed > 300, "World-space lyric glyphs must actually reach the framebuffer");
    QVERIFY(lyrics.save(QCoreApplication::applicationDirPath() + QStringLiteral("/immersive-native-lyrics.png")));
    // Change the live controller while the scene is active: assert actual
    // framebuffer differences, not just property storage or signal delivery.
    for (const auto& setting : {
             std::pair<const char*, int>{"lyricPositionX", 60},
             {"lyricPositionY", 65}, {"lyricSize", 130},
             {"lyricDepth", 85}, {"lyricOpacity", 20}}) {
        const QVariant saved = style.property(setting.first);
        const QImage before = window.grabWindow();
        QVERIFY(style.setProperty(setting.first, setting.second));
        QTest::qWait(100);
        const QImage after = window.grabWindow();
        int pixels = 0;
        for (int y = 0; y < before.height(); ++y)
            for (int x = 0; x < before.width(); ++x)
                if (std::abs(before.pixelColor(x,y).lightness()
                             - after.pixelColor(x,y).lightness()) > 20) ++pixels;
        qInfo() << setting.first << "lyric changed pixels" << pixels;
        QVERIFY2(pixels > 100, setting.first);
        QVERIFY(style.setProperty(setting.first, saved));
        QTest::qWait(100);
    }
    item.setSpatialLyrics({});
    QTest::qWait(100);
    item.triggerRipple(480, 350);
    QTest::qWait(180);
    const QImage ripple = window.grabWindow();
    QVERIFY(ripple != baseline);
    QCOMPARE(item.renderStatus(), TerrainReactorItem::RenderStatus::Ready);
    item.setSpatialLyrics({QString{}, QStringLiteral("切换与隐藏后重新进入"), QString{}});
    item.setActive(false);
    QTest::qWait(80);
    item.setActive(true);
    QTest::qWait(150);
    QCOMPARE(item.renderStatus(), TerrainReactorItem::RenderStatus::Ready);
}

void TerrainReactorGpuSmokeTest::cleanup()
{
    // QQuickWindow tears its threaded scene graph down asynchronously on
    // Windows. Drain teardown events between cases before exposing another
    // native window backed by the same graphics device.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::qWait(20);
}

void TerrainReactorGpuSmokeTest::referencePcmAnalysisFollowsActualFramesAndStopsWhenHidden()
{
    QTemporaryFile wav(QDir::tempPath() + QStringLiteral("/terrain-cadence-XXXXXX.wav"));
    QVERIFY(wav.open());
    constexpr int rate = 48000, samples = rate * 12;
    QDataStream stream(&wav);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF",4); stream << quint32(36 + samples * 2);
    stream.writeRawData("WAVEfmt ",8); stream << quint32(16) << quint16(1)
        << quint16(1) << quint32(rate) << quint32(rate*2) << quint16(2) << quint16(16);
    stream.writeRawData("data",4); stream << quint32(samples*2);
    for (int i=0; i<samples; ++i)
        stream << qint16(std::sin(6.283185307179586 * 80 * i / rate) * 20000);
    wav.flush(); wav.close();
    ag_player* raw=nullptr;
    const ag_player_config config{AG_AUDIO_BACKEND_NULL,4096U};
    QCOMPARE(ag_player_create_with_config(&config,&raw),AG_OK);
    const auto cleanup=qScopeGuard([&]{ag_player_destroy(raw);});
    PlaybackController playback(raw);
    AudioVisualFeatureController features(&playback);
    PlayerExperienceController style;
    QVERIFY(style.applyTheme(QStringLiteral("nocturnal")));
    style.setTopographyDensity(0);
    style.setFloatingCubesEnabled(false);
    style.setMeteorsEnabled(false);
    QQuickWindow window;
    window.resize(480,270);
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(480,270));
    item.setStyleSource(&style); item.setFeatureSource(&features);
    features.setActive(true);
    QCOMPARE(ag_player_load(raw,wav.fileName().toUtf8().constData()),AG_OK);
    QCOMPARE(ag_player_play(raw),AG_OK);
    window.show(); QVERIFY(waitForGpuWindow(window));
    for (auto quality : {TerrainReactorItem::Quality::Eco,TerrainReactorItem::Quality::High}) {
        item.setQuality(quality);
        const auto framesBefore=item.frameCount();
        const auto analysesBefore=item.featureRevision();
        item.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() >= framesBefore+20,4000);
        QTRY_VERIFY_WITH_TIMEOUT(item.featureEnergy()>0,1000);
        if (quality == TerrainReactorItem::Quality::Eco) {
            const auto energyBeforePause=item.featureEnergy();
            const auto revisionBeforePause=item.featureRevision();
            QCOMPARE(ag_player_pause(raw),AG_OK);
            QTRY_VERIFY_WITH_TIMEOUT(item.featureRevision()>revisionBeforePause,1000);
            QTRY_VERIFY_WITH_TIMEOUT(item.featureEnergy()<energyBeforePause,1000);
            QVERIFY(item.featureEnergy()>0);
            QTRY_VERIFY_WITH_TIMEOUT(item.featureEnergy()<0.001,4000);
            QCOMPARE(ag_player_play(raw),AG_OK);
            QTRY_VERIFY_WITH_TIMEOUT(item.featureEnergy()>0,1000);
        }
        item.setActive(false);
        QTest::qWait(100); // Drain the GUI's queued render-frame reports.
        const auto frames=item.frameCount()-framesBefore;
        const auto analyses=item.featureRevision()-analysesBefore;
        qInfo() << "PCM render cadence quality/frames/GUI reports:" << int(quality)
                << frames << analyses;
        QVERIFY2(std::abs(qint64(frames)-qint64(analyses))<=2,
                 "Analysis cadence diverges from actual rendered frames");
        QCOMPARE(features.visualSpectrumUpdateCount(),quint64{0});
    }
    item.setActive(true);
    for (const auto& key : {"coolColor", "warmColor", "accentColor", "peakColor", "baseColor"}) {
        const auto revision = item.featureRevision();
        QVERIFY(style.setCustomColor(QString::fromLatin1(key), QStringLiteral("#AC38E8")));
        QCOMPARE(style.themeId(), QStringLiteral("custom"));
        QTRY_VERIFY_WITH_TIMEOUT(item.featureRevision() > revision + 3, 1000);
        QVERIFY(item.featureEnergy() > 0);
    }
    QTest::qWait(100);
    item.setVisible(false);
    QTest::qWait(100);
    const auto hiddenFrames=item.frameCount(), hiddenAnalyses=item.featureRevision();
    QTest::qWait(200);
    QCOMPARE(item.frameCount(),hiddenFrames);
    QCOMPARE(item.featureRevision(),hiddenAnalyses);
}

void TerrainReactorGpuSmokeTest::densityAndQualityChangesKeepDrawingCompleteFrames()
{
    PlayerExperienceController style;
    style.applyTheme(QStringLiteral("nocturnal"));
    style.setAutoRotate(0);
    QQuickWindow window;
    window.resize(640, 360);
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(640, 360));
    item.setStyleSource(&style);
    item.setQuality(TerrainReactorItem::Quality::High);
    item.setUseSyntheticFeatures(true);
    item.setSyntheticFeatures({.8,.7,.6,.5,.4,.3,.2,.1}, .6,.35,false,false);
    window.show();
    QVERIFY(waitForGpuWindow(window));
    item.setActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() > 3, 5000);
    for (int density : {46, 80, 100, 35, 100}) {
        const auto before = item.frameCount();
        style.setTopographyDensity(density);
        const int grid = agplayer::terrain::referenceTerrainGridSize(density);
        QTRY_COMPARE_WITH_TIMEOUT(item.renderedTerrainCount(), grid * grid, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() > before, 5000);
        const auto image = window.grabWindow();
        QVERIFY(!image.isNull());
        int visible = 0;
        for (int y = 0; y < image.height(); y += 4)
            for (int x = 0; x < image.width(); x += 4)
                if (qGray(image.pixel(x,y)) > 20) ++visible;
        QVERIFY2(visible > 50, "Density transition produced an empty/black scene");
    }
    for (auto quality : {TerrainReactorItem::Quality::Eco,
                         TerrainReactorItem::Quality::High,
                         TerrainReactorItem::Quality::Balanced,
                         TerrainReactorItem::Quality::High}) {
        const auto before = item.frameCount();
        item.setQuality(quality);
        window.resize(window.width() + 2, window.height());
        QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() > before + 4, 5000);
        QCOMPARE(item.renderStatus(), TerrainReactorItem::RenderStatus::Ready);
    }
    item.setActive(false);
}

void TerrainReactorGpuSmokeTest::denseMaterialFrameBudgetProbe_data()
{
    QTest::addColumn<int>("terrainDensity");
    QTest::addColumn<QSize>("viewport");
    QTest::newRow("default-1080p") << 46 << QSize(1920, 1080);
    QTest::newRow("maximum-1080p") << 100 << QSize(1920, 1080);
    if (auto* screen = QGuiApplication::primaryScreen())
        QTest::newRow("maximum-native-screen") << 100 << screen->size();
}

void TerrainReactorGpuSmokeTest::denseMaterialFrameBudgetProbe()
{
    if (!qEnvironmentVariableIsSet("AGPLAYER_MATERIAL_BENCHMARK"))
        QSKIP("Opt-in comparative wall-frame probe, not a GPU timestamp benchmark");
    QFETCH(int, terrainDensity);
    QFETCH(QSize, viewport);
    PlayerExperienceController style;
    style.applyTheme(QStringLiteral("neon-tokyo"));
    style.setTopographyDensity(terrainDensity);
    style.setAutoRotate(0);
    style.setAutoRotateSpeed(0);
    QQuickWindow window;
    window.resize(viewport);
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(viewport));
    item.setStyleSource(&style);
    item.setQuality(TerrainReactorItem::Quality::High);
    item.setUseSyntheticFeatures(true);
    item.setSyntheticFeatures({.8,.7,.6,.5,.4,.3,.2,.1}, .6,.35,false,false);
    window.show();
    QVERIFY(waitForGpuWindow(window));
    item.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderStatus(), TerrainReactorItem::RenderStatus::Ready, 5000);
    QTest::qWait(1500);
    const auto first = item.frameCount();
    QElapsedTimer timer;
    timer.start();
    QTest::qWait(3000);
    const auto count = item.frameCount() - first;
    qInfo() << "Dense material elapsed/frames/instances/ms per frame:"
            << timer.elapsed() << count << item.renderedTerrainCount()
            << double(timer.elapsed()) / double(std::max<quint64>(1, count));
    qInfo() << "Actual material buffer / device pixel ratio:"
            << item.effectiveColorBufferSize() << window.devicePixelRatio();
    QVERIFY2(count > 0, "Dense terrain did not produce new frames");
    item.setActive(false);
}

void TerrainReactorGpuSmokeTest::columnLayeringReferenceFixture_data()
{
    QTest::addColumn<int>("materialMode");
    QTest::addColumn<bool>("rainbow");
    QTest::newRow("crystal") << 0 << false;
    QTest::newRow("jelly") << 1 << false;
    QTest::newRow("rainbow") << 1 << true;
}

void TerrainReactorGpuSmokeTest::columnLayeringReferenceFixture()
{
    QFETCH(int, materialMode);
    QFETCH(bool, rainbow);
    // A close, dense scene checks aggregate color/edge readability. It cannot
    // identify an unoccluded column foot; that belongs to the single fixture.
    PlayerExperienceController style;
    const QVariant oldDensity = style.property("columnDensity");
    const auto restoreDensity = qScopeGuard([&] { style.setProperty("columnDensity", oldDensity); });
    style.applyTheme(QStringLiteral("nocturnal"));
    style.setMaterialMode(materialMode);
    style.setColorMode(rainbow ? 3 : 1);
    style.setCoolColor("#187DA5");
    style.setWarmColor("#187DA5");
    style.setAccentColor("#187DA5");
    style.setPeakColor("#54C9E7");
    style.setBaseColor("#03080C");
    style.setSongAdaptiveColorEnabled(false);
    style.setThemeCycleEnabled(false);
    style.setColumnSize(180);
    // Pin the inspected column geometry independently of preset art direction.
    style.setResponseRange(100);
    style.setColumnDensity(50);
    style.setColumnOpacity(100);
    style.setReactorBrightness(100);
    style.setTerrainAmplitude(85);
    style.setRhythmStrength(0);
    style.setAutoRotate(0);
    style.setAutoRotateSpeed(0);
    style.setCinemaShake(0);
    style.setRipplesEnabled(false);
    style.setFloatingCubesEnabled(false);
    style.setMeteorsEnabled(false);
    style.setBurstEnabled(false);
    style.setStreamHighlightEnabled(false);
    style.setIdleBreathingEnabled(false);
    QQuickWindow window;
    QScreen* const screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    window.setScreen(screen);
    window.setFlags(Qt::Window | Qt::FramelessWindowHint);
    window.setGeometry(QRect(screen->availableGeometry().center()
                             - QPoint(480, 320), QSize(960, 640)));
    window.setColor(QColor("#03080C"));
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(960, 640));
    item.setQuality(TerrainReactorItem::Quality::High);
    item.setStyleSource(&style);
    item.setTrackIdentity("column-layering-reference");
    item.setUseSyntheticFeatures(true);
    item.setSyntheticFeatures({.65,.60,.50,.40,.12,.08,.04,.02}, .48, 0, false, false);
    item.orbitBy(-item.cameraYaw(), 0.30 - item.cameraPitch(), 0);
    item.zoomBy((85.0 - item.cameraDistance()) / 0.04, 0);
    window.show();
    QVERIFY(waitForGpuWindow(window));
    const auto api = window.rendererInterface()->graphicsApi();
    if (api != QSGRendererInterface::Direct3D11 && api != QSGRendererInterface::OpenGL
        && api != QSGRendererInterface::Vulkan && api != QSGRendererInterface::Metal)
        QSKIP("No accelerated Qt Quick backend is available");
    item.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderStatus(), TerrainReactorItem::RenderStatus::Ready, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderedStyleRevision(), item.styleRevision(), 3000);
    QTest::qWait(1000);
    const QString output = qEnvironmentVariable("AGPLAYER_COLUMN_FIXTURE_DIR");
    // Dense relief can occlude a column between frames. Only assert scene-wide
    // readability here; same-column material acceptance lives in the isolated
    // terrain_column_material_test fixture.
    for (int capture = 0; capture < 3; ++capture) {
        const QImage frame = window.grabWindow();
        QVERIFY(!frame.isNull());
        if (!output.isEmpty()) {
            QVERIFY(QDir().mkpath(output));
            QVERIFY(frame.save(QDir(output).filePath(
                QStringLiteral("column-%1-%2.png").arg(materialMode).arg(capture))));
        }
        int visible = 0, colored = 0, nearWhite = 0, strongestEdge = 0;
        for (int y = frame.height() / 4; y < frame.height() - 1; ++y) {
            for (int x = frame.width() / 10; x < frame.width() * 9 / 10; ++x) {
                const QColor c = frame.pixelColor(x, y);
                if (qGray(c.rgb()) <= 15) continue;
                ++visible;
                if (c.red() >= 235 && c.green() >= 235 && c.blue() >= 235) ++nearWhite;
                const bool tinted = rainbow
                    ? std::max({c.red(), c.green(), c.blue()}) - std::min({c.red(), c.green(), c.blue()}) > 12
                    : c.blue() >= c.red() + 10 && c.green() >= c.red() + 8 && c.blue() > 25;
                if (tinted) ++colored;
                const int below = qGray(frame.pixel(x, y + 1));
                if (below > 15) strongestEdge = std::max(strongestEdge, std::abs(qGray(c.rgb()) - below));
            }
        }
        QVERIFY(visible > 10000);
        QVERIFY2(double(nearWhite) / visible < 0.03, "Dense columns must not become a near-white sheet");
        QVERIFY2(double(colored) / visible > 0.25, "Dense relief must retain visibly colored surfaces");
        QVERIFY2(strongestEdge >= 5, "Dense relief must retain readable internal edges");
        QTest::qWait(100);
    }
    item.setActive(false);
}

void TerrainReactorGpuSmokeTest::referenceSharpnessReachesSideGlowUnclipped()
{
    const QDir directory = QFileInfo(QString::fromUtf8(AGPLAYER_TERRAIN_SHADER_SOURCE)).dir();
    QFile shader(directory.filePath(QStringLiteral("terrain_reactor.frag")));
    QVERIFY(shader.open(QIODevice::ReadOnly));
    const QString source = QString::fromUtf8(shader.readAll());
    const int begin = source.indexOf(QStringLiteral("if (referenceMode) {"));
    const int end = source.indexOf(QStringLiteral("if (ubuf.bodyColor.a > 0.5) {"), begin);
    QVERIFY(begin >= 0 && end > begin);
    const QString material = source.mid(begin, end-begin);
    QVERIFY2(!material.contains(QStringLiteral("clamp(ubuf.timbre.z")),
             "Original sharpness may exceed one; clipping it lengthens the side glow on sharp attacks");
}

void TerrainReactorGpuSmokeTest::shaderFalloffsKeepSmoothstepEdgesAscending()
{
    QFile shader(QString::fromUtf8(AGPLAYER_TERRAIN_SHADER_SOURCE));
    QVERIFY2(shader.open(QIODevice::ReadOnly),
             qPrintable(shader.errorString()));
    const QString source = QString::fromUtf8(shader.readAll());

    QVERIFY2(!source.contains(
                 QStringLiteral("smoothstep(responseRadius * 1.15")),
             "Core terrain falloff uses reversed smoothstep edges");
    QVERIFY2(!source.contains(
                 QStringLiteral("smoothstep(responseRadius * 0.84")),
             "Maximum response range can reverse the outer smoothstep edges");
    QVERIFY2(source.contains(
                 QStringLiteral("if (ubuf.styleToggles.x > 0.5")),
             "Disabled ripples must bypass the multi-wave shader work");
    QVERIFY2(source.contains(QStringLiteral("waveIndex >= waveCount")),
             "Reduced ripple quality must reduce active wave-source work");
    QVERIFY2(!source.contains(QStringLiteral("localBeatLift")),
             "Canonical terrain height must come from the original continuous band response, not a discrete onset lift");
    QVERIFY2(!source.contains(QStringLiteral("responsiveLow")),
             "Canonical terrain response must preserve the original linear low-band mapping");
    QVERIFY2(!source.contains(QStringLiteral("motionGain")),
             "Motion response is already applied by terrainResponse and must not be multiplied again in the shader");
}

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
    Q_PROPERTY(quint64 beatRevision READ beatRevision NOTIFY featuresChanged)
    Q_PROPERTY(double beatStrength READ beatStrength NOTIFY featuresChanged)

public:
    QVariantList bands() const { return QVariantList(8, 0.0); }
    double energy() const noexcept { return 0.0; }
    double spectralFlux() const noexcept { return 0.0; }
    bool kickPulse() const noexcept { return false; }
    bool snarePulse() const noexcept { return false; }
    quint64 impactRevision() const noexcept { return revision_; }
    double impactStrength() const noexcept { return strength_; }
    quint64 beatRevision() const noexcept { return beatRevision_; }
    double beatStrength() const noexcept { return beatStrength_; }
    void publishBeat(double strength)
    {
        beatStrength_ = strength;
        ++beatRevision_;
        emit featuresChanged();
    }
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
    quint64 beatRevision_ = 0;
    double beatStrength_ = 0.0;
};

void TerrainReactorGpuSmokeTest::independentCustomColorsChangeGpuPixels()
{
    const QStringList keys{"coolColor", "warmColor", "accentColor", "peakColor", "baseColor"};
    for (const auto& key : keys) {
        std::array<qint64, 2> greenMinusRed{};
        for (int pass = 0; pass < 2; ++pass) {
            PlayerExperienceController style;
            for (const auto& role : keys)
                QVERIFY(style.setCustomColor(role, role == "baseColor" ? "#030208" : "#704080"));
            QVERIFY(style.setCustomColor(key, pass ? "#10F020" : "#F01020"));
            style.restoreDynamicDefaults();
            style.setTopographyDensity(20);
            style.setAutoRotate(0);
            style.setCinemaShake(0);
            style.setIdleBreathingEnabled(false);
            style.setFloatingCubesEnabled(false);
            style.setMeteorsEnabled(false);
            StableImpactSource source;
            QQuickWindow window;
            window.resize(640, 480);
            window.setColor(style.themeBackground());
            TerrainReactorItem item(window.contentItem());
            item.setSize(QSizeF(640, 480));
            item.setStyleSource(&style);
            item.setFeatureSource(&source);
            item.setQuality(TerrainReactorItem::Quality::High);
            window.show();
            QVERIFY(waitForGpuWindow(window));
            if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software)
                QSKIP("Pixel proof requires an accelerated renderer");
            item.setActive(true);
            QTRY_COMPARE_WITH_TIMEOUT(item.renderStatus(), TerrainReactorItem::RenderStatus::Ready, 5000);
            QTest::qWait(400);
            source.publishBeat(1.0);
            QTest::qWait(160);
            const auto image = window.grabWindow();
            QVERIFY(!image.isNull());
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const auto color = image.pixelColor(x, y);
                    greenMinusRed[pass] += color.green() - color.red();
                }
            item.setActive(false);
        }
        qInfo() << key << "GPU green-minus-red:" << greenMinusRed[0] << greenMinusRed[1];
        QVERIFY2(greenMinusRed[1] > greenMinusRed[0] + 1000,
                 qPrintable(key + " did not produce its chosen hue in GPU pixels"));
    }
}

void TerrainReactorGpuSmokeTest::violetHeartKeepsPurpleSurfacesAndLocalizedWarmInterior()
{
    PlayerExperienceController style;
    QVERIFY(style.applyTheme(QStringLiteral("violet-heart")));
    style.restoreDynamicDefaults();
    style.setTopographyDensity(46);
    style.setAutoRotate(0);
    style.setCinemaShake(0);
    style.setIdleBreathingEnabled(false);
    style.setRipplesEnabled(false);
    style.setFloatingCubesEnabled(false);
    style.setMeteorsEnabled(false);
    QQuickWindow window;
    window.resize(960, 720);
    window.setColor(style.themeBackground());
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(960, 720));
    item.setStyleSource(&style);
    item.setUseSyntheticFeatures(true);
    window.show();
    QVERIFY(waitForGpuWindow(window));
    if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software)
        QSKIP("Color proof requires an accelerated renderer");
    item.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderStatus(), TerrainReactorItem::RenderStatus::Ready, 5000);
    for (int raised = 0; raised < 2; ++raised) {
        item.setSyntheticFeatures(raised ? QVariantList{.95,.88,.75,.35,0,0,0,0}
                                         : QVariantList{0,0,0,0,0,0,0,0},
                                  raised ? .75 : 0, 0, false, false);
        QTest::qWait(1000);
        const auto image = window.grabWindow();
        QVERIFY(!image.isNull());
        int purple = 0, warm = 0, warmOutsideCore = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const auto c = image.pixelColor(x, y);
                if (c.blue() > 30 && c.blue() > c.red() * 1.15
                    && c.red() > c.green() * 1.4) ++purple;
                if (c.red() > 50 && c.red() > c.blue() * 1.10
                    && c.red() > c.green() * 1.25) {
                    ++warm;
                    if (x < image.width() / 4 || x > image.width() * 3 / 4)
                        ++warmOutsideCore;
                }
            }
        qInfo() << "Violet Heart raised/purple/warm/outside:" << raised
                << purple << warm << warmOutsideCore;
        QVERIFY(purple > 1000);
        QVERIFY2(purple > warm * 3, "Pink must not replace the dominant purple body");
        QCOMPARE(warmOutsideCore, 0);
        if (raised) QVERIFY2(warm > 50, "Raised central interior must visibly glow pink/warm");
        else QCOMPARE(warm, 0);
        QVERIFY(image.save(QCoreApplication::applicationDirPath()
            + (raised ? "/violet-heart-raised.png" : "/violet-heart-quiet.png")));
    }
    item.setActive(false);
}

void TerrainReactorGpuSmokeTest::cameraPunchDoesNotMoveTheGroundProjection()
{
    QQuickWindow window;
    window.setScreen(QGuiApplication::primaryScreen());
    window.setFlags(Qt::Window | Qt::FramelessWindowHint);
    window.setGeometry(QRect(window.screen()->availableGeometry().center()
                             - QPoint(320, 240), QSize(640, 480)));
    PlayerExperienceController style;
    style.applyTheme(QStringLiteral("nocturnal"));
    style.setAutoRotate(0);
    style.setAutoRotateSpeed(0);
    style.setCinemaShake(1.8);
    style.setIdleBreathingEnabled(false);
    style.setRipplesEnabled(false);
    style.setFloatingCubesEnabled(false);
    style.setMeteorsEnabled(false);
    style.setBurstEnabled(false);
    style.setStreamHighlightEnabled(false);
    style.setThemeCycleEnabled(false);
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(640, 480));
    item.setStyleSource(&style);
    item.setUseSyntheticFeatures(true);
    item.setSyntheticFeatures({.55,.45,.35,.25,0,0,0,0}, .3, 0, false, false);
    window.show();
    QVERIFY(waitForGpuWindow(window));
    if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software)
        QSKIP("No accelerated backend");
    item.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderStatus(), TerrainReactorItem::RenderStatus::Ready, 5000);
    QTest::qWait(1200);
    const auto capture = [&]() { return window.grabWindow().convertToFormat(QImage::Format_RGB32); };
    const QImage first = capture();
    QTest::qWait(100);
    const QImage quiet = capture();
    // Inject only the revisioned camera impulse. No beat/flux/material change
    // can disguise a moving projection as legitimate column deformation.
    item.triggerCameraPunch(1);
    const quint64 frame = item.frameCount();
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() >= frame + 2, 3000);
    const QImage punched = capture();
    QCOMPARE(first.size(), quiet.size());
    QCOMPARE(quiet.size(), punched.size());
    QVERIFY(!punched.isNull());
    int quietChanges = 0, impulseChanges = 0, visible = 0;
    for (int y = quiet.height() * 4 / 10; y < quiet.height() * 85 / 100; ++y)
        for (int x = quiet.width() * 3 / 10; x < quiet.width() * 7 / 10; ++x) {
            const QColor a = first.pixelColor(x,y), b = quiet.pixelColor(x,y), c = punched.pixelColor(x,y);
            const auto delta = [](QColor u, QColor v) {
                return std::abs(u.red()-v.red()) + std::abs(u.green()-v.green()) + std::abs(u.blue()-v.blue());
            };
            if (b.red()+b.green()+b.blue() > 30) ++visible;
            if (delta(a,b) > 24) ++quietChanges;
            if (delta(b,c) > 24) ++impulseChanges;
        }
    QVERIFY(visible > 1000);
    qInfo() << "Ground projection quiet/punch changed pixels:" << quietChanges << impulseChanges;
    QVERIFY2(impulseChanges <= quietChanges + 80,
             "An audio camera impulse changes the supposedly fixed ground projection");
    item.setActive(false);
}

void TerrainReactorGpuSmokeTest::materialControlsChangeRenderedSurface_data()
{
    QTest::addColumn<QByteArray>("control");
    QTest::addColumn<int>("mode");
    QTest::addColumn<int>("low");
    QTest::addColumn<int>("high");
    QTest::newRow("gel-material") << QByteArray("materialMode") << 0 << 0 << 1;
    QTest::newRow("gel-softness") << QByteArray("materialSoftness") << 1 << 0 << 100;
    QTest::newRow("crystal-softness") << QByteArray("materialSoftness") << 0 << 0 << 100;
    QTest::newRow("ink-density") << QByteArray("inkDensity") << 2 << 0 << 100;
    QTest::newRow("column-size") << QByteArray("columnSize") << 0 << 50 << 200;
    QTest::newRow("column-density") << QByteArray("columnDensity") << 0 << 50 << 200;
    QTest::newRow("column-opacity") << QByteArray("columnOpacity") << 0 << 0 << 100;
    QTest::newRow("reactor-brightness") << QByteArray("reactorBrightness") << 0 << 20 << 180;
    QTest::newRow("column-inner-light") << QByteArray("columnInnerLight") << 0 << 0 << 200;
    QTest::newRow("column-light-spill") << QByteArray("columnLightSpill") << 0 << 0 << 200;
    QTest::newRow("column-light-radius") << QByteArray("columnLightRadius") << 0 << 20 << 200;
    QTest::newRow("center-highlight") << QByteArray("centerHighlight") << 0 << 0 << 100;
    QTest::newRow("depth-of-field") << QByteArray("depthOfField") << 0 << 0 << 150;
    QTest::newRow("theme-surface-sheen") << QByteArray("glowIntensity") << 0 << 0 << 100;
    QTest::newRow("column-clarity") << QByteArray("subjectClarity") << 0 << 20 << 140;
    QTest::newRow("ink-clarity") << QByteArray("subjectClarity") << 2 << 20 << 140;
}

void TerrainReactorGpuSmokeTest::materialControlsChangeRenderedSurface()
{
    QFETCH(QByteArray, control);
    QFETCH(int, mode);
    QFETCH(int, low);
    QFETCH(int, high);
    PlayerExperienceController style;
    const QVariant previousDensity = style.property("columnDensity");
    const auto restoreDensity = qScopeGuard([&] { style.setProperty("columnDensity", previousDensity); });
    style.applyTheme(QStringLiteral("nocturnal"));
    if (control != "reactorBrightness") style.setReactorBrightness(100);
    if (control != "reactorBrightness" && control != "glowIntensity")
        style.setColorMode(PlayerExperienceController::Custom);
    QVERIFY2(style.setProperty("materialMode", mode), "Native material control is missing");
    QVERIFY(style.setProperty(control.constData(), low));
    style.setAutoRotate(0);
    style.setAutoRotateSpeed(0);
    style.setCinemaShake(0);
    style.setRipplesEnabled(false);
    style.setFloatingCubesEnabled(false);
    style.setMeteorsEnabled(false);
    style.setStreamHighlightEnabled(control == "glowIntensity");
    QQuickWindow window;
    window.resize(480, 270);
    window.setColor(mode == 2 ? QColor("#f6f5ef") : QColor("#04060b"));
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(480, 270));
    item.setStyleSource(&style);
    item.setUseSyntheticFeatures(true);
    item.setSyntheticFeatures({.7,.6,.5,.4,.3,.3,.2,.2}, .5, 0, false, false);
    if (control == "columnDensity") item.setQuality(TerrainReactorItem::Quality::High);
    if (control == "subjectClarity") {
        // Isolate face contrast from animated relief: unchanged terrain must
        // not produce a false positive merely because capture time advanced.
        style.setIdleBreathingEnabled(false);
        item.setSyntheticFeatures({0.,0.,0.,0.,0.,0.,0.,0.}, 0, 0, false, false);
    }
    window.show();
    QVERIFY(waitForGpuWindow(window));
    const auto api = window.rendererInterface()->graphicsApi();
    if (api != QSGRendererInterface::Direct3D11 && api != QSGRendererInterface::OpenGL
        && api != QSGRendererInterface::Vulkan && api != QSGRendererInterface::Metal)
        QSKIP("No accelerated Qt Quick backend is available");
    item.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderStatus(), TerrainReactorItem::RenderStatus::Ready, 5000);
    QTest::qWait(1000); // palette/envelope settle, not timed beat input
    const QImage a = window.grabWindow();
    const quint64 frameBeforeChange = item.frameCount();
    QVERIFY(style.setProperty(control.constData(), high));
    QTRY_COMPARE_WITH_TIMEOUT(item.renderedStyleRevision(), item.styleRevision(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() >= frameBeforeChange + 2, 3000);
    const QImage b = window.grabWindow();
    QVERIFY(!a.isNull());
    QCOMPARE(a.size(), b.size());
    int changed = 0;
    int maximumDifference = 0;
    quint64 totalDifference = 0;
    for (int y = a.height() / 3; y < a.height() * 4 / 5; ++y) {
        for (int x = a.width() / 4; x < a.width() * 3 / 4; ++x) {
            const QColor ca = a.pixelColor(x,y), cb = b.pixelColor(x,y);
            const int difference = std::abs(ca.red()-cb.red())
                + std::abs(ca.green()-cb.green()) + std::abs(ca.blue()-cb.blue());
            maximumDifference = std::max(maximumDifference, difference);
            totalDifference += quint64(difference);
            if (difference > 24) ++changed;
        }
    }
    qInfo() << control << "material pixels changed:" << changed
            << "max/total delta:" << maximumDifference << totalDifference;
    QVERIFY2(changed > 500, "Material parameter changes numbers but not the rendered surface");
    item.setActive(false);
}

void TerrainReactorGpuSmokeTest::beatMaterialControlsChangeRenderedSurface_data()
{
    QTest::addColumn<QByteArray>("control");
    QTest::addColumn<int>("low");
    QTest::addColumn<int>("high");
    QTest::addColumn<int>("delay");
    // Elasticity's moving light is checked with fixed beat uniforms in
    // terrain_column_material_test; asynchronous event captures are not a
    // stable pixel comparison for a short traveling band.
    QTest::newRow("wave-strength") << QByteArray("rippleStrength") << 0 << 200 << 200;
    QTest::newRow("wave-width") << QByteArray("rippleWidth") << 20 << 200 << 200;
    QTest::newRow("wave-decay") << QByteArray("rippleDecay") << 20 << 200 << 1000;
    QTest::newRow("impact-decay") << QByteArray("rippleDecay") << 20 << 200 << 500;
}

void TerrainReactorGpuSmokeTest::beatMaterialControlsChangeRenderedSurface()
{
    QFETCH(QByteArray, control);
    QFETCH(int, low);
    QFETCH(int, high);
    QFETCH(int, delay);
    QImage frames[2];
    // Keep the native surface and its DPI unchanged across both parameter
    // samples. Recreating two independently placed Windows windows can put
    // them on different monitors or capture during WM_DPICHANGED resizing.
    QQuickWindow window;
    QScreen* const screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    window.setScreen(screen);
    window.setFlags(Qt::Window | Qt::FramelessWindowHint);
    const QSize logicalSize(480, 270);
    window.setMinimumSize(logicalSize);
    window.setMaximumSize(logicalSize);
    window.setGeometry(QRect(screen->availableGeometry().center()
                             - QPoint(240, 135), logicalSize));
    window.show();
    QVERIFY(waitForGpuWindow(window));
    QTRY_COMPARE_WITH_TIMEOUT(window.screen(), screen, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(window.size(), logicalSize, 3000);
    const QSize physicalSize(qRound(480 * window.devicePixelRatio()),
                             qRound(270 * window.devicePixelRatio()));
    for (int pass = 0; pass < 2; ++pass) {
        PlayerExperienceController style;
    style.applyTheme(QStringLiteral("nocturnal"));
        style.setMaterialMode(control == "jellyElasticity" ? 1 : 0);
        style.setAutoRotate(0);
        style.setAutoRotateSpeed(0);
        style.setCinemaShake(0);
        style.setIdleBreathingEnabled(false);
        style.setFloatingCubesEnabled(false);
        style.setMeteorsEnabled(false);
        style.setBurstEnabled(false);
        style.setStreamHighlightEnabled(false);
        QVERIFY(style.setProperty(control.constData(), pass ? high : low));
        StableImpactSource source;
        TerrainReactorItem item(window.contentItem());
        item.setSize(QSizeF(480,270));
        item.setStyleSource(&style);
        item.setFeatureSource(&source);
        if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software)
            QSKIP("No accelerated backend");
        item.setActive(true);
        QTRY_COMPARE_WITH_TIMEOUT(item.renderStatus(),TerrainReactorItem::RenderStatus::Ready,5000);
        QImage settled;
        QTRY_VERIFY_WITH_TIMEOUT((settled = window.grabWindow()).size()
                                == physicalSize, 3000);
        const quint64 readyFrame = item.frameCount();
        QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() >= readyFrame + 3, 3000);
        QCOMPARE(window.screen(), screen);
        QCOMPARE(window.size(), logicalSize);
        QTest::qWait(200);
        if (QByteArray(QTest::currentDataTag()) == "impact-decay")
            source.publishImpact(.9);
        else
            source.publishBeat(.9);
        QTest::qWait(delay);
        frames[pass] = window.grabWindow();
        QVERIFY(!frames[pass].isNull());
        QCOMPARE(frames[pass].size(), physicalSize);
        item.setActive(false);
    }
    QCOMPARE(frames[0].size(),frames[1].size());
    int changed=0;
    for(int y=90;y<240;++y) for(int x=80;x<400;++x) {
        const auto a=frames[0].pixelColor(x,y), b=frames[1].pixelColor(x,y);
        if(std::abs(a.red()-b.red())+std::abs(a.green()-b.green())+std::abs(a.blue()-b.blue())>24)
            ++changed;
    }
    qInfo() << control << "beat material pixels changed:" << changed;
    QVERIFY2(changed>150,"Beat material parameter has no visible effect");
}

void TerrainReactorGpuSmokeTest::regularBeatBrieflyBrightensThenReturns()
{
    QQuickWindow window;
    window.resize(480, 270);
    window.setColor(QColor(4, 6, 11));
    PlayerExperienceController style;
    style.applyTheme(QStringLiteral("nocturnal"));
    // Extra center lamps are a legacy custom-material feature only.
    style.setColorMode(PlayerExperienceController::Custom);
    style.setAutoRotate(0);
    style.setAutoRotateSpeed(0);
    style.setMotionResponse(0);
    style.setCinemaShake(0);
    style.setTerrainAmplitude(0);
    style.setIdleBreathingEnabled(false);
    style.setRipplesEnabled(false);
    style.setFloatingCubesEnabled(false);
    style.setMeteorsEnabled(false);
    style.setBurstEnabled(false);
    style.setStreamHighlightEnabled(false);
    StableImpactSource source;
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(480, 270));
    item.setStyleSource(&style);
    item.setFeatureSource(&source);
    window.show();
    QVERIFY(waitForGpuWindow(window));
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
    QTest::qWait(150);
    const auto centerLight = [&window](double* nearWhiteFraction = nullptr) {
        const QImage frame = window.grabWindow();
        quint64 total = 0;
        quint64 visiblePixels = 0;
        quint64 nearWhitePixels = 0;
        for (int y = frame.height() / 2; y < frame.height() * 3 / 4; ++y) {
            for (int x = frame.width() / 3; x < frame.width() * 2 / 3; ++x) {
                const QColor c = frame.pixelColor(x, y);
                total += quint64(c.red() + c.green() + c.blue());
                // Exclude the dark backdrop from the exposure denominator.
                // Small pale highlights are allowed, but not a white sheet.
                if (qGray(c.rgb()) > 20) {
                    ++visiblePixels;
                    if (c.red() >= 235 && c.green() >= 235 && c.blue() >= 235)
                        ++nearWhitePixels;
                }
            }
        }
        if (nearWhiteFraction)
            *nearWhiteFraction = visiblePixels > 0
                ? double(nearWhitePixels) / double(visiblePixels) : 0.0;
        return total;
    };
    const quint64 baseline = centerLight();
    QVERIFY(baseline > 0);
    source.publishBeat(0.7);
    quint64 peak = baseline;
    double peakNearWhiteFraction = 0;
    for (int frame = 0; frame < 5; ++frame) {
        QTest::qWait(16);
        double nearWhiteFraction = 0;
        const quint64 current = centerLight(&nearWhiteFraction);
        if (current > peak) {
            peak = current;
            peakNearWhiteFraction = nearWhiteFraction;
        }
    }
    QTest::qWait(500);
    const quint64 settled = centerLight();
    qInfo() << "Regular beat light:" << baseline << peak << settled;
    qInfo() << "Regular beat maximum near-white fraction:" << peakNearWhiteFraction;
    QVERIFY2(peakNearWhiteFraction < 0.05,
             "Beat highlights must not wash out the visible terrain into near-white");
    QVERIFY2(peak > baseline * 105 / 100,
             "A regular beat must visibly brighten the terrain without an impact");
    QVERIFY2(settled < peak * 97 / 100
                 && std::abs(double(settled) / double(baseline) - 1.0) < 0.03,
             "Beat lighting must return to the restrained steady exposure");
    style.setRhythmStrength(0);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderedStyleRevision(), item.styleRevision(), 3000);
    const quint64 disabled = centerLight();
    source.publishBeat(0.7);
    quint64 disabledPeak = disabled;
    for (int frame = 0; frame < 5; ++frame) {
        QTest::qWait(16);
        disabledPeak = std::max(disabledPeak, centerLight());
    }
    QVERIFY2(disabledPeak <= disabled * 103 / 100,
             "Zero rhythm strength must disable the beat flash");

    style.setRhythmStrength(100);
    style.setRhythmSensitivity(0);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderedStyleRevision(), item.styleRevision(), 3000);
    const quint64 insensitive = centerLight();
    source.publishBeat(0.9);
    quint64 insensitivePeak = insensitive;
    for (int frame = 0; frame < 5; ++frame) {
        QTest::qWait(16);
        insensitivePeak = std::max(insensitivePeak, centerLight());
    }
    QVERIFY2(insensitivePeak <= insensitive * 103 / 100,
             "Zero rhythm sensitivity must suppress an otherwise strong beat flash");
    item.setActive(false);
}

void TerrainReactorGpuSmokeTest::ordinaryBeatProducesRippleWithMeteorsEnabled()
{
    PlayerExperienceController style;
    QVERIFY(style.applyTheme(QStringLiteral("nocturnal")));
    style.setAutoRotate(0); style.setAutoRotateSpeed(0);
    style.setTerrainAmplitude(0); style.setIdleBreathingEnabled(false);
    style.setFloatingCubesEnabled(false); style.setStreamHighlightEnabled(false);
    style.setColumnInnerLight(0); style.setMeteorsEnabled(true);
    style.setRipplesEnabled(true); style.setRippleStrength(100);
    style.setRippleWidth(100); style.setRippleDecay(100);
    style.setReactorBrightness(100); style.setSubjectClarity(114);
    StableImpactSource source;
    QQuickWindow window; window.resize(640,360); window.setColor(QColor(4,6,11));
    TerrainReactorItem item(window.contentItem()); item.setSize(QSizeF(640,360));
    item.setStyleSource(&style); item.setFeatureSource(&source);
    window.show(); QVERIFY(waitForGpuWindow(window)); item.setActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount()>3,5000);
    const QImage before=window.grabWindow();
    source.publishBeat(1.0); // No impact: no meteor launch may explain the wave.
    QTest::qWait(350);
    const QImage after=window.grabWindow();
    int changed=0;
    for(int y=after.height()/3;y<after.height()*9/10;++y)
        for(int x=after.width()/10;x<after.width()*9/10;++x){
            const QColor a=before.pixelColor(x,y),b=after.pixelColor(x,y);
            if(std::abs(a.red()-b.red())+std::abs(a.green()-b.green())
                +std::abs(a.blue()-b.blue())>30)++changed;
        }
    qInfo()<<"Ordinary beat ripple with meteors enabled changed pixels:"<<changed;
    QVERIFY2(changed>150,"Enabling meteors must not suppress ordinary beat ripples");
    item.setActive(false);
}

void TerrainReactorGpuSmokeTest::subjectClarityPreservesBrightThemeExposure()
{
    PlayerExperienceController style;
    QVERIFY(style.applyTheme(QStringLiteral("glacier-day")));
    style.setSubjectClarity(114);
    style.setAutoRotate(0);
    style.setAutoRotateSpeed(0);
    style.setCinemaShake(0);
    style.setIdleBreathingEnabled(false);
    style.setRipplesEnabled(false);
    style.setFloatingCubesEnabled(false);
    style.setMeteorsEnabled(false);
    style.setStreamHighlightEnabled(false);

    QQuickWindow window;
    window.resize(480, 270);
    window.setColor(QColor("#f6f5ef"));
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(480, 270));
    item.setStyleSource(&style);
    item.setUseSyntheticFeatures(true);
    item.setSyntheticFeatures({0.,0.,0.,0.,0.,0.,0.,0.}, 0, 0, false, false);
    window.show();
    QVERIFY(waitForGpuWindow(window));
    item.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderStatus(), TerrainReactorItem::RenderStatus::Ready, 5000);
    QTest::qWait(250);
    const QImage neutral = window.grabWindow();
    style.setSubjectClarity(140);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderedStyleRevision(), item.styleRevision(), 3000);
    QTest::qWait(100);
    const QImage clear = window.grabWindow();

    quint64 neutralLight = 0;
    quint64 clearLight = 0;
    int changed = 0;
    int samples = 0;
    for (int y = neutral.height() / 3; y < neutral.height() * 4 / 5; ++y) {
        for (int x = neutral.width() / 4; x < neutral.width() * 3 / 4; ++x) {
            const QColor a = neutral.pixelColor(x, y);
            const QColor b = clear.pixelColor(x, y);
            neutralLight += quint64(54 * a.red() + 183 * a.green() + 19 * a.blue());
            clearLight += quint64(54 * b.red() + 183 * b.green() + 19 * b.blue());
            if (std::abs(a.red() - b.red()) + std::abs(a.green() - b.green())
                + std::abs(a.blue() - b.blue()) > 18)
                ++changed;
            ++samples;
        }
    }
    const double exposureRatio = neutralLight > 0
        ? double(clearLight) / double(neutralLight) : 1.0;
    qInfo() << "Bright-theme clarity changed pixels/exposure ratio:"
            << changed << exposureRatio << "samples" << samples;
    QCOMPARE(changed, 0);
    QVERIFY2(std::abs(exposureRatio - 1.0) < .001,
             "Hidden legacy clarity must not alter the authored reference palette");
    item.setActive(false);
}

void TerrainReactorGpuSmokeTest::floatingControlsReachGpu_data()
{
    QTest::addColumn<QByteArray>("control");
    for(const auto* key:{"floatingBlockMinSize","floatingBlockMaxSize","floatingBlockSpeed","floatingBlockIntensity"})
        QTest::newRow(key)<<QByteArray(key);
}
void TerrainReactorGpuSmokeTest::floatingControlsReachGpu()
{
    QFETCH(QByteArray,control);
    QImage frames[2];
    for(int pass=0;pass<2;++pass){
        PlayerExperienceController style;
        QVERIFY(style.applyTheme(control=="floatingBlockMinSize"
            ?QStringLiteral("ink-wash"):QStringLiteral("nocturnal")));
        style.setAutoRotate(0);style.setAutoRotateSpeed(0);style.setTerrainAmplitude(0);
        style.setColumnInnerLight(0);style.setRipplesEnabled(false);style.setMeteorsEnabled(false);
        style.setIdleBreathingEnabled(false);style.setStreamHighlightEnabled(false);style.setBurstEnabled(false);
        style.setFloatingCubesEnabled(true);style.setFloatingBlockMinSize(9);style.setFloatingBlockMaxSize(80);
        style.setFloatingBlockIntensity(55);style.setFloatingBlockSpeed(77);
        QVERIFY(style.setProperty(control.constData(),pass?100:0));
        QQuickWindow window;window.resize(640,360);window.setColor(QColor(4,6,11));
        TerrainReactorItem item(window.contentItem());item.setSize(QSizeF(640,360));item.setStyleSource(&style);
        item.setUseSyntheticFeatures(true);item.setSyntheticFeatures({0,0,0,0,0,0,0,0},0,0,false,false);
        window.show();QVERIFY(waitForGpuWindow(window));item.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(item.frameCount()>3,5000);
        if(control!="floatingBlockMinSize")item.setSyntheticFeatures({0,0,0,0,0,0,0,0},0,0,true,false);
        QTest::qWait(100);frames[pass]=window.grabWindow();item.setActive(false);
    }
    int changed=0;
    for(int y=0;y<frames[0].height()*2/3;++y)for(int x=0;x<frames[0].width();++x){
        const QColor a=frames[0].pixelColor(x,y),b=frames[1].pixelColor(x,y);
        if(std::abs(a.red()-b.red())+std::abs(a.green()-b.green())+std::abs(a.blue()-b.blue())>30)++changed;
    }
    qInfo()<<control<<"floating changed pixels"<<changed;
    QVERIFY2(changed>50,"Floating control did not visibly reach the GPU");
}

void TerrainReactorGpuSmokeTest::meteorParticlePoolUploadsAndClearsWithoutResourceRebuild()
{
    PlayerExperienceController style;
    QVERIFY(style.applyTheme(QStringLiteral("nocturnal")));
    style.setAutoRotate(0); style.setAutoRotateSpeed(0);
    style.setTerrainAmplitude(0); style.setIdleBreathingEnabled(false);
    style.setFloatingCubesEnabled(false); style.setStreamHighlightEnabled(false);
    style.setColumnInnerLight(0); style.setMeteorsEnabled(true); style.setRipplesEnabled(false);
    StableImpactSource source;
    QQuickWindow window; window.resize(640,360); window.setColor(QColor(4,6,11));
    TerrainReactorItem item(window.contentItem()); item.setSize(QSizeF(640,360));
    // Put the particles inside the original MeshBasicMaterial fog's 30..95
    // view-depth interval instead of testing fully fogged geometry at 103.
    item.zoomBy((60.0-item.cameraDistance())/.04,0);
    item.setStyleSource(&style); item.setFeatureSource(&source);
    window.show(); QVERIFY(waitForGpuWindow(window)); item.setActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount()>3,5000);
    const QImage before=window.grabWindow();
    source.publishImpact(1);
    const auto generation=item.telemetry_->resourceGeneration.load();
    QTRY_VERIFY_WITH_TIMEOUT(item.telemetry_->activeMeteorParticles.load()>0,1000);
    QCOMPARE(item.telemetry_->meteorParticleSlots.load(),200);
    // Strength1 always lands by .267s. No white wave is enabled, and the
    // reference collision particles move upward after the meteor is gone.
    QTest::qWait(300);
    const QImage after=window.grabWindow();
    if (!qEnvironmentVariable("AGPLAYER_PARTICLE_QA_OUTPUT").isEmpty()) {
        before.save(qEnvironmentVariable("AGPLAYER_PARTICLE_QA_OUTPUT")+"-before.png");
        after.save(qEnvironmentVariable("AGPLAYER_PARTICLE_QA_OUTPUT")+"-after.png");
    }
    int brightChanges=0;
    for(int y=0;y<after.height()/2;++y) for(int x=0;x<after.width();++x) {
        const auto a=before.pixelColor(x,y), b=after.pixelColor(x,y);
        if(b.red()>100 && b.green()>100 && b.blue()>100
            && b.red()-a.red()>35 && b.green()-a.green()>35) ++brightChanges;
    }
    qInfo()<<"Diagnostic upper-half bright pixel changes (not a reference visual oracle)"<<brightChanges;
    // The photographed particles are subpixel, alpha .6 and fogged. The old
    // >100 RGB threshold had no reference oracle and is retained in QA logs,
    // not used as a false material-parity claim. Verify actual pool uploads.
    QVERIFY(item.telemetry_->activeMeteorParticles.load()>0);
    style.setMeteorsEnabled(false);
    const auto beforeDisable=item.frameCount();
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount()>beforeDisable,1000);
    QTRY_COMPARE_WITH_TIMEOUT(item.telemetry_->activeMeteorParticles.load(),0,1000);
    QCOMPARE(item.telemetry_->meteorParticleSlots.load(),0);
    QCOMPARE(item.telemetry_->resourceGeneration.load(),generation);
    QVERIFY(item.renderStatus()==TerrainReactorItem::RenderStatus::Ready);
    item.setActive(false);
}

void TerrainReactorGpuSmokeTest::meteorTouchdownProducesReferenceWhiteRipple()
{
    PlayerExperienceController style;
    QVERIFY(style.applyTheme(QStringLiteral("nocturnal")));
    style.setAutoRotate(0);
    style.setAutoRotateSpeed(0);
    style.setTerrainAmplitude(0);
    style.setIdleBreathingEnabled(false);
    style.setFloatingCubesEnabled(false);
    style.setStreamHighlightEnabled(false);
    style.setColumnInnerLight(0);
    style.setMeteorsEnabled(true);
    style.setRipplesEnabled(true);
    style.setRippleStrength(100);
    style.setRippleWidth(100);
    style.setRippleDecay(100);
    style.setReactorBrightness(100);
    style.setSubjectClarity(114);
    StableImpactSource source;
    QQuickWindow window;
    window.resize(640, 360);
    window.setColor(QColor(4, 6, 11));
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(640, 360));
    item.setStyleSource(&style);
    item.setFeatureSource(&source);
    window.show();
    QVERIFY(waitForGpuWindow(window));
    item.setActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() > 3, 5000);
    source.publishImpact(1.0);
    // At strength1, original height30..40 / speed2.5..3 /60 lands within .267s.
    // Inspect the remaining white wave after the meteor has disappeared.
    QTest::qWait(620);
    int maximumNeutralPixels = 0;
    for (int sample = 0; sample < 10; ++sample) {
        const QImage frame = window.grabWindow();
        int neutralPixels = 0;
        for (int y = frame.height()/3; y < frame.height()*9/10; ++y)
            for (int x = frame.width()/10; x < frame.width()*9/10; ++x) {
                const QColor color = frame.pixelColor(x,y);
                if (color.red() > 70 && color.green() > 70 && color.blue() > 70
                    && std::max({color.red(),color.green(),color.blue()})
                        - std::min({color.red(),color.green(),color.blue()}) < 25)
                    ++neutralPixels;
            }
        maximumNeutralPixels = std::max(maximumNeutralPixels, neutralPixels);
        QTest::qWait(16);
    }
    qInfo() << "Post-touchdown white ripple pixels" << maximumNeutralPixels;
    QVERIFY2(maximumNeutralPixels > 4,
             "Canonical meteor touchdown must emit a white ripple, not the cyan normal-wave palette");
    item.setActive(false);
}

void TerrainReactorGpuSmokeTest::wideGroundFadesBeforeCircularBoundary()
{
    PlayerExperienceController style;
    style.applyTheme(QStringLiteral("nocturnal"));
    style.setColumnDensity(125);
    style.setColumnInnerLight(100);
    style.setColumnLightSpill(100);
    style.setColumnLightRadius(100);
    style.setReactorBrightness(100);
    style.setAutoRotate(0);
    style.setIdleBreathingEnabled(false);
    style.setRipplesEnabled(false);
    style.setMeteorsEnabled(false);
    style.setFloatingCubesEnabled(false);
    style.setBurstEnabled(false);
    style.setStreamHighlightEnabled(false);
    style.setTerrainAmplitude(0);
    style.setCoolColor("#68AEDD");
    style.setWarmColor("#68AEDD");
    style.setAccentColor("#68AEDD");
    style.setPeakColor("#68AEDD");
    QQuickWindow window;
    window.setColor(Qt::black);
    window.resize(640, 640);
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(640, 640));
    item.setQuality(TerrainReactorItem::Quality::High);
    item.setStyleSource(&style);
    item.setUseSyntheticFeatures(true);
    // Test radial falloff under a fixed audio-powered source; silence has its
    // own dark-material fixture and must not require bright ambient lighting.
    item.setSyntheticFeatures({0.65, 0.60, 0.50, 0.40, 0, 0, 0, 0}, 0.5, 0, false, false);
    item.orbitBy(-item.cameraYaw(), 1.15 - item.cameraPitch(), 0);
    item.zoomBy(10000, 0);
    window.show();
    QVERIFY(waitForGpuWindow(window));
    item.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderStatus(), TerrainReactorItem::RenderStatus::Ready, 5000);
    QTest::qWait(250);
    const QImage frame = window.grabWindow().scaled(640, 640);
    QVERIFY(!frame.isNull());
    const auto mean = [&frame](int x0, int x1) {
        double sum = 0;
        for (int y = 300; y < 320; ++y)
            for (int x = x0; x < x1; ++x) sum += qGray(frame.pixel(x, y));
        return sum / ((x1 - x0) * 20);
    };
    const double inner = mean(425, 450);
    const double outer = mean(565, 585);
    qInfo() << "Wide stage inner/outer luminance:" << inner << outer;
    QVERIFY(inner > 12);
    QVERIFY2(outer < inner * 0.35, "Outer tiles must recede before the circular cutoff");
    item.setActive(false);
}

void TerrainReactorGpuSmokeTest::firstActiveCreatesResourcesAndRendersStaticFeatures()
{
    QQuickWindow window;
    window.resize(480, 270);
    TerrainReactorItem item(window.contentItem());
    QCOMPARE(item.liveRendererCount(), 0);
    item.setSize(QSizeF(480, 270));
    QCOMPARE(item.liveRendererCount(), 0);
    item.setUseSyntheticFeatures(true);
    item.setQuality(TerrainReactorItem::Quality::Balanced);
    QCOMPARE(item.liveRendererCount(), 0);
    item.setSyntheticFeatures({0.8, 0.7, 0.6, 0.5,
                               0.4, 0.3, 0.2, 0.1}, 0.7, 0.4, true, false);
    QCOMPARE(item.liveRendererCount(), 0);
    window.show();

    QVERIFY(waitForGpuWindow(window));
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
    const int expectedBalancedWidth = qRound(
        item.width() * window.effectiveDevicePixelRatio() * 0.90);
    QTRY_COMPARE_WITH_TIMEOUT(item.fixedColorBufferWidth(),
                              expectedBalancedWidth, 3000);
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

    const quint64 stressGeneration = item.resourceGeneration();
    const quint64 stressFrameBaseline = item.frameCount();
    for (int cycle = 0; cycle < 50; ++cycle) {
        item.setActive(false);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QVERIFY(!item.renderingRequested());
        item.setActive(true);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QVERIFY(item.renderingRequested());
        QCOMPARE(item.liveRendererCount(), 1);
    }
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() > stressFrameBaseline, 5000);
    QCOMPARE(item.resourceGeneration(), stressGeneration);
    QCOMPARE(item.liveRendererCount(), 1);
}

void TerrainReactorGpuSmokeTest::staticFeaturesKeepRenderingWithoutGuiFeatureUpdates()
{
    QQuickWindow window;
    window.resize(480, 270);
    StableImpactSource source;
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(480, 270));
    item.setQuality(TerrainReactorItem::Quality::Balanced);
    item.setFeatureSource(&source);
    window.show();
    QVERIFY(waitForGpuWindow(window));
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
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() >= 3, 5000);
    const quint64 staticFrames = item.frameCount();
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() >= staticFrames + 3, 1000);

    item.setActive(false);
    QTRY_VERIFY_WITH_TIMEOUT(!item.renderingRequested(), 3000);
    QTest::qWait(100);
    const quint64 inactiveFrames = item.frameCount();
    QTest::qWait(200);
    QCOMPARE(item.frameCount(), inactiveFrames);

    item.setActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() >= inactiveFrames + 3, 5000);
    window.hide();
    QTRY_VERIFY_WITH_TIMEOUT(!item.renderingRequested(), 3000);
    QTest::qWait(100);
    const quint64 hiddenFrames = item.frameCount();
    QTest::qWait(200);
    QCOMPARE(item.frameCount(), hiddenFrames);

    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(item.renderingRequested(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() >= hiddenFrames + 3, 5000);
}

void TerrainReactorGpuSmokeTest::explicitImpactBrightensAStableTerrainFrame()
{
    QQuickWindow window;
    window.resize(480, 270);
    // Match the actual immersive host, not Qt's default white window. Raised
    // columns otherwise occlude white gaps and invert the brightness metric.
    window.setColor(QColor(4, 6, 11));
    PlayerExperienceController style;
    style.setColorMode(PlayerExperienceController::Custom);
    style.setAutoRotate(0);
    style.setAutoRotateSpeed(0);
    style.setMotionResponse(0);
    style.setIdleBreathingEnabled(false);
    style.setRipplesEnabled(false);
    style.setFloatingCubesEnabled(false);
    style.setMeteorsEnabled(false);
    style.setBurstEnabled(false);
    style.setStreamHighlightEnabled(false);
    StableImpactSource source;
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(480, 270));
    item.setStyleSource(&style);
    item.setFeatureSource(&source);
    window.show();
    QVERIFY(waitForGpuWindow(window));
    const auto api = window.rendererInterface()->graphicsApi();
    if (api != QSGRendererInterface::Direct3D11
        && api != QSGRendererInterface::OpenGL
        && api != QSGRendererInterface::Vulkan
        && api != QSGRendererInterface::Metal) {
        QSKIP("No accelerated Qt Quick backend is available");
    }

    const auto presentedFrames = std::make_shared<std::atomic<int>>(0);
    QObject::connect(&window, &QQuickWindow::frameSwapped, &window,
                     [presentedFrames] {
        presentedFrames->fetch_add(1, std::memory_order_release);
    }, Qt::DirectConnection);

    item.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderStatus(),
                              TerrainReactorItem::RenderStatus::Ready, 5000);
    const quint64 baselineFeatureRevision = item.featureRevision();
    const quint64 baselineStyleRevision = item.styleRevision();
    QTRY_VERIFY_WITH_TIMEOUT(([&item, baselineFeatureRevision,
                               baselineStyleRevision] {
        item.update();
        return item.renderedFeatureRevision() == baselineFeatureRevision
            && item.renderedStyleRevision() == baselineStyleRevision
            && item.stableRenderedFrameCount() >= 3;
    }()), 5000);
    const int baselinePresented = presentedFrames->load(
        std::memory_order_acquire);
    QTRY_VERIFY_WITH_TIMEOUT(([&item, presentedFrames, baselinePresented] {
        item.update();
        return presentedFrames->load(std::memory_order_acquire)
            >= baselinePresented + 2;
    }()), 5000);
    const QImage baseline = window.grabWindow().convertToFormat(
        QImage::Format_RGBA8888);
    QVERIFY(!baseline.isNull());

    const quint64 beforeRevision = item.featureRevision();
    const int impactPresented = presentedFrames->load(
        std::memory_order_acquire);
    source.publishImpact(1.0);
    QTRY_VERIFY_WITH_TIMEOUT(item.featureRevision() > beforeRevision, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(item.impactRevision(), source.impactRevision(), 3000);
    QVERIFY(item.impactStrength() > 0.99);
    const quint64 impactFeatureRevision = item.featureRevision();
    QTRY_VERIFY_WITH_TIMEOUT(([&item, presentedFrames, impactPresented,
                               impactFeatureRevision] {
        item.update();
        return item.renderedFeatureRevision() == impactFeatureRevision
            && presentedFrames->load(std::memory_order_acquire)
                >= impactPresented + 2;
    }()), 3000);
    QImage impacted;
    quint64 strongestLight = 0;
    for (int sample = 0; sample < 5; ++sample) {
        QTest::qWait(16);
        const QImage candidate = window.grabWindow().convertToFormat(
            QImage::Format_RGBA8888);
        QCOMPARE(candidate.size(), baseline.size());
        quint64 light = 0;
        const QRect roi(candidate.width() / 4, candidate.height() / 4,
                        candidate.width() / 2, candidate.height() / 2);
        for (int y = roi.top(); y <= roi.bottom(); ++y)
            for (int x = roi.left(); x <= roi.right(); ++x) {
                const QColor c = candidate.pixelColor(x, y);
                light += quint64(c.red() + c.green() + c.blue());
            }
        if (light > strongestLight) {
            strongestLight = light;
            impacted = candidate;
        }
    }
    QVERIFY(!impacted.isNull());

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
    // This native artistic contract limits pixel-light response, not vertex
    // height. Original presets intentionally have strong high-band cap flashes;
    // their geometry is verified separately against captured GPU height data.
    style.setCoolColor(style.coolColor());
    QVERIFY(style.themeId().isEmpty());
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
    QVERIFY(waitForGpuWindow(window));
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
    // CPU terrain tests enforce the geometric height margin. The GPU frames
    // are sampled at successive animation times, so allow a small lighting
    // tolerance while still rejecting visibly dominant high-frequency relief.
    QVERIFY2(highStrongest <= lowMidStrongest * 108 / 100,
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

void TerrainReactorGpuSmokeTest::silentTerrainDoesNotGenerateTopFlashes()
{
    PlayerExperienceController style;
    style.applyTheme(QStringLiteral("nocturnal"));
    style.setAutoRotate(0);
    style.setAutoRotateSpeed(0);
    style.setMotionResponse(0);
    style.setCinemaShake(0);
    style.setIdleBreathingEnabled(false);
    style.setRipplesEnabled(false);
    style.setFloatingCubesEnabled(false);
    style.setMeteorsEnabled(false);
    style.setBurstEnabled(false);
    style.setStreamHighlightEnabled(false);
    QQuickWindow window;
    window.resize(640, 360);
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(640, 360));
    item.setStyleSource(&style);
    item.setQuality(TerrainReactorItem::Quality::High);
    item.setUseSyntheticFeatures(true);
    item.setSyntheticFeatures({0,0,0,0,0,0,0,0}, 0, 0, false, false);
    window.show();
    QVERIFY(waitForGpuWindow(window));
    item.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderStatus(), TerrainReactorItem::RenderStatus::Ready, 5000);
    QTest::qWait(300);
    const QImage off = window.grabWindow();
    QVERIFY(!off.isNull());
    style.setStreamHighlightEnabled(true);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderedStyleRevision(), item.styleRevision(), 3000);
    int brightestChanges = 0;
    for (int frame = 0; frame < 4; ++frame) {
        QTest::qWait(80);
        const QImage on = window.grabWindow();
        QCOMPARE(on.size(), off.size());
        int changed = 0;
        // Restrict the comparison to the dense central terrain. The wider
        // rectangle included independent starfield twinkles and made this
        // cap-light assertion depend on which stars crossed it that frame.
        for (int y = on.height() / 2; y < on.height() * 4 / 5; ++y)
            for (int x = on.width() / 3; x < on.width() * 2 / 3; ++x)
                if (qGray(on.pixel(x,y)) > qGray(off.pixel(x,y)) + 3) ++changed;
        brightestChanges = std::max(brightestChanges, changed);
    }
    qInfo() << "Silent top-flash added pixels:" << brightestChanges;
    // A few edge pixels can move as the fixed scene accumulates subpixel AA;
    // reject an actual flash region, not rasterization noise below 0.1% ROI.
    QVERIFY2(brightestChanges < 24, "The top-flash clock is producing light without audio excitation");
    item.setActive(false);
}

void TerrainReactorGpuSmokeTest::silentTerrainGeometryStaysFixedWithIdleReliefEnabled()
{
    PlayerExperienceController style;
    style.applyTheme(QStringLiteral("ink-wash"));
    style.setAutoRotate(0);
    style.setAutoRotateSpeed(0);
    style.setMotionResponse(0);
    style.setCinemaShake(0);
    style.setIdleBreathingEnabled(true);
    style.setRipplesEnabled(false);
    style.setFloatingCubesEnabled(false);
    style.setMeteorsEnabled(false);
    style.setBurstEnabled(false);
    style.setStreamHighlightEnabled(false);
    QQuickWindow window;
    window.resize(640, 360);
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(640, 360));
    item.setStyleSource(&style);
    item.setQuality(TerrainReactorItem::Quality::High);
    item.setUseSyntheticFeatures(true);
    item.setSyntheticFeatures({0,0,0,0,0,0,0,0}, 0, 0, false, false);
    window.show();
    QVERIFY(waitForGpuWindow(window));
    item.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(item.renderStatus(),
                              TerrainReactorItem::RenderStatus::Ready, 5000);
    QTest::qWait(250);
    const QImage first = window.grabWindow();
    QVERIFY(!first.isNull());
    QTest::qWait(650);
    const QImage later = window.grabWindow();
    QCOMPARE(later.size(), first.size());
    int changed = 0;
    int silhouetteChanged = 0;
    int largestDelta = 0;
    for (int y = later.height() / 4; y < later.height() * 9 / 10; ++y) {
        for (int x = later.width() / 10; x < later.width() * 9 / 10; ++x) {
            const QColor a = first.pixelColor(x, y);
            const QColor b = later.pixelColor(x, y);
            const int delta = std::abs(a.red() - b.red())
                + std::abs(a.green() - b.green())
                + std::abs(a.blue() - b.blue());
            largestDelta = std::max(largestDelta, delta);
            if (delta > 5) {
                ++changed;
            }
            if ((qGray(a.rgb()) > 12) != (qGray(b.rgb()) > 12))
                ++silhouetteChanged;
        }
    }
    qInfo() << "Silent idle-relief changed/silhouette/max-delta pixels:"
            << changed << silhouetteChanged << largestDelta;
    QVERIFY2(silhouetteChanged < 16,
             "The platform silhouette drifted without audio or an explicit event");
    // Sparse star twinkles remain part of the environment, but a moving
    // terrain field would change thousands of pixels and its silhouette.
    QVERIFY2(changed < 300,
             "Silent terrain produced a broad time-driven visual change");
    item.setActive(false);
}

void TerrainReactorGpuSmokeTest::highFrequencyHeightControlChangesActualRelief()
{
    QQuickWindow window;
    window.resize(640, 360);
    window.setColor(QColor(4, 6, 11));
    PlayerExperienceController style;
    style.setAutoRotate(0);
    style.setAutoRotateSpeed(0);
    style.setMotionResponse(0);
    style.setCinemaShake(0);
    style.setIdleBreathingEnabled(false);
    style.setRipplesEnabled(false);
    style.setFloatingCubesEnabled(false);
    style.setMeteorsEnabled(false);
    style.setBurstEnabled(false);
    style.setStreamHighlightEnabled(false);
    style.setTerrainAmplitude(0);
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(640, 360));
    item.setQuality(TerrainReactorItem::Quality::High);
    item.setStyleSource(&style);
    item.setUseSyntheticFeatures(true);
    item.setSyntheticFeatures({0, 0, 0, 0, 0.72, 0.72, 0.72, 0.72},
                              0, 0, false, false);
    window.show();
    QVERIFY(waitForGpuWindow(window));
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
    const auto settle = [&item] {
        QTRY_COMPARE_WITH_TIMEOUT(item.renderedStyleRevision(),
                                   item.styleRevision(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(item.stableRenderedFrameCount() >= 5, 5000);
    };
    settle();
    const QImage low = window.grabWindow();
    const quint64 frameBeforeAmplitude = item.frameCount();
    style.setTerrainAmplitude(100);
    settle();
    QTRY_VERIFY_WITH_TIMEOUT(item.frameCount() >= frameBeforeAmplitude + 2, 3000);
    const QImage high = window.grabWindow();
    QVERIFY(!low.isNull());
    QCOMPARE(high.size(), low.size());
    int changed = 0;
    int visibleRelief = 0;
    const int backgroundLight = qGray(window.color().rgb());
    for (int y = low.height() / 3; y < low.height() * 4 / 5; ++y) {
        for (int x = low.width() / 4; x < low.width() * 3 / 4; ++x) {
            const QColor a = low.pixelColor(x, y);
            const QColor b = high.pixelColor(x, y);
            if (std::max(qGray(a.rgb()), qGray(b.rgb())) > backgroundLight + 4)
                ++visibleRelief;
            if (std::abs(a.red() - b.red()) + std::abs(a.green() - b.green())
                + std::abs(a.blue() - b.blue()) > 36) ++changed;
        }
    }
    qInfo() << "High-frequency height changed / visible relief pixels:" << changed << visibleRelief;
    // The reference camera is farther away. Measure response against actual
    // terrain coverage, not the enlarged amount of empty dark sky.
    QVERIFY2(visibleRelief > 1000 && changed > visibleRelief / 10,
             "Height control has almost no visible effect on high-frequency relief");
    item.setActive(false);
}

void TerrainReactorGpuSmokeTest::steadyCorePreservesHighlightDetailWithoutWhitePlateau()
{
    QQuickWindow window;
    window.resize(480, 270);
    window.setColor(QColor(4, 6, 11));
    PlayerExperienceController style;
    QVERIFY(style.applyTheme(QStringLiteral("nocturnal")));
    // These coverage and response-radius limits describe the customizable
    // native lamp material (historically 1.28x columns and softer edge fade).
    // Original presets use narrower columns, dark linear bases and fixed
    // spectral regions; their colors/heights have independent GPU oracles.
    style.setCoolColor(style.coolColor());
    QVERIFY(style.themeId().isEmpty());
    // Fixed legacy geometry inputs from 7fba19a load() defaults. This
    // regression is not a moving golden for new canonical preset defaults.
    style.setTerrainAmplitude(34);
    style.setColumnSize(95);
    style.setThemeCycleEnabled(false);
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
    TerrainReactorItem item(window.contentItem());
    item.setSize(QSizeF(480, 270));
    item.setStyleSource(&style);
    item.setUseSyntheticFeatures(true);
    item.setQuality(TerrainReactorItem::Quality::High);
    item.setSyntheticFeatures({0.92, 0.88, 0.45, 0.42,
                               0.62, 0.70, 0.56, 0.38},
                              0.72, 0.64, false, false);
    const auto presentedFrames = std::make_shared<std::atomic<int>>(0);
    QObject::connect(&window, &QQuickWindow::frameSwapped, &window,
                     [presentedFrames] {
        presentedFrames->fetch_add(1, std::memory_order_release);
    }, Qt::DirectConnection);
    window.show();
    QVERIFY(waitForGpuWindow(window));
    const auto api = window.rendererInterface()->graphicsApi();
    if (api != QSGRendererInterface::Direct3D11
        && api != QSGRendererInterface::OpenGL
        && api != QSGRendererInterface::Vulkan
        && api != QSGRendererInterface::Metal) {
        QSKIP("No accelerated Qt Quick backend is available");
    }

    const auto waitForStableRevisions = [&item, presentedFrames] {
        const quint64 expectedFeatureRevision = item.featureRevision();
        const quint64 expectedStyleRevision = item.styleRevision();
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
    const QImage frame = window.grabWindow().convertToFormat(
        QImage::Format_RGBA8888);
    QVERIFY(!frame.isNull());

    const QRect roi(frame.width() * 18 / 100, frame.height() * 20 / 100,
                    frame.width() * 64 / 100, frame.height() * 62 / 100);
    QVector<int> visibleLuminance;
    visibleLuminance.reserve(roi.width() * roi.height());
    int nearWhitePixels = 0;
    double chromaSum = 0.0;
    for (int y = roi.top(); y <= roi.bottom(); ++y) {
        for (int x = roi.left(); x <= roi.right(); ++x) {
            const QColor sample = frame.pixelColor(x, y);
            const int luminance = (54 * sample.red() + 183 * sample.green()
                                   + 19 * sample.blue()) / 256;
            if (luminance >= 20) {
                visibleLuminance.append(luminance);
                chromaSum += sample.hsvSaturationF();
            }
            if (luminance >= 248) ++nearWhitePixels;
        }
    }
    std::sort(visibleLuminance.begin(), visibleLuminance.end());
    QVERIFY2(!visibleLuminance.isEmpty(),
             "Stable synthetic spectrum produced no visible reactor pixels");
    qInfo() << "Default lit terrain mean saturation:" << chromaSum / visibleLuminance.size();
    QVERIFY2(chromaSum / visibleLuminance.size() > 0.40,
             "The lit default terrain loses its palette in a gray/white wash");
    const auto percentile = [&visibleLuminance](int numerator) {
        const qsizetype index = std::min(
            visibleLuminance.size() - 1,
            visibleLuminance.size() * numerator / 100);
        return visibleLuminance.at(index);
    };
    const int p90 = percentile(90);
    const int p95 = percentile(95);
    const int p99 = percentile(99);
    const int roiPixels = roi.width() * roi.height();
    qInfo() << "Terrain Reactor steady highlight ROI: near-white"
            << nearWhitePixels << "/" << roiPixels
            << "visible" << visibleLuminance.size()
            << "P90/P95/P99" << p90 << p95 << p99;
    QVERIFY2(p90 <= 170,
             "Steady crown lighting is too bright; reserve headroom for beats");

    QVERIFY2(visibleLuminance.size() * 100 >= roiPixels * 20,
             "Stable core became too dark or too sparse");
    QVERIFY2(nearWhitePixels * 100 <= visibleLuminance.size(),
             "Stable core contains a broad near-white clipped plateau");
    // The approved self-lit direction has no bright studio key light.
    // Do not demand a permanently bright 165/195 focal plateau: the regular
    // beat/impact tests require the actual audio-driven light increase.
    QVERIFY2(p95 > p90,
             "The dim steady core must still retain distinguishable highlights");
    QVERIFY2(p99 >= p90 + 8,
             "Highlight tail collapsed instead of retaining visible gradation");

    // Keep both public response-range endpoints on the real GPU path. Besides
    // guarding the maximum-radius falloff, this proves the control changes
    // the rendered footprint instead of only updating its UI value.
    const auto visiblePixelCount = [&roi](const QImage& image) {
        int count = 0;
        for (int y = roi.top(); y <= roi.bottom(); ++y) {
            for (int x = roi.left(); x <= roi.right(); ++x) {
                const QColor sample = image.pixelColor(x, y);
                const int luminance = (54 * sample.red() + 183 * sample.green()
                                       + 19 * sample.blue()) / 256;
                if (luminance >= 20) ++count;
            }
        }
        return count;
    };
    style.setResponseRange(50);
    waitForStableRevisions();
    const QImage minimumRange = window.grabWindow().convertToFormat(
        QImage::Format_RGBA8888);
    QCOMPARE(minimumRange.size(), frame.size());
    const int minimumRangeVisible = visiblePixelCount(minimumRange);

    style.setResponseRange(220);
    waitForStableRevisions();
    const QImage maximumRange = window.grabWindow().convertToFormat(
        QImage::Format_RGBA8888);
    QCOMPARE(maximumRange.size(), frame.size());
    const int maximumRangeVisible = visiblePixelCount(maximumRange);
    qInfo() << "Terrain Reactor response-range visible pixels:"
            << minimumRangeVisible << "->" << maximumRangeVisible
            << "/" << roiPixels;
    QVERIFY2(minimumRangeVisible * 100 >= roiPixels * 2,
             "Minimum response range removed the visible terrain");
    QVERIFY2(maximumRangeVisible * 100 >= roiPixels * 20,
             "Maximum response range removed the visible terrain");
    QVERIFY2(maximumRangeVisible >= minimumRangeVisible + roiPixels / 100,
             "Response range endpoints did not change the rendered footprint");
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
    QVERIFY(waitForGpuWindow(window));
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

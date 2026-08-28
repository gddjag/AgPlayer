#include "terrain_reactor_item.hpp"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTest>

class TerrainReactorGpuSmokeTest final : public QObject {
    Q_OBJECT

private slots:
    void firstActiveCreatesResourcesAndRendersStaticFeatures();
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

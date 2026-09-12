#include "video_frame_item.hpp"

#include "video_playback_controller.hpp"

#include <QQuickWindow>
#include <QTest>

#include <cmath>
#include <memory>

class TestableVideoFrameItem final : public VideoFrameItem {
public:
    using VideoFrameItem::VideoFrameItem;
    using VideoFrameItem::releaseResources;
};

class VideoFrameItemTest final : public QObject {
    Q_OBJECT

private slots:
    void frameChangesRequestARealWindowUpdateWithoutReuploadingDuplicates();
    void aspectFitHonorsSampleAspectRatioAndRotation_data();
    void aspectFitHonorsSampleAspectRatioAndRotation();
    void rotationTransformsPixelOrientation_data();
    void rotationTransformsPixelOrientation();
    void detachingControllerClearsRenderResources();
    void windowInvalidationReleasesAndRebuildsFromTheLatestCpuFrame();
    void repeatedFramesAndReleaseResourcesKeepOneBoundedTextureNode();
};

namespace {

std::shared_ptr<const VideoFrameSnapshot> makeFrame(
    const int width, const int height, const int sarNum, const int sarDen,
    const int rotationDegrees, const quint64 serial)
{
    auto frame = std::make_shared<VideoFrameSnapshot>();
    frame->width = width;
    frame->height = height;
    frame->stride = width * 4;
    frame->pixels.resize(frame->stride * height);
    for (int index = 0; index < frame->pixels.size(); index += 4) {
        frame->pixels[index] = static_cast<char>(0x20);
        frame->pixels[index + 1] = static_cast<char>(0x60);
        frame->pixels[index + 2] = static_cast<char>(0xA0);
        frame->pixels[index + 3] = static_cast<char>(0xFF);
    }
    frame->sarNum = sarNum;
    frame->sarDen = sarDen;
    frame->rotationDegrees = rotationDegrees;
    frame->generation = 7;
    frame->serial = serial;
    return frame;
}

std::shared_ptr<const VideoFrameSnapshot> makeDirectionalFrame(
    const int rotationDegrees, const quint64 serial)
{
    auto mutableFrame = std::const_pointer_cast<VideoFrameSnapshot>(
        makeFrame(200, 100, 1, 1, rotationDegrees, serial));
    for (int y = 0; y < mutableFrame->height; ++y) {
        for (int x = 0; x < mutableFrame->width; ++x) {
            const int index = y * mutableFrame->stride + x * 4;
            const bool redHalf = x < mutableFrame->width / 2;
            mutableFrame->pixels[index] = static_cast<char>(
                redHalf ? 0x00 : 0xFF);
            mutableFrame->pixels[index + 1] = 0;
            mutableFrame->pixels[index + 2] = static_cast<char>(
                redHalf ? 0xFF : 0x00);
            mutableFrame->pixels[index + 3] = static_cast<char>(0xFF);
        }
    }
    return mutableFrame;
}

void compareRect(const QRectF& actual, const QRectF& expected)
{
    constexpr qreal epsilon = 0.01;
    QVERIFY(std::abs(actual.x() - expected.x()) < epsilon);
    QVERIFY(std::abs(actual.y() - expected.y()) < epsilon);
    QVERIFY(std::abs(actual.width() - expected.width()) < epsilon);
    QVERIFY(std::abs(actual.height() - expected.height()) < epsilon);
}

void showWindow(QQuickWindow& window)
{
    window.setColor(Qt::black);
    window.resize(400, 300);
    window.show();
    window.requestUpdate();
    QCoreApplication::processEvents();
}

void releaseWindow(QQuickWindow& window)
{
    window.hide();
    window.releaseResources();
    QCoreApplication::processEvents();
}

} // namespace

void VideoFrameItemTest::frameChangesRequestARealWindowUpdateWithoutReuploadingDuplicates()
{
    // Catches either missing update() requests or uploading the same immutable
    // generation/serial again on every Scene Graph synchronization.
    QQuickWindow window;
    auto* item = new TestableVideoFrameItem(window.contentItem());
    item->setParentItem(window.contentItem());
    item->setSize(QSizeF(400, 300));
    VideoPlaybackController controller(nullptr, nullptr);
    item->setController(&controller);
    showWindow(window);

    const auto first = makeFrame(200, 100, 1, 1, 0, 1);
    const int initialRequests = item->updateRequestCountForTesting();
    item->presentFrameForTesting(first);
    QCOMPARE(item->updateRequestCountForTesting(), initialRequests + 1);
    QTRY_COMPARE(item->textureUploadCountForTesting(), 1);
    QCOMPARE(item->renderedSerialForTesting(), quint64{1});

    item->presentFrameForTesting(
        makeFrame(200, 100, 1, 1, 0, 1));
    QCOMPARE(item->updateRequestCountForTesting(), initialRequests + 1);
    QTest::qWait(20);
    QCOMPARE(item->textureUploadCountForTesting(), 1);

    item->presentFrameForTesting(
        makeFrame(200, 100, 1, 1, 0, 2));
    QTRY_COMPARE(item->textureUploadCountForTesting(), 2);
    QCOMPARE(item->liveTextureNodeCountForTesting(), 1);
    releaseWindow(window);
}

void VideoFrameItemTest::aspectFitHonorsSampleAspectRatioAndRotation_data()
{
    QTest::addColumn<int>("sarNum");
    QTest::addColumn<int>("sarDen");
    QTest::addColumn<int>("rotation");
    QTest::addColumn<QRectF>("expected");

    QTest::newRow("square-pixels") << 1 << 1 << 0
        << QRectF(0, 50, 400, 200);
    QTest::newRow("wide-sample-aspect") << 2 << 1 << 0
        << QRectF(0, 100, 400, 100);
    QTest::newRow("rotate-90") << 1 << 1 << 90
        << QRectF(125, 0, 150, 300);
    QTest::newRow("rotate-180") << 1 << 1 << 180
        << QRectF(0, 50, 400, 200);
    QTest::newRow("rotate-270") << 1 << 1 << 270
        << QRectF(125, 0, 150, 300);
}

void VideoFrameItemTest::aspectFitHonorsSampleAspectRatioAndRotation()
{
    // Catches using coded pixel dimensions as display dimensions or rotating
    // the pixels without swapping the aspect-fit width and height.
    QFETCH(int, sarNum);
    QFETCH(int, sarDen);
    QFETCH(int, rotation);
    QFETCH(QRectF, expected);

    QQuickWindow window;
    auto* item = new TestableVideoFrameItem(window.contentItem());
    item->setParentItem(window.contentItem());
    item->setSize(QSizeF(400, 300));
    showWindow(window);
    item->presentFrameForTesting(
        makeFrame(200, 100, sarNum, sarDen, rotation, 1));

    QTRY_COMPARE(item->textureUploadCountForTesting(), 1);
    compareRect(item->renderedRectForTesting(), expected);
    releaseWindow(window);
}

void VideoFrameItemTest::rotationTransformsPixelOrientation_data()
{
    QTest::addColumn<int>("rotation");
    QTest::addColumn<bool>("vertical");
    QTest::addColumn<QColor>("firstColor");
    QTest::addColumn<QColor>("secondColor");

    QTest::newRow("rotate-90-clockwise")
        << 90 << true << QColor(Qt::red) << QColor(Qt::blue);
    QTest::newRow("rotate-180")
        << 180 << false << QColor(Qt::blue) << QColor(Qt::red);
    QTest::newRow("rotate-270-clockwise")
        << 270 << true << QColor(Qt::blue) << QColor(Qt::red);
}

void VideoFrameItemTest::rotationTransformsPixelOrientation()
{
    // Catches changing only the letterbox dimensions while leaving the actual
    // decoded pixels unrotated inside the Scene Graph texture.
    QFETCH(int, rotation);
    QFETCH(bool, vertical);
    QFETCH(QColor, firstColor);
    QFETCH(QColor, secondColor);

    QQuickWindow window;
    const QSize windowSize = vertical ? QSize(100, 200) : QSize(200, 100);
    auto* item = new TestableVideoFrameItem(window.contentItem());
    item->setParentItem(window.contentItem());
    item->setSize(windowSize);
    window.resize(windowSize);
    window.setColor(Qt::black);
    window.show();
    item->presentFrameForTesting(makeDirectionalFrame(rotation, 1));
    QTRY_COMPARE(item->textureUploadCountForTesting(), 1);

    const QImage rendered = window.grabWindow();
    QVERIFY(!rendered.isNull());
    const QPoint first = vertical
        ? QPoint(rendered.width() / 2, rendered.height() / 4)
        : QPoint(rendered.width() / 4, rendered.height() / 2);
    const QPoint second = vertical
        ? QPoint(rendered.width() / 2, rendered.height() * 3 / 4)
        : QPoint(rendered.width() * 3 / 4, rendered.height() / 2);
    QCOMPARE(rendered.pixelColor(first), firstColor);
    QCOMPARE(rendered.pixelColor(second), secondColor);
    releaseWindow(window);
}

void VideoFrameItemTest::detachingControllerClearsRenderResources()
{
    // Catches an old controller's last frame and GPU texture surviving after
    // the VideoFrameItem is rebound or detached.
    QQuickWindow window;
    auto* item = new TestableVideoFrameItem(window.contentItem());
    item->setParentItem(window.contentItem());
    item->setSize(QSizeF(400, 300));
    VideoPlaybackController controller(nullptr, nullptr);
    item->setController(&controller);
    showWindow(window);
    item->presentFrameForTesting(makeFrame(200, 100, 1, 1, 0, 1));
    QTRY_COMPARE(item->liveTextureNodeCountForTesting(), 1);

    item->setController(nullptr);
    QTRY_COMPARE(item->liveTextureNodeCountForTesting(), 0);
    QVERIFY(item->renderedRectForTesting().isEmpty());
    releaseWindow(window);
}

void VideoFrameItemTest::windowInvalidationReleasesAndRebuildsFromTheLatestCpuFrame()
{
    // Catches keeping a QSGTexture across a window Scene Graph invalidation,
    // or clearing the immutable CPU snapshot so the restored window stays blank.
    auto* window = new QQuickWindow;
    window->setPersistentSceneGraph(false);
    auto* item = new TestableVideoFrameItem;
    item->setParentItem(window->contentItem());
    item->setSize(QSizeF(400, 300));
    showWindow(*window);
    item->presentFrameForTesting(makeFrame(200, 100, 1, 1, 0, 1));
    QTRY_COMPARE(item->textureUploadCountForTesting(), 1);
    QTRY_COMPARE(item->liveTextureNodeCountForTesting(), 1);

    releaseWindow(*window);
    delete window;
    QTRY_COMPARE(item->liveTextureNodeCountForTesting(), 0);

    auto* restoredWindow = new QQuickWindow;
    item->setParentItem(restoredWindow->contentItem());
    showWindow(*restoredWindow);
    item->requestFrameUpdateForTesting();
    QTRY_COMPARE(item->textureUploadCountForTesting(), 2);
    QTRY_COMPARE(item->liveTextureNodeCountForTesting(), 1);
    item->setParentItem(nullptr);
    releaseWindow(*restoredWindow);
    delete restoredWindow;
    delete item;
}

void VideoFrameItemTest::repeatedFramesAndReleaseResourcesKeepOneBoundedTextureNode()
{
    // Catches leaking one QSGTexture/QSGNode per frame or touching/deleting a
    // render-thread resource directly from releaseResources() on the GUI thread.
    QQuickWindow window;
    auto* item = new TestableVideoFrameItem(window.contentItem());
    item->setParentItem(window.contentItem());
    item->setSize(QSizeF(400, 300));
    showWindow(window);

    for (quint64 serial = 1; serial <= 20; ++serial) {
        item->presentFrameForTesting(
            makeFrame(32, 18, 1, 1, (serial % 4) * 90, serial));
        QTRY_COMPARE(item->renderedSerialForTesting(), serial);
        QCOMPARE(item->liveTextureNodeCountForTesting(), 1);
    }
    QCOMPARE(item->textureUploadCountForTesting(), 20);

    item->releaseResources();
    item->requestFrameUpdateForTesting();
    QTRY_COMPARE(item->textureUploadCountForTesting(), 21);
    QCOMPARE(item->liveTextureNodeCountForTesting(), 1);

    item->presentFrameForTesting({});
    QTRY_COMPARE(item->liveTextureNodeCountForTesting(), 0);
    releaseWindow(window);
}

QTEST_MAIN(VideoFrameItemTest)
#include "video_frame_item_test.moc"

#include "track_waveform_thumbnail_item.hpp"

#include <QMetaProperty>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSignalSpy>
#include <QTest>

class TestableTrackWaveformThumbnailItem final
    : public TrackWaveformThumbnailItem {
public:
    using TrackWaveformThumbnailItem::TrackWaveformThumbnailItem;
    using TrackWaveformThumbnailItem::updatePaintNode;
};

class TrackWaveformThumbnailItemTest final : public QObject {
    Q_OBJECT

private slots:
    void usesEveryCanvasPixelForContinuousEnvelope();
    void downsamplingPreservesMinimumAndMaximumEnvelope();
    void buildsCoverageGeometryForAntialiasedSampledPolyline();
    void reusesGeometryWhenOnlyColorChanges();
    void mixesThreeBandColorsWithoutChangingGeometry();
    void rebuildsOnlyForPeaksOrSize();
    void clearsOldNodeForInvalidContentOrSize();
    void exposesNoPlaybackProgressApi();
};

namespace {

constexpr int kExpectedThumbnailBuckets = 2048;
constexpr int kExpectedThumbnailBytes = kExpectedThumbnailBuckets * 2;

QByteArray fullEnvelope(const unsigned char extent)
{
    QByteArray result(kExpectedThumbnailBytes, '\0');
    const unsigned char lower = extent >= 128U
        ? 0U : static_cast<unsigned char>(128U - extent);
    const unsigned char upper = extent >= 128U
        ? 255U : static_cast<unsigned char>(128U + extent);
    for (int bucket = 0; bucket < kExpectedThumbnailBuckets; ++bucket) {
        result[bucket * 2] = static_cast<char>(lower);
        result[bucket * 2 + 1] = static_cast<char>(upper);
    }
    return result;
}

QByteArray fullBand(const unsigned char energy)
{
    return QByteArray(kExpectedThumbnailBuckets, static_cast<char>(energy));
}

QSGGeometryNode* geometryNode(QSGNode* node)
{
    return static_cast<QSGGeometryNode*>(node);
}

} // namespace

void TrackWaveformThumbnailItemTest::usesEveryCanvasPixelForContinuousEnvelope()
{
    // Catches returning to a fixed-width 128-line thumbnail which becomes
    // visibly sparse when the title column grows.
    TestableTrackWaveformThumbnailItem item;
    item.setWidth(320.0);
    item.setHeight(10.0);
    item.setPeaks(fullEnvelope(128U));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    QCOMPARE(node->childCount(), 0);
    QCOMPARE(geometryNode(node)->geometry()->vertexCount(), 640);
    QCOMPARE(geometryNode(node)->geometry()->drawingMode(),
             QSGGeometry::DrawTriangleStrip);

    const auto* vertices =
        geometryNode(node)->geometry()->vertexDataAsColoredPoint2D();
    QCOMPARE(vertices[0].x, 0.0F);
    QCOMPARE(vertices[0].y, 0.0F);
    QCOMPARE(vertices[1].y, 10.0F);
    QCOMPARE(vertices[638].x, 320.0F);
    delete node;
}

void TrackWaveformThumbnailItemTest::downsamplingPreservesMinimumAndMaximumEnvelope()
{
    // Catches point sampling that loses a narrow transient between two
    // destination pixels.
    QByteArray peaks = fullEnvelope(0U);
    peaks[500 * 2] = static_cast<char>(0U);
    peaks[500 * 2 + 1] = static_cast<char>(255U);

    TestableTrackWaveformThumbnailItem item;
    item.setWidth(2.0);
    item.setHeight(10.0);
    item.setPeaks(peaks);

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* vertices =
        geometryNode(node)->geometry()->vertexDataAsColoredPoint2D();
    QCOMPARE(vertices[0].y, 0.0F);
    QCOMPARE(vertices[1].y, 10.0F);
    QCOMPARE(vertices[2].y, 10.0F * 128.0F / 255.0F);
    QCOMPARE(vertices[3].y, 10.0F * 128.0F / 255.0F);
    delete node;
}

void TrackWaveformThumbnailItemTest::buildsCoverageGeometryForAntialiasedSampledPolyline()
{
    // Catches claiming antialiasing through QQuickItem::antialiasing alone:
    // the sampled outline must carry transparent-to-opaque coverage geometry.
    TestableTrackWaveformThumbnailItem item;
    item.setWidth(4096.0);
    item.setHeight(10.0);
    item.setPeaks(fullEnvelope(64U));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    QCOMPARE(geometryNode(node)->geometry()->drawingMode(),
             QSGGeometry::DrawTriangleStrip);
    QCOMPARE(geometryNode(node)->geometry()->vertexCount(), 8192);
    QVERIFY2(node->childCount() == 2,
             "high-zoom rendering must include upper/lower coverage strips");

    auto* upperCoverage = geometryNode(node->firstChild());
    auto* lowerCoverage = geometryNode(node->lastChild());
    QCOMPARE(upperCoverage->geometry()->drawingMode(),
             QSGGeometry::DrawTriangleStrip);
    QCOMPARE(upperCoverage->geometry()->vertexCount(), 8192);
    QCOMPARE(lowerCoverage->geometry()->vertexCount(), 8192);
    const auto* upper =
        upperCoverage->geometry()->vertexDataAsColoredPoint2D();
    const auto* lower =
        lowerCoverage->geometry()->vertexDataAsColoredPoint2D();
    QCOMPARE(upper[0].a, 0U);
    QCOMPARE(upper[1].a, 255U);
    QVERIFY(upper[0].y < upper[1].y);
    QCOMPARE(lower[0].a, 255U);
    QCOMPARE(lower[1].a, 0U);
    QVERIFY(lower[0].y < lower[1].y);

    item.setWaveformColor(QColor(12, 34, 56, 0));
    node = item.updatePaintNode(node, nullptr);
    item.setWaveformColor(QColor(12, 34, 56, 255));
    node = item.updatePaintNode(node, nullptr);
    upperCoverage = geometryNode(node->firstChild());
    lowerCoverage = geometryNode(node->lastChild());
    upper = upperCoverage->geometry()->vertexDataAsColoredPoint2D();
    lower = lowerCoverage->geometry()->vertexDataAsColoredPoint2D();
    QCOMPARE(upper[0].a, 0U);
    QCOMPARE(upper[1].a, 255U);
    QCOMPARE(lower[0].a, 255U);
    QCOMPARE(lower[1].a, 0U);
    delete node;
}

void TrackWaveformThumbnailItemTest::reusesGeometryWhenOnlyColorChanges()
{
    // Catches a color setter that reallocates or rewrites the vertex buffer.
    TestableTrackWaveformThumbnailItem item;
    item.setWidth(128.0);
    item.setHeight(10.0);
    item.setPeaks(fullEnvelope(64U));
    item.setWaveformColor(QColor(QStringLiteral("#112233")));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    QSGGeometry* const geometry = geometryNode(node)->geometry();
    auto* const vertices = geometry->vertexDataAsColoredPoint2D();
    vertices[0].y = -123.0F;

    item.setWaveformColor(QColor(QStringLiteral("#abcdef")));
    QSGNode* const recolored = item.updatePaintNode(node, nullptr);
    QCOMPARE(recolored, node);
    QCOMPARE(geometryNode(recolored)->geometry(), geometry);
    QCOMPARE(geometry->vertexDataAsColoredPoint2D(), vertices);
    QCOMPARE(vertices[0].y, -123.0F);
    QCOMPARE(vertices[0].r, 0xABU);
    QCOMPARE(vertices[0].g, 0xCDU);
    QCOMPARE(vertices[0].b, 0xEFU);
    delete recolored;
}

void TrackWaveformThumbnailItemTest::mixesThreeBandColorsWithoutChangingGeometry()
{
    TestableTrackWaveformThumbnailItem item;
    item.setWidth(128.0);
    item.setHeight(10.0);
    item.setPeaks(fullEnvelope(64U));
    item.setBass(fullBand(255U));
    item.setMid(fullBand(0U));
    item.setHigh(fullBand(0U));
    item.setLowColor(QColor(QStringLiteral("#ff0000")));
    item.setMidColor(QColor(QStringLiteral("#00ff00")));
    item.setHighColor(QColor(QStringLiteral("#0000ff")));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    QSGGeometry* const geometry = geometryNode(node)->geometry();
    auto* const vertices = geometry->vertexDataAsColoredPoint2D();
    QCOMPARE(vertices[0].r, 255U);
    QCOMPARE(vertices[0].g, 0U);
    QCOMPARE(vertices[0].b, 0U);
    vertices[0].y = -123.0F;

    item.setLowColor(QColor(QStringLiteral("#8b3dff")));
    node = item.updatePaintNode(node, nullptr);
    QCOMPARE(geometryNode(node)->geometry(), geometry);
    QCOMPARE(geometry->vertexDataAsColoredPoint2D(), vertices);
    QCOMPARE(vertices[0].y, -123.0F);
    QCOMPARE(vertices[0].r, 0x8BU);
    QCOMPARE(vertices[0].g, 0x3DU);
    QCOMPARE(vertices[0].b, 0xFFU);
    delete node;
}

void TrackWaveformThumbnailItemTest::rebuildsOnlyForPeaksOrSize()
{
    // Catches missing geometry invalidation for either data or dimensions,
    // while ensuring repeated equivalent assignments do not rebuild.
    TestableTrackWaveformThumbnailItem item;
    item.setWidth(128.0);
    item.setHeight(10.0);
    const QByteArray initial = fullEnvelope(32U);
    item.setPeaks(initial);

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    auto* vertices = geometryNode(node)->geometry()->vertexDataAsColoredPoint2D();
    vertices[0].y = -123.0F;

    item.setPeaks(initial);
    node = item.updatePaintNode(node, nullptr);
    QCOMPARE(vertices[0].y, -123.0F);

    item.setPeaks(fullEnvelope(96U));
    node = item.updatePaintNode(node, nullptr);
    QVERIFY(vertices[0].y >= 0.0F);

    item.setWidth(256.0);
    node = item.updatePaintNode(node, nullptr);
    vertices = geometryNode(node)->geometry()->vertexDataAsColoredPoint2D();
    QCOMPARE(vertices[510].x, 256.0F);

    item.setHeight(20.0);
    node = item.updatePaintNode(node, nullptr);
    vertices = geometryNode(node)->geometry()->vertexDataAsColoredPoint2D();
    QCOMPARE(vertices[0].y, 20.0F * 32.0F / 255.0F);
    delete node;
}

void TrackWaveformThumbnailItemTest::clearsOldNodeForInvalidContentOrSize()
{
    // Catches stale geometry surviving after delegate reuse supplies empty or
    // malformed peaks, or after the item is collapsed to a boundary size.
    const QList<QByteArray> invalidPeaks{
        QByteArray{}, QByteArray(kExpectedThumbnailBytes - 1, '\1'),
        QByteArray(kExpectedThumbnailBytes + 1, '\1')};
    for (const QByteArray& invalid : invalidPeaks) {
        TestableTrackWaveformThumbnailItem item;
        item.setWidth(128.0);
        item.setHeight(10.0);
        item.setPeaks(fullEnvelope(128U));
        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        item.setPeaks(invalid);
        QCOMPARE(item.updatePaintNode(node, nullptr), nullptr);
    }

    TestableTrackWaveformThumbnailItem item;
    item.setPeaks(fullEnvelope(128U));
    item.setWidth(0.0);
    item.setHeight(10.0);
    QCOMPARE(item.updatePaintNode(nullptr, nullptr), nullptr);
    item.setWidth(128.0);
    item.setHeight(0.0);
    QCOMPARE(item.updatePaintNode(nullptr, nullptr), nullptr);
}

void TrackWaveformThumbnailItemTest::exposesNoPlaybackProgressApi()
{
    // Catches coupling the static list thumbnail to playback or seek state.
    const QMetaObject& meta = TrackWaveformThumbnailItem::staticMetaObject;
    const QList<QByteArray> forbidden{
        "position", "progress", "duration", "cursorPosition", "seekRequested"};
    for (const QByteArray& name : forbidden) {
        QCOMPARE(meta.indexOfProperty(name.constData()), -1);
        QCOMPARE(meta.indexOfSignal(name + QByteArrayLiteral("()")), -1);
    }
}

QTEST_MAIN(TrackWaveformThumbnailItemTest)
#include "track_waveform_thumbnail_item_test.moc"

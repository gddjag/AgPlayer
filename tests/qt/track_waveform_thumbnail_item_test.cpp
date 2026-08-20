#include "track_waveform_thumbnail_item.hpp"

#include <QMetaProperty>
#include <QSGFlatColorMaterial>
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
    void createsOneGeometryNodeFor128Bytes();
    void reusesGeometryWhenOnlyColorChanges();
    void rebuildsOnlyForPeaksOrSize();
    void clearsOldNodeForInvalidContentOrSize();
    void exposesNoPlaybackProgressApi();
};

namespace {

QByteArray fullPeaks(const unsigned char value)
{
    return QByteArray(TrackWaveformThumbnailItem::kPeakCount,
                      static_cast<char>(value));
}

QSGGeometryNode* geometryNode(QSGNode* node)
{
    return static_cast<QSGGeometryNode*>(node);
}

} // namespace

void TrackWaveformThumbnailItemTest::createsOneGeometryNodeFor128Bytes()
{
    // Catches accidental per-peak child nodes or a vertex count other than
    // two endpoints for each of the fixed 128 amplitude bytes.
    TestableTrackWaveformThumbnailItem item;
    item.setWidth(128.0);
    item.setHeight(10.0);
    item.setPeaks(fullPeaks(255U));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    QCOMPARE(node->childCount(), 0);
    QCOMPARE(geometryNode(node)->geometry()->vertexCount(), 256);
    QCOMPARE(geometryNode(node)->geometry()->drawingMode(),
             QSGGeometry::DrawLines);

    const auto* vertices =
        geometryNode(node)->geometry()->vertexDataAsPoint2D();
    QCOMPARE(vertices[0].x, 0.0F);
    QCOMPARE(vertices[0].y, 0.0F);
    QCOMPARE(vertices[1].y, 10.0F);
    QCOMPARE(vertices[254].x, 128.0F);
    delete node;
}

void TrackWaveformThumbnailItemTest::reusesGeometryWhenOnlyColorChanges()
{
    // Catches a color setter that reallocates or rewrites the vertex buffer.
    TestableTrackWaveformThumbnailItem item;
    item.setWidth(128.0);
    item.setHeight(10.0);
    item.setPeaks(fullPeaks(128U));
    item.setWaveformColor(QColor(QStringLiteral("#112233")));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    QSGGeometry* const geometry = geometryNode(node)->geometry();
    auto* const vertices = geometry->vertexDataAsPoint2D();
    vertices[0].y = -123.0F;

    item.setWaveformColor(QColor(QStringLiteral("#abcdef")));
    QSGNode* const recolored = item.updatePaintNode(node, nullptr);
    QCOMPARE(recolored, node);
    QCOMPARE(geometryNode(recolored)->geometry(), geometry);
    QCOMPARE(geometry->vertexDataAsPoint2D(), vertices);
    QCOMPARE(vertices[0].y, -123.0F);
    QCOMPARE(static_cast<QSGFlatColorMaterial*>(
                 geometryNode(recolored)->material())->color(),
             QColor(QStringLiteral("#abcdef")));
    delete recolored;
}

void TrackWaveformThumbnailItemTest::rebuildsOnlyForPeaksOrSize()
{
    // Catches missing geometry invalidation for either data or dimensions,
    // while ensuring repeated equivalent assignments do not rebuild.
    TestableTrackWaveformThumbnailItem item;
    item.setWidth(128.0);
    item.setHeight(10.0);
    const QByteArray initial = fullPeaks(64U);
    item.setPeaks(initial);

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    auto* vertices = geometryNode(node)->geometry()->vertexDataAsPoint2D();
    vertices[0].y = -123.0F;

    item.setPeaks(initial);
    node = item.updatePaintNode(node, nullptr);
    QCOMPARE(vertices[0].y, -123.0F);

    item.setPeaks(fullPeaks(192U));
    node = item.updatePaintNode(node, nullptr);
    QVERIFY(vertices[0].y >= 0.0F);

    item.setWidth(256.0);
    node = item.updatePaintNode(node, nullptr);
    QCOMPARE(vertices[254].x, 256.0F);

    item.setHeight(20.0);
    node = item.updatePaintNode(node, nullptr);
    QCOMPARE(vertices[0].y, 10.0F - 10.0F * 192.0F / 255.0F);
    delete node;
}

void TrackWaveformThumbnailItemTest::clearsOldNodeForInvalidContentOrSize()
{
    // Catches stale geometry surviving after delegate reuse supplies empty or
    // malformed peaks, or after the item is collapsed to a boundary size.
    const QList<QByteArray> invalidPeaks{
        QByteArray{}, QByteArray(127, '\1'), QByteArray(129, '\1')};
    for (const QByteArray& invalid : invalidPeaks) {
        TestableTrackWaveformThumbnailItem item;
        item.setWidth(128.0);
        item.setHeight(10.0);
        item.setPeaks(fullPeaks(255U));
        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        item.setPeaks(invalid);
        QCOMPARE(item.updatePaintNode(node, nullptr), nullptr);
    }

    TestableTrackWaveformThumbnailItem item;
    item.setPeaks(fullPeaks(255U));
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

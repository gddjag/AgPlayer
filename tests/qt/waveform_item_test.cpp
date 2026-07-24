#include "waveform_item.hpp"

#include <QHoverEvent>
#include <QMouseEvent>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <limits>

class TestableWaveformItem final : public WaveformItem {
public:
    using WaveformItem::updatePaintNode;

    void cancelPointerInteraction() { mouseUngrabEvent(); }
    void hoverAt(qreal x)
    {
        QHoverEvent event(
            QEvent::HoverMove, QPointF(x, 20), QPointF(x, 20), QPointF(0, 20));
        hoverMoveEvent(&event);
    }
};

class WaveformItemTest final : public QObject {
    Q_OBJECT

private slots:
    void mapsPointerToClampedTime();
    void buildsCenteredFiniteNormalizedLinePairs();
    void usesReferenceGradientAndPlayedOpacity();
    void reusesNodeAndUpdatesGeometryAfterResize();
    void clearsOldNodeForEmptyOrZeroSizedContent();
    void hoverUpdatesPreviewWithoutSeeking();
    void cancelDoesNotCommitAStaleSeek();
    void downsamplesPeaksToPixelBudget();
    void reusesGeometryWhenPositionChangesWithinBucket();
};

namespace {

QVariantList peaks(std::initializer_list<double> values)
{
    QVariantList result;
    for (const double value : values) {
        result.append(value);
    }
    return result;
}

const QSGGeometry::ColoredPoint2D* vertices(const QSGNode* node)
{
    const auto* geometryNode = static_cast<const QSGGeometryNode*>(node);
    return geometryNode->geometry()->vertexDataAsColoredPoint2D();
}

void compareColor(const QSGGeometry::ColoredPoint2D& vertex,
                  int red,
                  int green,
                  int blue,
                  int alpha)
{
    QCOMPARE(static_cast<int>(vertex.r), red);
    QCOMPARE(static_cast<int>(vertex.g), green);
    QCOMPARE(static_cast<int>(vertex.b), blue);
    QCOMPARE(static_cast<int>(vertex.a), alpha);
}

} // namespace

void WaveformItemTest::mapsPointerToClampedTime()
{
    WaveformItem item;
    item.setWidth(1000);
    item.setDuration(200000);

    QCOMPARE(item.timeForX(-10), 0);
    QCOMPARE(item.timeForX(250), 50000);
    QCOMPARE(item.timeForX(1200), 200000);

    item.setWidth(0);
    QCOMPARE(item.timeForX(100), 0);
    item.setWidth(1000);
    item.setDuration(0);
    QCOMPARE(item.timeForX(100), 0);
}

void WaveformItemTest::buildsCenteredFiniteNormalizedLinePairs()
{
    TestableWaveformItem item;
    item.setWidth(100);
    item.setHeight(40);
    item.setPeaks(peaks({-0.5,
                         2.0,
                         std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity()}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* geometryNode = static_cast<const QSGGeometryNode*>(node);
    QCOMPARE(geometryNode->geometry()->drawingMode(), QSGGeometry::DrawLines);
    QCOMPARE(geometryNode->geometry()->vertexCount(), 8);
    QVERIFY(geometryNode->material() != nullptr);
    QVERIFY(geometryNode->firstChild() == nullptr);

    const auto* data = vertices(node);
    QCOMPARE(data[0].y, 10.0F);
    QCOMPARE(data[1].y, 30.0F);
    QCOMPARE(data[2].y, 0.0F);
    QCOMPARE(data[3].y, 40.0F);
    QCOMPARE(data[4].y, 20.0F);
    QCOMPARE(data[5].y, 20.0F);
    QCOMPARE(data[6].y, 20.0F);
    QCOMPARE(data[7].y, 20.0F);
    for (int index = 0; index < geometryNode->geometry()->vertexCount(); ++index) {
        QVERIFY(std::isfinite(data[index].x));
        QVERIFY(std::isfinite(data[index].y));
    }
    delete node;
}

void WaveformItemTest::usesReferenceGradientAndPlayedOpacity()
{
    TestableWaveformItem item;
    item.setWidth(100);
    item.setHeight(20);
    item.setDuration(100);
    item.setPosition(50);
    item.setPeaks(peaks({1.0, 1.0, 1.0, 1.0, 1.0}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* data = vertices(node);
    compareColor(data[0], 0x00, 0xD4, 0xFF, 0xFF);
    compareColor(data[2], 0x16, 0x88, 0xFF, 0xFF);
    compareColor(data[4], 0x7B, 0x2F, 0xF7, 0xFF);
    compareColor(data[6], 0xE6, 0x2E, 0x9B, WaveformItem::unplayedAlpha());
    compareColor(data[8], 0xFF, 0x40, 0x57, WaveformItem::unplayedAlpha());
    delete node;
}

void WaveformItemTest::reusesNodeAndUpdatesGeometryAfterResize()
{
    TestableWaveformItem item;
    item.setWidth(100);
    item.setHeight(40);
    item.setPeaks(peaks({0.25, 0.75}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    QCOMPARE(vertices(node)[2].x, 100.0F);

    item.setWidth(240);
    QSGNode* resizedNode = item.updatePaintNode(node, nullptr);
    QCOMPARE(resizedNode, node);
    QCOMPARE(vertices(resizedNode)[2].x, 240.0F);

    item.setPosition(10);
    item.setDuration(20);
    QSGNode* recoloredNode = item.updatePaintNode(resizedNode, nullptr);
    QCOMPARE(recoloredNode, node);
    delete recoloredNode;
}

void WaveformItemTest::clearsOldNodeForEmptyOrZeroSizedContent()
{
    TestableWaveformItem item;
    item.setWidth(100);
    item.setHeight(40);
    item.setPeaks(peaks({0.5}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    item.setPeaks({});
    QCOMPARE(item.updatePaintNode(node, nullptr), nullptr);

    item.setPeaks(peaks({0.5}));
    item.setWidth(0);
    QCOMPARE(item.updatePaintNode(nullptr, nullptr), nullptr);
    item.setWidth(100);
    item.setHeight(0);
    QCOMPARE(item.updatePaintNode(nullptr, nullptr), nullptr);
}

void WaveformItemTest::cancelDoesNotCommitAStaleSeek()
{
    TestableWaveformItem item;
    item.setWidth(100);
    item.setHeight(40);
    item.setDuration(1000);
    QSignalSpy spy(&item, &WaveformItem::seekRequested);

    QMouseEvent press(QEvent::MouseButtonPress,
                      QPointF(20, 20),
                      QPointF(20, 20),
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(&item, &press);
    item.cancelPointerInteraction();
    QMouseEvent release(QEvent::MouseButtonRelease,
                        QPointF(80, 20),
                        QPointF(80, 20),
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    QCoreApplication::sendEvent(&item, &release);

    QCOMPARE(spy.count(), 0);
}

void WaveformItemTest::hoverUpdatesPreviewWithoutSeeking()
{
    TestableWaveformItem item;
    item.setWidth(800);
    item.setHeight(40);
    item.setDuration(100000);
    QSignalSpy spy(&item, &WaveformItem::seekRequested);

    item.hoverAt(400);

    QCOMPARE(item.hoverPosition(), 50000);
    QCOMPARE(spy.count(), 0);
}

void WaveformItemTest::downsamplesPeaksToPixelBudget()
{
    TestableWaveformItem item;
    item.setWidth(100);
    item.setHeight(40);

    QVariantList manyPeaks;
    manyPeaks.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        manyPeaks.append((i % 2) * 1.0);
    }
    item.setPeaks(manyPeaks);

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* geometryNode = static_cast<const QSGGeometryNode*>(node);
    const int vertexCount = geometryNode->geometry()->vertexCount();
    QVERIFY(vertexCount % 2 == 0);

    const int renderedPeaks = vertexCount / 2;
    const int expectedMaxPoints = static_cast<int>(item.width() * 2.0);
    QVERIFY2(renderedPeaks <= expectedMaxPoints,
             qPrintable(QStringLiteral("rendered %1 peaks, budget was %2")
                            .arg(renderedPeaks)
                            .arg(expectedMaxPoints)));
    delete node;
}

void WaveformItemTest::reusesGeometryWhenPositionChangesWithinBucket()
{
    TestableWaveformItem item;
    item.setWidth(100);
    item.setHeight(40);
    item.setDuration(100);
    item.setPosition(0);
    item.setPeaks(peaks({1.0, 1.0, 1.0, 1.0, 1.0}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* geometry = static_cast<const QSGGeometryNode*>(node)->geometry();
    const void* firstVertexData = geometry->vertexDataAsColoredPoint2D();
    const int firstVertexCount = geometry->vertexCount();

    // Same bucket: playedCount stays at 1, so geometry and colors are untouched.
    item.setPosition(10);
    QSGNode* sameNode = item.updatePaintNode(node, nullptr);
    QCOMPARE(sameNode, node);
    const auto* sameGeometry = static_cast<const QSGGeometryNode*>(sameNode)->geometry();
    QCOMPARE(sameGeometry->vertexCount(), firstVertexCount);
    QCOMPARE(sameGeometry->vertexDataAsColoredPoint2D(), firstVertexData);

    // Cross bucket: playedCount changes to 2. Only colors should update.
    item.setPosition(30);
    QSGNode* colorNode = item.updatePaintNode(node, nullptr);
    QCOMPARE(colorNode, node);
    const auto* colorGeometry = static_cast<const QSGGeometryNode*>(colorNode)->geometry();
    QCOMPARE(colorGeometry->vertexCount(), firstVertexCount);
    QCOMPARE(colorGeometry->vertexDataAsColoredPoint2D(), firstVertexData);
    QCOMPARE(static_cast<int>(vertices(colorNode)[2].a), 255);
    QCOMPARE(static_cast<int>(vertices(colorNode)[3].a), 255);

    // Color-only change also reuses geometry.
    item.setWaveformColor(QColor(255, 0, 0));
    QSGNode* coloredNode = item.updatePaintNode(node, nullptr);
    QCOMPARE(coloredNode, node);
    const auto* coloredGeometry = static_cast<const QSGGeometryNode*>(coloredNode)->geometry();
    QCOMPARE(coloredGeometry->vertexCount(), firstVertexCount);
    QCOMPARE(coloredGeometry->vertexDataAsColoredPoint2D(), firstVertexData);
    compareColor(vertices(coloredNode)[0], 255, 0, 0, 255);

    delete node;
}

QTEST_MAIN(WaveformItemTest)
#include "waveform_item_test.moc"

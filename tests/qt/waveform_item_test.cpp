#include "waveform_item.hpp"

#include <QHoverEvent>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QQuickWindow>
#include <QScreen>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSignalSpy>
#include <QTest>
#include <QVariantMap>

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
    void downsamplingKeepsImpulseOnItsTimelinePixel();
    void reusesGeometryWhenPositionChangesWithinBucket();
    void subPixelWidthDoesNotCrash();
    void setLayersPopulatesLayerProperties();
    void rendersMultiBandLayers();
    void fallsBackToFrequencyLayerWhenMixMissing();
    void densityAndLineWidthAffectRenderedGeometry();
    void visualModesUseConfiguredProgressAndBaseColors();
    void spectrumUsesBottomBaselineAndCenterEnvelope();
    void spectrumUpsamplesSparseInputToDenseBars();
    void spectrumContractUsesFixedBarsWithPeakCaps();
    void spectrumPeakCapsNeverFallInsideTheirBars();
    void spectrumRecolorsBarsAndPeakCapsAfterSeek();
    void zeroPositionLeavesCompleteWaveformUnplayed();
    void silentTailRemainsVisibleAtTheTimelineEnd();
    void cursorAndSeekShareRenderWidth();
    void resizeUpdatesCursorWithoutReplacingPeakSnapshot();
    void preservesTimelineMetadataInPeakSnapshot();
    void windowScaleAndScreenKeepCursorAligned();
    void resizeLoopStaysWithinInteractiveBudget();
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

QVariantMap makeLayers(const QVariantList& mix = {},
                       const QVariantList& bass = {},
                       const QVariantList& mid = {},
                       const QVariantList& high = {})
{
    QVariantMap map;
    map[QStringLiteral("mix")] = mix;
    map[QStringLiteral("bass")] = bass;
    map[QStringLiteral("mid")] = mid;
    map[QStringLiteral("high")] = high;
    return map;
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

void WaveformItemTest::densityAndLineWidthAffectRenderedGeometry()
{
    TestableWaveformItem item;
    item.setWidth(80);
    item.setHeight(40);
    QVariantList values;
    for (int i = 0; i < 100; ++i) {
        values.append(0.5);
    }
    item.setPeaks(values);

    item.setDensity(0);
    item.setLineWidth(3.0);
    QSGNode* sparseNode = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(sparseNode != nullptr);
    auto* sparseGeometry = static_cast<QSGGeometryNode*>(sparseNode)->geometry();
    // The minimum density is 0.15 so a 4 px bar plus a 2 px gap can be
    // represented by the spectrum renderer without being clamped upward.
    QCOMPARE(sparseGeometry->vertexCount(), 36);
    QCOMPARE(sparseGeometry->lineWidth(), 1.0F);

    item.setDensity(2);
    QSGNode* fineNode = item.updatePaintNode(sparseNode, nullptr);
    QCOMPARE(fineNode, sparseNode);
    auto* fineGeometry = static_cast<QSGGeometryNode*>(fineNode)->geometry();
    QCOMPARE(fineGeometry->vertexCount(), 480);

    item.setDensity(99);
    item.setLineWidth(99.0);
    QCOMPARE(item.density(), 5.0);
    QCOMPARE(item.lineWidth(), 8.0);
    delete fineNode;
}

void WaveformItemTest::visualModesUseConfiguredProgressAndBaseColors()
{
    TestableWaveformItem item;
    item.setWidth(100);
    item.setHeight(40);
    item.setDuration(100);
    item.setPosition(50);
    item.setDensity(2.0);
    item.setLineWidth(1.0);
    item.setAmplitudeScale(0.5);
    item.setVisualMode(0);
    item.setBaseColor(QColor(QStringLiteral("#ffffff")));
    item.setProgressColor(QColor(QStringLiteral("#ffdd00")));
    item.setPeaks(peaks({1.0, 1.0, 1.0, 1.0}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* data = vertices(node);
    compareColor(data[0], 0xFF, 0xDD, 0x00, 0xFF);
    compareColor(data[4], 0xFF, 0xFF, 0xFF, 0xFF);
    QCOMPARE(data[0].y, 10.0F);
    QCOMPARE(data[1].y, 30.0F);

    item.setVisualMode(1);
    item.setGradientStartColor(QColor(QStringLiteral("#00d4ff")));
    item.setGradientMiddleColor(QColor(QStringLiteral("#7b2ff7")));
    item.setGradientEndColor(QColor(QStringLiteral("#e62e9b")));
    item.setRgbProgress(true);
    node = item.updatePaintNode(node, nullptr);
    compareColor(vertices(node)[0], 0x00, 0xD4, 0xFF, 0xFF);
    compareColor(vertices(node)[4], 0xFF, 0xFF, 0xFF, 0xFF);
    delete node;
}

void WaveformItemTest::spectrumUsesBottomBaselineAndCenterEnvelope()
{
    TestableWaveformItem item;
    item.setWidth(100);
    item.setHeight(40);
    item.setVisualMode(2);
    item.setAmplitudeScale(1.0);
    item.setDensity(1.0 / 6.0);
    item.setLineWidth(4.0);
    QCOMPARE(item.lineWidth(), 4.0);
    item.setPeaks(peaks({1.0, 1.0, 1.0, 1.0, 1.0}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* data = vertices(node);
    QCOMPARE(data[1].y, 40.0F);
    QCOMPARE(data[9].y, 40.0F);
    QVERIFY(data[4].y < data[0].y);
    QVERIFY(data[0].y <= 16.0F);
    delete node;
}

void WaveformItemTest::spectrumUpsamplesSparseInputToDenseBars()
{
    TestableWaveformItem item;
    item.setWidth(120);
    item.setHeight(48);
    item.setVisualMode(2);
    item.setAmplitudeScale(1.0);
    item.setLineWidth(3.0);
    QCOMPARE(item.lineWidth(), 3.0);
    item.setPeaks(peaks({0.2, 0.4, 0.7, 1.0, 1.0, 0.7, 0.4, 0.2}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* geometry = static_cast<QSGGeometryNode*>(node)->geometry();
    // Width 120 packs 18 responsive bars at 5 px + 2 px gap. Each bar has
    // five adjacent strokes plus a peak-hold cap: 12 vertices per bar.
    QCOMPARE(geometry->vertexCount(), 18 * 12);
    delete node;
}

void WaveformItemTest::spectrumContractUsesFixedBarsWithPeakCaps()
{
    TestableWaveformItem item;
    QCOMPARE(item.spectrumBarCount(), 128);
    QCOMPARE(item.spectrumBarWidth(), 5.0);
    QCOMPARE(item.spectrumBarGap(), 2.0);
    QCOMPARE(item.spectrumMaxHeight(), 96.0);
    QCOMPARE(item.spectrumAttackSeconds(), 0.02);
    QCOMPARE(item.spectrumDecaySeconds(), 0.10);
    QCOMPARE(item.spectrumPeakFallSeconds(), 0.75);

    item.setWidth(600);
    item.setHeight(96);
    item.setVisualMode(2);
    item.setPeaks(peaks({1.0, 1.0, 1.0, 1.0}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* data = vertices(node);
    const auto* geometryNode = static_cast<const QSGGeometryNode*>(node);
    // The live spectrum must use the complete waveform canvas.  The fixed
    // source is resampled into 86 responsive bars at this width.
    QVERIFY(data[0].x >= 0.0F && data[0].x <= 2.0F);
    const int lastBarVertex = 86 * 2 * 4 + (86 - 1) * 2;
    QVERIFY(data[lastBarVertex].x >= 598.0F && data[lastBarVertex].x <= 600.0F);
    // The faster, taller spectrum should lift the center bar above the former
    // 72 px visual cap while remaining bottom-aligned.
    const int centerBarVertex = 43 * 2;
    QVERIFY(data[centerBarVertex].y <= 23.0F);
    QCOMPARE(data[centerBarVertex + 1].y, 96.0F);
    // A one-pixel horizontal cap must remain visible above each bottom-aligned
    // bar so the live spectrum has the square peak markers from the reference.
    const int capVertex = 86 * 2 * 5 + 43 * 2;
    QCOMPARE(geometryNode->geometry()->vertexCount(), 86 * 12);
    QCOMPARE(data[capVertex].y, data[centerBarVertex].y);
    QCOMPARE(data[capVertex + 1].y, data[centerBarVertex].y);
    QVERIFY(data[capVertex].x < data[capVertex + 1].x);
    delete node;
}

void WaveformItemTest::spectrumPeakCapsNeverFallInsideTheirBars()
{
    TestableWaveformItem item;
    item.setWidth(70);
    item.setHeight(48);
    item.setVisualMode(2);
    item.setPeaks(peaks({0.10, 1.0, 0.10, 1.0}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* data = vertices(node);
    constexpr int barCount = 10;
    constexpr int strokeCopies = 5;
    constexpr int lowBar = 0;
    const int barTopVertex = lowBar * 2;
    const int capVertex = barCount * 2 * strokeCopies + lowBar * 2;

    QVERIFY2(data[capVertex].y <= data[barTopVertex].y + 0.01F,
             "peak-hold cap must stay at or above the rendered bar top");
    compareColor(data[capVertex], data[barTopVertex].r,
                 data[barTopVertex].g, data[barTopVertex].b,
                 data[barTopVertex].a);
    delete node;
}

void WaveformItemTest::spectrumRecolorsBarsAndPeakCapsAfterSeek()
{
    TestableWaveformItem item;
    item.setWidth(70);
    item.setHeight(48);
    item.setDuration(100);
    item.setVisualMode(2);
    item.setBaseColor(QColor(QStringLiteral("#202020")));
    item.setGradientStartColor(QColor(QStringLiteral("#002fa7")));
    item.setGradientMiddleColor(QColor(QStringLiteral("#002fa7")));
    item.setGradientEndColor(QColor(QStringLiteral("#002fa7")));
    item.setPeaks(peaks({1.0, 1.0, 1.0, 1.0}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    compareColor(vertices(node)[0], 0x20, 0x20, 0x20, 0xFF);

    item.setPosition(50);
    node = item.updatePaintNode(node, nullptr);
    const auto* data = vertices(node);
    // The first spectrum bar and its peak-hold cap must change together on seek.
    compareColor(data[0], 0x00, 0x2F, 0xA7, 0xFF);
    const int barCount = 10;
    const int capOffset = barCount * 2 * 5;
    compareColor(data[capOffset], 0x00, 0x2F, 0xA7, 0xFF);
    delete node;
}

void WaveformItemTest::zeroPositionLeavesCompleteWaveformUnplayed()
{
    TestableWaveformItem item;
    item.setWidth(100);
    item.setHeight(40);
    item.setDuration(100);
    item.setPosition(0);
    item.setVisualMode(0);
    item.setBaseColor(QColor(QStringLiteral("#ffffff")));
    item.setProgressColor(QColor(QStringLiteral("#ffdd00")));
    item.setPeaks(peaks({1.0, 1.0}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    compareColor(vertices(node)[0], 0xFF, 0xFF, 0xFF, 0xFF);
    compareColor(vertices(node)[2], 0xFF, 0xFF, 0xFF, 0xFF);
    delete node;
}

void WaveformItemTest::silentTailRemainsVisibleAtTheTimelineEnd()
{
    TestableWaveformItem item;
    item.setWidth(100);
    item.setHeight(40);
    item.setDuration(100);
    item.setPosition(100);
    item.setVisualMode(0);
    item.setBaseColor(QColor(QStringLiteral("#9098a6")));
    item.setProgressColor(QColor(QStringLiteral("#e4007f")));
    item.setPeaks(peaks({1.0, 0.0, 0.0}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* points = vertices(node);
    QVERIFY(points[4].x >= 99.0F);
    QVERIFY2(std::abs(points[5].y - points[4].y) >= 1.0F,
             "silent timeline buckets must render a visible baseline");
    compareColor(points[4], 0xE4, 0x00, 0x7F, 0xFF);
    delete node;
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
    QCOMPARE(geometryNode->geometry()->vertexCount(), 16);
    QVERIFY(geometryNode->material() != nullptr);
    QVERIFY(geometryNode->firstChild() == nullptr);

    const auto* data = vertices(node);
    QCOMPARE(data[0].y, 10.0F);
    QCOMPARE(data[1].y, 30.0F);
    QCOMPARE(data[2].y, 0.0F);
    QCOMPARE(data[3].y, 40.0F);
    QCOMPARE(data[4].y, 19.5F);
    QCOMPARE(data[5].y, 20.5F);
    QCOMPARE(data[6].y, 19.5F);
    QCOMPARE(data[7].y, 20.5F);
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
    QCOMPARE(vertices(node)[6].x, 100.0F);

    item.setWidth(240);
    QSGNode* resizedNode = item.updatePaintNode(node, nullptr);
    QCOMPARE(resizedNode, node);
    QCOMPARE(vertices(resizedNode)[6].x, 240.0F);

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

    item.hoverAt(120);
    QCOMPARE(item.hoverPosition(), 15000);
    item.hoverAt(680);
    QCOMPARE(item.hoverPosition(), 85000);
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

    const int renderedPeaks = vertexCount
                              / (2 * static_cast<int>(std::ceil(item.lineWidth())));
    const int expectedMaxPoints = static_cast<int>(
        std::ceil(item.width() * item.density() / 2.0));
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
    item.setPosition(1);
    item.setPeaks(peaks({1.0, 1.0, 1.0, 1.0, 1.0}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* geometry = static_cast<const QSGGeometryNode*>(node)->geometry();
    const void* firstVertexData = geometry->vertexDataAsColoredPoint2D();
    const int firstVertexCount = geometry->vertexCount();

    // Same bucket: playedCount stays at 1, so geometry is untouched and colors are not updated.
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

void WaveformItemTest::subPixelWidthDoesNotCrash()
{
    TestableWaveformItem item;
    item.setWidth(0.1);
    item.setHeight(40);
    item.setDuration(100);
    item.setPosition(0);
    item.setPeaks(peaks({1.0, 1.0, 1.0, 1.0, 1.0}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* geometryNode = static_cast<const QSGGeometryNode*>(node);
    QCOMPARE(geometryNode->geometry()->vertexCount(), 4);
    delete node;
}

void WaveformItemTest::setLayersPopulatesLayerProperties()
{
    TestableWaveformItem item;
    QSignalSpy layersSpy(&item, &WaveformItem::layersChanged);
    QSignalSpy peaksSpy(&item, &WaveformItem::peaksChanged);

    const QVariantMap input = makeLayers(peaks({0.5, 1.0}), peaks({0.2, 0.8}));
    item.setLayers(input);

    QCOMPARE(item.layers(), input);
    QCOMPARE(item.peaks().size(), 0);
    QCOMPARE(layersSpy.count(), 1);
    QCOMPARE(peaksSpy.count(), 1);
}

void WaveformItemTest::rendersMultiBandLayers()
{
    TestableWaveformItem item;
    item.setWidth(100);
    item.setHeight(40);
    item.setDuration(100);
    item.setPosition(100);

    const QVariantMap input = makeLayers(
        peaks({1.0, 1.0}),
        peaks({0.5, 0.5}),
        peaks({0.25, 0.25}),
        peaks({0.125, 0.125}));
    item.setLayers(input);

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* geometryNode = static_cast<const QSGGeometryNode*>(node);
    QCOMPARE(geometryNode->geometry()->vertexCount(), 32);

    const auto* data = vertices(node);
    // Mix layer uses the reference gradient (all played at end position).
    compareColor(data[0], 0x00, 0xD4, 0xFF, 0xFF);
    compareColor(data[2], 0xFF, 0x40, 0x57, 0xFF);
    // Bass layer: kBassColor.
    compareColor(data[8], 170, 55, 55, 158);
    compareColor(data[10], 170, 55, 55, 158);
    // Mid layer: kMidColor.
    compareColor(data[16], 55, 140, 55, 148);
    compareColor(data[18], 55, 140, 55, 148);
    // High layer: kHighColor.
    compareColor(data[24], 55, 90, 145, 133);
    compareColor(data[26], 55, 90, 145, 133);

    delete node;
}

void WaveformItemTest::fallsBackToFrequencyLayerWhenMixMissing()
{
    TestableWaveformItem item;
    item.setWidth(100);
    item.setHeight(40);
    item.setDuration(100);
    item.setPosition(0);

    const QVariantMap input = makeLayers({}, peaks({1.0, 1.0}), peaks({0.5, 0.5}), {});
    item.setLayers(input);

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* geometryNode = static_cast<const QSGGeometryNode*>(node);
    // Two layers, two peaks, two vertices and two 1 px copies per peak.
    QCOMPARE(geometryNode->geometry()->vertexCount(), 16);

    const auto* data = vertices(node);
    QCOMPARE(data[0].x, 0.0F);
    QCOMPARE(data[6].x, 100.0F);

    delete node;
}

void WaveformItemTest::cursorAndSeekShareRenderWidth()
{
    TestableWaveformItem item;
    item.setWidth(1000.0);
    item.setHeight(80.0);
    item.setDuration(300000);
    item.setPosition(150000);
    item.setPeaks(peaks({0.2, 0.4, 0.8, 0.3}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    QCOMPARE(item.renderWidth(), 1000.0);
    QCOMPARE(item.waveformCursorX(), 500.0);
    QCOMPARE(item.timeForX(item.waveformCursorX()), qint64{150000});
    delete node;
}

void WaveformItemTest::downsamplingKeepsImpulseOnItsTimelinePixel()
{
    TestableWaveformItem item;
    item.setWidth(600);
    item.setHeight(40);
    item.setDensity(2.0); // exactly 600 rendered points

    QVariantList source;
    source.reserve(2000);
    for (int index = 0; index < 2000; ++index) {
        source.append(index == 1000 ? 1.0 : 0.0);
    }
    item.setPeaks(source);

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* data = vertices(node);
    int tallestIndex = -1;
    float tallestHeight = -1.0F;
    for (int index = 0; index < 600; ++index) {
        const float height = data[index * 2 + 1].y - data[index * 2].y;
        if (height > tallestHeight) {
            tallestHeight = height;
            tallestIndex = index;
        }
    }
    const double impulseX = static_cast<double>(tallestIndex) / 599.0 * 600.0;
    QVERIFY2(std::abs(impulseX - 300.0) <= 1.0,
             qPrintable(QStringLiteral("mid-track impulse rendered at x=%1")
                            .arg(impulseX)));
    delete node;
}

void WaveformItemTest::resizeUpdatesCursorWithoutReplacingPeakSnapshot()
{
    TestableWaveformItem item;
    item.setWidth(500.0);
    item.setHeight(80.0);
    item.setDuration(300000);
    item.setPosition(60000);
    item.setPeaks(peaks({0.2, 0.4, 0.8, 0.3}));
    const QVariantList originalPeaks = item.peaks();

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    auto* geometry = static_cast<QSGGeometryNode*>(node)->geometry();
    const auto* vertexStorage = geometry->vertexDataAsColoredPoint2D();
    QCOMPARE(item.waveformCursorX(), 100.0);
    item.setWidth(3840.0);
    node = item.updatePaintNode(node, nullptr);

    QCOMPARE(item.renderWidth(), 3840.0);
    QCOMPARE(item.waveformCursorX(), 768.0);
    QCOMPARE(item.peaks(), originalPeaks);
    QCOMPARE(static_cast<QSGGeometryNode*>(node)->geometry(), geometry);
    QCOMPARE(static_cast<QSGGeometryNode*>(node)->geometry()
                 ->vertexDataAsColoredPoint2D(), vertexStorage);
    delete node;
}

void WaveformItemTest::preservesTimelineMetadataInPeakSnapshot()
{
    TestableWaveformItem item;
    QVariantMap layers = makeLayers(peaks({0.2, 0.4, 0.8, 0.3}));
    layers[QStringLiteral("_sampleRate")] = 44'100;
    layers[QStringLiteral("_totalSamples")] = 13'229'956;
    layers[QStringLiteral("_peakCount")] = 4;

    item.setLayers(layers);

    QCOMPARE(item.sampleRate(), qint64{44'100});
    QCOMPARE(item.totalSamples(), qint64{13'229'956});
    QCOMPARE(item.peakCount(), qsizetype{4});
}

void WaveformItemTest::windowScaleAndScreenKeepCursorAligned()
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    QVERIFY(!screens.isEmpty());

    for (QScreen* screen : screens) {
        QQuickWindow window;
        window.setScreen(screen);
        window.setGeometry(screen->availableGeometry().topLeft().x(),
                           screen->availableGeometry().topLeft().y(),
                           1000, 500);

        TestableWaveformItem item;
        item.setParentItem(window.contentItem());
        item.setWidth(1000.0);
        item.setHeight(80.0);
        item.setDuration(300000);
        item.setPosition(150000);
        item.setPeaks(peaks({0.2, 0.4, 0.8, 0.3}));

        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        QCOMPARE(item.waveformCursorX(), 500.0);
        const double physicalCursor = item.waveformCursorX()
                                      * window.devicePixelRatio();
        const double expectedPhysical = window.width()
                                        * window.devicePixelRatio() * 0.5;
        QVERIFY(std::abs(physicalCursor - expectedPhysical) <= 0.5);
        delete node;
    }
}

void WaveformItemTest::resizeLoopStaysWithinInteractiveBudget()
{
    TestableWaveformItem item;
    item.setWidth(500.0);
    item.setHeight(80.0);
    item.setDuration(300000);
    QVariantList values;
    values.reserve(2000);
    for (int index = 0; index < 2000; ++index) {
        values.append(static_cast<double>(index % 100) / 100.0);
    }
    item.setPeaks(values);

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QElapsedTimer timer;
    timer.start();
    for (int index = 0; index < 300; ++index) {
        item.setWidth(500.0 + (index % 1200));
        node = item.updatePaintNode(node, nullptr);
    }
    const qint64 elapsedMs = timer.elapsed();
    QVERIFY2(elapsedMs < 1000,
             qPrintable(QStringLiteral("300 resize remaps took %1 ms")
                            .arg(elapsedMs)));
    QCOMPARE(item.peaks(), values);
    delete node;
}

QTEST_MAIN(WaveformItemTest)
#include "waveform_item_test.moc"

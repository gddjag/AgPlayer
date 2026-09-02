#include "waveform_item.hpp"

#include <QHoverEvent>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QQuickWindow>
#include <QScreen>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSignalSpy>
#include <QTest>
#include <QVariantMap>

#include <array>
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
    void spectralModeUsesAmplitudeGeometryAndCentroidPalette();
    void spectralPaletteChangeDoesNotReplaceGeometryNode();
    void spectralProgressUpdatesAlphaWithoutReplacingGeometry();
    void reusesNodeAndUpdatesGeometryAfterResize();
    void clearsOldNodeForEmptyOrZeroSizedContent();
    void hoverUpdatesPreviewWithoutSeeking();
    void cancelDoesNotCommitAStaleSeek();
    void downsamplesPeaksToPixelBudget();
    void sparseWaveformFillsWideDisplayBudget();
    void downsamplingKeepsImpulseOnItsTimelinePixel();
    void reusesGeometryWhenPositionChangesWithinBucket();
    void subPixelWidthDoesNotCrash();
    void setLayersPopulatesLayerProperties();
    void nonFrequencyModesIgnoreFrequencyLayers();
    void densityAndLineWidthAffectRenderedGeometry();
    void waveformStrokesStayInsideContainerEdges();
    void onePixelWaveformLeavesTheCanvasEdgeClear();
    void visualModesUseConfiguredProgressAndBaseColors();
    void frequencyModeUpdatesProgressOpacityWithoutRebuildingNode();
    void spectrumUsesBottomBaselineAndCenterEnvelope();
    void spectrumUpsamplesSparseInputToDenseBars();
    void spectrumContractUsesFixedBarsWithPeakCaps();
    void spectrumPeakCapsNeverFallInsideTheirBars();
    void spectrumColorIsIndependentOfPlaybackProgress();
    void zeroPositionLeavesCompleteWaveformUnplayed();
    void silentTailRemainsVisibleAtTheTimelineEnd();
    void cursorAndSeekShareRenderWidth();
    void resizeUpdatesCursorWithoutReplacingPeakSnapshot();
    void preservesTimelineMetadataInPeakSnapshot();
    void windowScaleAndScreenKeepCursorAligned();
    void resizeLoopStaysWithinInteractiveBudget();
    void zoomKeepsAnchorStableAndUsesVisibleRange();
    void zoomClampsToEightTimesAndResizeDoesNotResetViewport();
    void visibleRangeCanBePannedWithoutChangingItsSpan();
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
                       const QVariantList& high = {},
                       const QVariantList& spectralIndex = {})
{
    QVariantMap map;
    map[QStringLiteral("mix")] = mix;
    map[QStringLiteral("bass")] = bass;
    map[QStringLiteral("mid")] = mid;
    map[QStringLiteral("high")] = high;
    map[QStringLiteral("spectralIndex")] = spectralIndex;
    return map;
}

const QSGGeometry::ColoredPoint2D* vertices(const QSGNode* node)
{
    const auto* geometryNode = static_cast<const QSGGeometryNode*>(node);
    return geometryNode->geometry()->vertexDataAsColoredPoint2D();
}

int renderedPeakCount(const QSGNode* node, const WaveformItem& item,
                      int activeLayers = 1)
{
    const auto* geometryNode = static_cast<const QSGGeometryNode*>(node);
    const int strokeCopies = static_cast<int>(std::ceil(item.lineWidth()));
    return geometryNode->geometry()->vertexCount()
           / (2 * strokeCopies * activeLayers);
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

void WaveformItemTest::waveformStrokesStayInsideContainerEdges()
{
    TestableWaveformItem item;
    item.setWidth(80);
    item.setHeight(40);
    item.setDensity(0.2);
    item.setLineWidth(4.0);
    item.setPeaks(peaks({1.0, 1.0, 1.0, 1.0}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* data = vertices(node);
    const int peakCount = renderedPeakCount(node, item);
    const int strokeCopies = static_cast<int>(std::ceil(item.lineWidth()));

    float previousFirstX = -1.0F;
    for (int copy = 0; copy < strokeCopies; ++copy) {
        const int firstVertex = copy * peakCount * 2;
        const int lastVertex = firstVertex + (peakCount - 1) * 2;
        QVERIFY2(data[firstVertex].x > 0.0F,
                 "the first waveform stroke must not overlap the left border");
        QVERIFY2(data[lastVertex].x < item.width(),
                 "the last waveform stroke must not overlap the right border");
        QVERIFY2(data[firstVertex].x > previousFirstX,
                 "stroke copies must not stack into a bright edge line");
        previousFirstX = data[firstVertex].x;
    }

    delete node;
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
    compareColor(data[100], 0xFF, 0xFF, 0xFF, 0xFF);
    QCOMPARE(data[0].y, 10.0F);
    QCOMPARE(data[1].y, 30.0F);

    item.setVisualMode(1);
    item.setGradientStartColor(QColor(QStringLiteral("#00d4ff")));
    item.setGradientMiddleColor(QColor(QStringLiteral("#7b2ff7")));
    item.setGradientEndColor(QColor(QStringLiteral("#e62e9b")));
    item.setRgbProgress(true);
    node = item.updatePaintNode(node, nullptr);
    compareColor(vertices(node)[0], 0x00, 0xD4, 0xFF, 0xFF);
    compareColor(vertices(node)[100], 0xFF, 0xFF, 0xFF, 0xFF);
    delete node;
}

void WaveformItemTest::spectralModeUsesAmplitudeGeometryAndCentroidPalette()
{
    TestableWaveformItem plain;
    plain.setWidth(100);
    plain.setHeight(40);
    plain.setDuration(100);
    plain.setPosition(50);
    plain.setDensity(2.0);
    plain.setLineWidth(1.0);
    plain.setVisualMode(0);
    plain.setLayers(makeLayers(peaks({0.25, 0.5, 0.75, 1.0})));
    QSGNode* plainNode = plain.updatePaintNode(nullptr, nullptr);

    TestableWaveformItem spectral;
    spectral.setWidth(100);
    spectral.setHeight(40);
    spectral.setDuration(100);
    spectral.setPosition(50);
    spectral.setDensity(2.0);
    spectral.setLineWidth(1.0);
    spectral.setVisualMode(3);
    spectral.setSpectralPalette(
        {QStringLiteral("#000000"), QStringLiteral("#ffffff")});
    spectral.setSpectralUnplayedOpacity(0.88);
    spectral.setLayers(makeLayers(
        peaks({0.25, 0.5, 0.75, 1.0}), {}, {}, {},
        peaks({0, 85, 170, 255})));
    QSGNode* spectralNode = spectral.updatePaintNode(nullptr, nullptr);

    QVERIFY(plainNode != nullptr);
    QVERIFY(spectralNode != nullptr);
    const auto* plainVertices = vertices(plainNode);
    const auto* spectralVertices = vertices(spectralNode);
    const int count = static_cast<QSGGeometryNode*>(plainNode)
                          ->geometry()->vertexCount();
    QCOMPARE(static_cast<QSGGeometryNode*>(spectralNode)
                 ->geometry()->vertexCount(), count);
    for (int index = 0; index < count; ++index) {
        QCOMPARE(spectralVertices[index].x, plainVertices[index].x);
        QCOMPARE(spectralVertices[index].y, plainVertices[index].y);
    }
    compareColor(spectralVertices[0], 0, 0, 0, 255);
    const int last = count - 2;
    compareColor(spectralVertices[last], 255, 255, 255, 224);
    delete plainNode;
    delete spectralNode;
}

void WaveformItemTest::spectralPaletteChangeDoesNotReplaceGeometryNode()
{
    TestableWaveformItem item;
    item.setWidth(120);
    item.setHeight(48);
    item.setDuration(100);
    item.setVisualMode(3);
    item.setLayers(makeLayers(peaks({1.0, 0.5, 0.75, 0.25}), {}, {}, {},
                              peaks({0, 85, 170, 255})));
    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    auto* geometry = static_cast<QSGGeometryNode*>(node)->geometry();
    const void* vertexStorage = geometry->vertexData();

    item.setSpectralPalette(
        {QStringLiteral("#ff0000"), QStringLiteral("#00ff00")});
    QSGNode* updated = item.updatePaintNode(node, nullptr);
    QCOMPARE(updated, node);
    QCOMPARE(static_cast<QSGGeometryNode*>(updated)->geometry(), geometry);
    QCOMPARE(geometry->vertexData(), vertexStorage);
    compareColor(vertices(updated)[0], 255, 0, 0, 224);
    delete updated;
}

void WaveformItemTest::spectralProgressUpdatesAlphaWithoutReplacingGeometry()
{
    TestableWaveformItem item;
    item.setWidth(1000);
    item.setHeight(48);
    item.setDuration(100000);
    item.setVisualMode(3);
    item.setSpectralUnplayedOpacity(0.60);
    item.setSpectralPalette({QStringLiteral("#ff0000"),
                             QStringLiteral("#00ff00")});
    item.setLayers(makeLayers(peaks({1.0, 0.8, 0.6, 0.4}), {}, {}, {},
                              peaks({0, 85, 170, 255})));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    auto* geometry = static_cast<QSGGeometryNode*>(node)->geometry();
    const void* vertexStorage = geometry->vertexData();

    const QList<qreal> progressValues{0.0, 0.001, 0.5, 0.999, 1.0};
    for (const qreal progress : progressValues) {
        item.setPosition(progress * item.duration());
        node = item.updatePaintNode(node, nullptr);
        QCOMPARE(static_cast<QSGGeometryNode*>(node)->geometry(), geometry);
        QCOMPARE(geometry->vertexData(), vertexStorage);
    }

    item.setPosition(0);
    node = item.updatePaintNode(node, nullptr);
    QCOMPARE(static_cast<int>(vertices(node)[0].a), 153);
    item.setPosition(item.duration());
    node = item.updatePaintNode(node, nullptr);
    QCOMPARE(static_cast<int>(vertices(node)[0].a), 255);
    delete node;
}

void WaveformItemTest::onePixelWaveformLeavesTheCanvasEdgeClear()
{
    TestableWaveformItem item;
    item.setWidth(80);
    item.setHeight(40);
    item.setDensity(2.0);
    item.setLineWidth(1.0);
    item.setPeaks(peaks({1.0, 0.8, 0.7, 0.6}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* data = vertices(node);
    QVERIFY2(data[0].x >= 1.0F,
             "the first waveform column must not rasterize as a canvas border");
    QCOMPARE(data[0].y + data[1].y, item.height());
    delete node;
}

void WaveformItemTest::frequencyModeUpdatesProgressOpacityWithoutRebuildingNode()
{
    TestableWaveformItem item;
    item.setWidth(4);
    item.setHeight(40);
    item.setDuration(100);
    item.setDensity(2.0);
    item.setLineWidth(1.0);
    item.setVisualMode(3);
    item.setSpectralUnplayedOpacity(0.60);
    item.setLayers(makeLayers(
        peaks({1.0, 1.0, 1.0, 1.0}),
        peaks({1.0, 1.0, 1.0, 1.0}),
        peaks({0.0, 0.0, 0.0, 0.0}),
        peaks({0.0, 0.0, 0.0, 0.0}),
        peaks({0, 85, 170, 255})));

    item.setPosition(0);
    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const int unplayedAlpha = static_cast<int>(vertices(node)[0].a);
    QCOMPARE(unplayedAlpha, 153);

    item.setPosition(100);
    QSGNode* updatedNode = item.updatePaintNode(node, nullptr);
    QCOMPARE(updatedNode, node);
    QCOMPARE(static_cast<int>(vertices(updatedNode)[0].a), 255);
    QCOMPARE(static_cast<int>(vertices(updatedNode)[6].a), 255);
    QVERIFY(static_cast<int>(vertices(updatedNode)[0].a) > unplayedAlpha + 100);
    delete updatedNode;
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

void WaveformItemTest::spectrumColorIsIndependentOfPlaybackProgress()
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
    compareColor(vertices(node)[0], 0x00, 0x2F, 0xA7, 0xFF);

    item.setPosition(50);
    node = item.updatePaintNode(node, nullptr);
    const auto* data = vertices(node);
    // Spectrum colour represents frequency, not playback progress. Seeking must
    // leave both the bar and its peak-hold cap on the configured colour model.
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
    const auto* geometryNode = static_cast<const QSGGeometryNode*>(node);
    const int lastVertex = geometryNode->geometry()->vertexCount() - 2;
    QVERIFY(points[lastVertex].x < item.width());
    QVERIFY(points[lastVertex].x >= item.width() - 0.5F);
    QVERIFY2(std::abs(points[lastVertex + 1].y - points[lastVertex].y) >= 1.0F,
             "silent timeline buckets must render a visible baseline");
    compareColor(points[lastVertex], 0xE4, 0x00, 0x7F, 0xFF);
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
    QCOMPARE(geometryNode->geometry()->vertexCount(), 200);
    QVERIFY(geometryNode->material() != nullptr);
    QVERIFY(geometryNode->firstChild() == nullptr);

    const auto* data = vertices(node);
    QCOMPARE(data[0].y, 10.0F);
    QCOMPARE(data[1].y, 30.0F);
    QVERIFY(data[32].y < 1.0F);
    QVERIFY(data[33].y > 39.0F);
    QCOMPARE(data[66].y, 19.5F);
    QCOMPARE(data[67].y, 20.5F);
    QCOMPARE(data[98].y, 19.5F);
    QCOMPARE(data[99].y, 20.5F);
    for (int index = 0; index < geometryNode->geometry()->vertexCount(); ++index) {
        QVERIFY(std::isfinite(data[index].x));
        QVERIFY(std::isfinite(data[index].y));
    }
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
    const auto* initialGeometry = static_cast<const QSGGeometryNode*>(node)->geometry();
    QVERIFY(vertices(node)[initialGeometry->vertexCount() - 2].x < item.width());
    QVERIFY(vertices(node)[initialGeometry->vertexCount() - 2].x
            >= item.width() - 0.5F);

    item.setWidth(240);
    QSGNode* resizedNode = item.updatePaintNode(node, nullptr);
    QCOMPARE(resizedNode, node);
    const auto* resizedGeometry =
        static_cast<const QSGGeometryNode*>(resizedNode)->geometry();
    QVERIFY(vertices(resizedNode)[resizedGeometry->vertexCount() - 2].x < item.width());
    QVERIFY(vertices(resizedNode)[resizedGeometry->vertexCount() - 2].x
            >= item.width() - 0.5F);

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

void WaveformItemTest::sparseWaveformFillsWideDisplayBudget()
{
    TestableWaveformItem item;
    item.setHeight(80.0);
    item.setDensity(1.0);

    QVariantList source;
    source.reserve(128);
    for (int index = 0; index < 128; ++index) {
        source.append(index == 63 ? 1.0
                                  : 0.15 + 0.55 * std::abs(std::sin(index * 0.21)));
    }
    item.setPeaks(source);

    QSGNode* node = nullptr;
    for (const qreal width : {600.0, 1920.0, 3840.0}) {
        item.setWidth(width);
        node = item.updatePaintNode(node, nullptr);
        QVERIFY(node != nullptr);
        const auto* geometry = static_cast<const QSGGeometryNode*>(node)->geometry();
        const int strokeCopies = static_cast<int>(std::ceil(item.lineWidth()));
        const int renderedPeaks = geometry->vertexCount() / (2 * strokeCopies);
        const int displayBudget = static_cast<int>(
            std::ceil(width * item.density() / 2.0));
        qInfo().nospace() << "waveform-density width=" << width
                          << " rendered=" << renderedPeaks
                          << " budget=" << displayBudget;
        QCOMPARE(renderedPeaks, displayBudget);
    }
    QCOMPARE(item.peaks(), source);
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

void WaveformItemTest::nonFrequencyModesIgnoreFrequencyLayers()
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
    QCOMPARE(geometryNode->geometry()->vertexCount(), 200);

    const auto* data = vertices(node);
    // Mix layer uses the reference gradient (all played at end position).
    compareColor(data[0], 0x00, 0xD4, 0xFF, 0xFF);
    compareColor(data[98], 0xFF, 0x40, 0x57, 0xFF);

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
    QCOMPARE(item.waveformCursorX(), 100.0);
    item.setWidth(3840.0);
    node = item.updatePaintNode(node, nullptr);

    QCOMPARE(item.renderWidth(), 3840.0);
    QCOMPARE(item.waveformCursorX(), 768.0);
    QCOMPARE(item.peaks(), originalPeaks);
    QCOMPARE(static_cast<QSGGeometryNode*>(node)->geometry(), geometry);
    QCOMPARE(renderedPeakCount(node, item), 1920);
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
    qInfo() << "waveform-resize-300-ms" << elapsedMs;
    QVERIFY2(elapsedMs < 1000,
             qPrintable(QStringLiteral("300 resize remaps took %1 ms")
                            .arg(elapsedMs)));
    QCOMPARE(item.peaks(), values);
    delete node;
}

void WaveformItemTest::visibleRangeCanBePannedWithoutChangingItsSpan()
{
    WaveformItem item;
    item.setWidth(1000);
    item.setDuration(100000);
    item.zoomAt(500.0, 2.0);
    QCOMPARE(item.visibleEndMs() - item.visibleStartMs(), qint64{50000});

    const bool invoked = QMetaObject::invokeMethod(
        &item, "setVisibleRange", Qt::DirectConnection,
        Q_ARG(qint64, 40000), Q_ARG(qint64, 90000));

    QVERIFY2(invoked, "WaveformItem must expose one atomic visible-range update");
    QCOMPARE(item.visibleStartMs(), qint64{40000});
    QCOMPARE(item.visibleEndMs(), qint64{90000});
    QCOMPARE(item.timeForX(500.0), qint64{65000});
}

void WaveformItemTest::zoomKeepsAnchorStableAndUsesVisibleRange()
{
    WaveformItem item;
    item.setWidth(1000.0);
    item.setDuration(200000);

    item.zoomAt(250.0, 2.0);

    QCOMPARE(item.visibleStartMs(), qint64{25000});
    QCOMPARE(item.visibleEndMs(), qint64{125000});
    QCOMPARE(item.timeForX(0.0), qint64{25000});
    QCOMPARE(item.timeForX(250.0), qint64{50000});
    QCOMPARE(item.pixelForTime(50000), 250.0);
}

void WaveformItemTest::zoomClampsToEightTimesAndResizeDoesNotResetViewport()
{
    WaveformItem item;
    item.setWidth(1000.0);
    item.setDuration(200000);
    item.zoomAt(500.0, 32.0);

    QCOMPARE(item.visibleStartMs(), qint64{87500});
    QCOMPARE(item.visibleEndMs(), qint64{112500});

    item.setWidth(2000.0);
    QCOMPARE(item.visibleStartMs(), qint64{87500});
    QCOMPARE(item.visibleEndMs(), qint64{112500});
    QCOMPARE(item.timeForX(1000.0), qint64{100000});
    QCOMPARE(item.pixelForTime(100000), 1000.0);
}

QTEST_MAIN(WaveformItemTest)
#include "waveform_item_test.moc"

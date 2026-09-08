#include "waveform_item.hpp"
#include "frequency_color_mix.hpp"
#include "frequency_color_waveform_settings.hpp"
#include "track_waveform_thumbnail_item.hpp"

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
    void defaultFrequencyColorsUseFixedPalette();
    void threeBandMixerKeepsPureColorsAndCreatesCombinations();
    void threeBandMixerPreservesDominantBandChroma();
    void threeBandMixerKeepsModerateDominanceVivid_data();
    void threeBandMixerKeepsModerateDominanceVivid();
    void frequencyModeUsesAmplitudeGeometryAndBrightnessOnlyProgress();
    void opaqueFrequencyModeDoesNotRewriteColorsForPosition();
    void frequencyColorChangeDoesNotReplaceGeometryNode();
    void reusesNodeAndUpdatesGeometryAfterResize();
    void clearsOldNodeForEmptyOrZeroSizedContent();
    void hoverUpdatesPreviewWithoutSeeking();
    void cancelDoesNotCommitAStaleSeek();
    void downsamplesPeaksToPixelBudget();
    void sparseWaveformFillsWideDisplayBudget();
    void sourceDensityModeDoesNotUpsampleSparsePeaks();
    void downsamplingKeepsImpulseOnItsTimelinePixel();
    void reusesGeometryWhenPositionChangesWithinBucket();
    void subPixelWidthDoesNotCrash();
    void setLayersPopulatesLayerProperties();
    void unchangedLayersDoNotInvalidateRenderData();
    void nonFrequencyModesIgnoreFrequencyLayers();
    void densityAndLineWidthAffectRenderedGeometry();
    void waveformStrokesStayInsideContainerEdges();
    void onePixelWaveformLeavesTheCanvasEdgeClear();
    void visualModesUseConfiguredProgressAndBaseColors();
    void frequencyModeUpdatesProgressBrightnessWithoutRebuildingNode();
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
    void fullTrackRangeFollowsLongerTrackAndDecodedDuration();
    void explicitViewportSurvivesDurationCorrection();
    void subBucketPanMovesWaveformContinuously();
    void sourceAnchoredPanKeepsOverlappingSamplesStable();
    void bufferedRangeKeepsSourcePixelDensity();
    void frequencyIgnoresOptionalPeakRmsGeometry();
    void frequencyOverviewIntegratesEnergyInsteadOfIndependentMaxima();
    void frequencyZoomKeepsBucketColorOnPlainMixContour();
    void frequencyZoomDoesNotMixOutsidePixelInterval();
    void frequencyStrokesStaySolidAfterRecolor();
    void frequencyStrokeMatchesMixtureWithoutVerticalHighlight();
    void frequencyMatchesPlainGeometryAcrossModesZoomAndPaletteChanges();
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
        QVERIFY2(data[firstVertex].x >= 0.0F,
                 "the first waveform stroke must stay inside the left border");
        QVERIFY2(data[lastVertex].x <= item.width(),
                 "the last waveform stroke must stay inside the right border");
        QVERIFY2(data[firstVertex].x > previousFirstX,
                 "stroke copies must not stack into a bright edge line");
        previousFirstX = data[firstVertex].x;
    }
    QVERIFY(data[0].x >= 1.0F);
    const int outerLastVertex =
        (strokeCopies - 1) * peakCount * 2 + (peakCount - 1) * 2;
    QCOMPARE(data[outerLastVertex].x,
             static_cast<float>(item.width() - 0.5));

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

void WaveformItemTest::threeBandMixerKeepsPureColorsAndCreatesCombinations()
{
    const QColor low(QStringLiteral("#FC0909"));
    const QColor mid(QStringLiteral("#03FF00"));
    const QColor high(QStringLiteral("#0048FF"));
    QCOMPARE(agplayer::ui::mixFrequencyColor(0.0, 0.0, 0.0,
                                             low, mid, high), low);
    QCOMPARE(agplayer::ui::mixFrequencyColor(1.0, 0.0, 0.0,
                                             low, mid, high), low);
    QCOMPARE(agplayer::ui::mixFrequencyColor(0.0, 1.0, 0.0,
                                             low, mid, high), mid);
    QCOMPARE(agplayer::ui::mixFrequencyColor(0.0, 0.0, 1.0,
                                             low, mid, high), high);

    const QColor lowMid = agplayer::ui::mixFrequencyColor(
        1.0, 1.0, 0.0, low, mid, high);
    const QColor midHigh = agplayer::ui::mixFrequencyColor(
        0.0, 1.0, 1.0, low, mid, high);
    const QColor lowHigh = agplayer::ui::mixFrequencyColor(
        1.0, 0.0, 1.0, low, mid, high);
    const QColor all = agplayer::ui::mixFrequencyColor(
        1.0, 1.0, 1.0, low, mid, high);
    const QColor denseMusic = agplayer::ui::mixFrequencyColor(
        0.63, 0.74, 0.88, low, mid, high);
    const QColor quietMidHigh = agplayer::ui::mixFrequencyColor(
        0.0, 0.35, 0.15, low, mid, high);

    const auto verifyFiniteColor = [](const QColor& color) {
        QVERIFY(color.isValid());
        QVERIFY(std::isfinite(color.redF()));
        QVERIFY(std::isfinite(color.greenF()));
        QVERIFY(std::isfinite(color.blueF()));
    };
    verifyFiniteColor(lowMid);
    verifyFiniteColor(midHigh);
    verifyFiniteColor(lowHigh);
    verifyFiniteColor(all);
    verifyFiniteColor(denseMusic);
    verifyFiniteColor(quietMidHigh);

    QVERIFY2(lowMid.red() >= 245 && lowMid.green() >= 245
                 && lowMid.blue() <= 40,
             "full Low + Mid must add to bright yellow");
    QVERIFY2(midHigh.red() <= 40 && midHigh.green() >= 245
                 && midHigh.blue() >= 245,
             "full Mid + High must add to bright cyan");
    QVERIFY2(lowHigh.red() >= 245 && lowHigh.green() <= 128
                 && lowHigh.blue() >= 245,
             "full Low + High must add to bright magenta");
    QVERIFY2(all.hsvSaturationF() < 0.1 && all.valueF() >= 0.95,
             "balanced full-band energy must allow a neutral highlight");
    QVERIFY2(denseMusic.hsvSaturationF() > 0.02
                 && denseMusic.hsvSaturationF() < 0.25,
             "near-balanced music must retain gentle pastel chroma");
    QVERIFY2(denseMusic.green() > denseMusic.red()
                 && denseMusic.blue() > denseMusic.red(),
             "the representative High-heavy mix must remain blue/cyan");
    QVERIFY(quietMidHigh != mid);
    QVERIFY2(quietMidHigh.blue() >= 150,
             "quiet High energy must remain visible in a Mid-heavy mix");
}

void WaveformItemTest::threeBandMixerPreservesDominantBandChroma()
{
    const QColor red("#ff0000"), green("#00ff00"), blue("#0000ff");
    const QColor bass = agplayer::ui::mixFrequencyColor(
        1.0, 0.2, 0.05, red, green, blue);
    QVERIFY2(bass.red() >= 245 && bass.green() < 130 && bass.blue() < 75,
             "weak secondary bands must not wash a bass-dominant sample pastel");
    const QColor treble = agplayer::ui::mixFrequencyColor(
        0.05, 0.2, 1.0, red, green, blue);
    QCOMPARE(bass.red(), treble.blue());
    QCOMPARE(bass.blue(), treble.red());
    QCOMPARE(bass.green(), treble.green());
    const QColor quiet = agplayer::ui::mixFrequencyColor(
        0.1, 0.02, 0.005, red, green, blue);
    QVERIFY(quiet.red() > 100);
    QVERIFY(quiet.red() > quiet.green() && quiet.green() > quiet.blue());
    const QColor adjacent = agplayer::ui::mixFrequencyColor(
        1.0, 0.2001, 0.05, red, green, blue);
    QVERIFY(std::abs(adjacent.green() - bass.green()) <= 1);
}

void WaveformItemTest::threeBandMixerKeepsModerateDominanceVivid_data()
{
    QTest::addColumn<double>("low");
    QTest::addColumn<double>("mid");
    QTest::addColumn<double>("high");
    QTest::newRow("bass") << 0.8 << 0.35 << 0.2;
    QTest::newRow("treble") << 0.2 << 0.35 << 0.8;
    QTest::newRow("quiet-bass") << 0.08 << 0.035 << 0.02;
    QTest::newRow("quiet-treble") << 0.02 << 0.035 << 0.08;
}

void WaveformItemTest::threeBandMixerKeepsModerateDominanceVivid()
{
    QFETCH(double, low);
    QFETCH(double, mid);
    QFETCH(double, high);
    const QColor mixed = agplayer::ui::mixFrequencyColor(
        low, mid, high, Qt::red, Qt::green, Qt::blue);
    qInfo() << "moderate frequency RGB" << mixed.red() << mixed.green()
            << mixed.blue() << "saturation" << mixed.hsvSaturationF();
    // A four-to-one dominant/weak-band ratio should remain visibly colored
    // even with a substantial middle band, at both normal and quiet levels.
    QVERIFY2(mixed.hsvSaturationF() >= 0.51,
             "moderate band dominance must not fade into a pastel mixture");
    const int dominant = low > high ? mixed.red() : mixed.blue();
    const int weakest = low > high ? mixed.blue() : mixed.red();
    QVERIFY(dominant > mixed.green() && mixed.green() > weakest);
    QVERIFY2(weakest >= 90, "weaker frequency energy must remain visible");
    QVERIFY(dominant >= 190);
}

void WaveformItemTest::defaultFrequencyColorsUseFixedPalette()
{
    const QColor expectedLow(QStringLiteral("#FF0000"));
    const QColor expectedMid(QStringLiteral("#00FF00"));
    const QColor expectedHigh(QStringLiteral("#0000FF"));

    const FrequencyColorWaveformSettings settings;
    QCOMPARE(settings.lowColor(), expectedLow);
    QCOMPARE(settings.midColor(), expectedMid);
    QCOMPARE(settings.highColor(), expectedHigh);

    WaveformItem waveform;
    QCOMPARE(waveform.lowColor(), expectedLow);
    QCOMPARE(waveform.midColor(), expectedMid);
    QCOMPARE(waveform.highColor(), expectedHigh);
    QCOMPARE(waveform.frequencyUnplayedOpacity(), 0.20);

    waveform.setFrequencyUnplayedOpacity(-1.0);
    QCOMPARE(waveform.frequencyUnplayedOpacity(), 0.0);

    const TrackWaveformThumbnailItem thumbnail;
    QCOMPARE(thumbnail.lowColor(), expectedLow);
    QCOMPARE(thumbnail.midColor(), expectedMid);
    QCOMPARE(thumbnail.highColor(), expectedHigh);
}

void WaveformItemTest::frequencyModeUsesAmplitudeGeometryAndBrightnessOnlyProgress()
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

    TestableWaveformItem frequency;
    frequency.setWidth(100);
    frequency.setHeight(40);
    frequency.setDuration(100);
    frequency.setPosition(0);
    frequency.setDensity(2.0);
    frequency.setLineWidth(1.0);
    frequency.setVisualMode(3);
    frequency.setLowColor(QColor(QStringLiteral("#ff0000")));
    frequency.setMidColor(QColor(QStringLiteral("#00ff00")));
    frequency.setHighColor(QColor(QStringLiteral("#0000ff")));
    frequency.setFrequencyUnplayedOpacity(0.88);
    frequency.setLayers(makeLayers(
        peaks({0.25, 0.5, 0.75, 1.0}),
        peaks({1.0, 0.0, 1.0, 1.0}),
        peaks({0.0, 1.0, 1.0, 1.0}),
        peaks({0.0, 0.0, 0.0, 1.0})));
    QSGNode* frequencyNode = frequency.updatePaintNode(nullptr, nullptr);

    QVERIFY(plainNode != nullptr);
    QVERIFY(frequencyNode != nullptr);
    const auto* plainVertices = vertices(plainNode);
    const auto* frequencyVertices = vertices(frequencyNode);
    const int count = static_cast<QSGGeometryNode*>(plainNode)
                          ->geometry()->vertexCount();
    QCOMPARE(count, 200);
    QCOMPARE(static_cast<QSGGeometryNode*>(frequencyNode)
                 ->geometry()->vertexCount(), count);
    for (int index = 0; index < count; ++index) {
        QCOMPARE(frequencyVertices[index].x, plainVertices[index].x);
        QCOMPARE(frequencyVertices[index].y, plainVertices[index].y);
        QCOMPARE(frequencyVertices[index].a, static_cast<unsigned char>(255));
    }
    compareColor(frequencyVertices[1], frequencyVertices[0].r,
                 frequencyVertices[0].g, frequencyVertices[0].b, 255);
    const auto beforeEdge = frequencyVertices[0];
    const auto beforeCenter = frequencyVertices[1];

    std::vector<QPointF> beforePositions;
    beforePositions.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        beforePositions.emplace_back(frequencyVertices[index].x,
                                     frequencyVertices[index].y);
    }
    frequency.setPosition(100);
    frequencyNode = frequency.updatePaintNode(frequencyNode, nullptr);
    const auto* playedVertices = vertices(frequencyNode);
    for (int index = 0; index < count; ++index) {
        QCOMPARE(playedVertices[index].x, beforePositions[index].x());
        QCOMPARE(playedVertices[index].y, beforePositions[index].y());
    }
    QCOMPARE(static_cast<int>(playedVertices[0].a), 255);
    QCOMPARE(static_cast<int>(playedVertices[1].a), 255);
    QCOMPARE(static_cast<int>(playedVertices[2].a), 255);
    QCOMPARE(static_cast<int>(playedVertices[3].a), 255);
    QVERIFY(playedVertices[0].r > beforeEdge.r);
    QCOMPARE(playedVertices[0].g, beforeEdge.g);
    QCOMPARE(playedVertices[0].b, beforeEdge.b);
    QVERIFY(playedVertices[1].r > beforeCenter.r);
    QCOMPARE(playedVertices[1].g, beforeCenter.g);
    QCOMPARE(playedVertices[1].b, beforeCenter.b);
    delete plainNode;
    delete frequencyNode;
}

void WaveformItemTest::opaqueFrequencyModeDoesNotRewriteColorsForPosition()
{
    TestableWaveformItem item;
    item.setWidth(100);
    item.setHeight(40);
    item.setDuration(100);
    item.setPosition(0);
    item.setDensity(2.0);
    item.setLineWidth(1.0);
    item.setVisualMode(3);
    item.setFrequencyUnplayedOpacity(1.0);
    item.setLayers(makeLayers(
        peaks({0.25, 0.5, 0.75, 1.0}),
        peaks({1.0, 0.0, 1.0, 1.0}),
        peaks({0.0, 1.0, 1.0, 1.0}),
        peaks({0.0, 0.0, 0.0, 1.0})));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    auto* before = static_cast<QSGGeometryNode*>(node)
                       ->geometry()->vertexDataAsColoredPoint2D();
    before[0].set(before[0].x, before[0].y, 7, 11, 13, 17);

    item.setPosition(100);
    QCOMPARE(item.updatePaintNode(node, nullptr), node);
    const auto* after = vertices(node);
    compareColor(after[0], 7, 11, 13, 17);

    delete node;
}

void WaveformItemTest::frequencyColorChangeDoesNotReplaceGeometryNode()
{
    TestableWaveformItem item;
    item.setWidth(120);
    item.setHeight(48);
    item.setDuration(100);
    item.setVisualMode(3);
    item.setFrequencyUnplayedOpacity(0.88);
    item.setLayers(makeLayers(peaks({1.0, 0.5, 0.75, 0.25}),
                              peaks({1.0, 1.0, 1.0, 1.0}),
                              peaks({0.0, 0.0, 0.0, 0.0}),
                              peaks({0.0, 0.0, 0.0, 0.0})));
    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    auto* geometry = static_cast<QSGGeometryNode*>(node)->geometry();
    const void* vertexStorage = geometry->vertexData();

    item.setLowColor(QColor(QStringLiteral("#ef1010")));
    QSGNode* updated = item.updatePaintNode(node, nullptr);
    QCOMPARE(updated, node);
    QCOMPARE(static_cast<QSGGeometryNode*>(updated)->geometry(), geometry);
    QCOMPARE(geometry->vertexData(), vertexStorage);
    const auto* updatedVertices = vertices(updated);
    QCOMPARE(updatedVertices[0].r, updatedVertices[1].r);
    QVERIFY(updatedVertices[1].r > 220);
    QVERIFY(updatedVertices[1].r > updatedVertices[1].g);
    QVERIFY(updatedVertices[1].r > updatedVertices[1].b);
    QCOMPARE(static_cast<int>(updatedVertices[1].a), 255);
    QCOMPARE(static_cast<int>(updatedVertices[0].a), 255);
    QCOMPARE(static_cast<int>(updatedVertices[2].a), 255);
    QCOMPARE(static_cast<int>(updatedVertices[3].a), 255);
    delete updated;
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

void WaveformItemTest::frequencyModeUpdatesProgressBrightnessWithoutRebuildingNode()
{
    TestableWaveformItem item;
    item.setWidth(4);
    item.setHeight(40);
    item.setDuration(100);
    item.setDensity(2.0);
    item.setLineWidth(1.0);
    item.setVisualMode(3);
    item.setFrequencyUnplayedOpacity(0.38);
    QCOMPARE(item.frequencyUnplayedOpacity(), 0.38);
    item.setLayers(makeLayers(
        peaks({1.0, 1.0, 1.0, 1.0}),
        peaks({1.0, 1.0, 1.0, 1.0}),
        peaks({0.0, 0.0, 0.0, 0.0}),
        peaks({0.0, 0.0, 0.0, 0.0})));

    item.setPosition(0);
    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const int unplayedBrightness = static_cast<int>(vertices(node)[1].r);
    QCOMPARE(static_cast<int>(vertices(node)[1].a), 255);
    QCOMPARE(static_cast<int>(vertices(node)[0].a), 255);

    item.setPosition(100);
    QSGNode* updatedNode = item.updatePaintNode(node, nullptr);
    QCOMPARE(updatedNode, node);
    QCOMPARE(static_cast<int>(vertices(updatedNode)[0].a), 255);
    QCOMPARE(static_cast<int>(vertices(updatedNode)[1].a), 255);
    QCOMPARE(static_cast<int>(vertices(updatedNode)[2].a), 255);
    QCOMPARE(static_cast<int>(vertices(updatedNode)[3].a), 255);
    QVERIFY(static_cast<int>(vertices(updatedNode)[1].r)
            > unplayedBrightness + 20);
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
    item.setPeaks(peaks({0.0, 0.0, 1.0}));

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* points = vertices(node);
    const auto* geometryNode = static_cast<const QSGGeometryNode*>(node);
    const int lastVertex = geometryNode->geometry()->vertexCount() - 2;
    QCOMPARE(points[lastVertex].x,
             static_cast<float>(item.width() - 0.5));
    QVERIFY2(std::abs(points[lastVertex + 1].y - points[lastVertex].y) >= 39.0F,
             "the final non-zero peak must render at full height inside the edge");
    compareColor(points[lastVertex], 0xE4, 0x00, 0x7F, 0xFF);
    delete node;
}

void WaveformItemTest::sourceDensityModeDoesNotUpsampleSparsePeaks()
{
    TestableWaveformItem item;
    item.setWidth(200);
    item.setHeight(40);
    item.setDuration(1000);
    item.setLineWidth(1);
    item.setPeaks(peaks({0.2, 0.4, 0.6, 0.8}));
    item.setPreserveSourcePeakDensity(true);

    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node != nullptr);
    const auto* geometryNode = static_cast<const QSGGeometryNode*>(node);
    QCOMPARE(geometryNode->geometry()->vertexCount(), 8);
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
    QCOMPARE(vertices(node)[initialGeometry->vertexCount() - 2].x,
             static_cast<float>(item.width() - 0.5));

    item.setWidth(240);
    QSGNode* resizedNode = item.updatePaintNode(node, nullptr);
    QCOMPARE(resizedNode, node);
    const auto* resizedGeometry =
        static_cast<const QSGGeometryNode*>(resizedNode)->geometry();
    QCOMPARE(vertices(resizedNode)[resizedGeometry->vertexCount() - 2].x,
             static_cast<float>(item.width() - 0.5));

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

void WaveformItemTest::unchangedLayersDoNotInvalidateRenderData()
{
    WaveformItem item;
    const auto layers = makeLayers(peaks({0.2, 0.8, 0.4}), peaks({0.1, 0.5, 0.2}));
    item.setLayers(layers);
    QSignalSpy changed(&item, &WaveformItem::layersChanged);
    // Session duration and layers notifications can deliver the same payload twice.
    item.setLayers(layers);
    QCOMPARE(changed.count(), 0);
    auto updated = layers;
    updated[QStringLiteral("mix")] = peaks({0.3, 0.9, 0.5});
    item.setLayers(updated);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(item.layers(), updated);
    // Returning from spectrum data must restore layers even when empty.
    item.setLayers({});
    item.setPeaks(peaks({0.5}));
    item.setLayers({});
    QVERIFY(item.peaks().isEmpty());
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

void WaveformItemTest::fullTrackRangeFollowsLongerTrackAndDecodedDuration()
{
    TestableWaveformItem item;
    item.setWidth(863);
    item.setHeight(80);
    item.setDuration(142000);
    item.setPeaks(peaks({0.1, 0.1, 0.1, 0.8}));
    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    item.setDuration(300000);
    QCOMPARE(item.visibleEndMs(), qint64{300000});
    QCOMPARE(item.timeForX(863), qint64{300000});
    node = item.updatePaintNode(node, nullptr);
    const auto* geometry = static_cast<QSGGeometryNode*>(node)->geometry();
    const auto* data = vertices(node);
    const int last = geometry->vertexCount() - 1;
    QVERIFY(data[last].x > 860);
    QVERIFY(std::abs(data[last].y - 40.0F) > 20.0F);
    item.setDuration(300026); // decoded duration replacing container metadata
    QCOMPARE(item.visibleEndMs(), qint64{300026});
    delete node;
}

void WaveformItemTest::explicitViewportSurvivesDurationCorrection()
{
    WaveformItem item;
    item.setDuration(300000);
    item.setVisibleRange(100000, 104000);
    item.setDuration(300026);
    QCOMPARE(item.visibleStartMs(), qint64{100000});
    QCOMPARE(item.visibleEndMs(), qint64{104000});
}

void WaveformItemTest::subBucketPanMovesWaveformContinuously()
{
    TestableWaveformItem item;
    item.setWidth(100);
    item.setHeight(100);
    item.setDuration(100000);
    item.setPeaks(peaks({0.1, 0.9}));
    item.setVisibleRange(20000, 24000);
    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    const float before = vertices(node)[1].y;
    item.setVisibleRange(21000, 25000);
    node = item.updatePaintNode(node, nullptr);
    QVERIFY(std::abs(vertices(node)[1].y - before) > 0.01F);
    delete node;
}

void WaveformItemTest::sourceAnchoredPanKeepsOverlappingSamplesStable()
{
    TestableWaveformItem item;
    item.setWidth(8);
    item.setHeight(100);
    item.setDensity(1);
    item.setLineWidth(1);
    item.setDuration(8000);
    item.setPosition(3000);
    item.setVisualMode(3);
    item.setFrequencyUnplayedOpacity(1);
    item.setSourceAnchoredSampling(true);
    item.setLayers(makeLayers(
        peaks({0.10, 0.25, 0.40, 0.55, 0.70, 0.85, 0.60, 0.35}),
        peaks({1.00, 0.75, 0.50, 0.25, 0.00, 0.25, 0.50, 0.75}),
        peaks({0.00, 0.25, 0.50, 0.75, 1.00, 0.75, 0.50, 0.25}),
        peaks({0.20, 0.35, 0.50, 0.65, 0.80, 0.65, 0.50, 0.35})));

    item.setVisibleRange(1000, 5000);
    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node);
    const int beforeCount = renderedPeakCount(node, item);
    QCOMPARE(beforeCount, 4);
    std::vector<QSGGeometry::ColoredPoint2D> before;
    before.reserve(static_cast<std::size_t>(beforeCount));
    for (int index = 0; index < beforeCount; ++index) {
        before.push_back(vertices(node)[index * 2]);
    }

    item.setVisibleRange(1100, 5100);
    node = item.updatePaintNode(node, nullptr);
    QVERIFY(node);
    QCOMPARE(renderedPeakCount(node, item), beforeCount);
    for (int index = 0; index < beforeCount; ++index) {
        const auto& after = vertices(node)[index * 2];
        QCOMPARE(after.y, before[static_cast<std::size_t>(index)].y);
        QCOMPARE(after.r, before[static_cast<std::size_t>(index)].r);
        QCOMPARE(after.g, before[static_cast<std::size_t>(index)].g);
        QCOMPARE(after.b, before[static_cast<std::size_t>(index)].b);
        QCOMPARE(after.a, before[static_cast<std::size_t>(index)].a);
        QVERIFY(after.x < before[static_cast<std::size_t>(index)].x);
    }

    // A buffered rolling waveform eventually rebases by many lattice points,
    // not only by the sub-bucket pan above. The overlapping source points must
    // retain both their amplitude and frequency colour across that rebase.
    item.setVisibleRange(3000, 7000);
    node = item.updatePaintNode(node, nullptr);
    QVERIFY(node);
    QCOMPARE(renderedPeakCount(node, item), beforeCount);
    for (int index = 0; index < 2; ++index) {
        const auto& after = vertices(node)[index * 2];
        const auto& overlappingBefore = before[static_cast<std::size_t>(index + 2)];
        QCOMPARE(after.y, overlappingBefore.y);
        QCOMPARE(after.r, overlappingBefore.r);
        QCOMPARE(after.g, overlappingBefore.g);
        QCOMPARE(after.b, overlappingBefore.b);
        QCOMPARE(after.a, overlappingBefore.a);
        QVERIFY(after.x < overlappingBefore.x);
    }
    delete node;
}

void WaveformItemTest::bufferedRangeKeepsSourcePixelDensity()
{
    TestableWaveformItem item;
    item.setHeight(100);
    item.setDensity(1);
    item.setLineWidth(1);
    item.setDuration(120000);
    item.setVisualMode(3);
    item.setFrequencyUnplayedOpacity(1);
    item.setSourceAnchoredSampling(true);

    QVariantList values;
    values.reserve(4096);
    for (int index = 0; index < 4096; ++index) {
        values.append(0.1 + 0.8 * std::abs(std::sin(index * 0.13)));
    }
    item.setLayers(makeLayers(values, values, values, values));

    item.setWidth(640);
    item.setVisibleRange(52500, 67500);
    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node);
    const int oneViewportPeaks = renderedPeakCount(node, item);
    QCOMPARE(oneViewportPeaks, 320);

    item.setWidth(1280);
    item.setVisibleRange(45000, 75000);
    node = item.updatePaintNode(node, nullptr);
    QVERIFY(node);
    const int bufferedPeaks = renderedPeakCount(node, item);
    QCOMPARE(bufferedPeaks, oneViewportPeaks * 2);
    QCOMPARE(static_cast<double>(bufferedPeaks) / 30000.0,
             static_cast<double>(oneViewportPeaks) / 15000.0);

    delete node;
}

void WaveformItemTest::frequencyIgnoresOptionalPeakRmsGeometry()
{
    TestableWaveformItem item;
    item.setWidth(4);
    item.setHeight(100);
    item.setDensity(1);
    item.setLineWidth(1);
    item.setVisualMode(3);
    auto layers = makeLayers(peaks({0.1, 0.1}), peaks({1, 1}),
                             peaks({0, 0}), peaks({0, 0}));
    layers[QStringLiteral("peak")] = peaks({0.8, 0.8});
    layers[QStringLiteral("rms")] = peaks({0.2, 0.6});
    item.setLayers(layers);
    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node);
    const auto* geometry = static_cast<QSGGeometryNode*>(node)->geometry();
    QCOMPARE(geometry->vertexCount(), 4);
    const auto* data = vertices(node);
    QVERIFY(std::abs(data[0].y - 45.0F) < 0.001F);
    QVERIFY(std::abs(data[1].y - 55.0F) < 0.001F);
    QVERIFY(std::abs(data[2].y - 45.0F) < 0.001F);
    QVERIFY(std::abs(data[3].y - 55.0F) < 0.001F);
    const float originalInner = data[1].y;
    item.setLowColor(QColor("#ff00ff"));
    QCOMPARE(item.updatePaintNode(node, nullptr), node);
    QCOMPARE(vertices(node)[1].y, originalInner);
    QVERIFY(vertices(node)[1].b > 0);
    delete node;
}

void WaveformItemTest::frequencyOverviewIntegratesEnergyInsteadOfIndependentMaxima()
{
    TestableWaveformItem item;
    item.setWidth(1);
    item.setHeight(80);
    item.setDensity(1);
    item.setLineWidth(1);
    item.setVisualMode(3);
    item.setFrequencyUnplayedOpacity(1);
    item.setLowColor(Qt::red);
    item.setMidColor(Qt::green);
    item.setHighColor(Qt::blue);
    // One low-frequency impulse: sqrt((1+0+0+0)/4) = 0.5.
    // Sustained high-frequency amplitude 0.6 should dominate this overview bin.
    item.setLayers(makeLayers(peaks({1, 1, 1, 1}), peaks({1, 0, 0, 0}),
                              peaks({0, 0, 0, 0}), peaks({0.6, 0.6, 0.6, 0.6})));
    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node);
    QVERIFY(vertices(node)[1].b > vertices(node)[1].r);
    delete node;
}

void WaveformItemTest::frequencyZoomKeepsBucketColorOnPlainMixContour()
{
    TestableWaveformItem item, plain;
    item.setWidth(16);
    item.setHeight(100);
    item.setDensity(1);
    item.setLineWidth(1);
    item.setVisualMode(3);
    item.setDuration(4000);
    auto layers = makeLayers(peaks({0, 1, 0, 0}), peaks({0, 1, 0, 0}),
                             peaks({0, 0, 0, 0}), peaks({0, 0, 0, 0}));
    layers[QStringLiteral("peak")] = peaks({0, 1, 0, 0});
    layers[QStringLiteral("rms")] = peaks({0, 0.5, 0, 0});
    item.setLayers(layers);
    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node);
    plain.setWidth(16);
    plain.setHeight(100);
    plain.setDensity(1);
    plain.setLineWidth(1);
    plain.setDuration(4000);
    plain.setVisualMode(0);
    plain.setLayers(layers);
    QSGNode* plainNode = plain.updatePaintNode(nullptr, nullptr);
    QVERIFY(plainNode);
    const int count = static_cast<QSGGeometryNode*>(plainNode)->geometry()->vertexCount();
    QCOMPARE(static_cast<QSGGeometryNode*>(node)->geometry()->vertexCount(), count);
    for (int vertex = 0; vertex < count; ++vertex) {
        QCOMPARE(vertices(node)[vertex].x, vertices(plainNode)[vertex].x);
        QCOMPARE(vertices(node)[vertex].y, vertices(plainNode)[vertex].y);
    }
    // Bucket statistics still control color; geometry follows only mix.
    QVERIFY(vertices(node)[2 * 2].r > 0);
    QCOMPARE(vertices(node)[2 * 2].b, static_cast<unsigned char>(0));
    delete plainNode;
    delete node;
}

void WaveformItemTest::frequencyZoomDoesNotMixOutsidePixelInterval()
{
    TestableWaveformItem item;
    item.setWidth(16);
    item.setHeight(100);
    item.setDensity(1);
    item.setLineWidth(1);
    item.setVisualMode(3);
    item.setFrequencyUnplayedOpacity(1);
    item.setDuration(4000);
    auto layers = makeLayers(peaks({1, 1, 1, 1}), peaks({0, 1, 0, 0}),
                             peaks({0, 0, 0, 0}), peaks({1, 0, 1, 1}));
    layers[QStringLiteral("peak")] = peaks({1, 1, 1, 1});
    layers[QStringLiteral("rms")] = peaks({0.5, 0.5, 0.5, 0.5});
    item.setLayers(layers);
    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node);
    // Column 1 covers 500..1000 ms: all blue, no red from 1000..2000 ms.
    QCOMPARE(vertices(node)[2 + 1].r, static_cast<unsigned char>(0));
    QVERIFY(vertices(node)[2 + 1].b > 0);
    delete node;
}

void WaveformItemTest::frequencyStrokesStaySolidAfterRecolor()
{
    for (const bool detailed : {false, true}) {
        TestableWaveformItem item;
        item.setWidth(2);
        item.setHeight(100);
        item.setDensity(1);
        item.setLineWidth(1);
        item.setVisualMode(3);
        item.setFrequencyUnplayedOpacity(1);
        auto layers = makeLayers(peaks({1}), peaks({1}), peaks({0}), peaks({0}));
        if (detailed) {
            layers[QStringLiteral("peak")] = peaks({1});
            layers[QStringLiteral("rms")] = peaks({0.5});
        }
        item.setLayers(layers);
        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node);
        QCOMPARE(static_cast<QSGGeometryNode*>(node)->geometry()->vertexCount(), 2);
        compareColor(vertices(node)[0], 255, 0, 0, 255);
        compareColor(vertices(node)[1], 255, 0, 0, 255);
        item.setLowColor(Qt::blue);
        QCOMPARE(item.updatePaintNode(node, nullptr), node);
        compareColor(vertices(node)[0], 0, 0, 255, 255);
        compareColor(vertices(node)[1], 0, 0, 255, 255);
        delete node;
    }
}

void WaveformItemTest::frequencyStrokeMatchesMixtureWithoutVerticalHighlight()
{
    TestableWaveformItem item;
    item.setWidth(2);
    item.setHeight(100);
    item.setDensity(1);
    item.setLineWidth(1);
    item.setVisualMode(3);
    item.setFrequencyUnplayedOpacity(1);
    auto layers = makeLayers(peaks({1}), peaks({0.2}), peaks({0}), peaks({1}));
    layers[QStringLiteral("peak")] = peaks({1});
    layers[QStringLiteral("rms")] = peaks({0.5});
    item.setLayers(layers);
    QSGNode* node = item.updatePaintNode(nullptr, nullptr);
    QVERIFY(node);
    for (const QColor& palette : {QColor(Qt::red), QColor("#e00000")}) {
        item.setLowColor(palette);
        QCOMPARE(item.updatePaintNode(node, nullptr), node);
        const QColor expected = agplayer::ui::mixFrequencyColor(
            0.2, 0, 1, palette, Qt::green, Qt::blue);
        compareColor(vertices(node)[0], expected.red(), expected.green(), expected.blue(), 255);
        compareColor(vertices(node)[1], expected.red(), expected.green(), expected.blue(), 255);
    }
    delete node;
}

void WaveformItemTest::frequencyMatchesPlainGeometryAcrossModesZoomAndPaletteChanges()
{
    const auto mix = peaks({0.08, 0.18, 0.12, 0.24, 0.09, 0.21, 0.13, 0.06});
    auto layers = makeLayers(mix, peaks({1, 0.1, 0.2, 0.8, 0.1, 0.9, 0.3, 0.2}),
                             peaks({0.1, 0.8, 0.2, 0.1, 0.7, 0.2, 0.3, 0.8}),
                             peaks({0.2, 0.1, 0.8, 0.2, 0.1, 0.3, 0.9, 0.1}));
    layers[QStringLiteral("peak")] = peaks({0.99, 0.99, 0.99, 0.99, 0.99, 0.99, 0.99, 0.99});
    layers[QStringLiteral("rms")] = peaks({0.7, 0.6, 0.8, 0.7, 0.6, 0.8, 0.7, 0.6});
    for (const int mode : {0, 1}) {
        TestableWaveformItem plain, frequency;
        for (auto* item : {&plain, &frequency}) {
            item->setWidth(127);
            item->setHeight(84);
            item->setDuration(8000);
            item->setDensity(1.5);
            item->setLineWidth(2);
            item->setAmplitudeScale(0.8);
            item->setLayers(layers);
        }
        plain.setVisualMode(mode);
        frequency.setVisualMode(3);
        QSGNode* plainNode = nullptr;
        QSGNode* frequencyNode = nullptr;
        for (int stage = 0; stage < 3; ++stage) {
            if (stage == 1) {
                plain.setVisibleRange(1750, 3250);
                frequency.setVisibleRange(1750, 3250);
            } else if (stage == 2) {
                frequency.setLowColor(QColor("#ab1234"));
                frequency.setMidColor(QColor("#12ab34"));
                frequency.setHighColor(QColor("#1234ab"));
                frequency.setFrequencyUnplayedOpacity(0.4);
                plain.setPosition(2500);
                frequency.setPosition(2500);
            }
            plainNode = plain.updatePaintNode(plainNode, nullptr);
            frequencyNode = frequency.updatePaintNode(frequencyNode, nullptr);
            QVERIFY(plainNode);
            QVERIFY(frequencyNode);
            const auto count = static_cast<QSGGeometryNode*>(plainNode)->geometry()->vertexCount();
            QCOMPARE(static_cast<QSGGeometryNode*>(frequencyNode)->geometry()->vertexCount(), count);
            for (int vertex = 0; vertex < count; ++vertex) {
                QCOMPARE(vertices(frequencyNode)[vertex].x, vertices(plainNode)[vertex].x);
                QCOMPARE(vertices(frequencyNode)[vertex].y, vertices(plainNode)[vertex].y);
            }
        }
        delete plainNode;
        delete frequencyNode;
    }
}

QTEST_MAIN(WaveformItemTest)
#include "waveform_item_test.moc"

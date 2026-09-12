#include "audio_editor/audio_editor_waveform_item.hpp"

#include <QGuiApplication>
#include <QImage>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QtTest>

class TestableAudioEditorWaveformItem final : public AudioEditorWaveformItem {
public:
    using AudioEditorWaveformItem::updatePaintNode;
};

class AudioEditorWaveformItemTest final : public QObject {
    Q_OBJECT

private slots:
    void rendersBoundedStereoPeakGeometryInOneBuffer()
    {
        TestableAudioEditorWaveformItem item;
        item.setWidth(4.0);
        item.setHeight(100.0);
        item.setChannelPeaks({
            QVariantList{-1.0, 1.0, -0.5, 0.5},
            QVariantList{-0.25, 0.25, -0.75, 0.75}});

        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        const auto* geometry_node = static_cast<QSGGeometryNode*>(node);
        QCOMPARE(geometry_node->geometry()->drawingMode(),
                 QSGGeometry::DrawTriangles);
        QCOMPARE(geometry_node->geometry()->vertexCount(), 72);
        QCOMPARE(node->childCount(), 0);
        QCOMPARE(item.generatedPointCount(), 16);
        delete node;
    }

    void filledEnvelopeAndCenterLineShareOnePixelAlignedBuffer()
    {
        TestableAudioEditorWaveformItem item;
        item.setWidth(3.0);
        item.setHeight(20.0);
        item.setLineWidth(1.0);
        item.setChannelPeaks({QVariant(QVariantList{
            -1.0, 1.0, -0.5, 0.5, -0.25, 0.25})});

        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        const auto* geometryNode = static_cast<QSGGeometryNode*>(node);
        QCOMPARE(node->childCount(), 0);
        QCOMPARE(geometryNode->geometry()->drawingMode(),
                 QSGGeometry::DrawTriangles);
        QCOMPARE(geometryNode->geometry()->vertexCount(), 24);
        const auto* vertices = geometryNode->geometry()->vertexDataAsPoint2D();
        bool hasCenterTop = false;
        bool hasCenterBottom = false;
        for (int index = 0; index < geometryNode->geometry()->vertexCount(); ++index) {
            QVERIFY(std::abs(vertices[index].x * 2.0F
                             - std::round(vertices[index].x * 2.0F)) < 0.001F);
            hasCenterTop = hasCenterTop
                || std::abs(vertices[index].y - 9.5F) < 0.001F;
            hasCenterBottom = hasCenterBottom
                || std::abs(vertices[index].y - 10.5F) < 0.001F;
        }
        QVERIFY(hasCenterTop);
        QVERIFY(hasCenterBottom);
        delete node;
    }

    void emptyDataClearsOldNode()
    {
        TestableAudioEditorWaveformItem item;
        item.setWidth(10.0);
        item.setHeight(40.0);
        QVariantList channels;
        channels.append(QVariant(QVariantList{-1.0, 1.0, -0.5, 0.5}));
        item.setChannelPeaks(channels);
        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        item.setChannelPeaks({});
        node = item.updatePaintNode(node, nullptr);
        QCOMPARE(node, nullptr);
    }

    void visiblePeaksAreBoundedToTwoPointsPerLogicalPixel()
    {
        TestableAudioEditorWaveformItem item;
        item.setWidth(10.0);
        item.setHeight(40.0);
        QVariantList values;
        for (int index = 0; index < 100; ++index) {
            values.append(-0.5);
            values.append(0.5);
        }
        QVariantList channels{QVariant(values)};
        item.setChannelPeaks(channels);

        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        QVERIFY(item.generatedPointCount() <= 20);
        delete node;
    }

    void stereoUsesTheFullLogicalPixelBudgetPerChannel()
    {
        TestableAudioEditorWaveformItem item;
        item.setWidth(10.0);
        item.setHeight(80.0);
        QVariantList dense;
        for (int index = 0; index < 1'000; ++index) {
            dense.append(-0.75);
            dense.append(0.75);
        }
        item.setChannelPeaks({QVariant(dense), QVariant(dense)});

        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        QCOMPARE(item.generatedPointCount(), 40);
        delete node;
    }

    void devicePixelRatioScalesEachChannelBudget()
    {
        QQuickWindow window;
        QImage image(40, 160, QImage::Format_RGBA8888_Premultiplied);
        QQuickRenderTarget renderTarget = QQuickRenderTarget::fromPaintDevice(
            &image);
        renderTarget.setDevicePixelRatio(2.0);
        window.setRenderTarget(renderTarget);

        TestableAudioEditorWaveformItem item;
        item.setParentItem(window.contentItem());
        item.setWidth(10.0);
        item.setHeight(80.0);
        QVariantList dense;
        for (int index = 0; index < 100; ++index) {
            dense.append(-0.75);
            dense.append(0.75);
        }
        item.setChannelPeaks({QVariant(dense), QVariant(dense)});

        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        QCOMPARE(window.effectiveDevicePixelRatio(), 2.0);
        QCOMPARE(item.generatedPointCount(), 80);
        delete node;
    }

    void blankBucketsDoNotBridgeTimelineGaps()
    {
        TestableAudioEditorWaveformItem item;
        item.setWidth(100.0);
        item.setHeight(40.0);
        QVariantList values{-1.0, 1.0, QVariant{}, QVariant{}, -0.5, 0.5};
        item.setChannelPeaks({QVariant(values)});

        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        const auto* geometryNode = static_cast<QSGGeometryNode*>(node);
        QVERIFY(geometryNode->geometry()->vertexCount() > 4);
        const auto* vertices = geometryNode->geometry()->vertexDataAsPoint2D();
        for (int index = 0; index < geometryNode->geometry()->vertexCount(); ++index) {
            QVERIFY2(vertices[index].x < 34.0F || vertices[index].x > 65.0F,
                     "resampling must preserve the blank timeline gap");
        }
        QVERIFY(item.generatedPointCount() <= 200);
        delete node;
    }

    void downsampledBlankBucketsDoNotBridgeTimelineGaps()
    {
        TestableAudioEditorWaveformItem item;
        item.setWidth(2.0);
        item.setHeight(40.0);
        item.setDensity(2.0);
        item.setChannelPeaks({QVariant(QVariantList{
            -1.0, 1.0, QVariant{}, QVariant{},
            QVariant{}, QVariant{}, -0.5, 0.5})});

        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        const auto* geometryNode = static_cast<QSGGeometryNode*>(node);
        QCOMPARE(geometryNode->geometry()->vertexCount(), 0);
        QCOMPARE(item.generatedPointCount(), 0);
        delete node;
    }

    void zoomedSparseFramesAreResampledAcrossLogicalPixels()
    {
        TestableAudioEditorWaveformItem item;
        item.setWidth(100.0);
        item.setHeight(40.0);
        item.setDensity(2.0);
        item.setChannelPeaks({QVariant(QVariantList{
            -0.2, 0.2, -0.8, 0.8, -0.4, 0.4})});

        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        QVERIFY2(item.generatedPointCount() >= 190,
                 "zooming into a short range must not leave sparse peak rails");
        QVERIFY(item.generatedPointCount() <= 200);
        delete node;
    }

    void sampleModeConnectsEveryConsecutiveSampleAtHighZoom()
    {
        TestableAudioEditorWaveformItem item;
        item.setWidth(100.0);
        item.setHeight(40.0);
        item.setSampleMode(true);
        item.setChannelPeaks({QVariant(QVariantList{
            -1.0, -1.0, 0.0, 0.0, 1.0, 1.0})});

        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        const auto* geometryNode = static_cast<QSGGeometryNode*>(node);
        QCOMPARE(geometryNode->geometry()->drawingMode(),
                 QSGGeometry::DrawTriangles);
        QCOMPARE(geometryNode->geometry()->vertexCount(), 24);
        QCOMPARE(item.generatedPointCount(), 6);
        const auto* vertices = geometryNode->geometry()->vertexDataAsPoint2D();
        bool hasMiddleSample = false;
        for (int index = 0; index < geometryNode->geometry()->vertexCount();
             ++index) {
            hasMiddleSample = hasMiddleSample
                || std::abs(vertices[index].x - 50.0F) < 0.001F;
        }
        QVERIFY(hasMiddleSample);
        delete node;
    }

    void devicePixelRatioDoesNotExceedSharedWaveformBudget()
    {
        QQuickWindow window;
        QImage image(80, 320, QImage::Format_RGBA8888_Premultiplied);
        QQuickRenderTarget renderTarget = QQuickRenderTarget::fromPaintDevice(
            &image);
        renderTarget.setDevicePixelRatio(8.0);
        window.setRenderTarget(renderTarget);

        TestableAudioEditorWaveformItem item;
        item.setParentItem(window.contentItem());
        item.setWidth(10.0);
        item.setHeight(80.0);
        QVariantList dense;
        for (int index = 0; index < 100; ++index) {
            dense.append(-0.75);
            dense.append(0.75);
        }
        item.setChannelPeaks({QVariant(dense), QVariant(dense)});

        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        QCOMPARE(window.effectiveDevicePixelRatio(), 8.0);
        QCOMPARE(item.generatedPointCount(), 160);
        delete node;
    }

    void sparseFiniteBucketsInterpolateWithoutCrossingBlankBuckets()
    {
        TestableAudioEditorWaveformItem item;
        item.setWidth(5.0);
        item.setHeight(100.0);
        item.setChannelPeaks({QVariant(QVariantList{
            -0.2, 0.2, -0.8, 0.8, -0.4, 0.4})});

        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        const auto* geometryNode = static_cast<QSGGeometryNode*>(node);
        const auto* vertices = geometryNode->geometry()->vertexDataAsPoint2D();
        // The second rendered bucket is halfway between the first two finite
        // buckets.  Geometry is half-pixel aligned on this five-pixel item.
        // Nearest-neighbour replication leaves it at -0.2 instead of -0.5.
        bool sawInterpolatedTop = false;
        for (int index = 0; index < geometryNode->geometry()->vertexCount(); ++index) {
            if (std::abs(vertices[index].x - 1.5F) < 0.001F
                && std::abs(vertices[index].y - 27.0F) < 0.6F) {
                sawInterpolatedTop = true;
            }
        }
        QVERIFY2(sawInterpolatedTop,
                 "finite sparse buckets must interpolate rather than stair-step");
        delete node;
    }

    void exposesSharedPlayerStyleInputs()
    {
        AudioEditorWaveformItem item;
        item.setDensity(1.5);
        item.setLineWidth(2.0);
        item.setSampleMode(true);
        QCOMPARE(item.density(), 1.5);
        QCOMPARE(item.lineWidth(), 2.0);
        QCOMPARE(item.sampleMode(), true);
        const QMetaObject& meta = AudioEditorWaveformItem::staticMetaObject;
        QVERIFY(meta.indexOfProperty("sampleMode") >= 0);
        QVERIFY(meta.indexOfProperty("density") >= 0);
        QVERIFY(meta.indexOfProperty("lineWidth") >= 0);
    }

    void obsoleteSecondCropPropertiesAreAbsent()
    {
        const QMetaObject& meta = AudioEditorWaveformItem::staticMetaObject;
        QCOMPARE(meta.indexOfProperty("renderMode"), -1);
        QCOMPARE(meta.indexOfProperty("visibleStartRatio"), -1);
        QCOMPARE(meta.indexOfProperty("visibleEndRatio"), -1);
    }

    void rejectsOddAndNonFinitePeakPairs()
    {
        AudioEditorWaveformItem item;
        QVariantList channels;
        channels.append(QVariant(QVariantList{-1.0, 1.0, 0.5}));
        item.setChannelPeaks(channels);
        QVERIFY(item.channelPeaks().isEmpty());
        channels.clear();
        channels.append(QVariant(QVariantList{-1.0,
            std::numeric_limits<double>::infinity()}));
        item.setChannelPeaks(channels);
        QVERIFY(item.channelPeaks().isEmpty());
    }
};

QTEST_MAIN(AudioEditorWaveformItemTest)

#include "audio_editor_waveform_item_test.moc"

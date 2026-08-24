#include "audio_editor/audio_editor_waveform_item.hpp"

#include <QGuiApplication>
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
        QCOMPARE(geometry_node->geometry()->vertexCount(), 8);
        QCOMPARE(node->childCount(), 0);
        QCOMPARE(item.generatedPointCount(), 8);
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

    void highDensityStereoUsesOneTotalTwoPointsPerPixelBudget()
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
        QVERIFY(item.generatedPointCount() <= 20);
        delete node;
    }

    void stereoBudgetSkipsChannelsThatDoNotOwnALogicalPixel()
    {
        TestableAudioEditorWaveformItem item;
        item.setWidth(1.0);
        item.setHeight(80.0);
        QVariantList dense;
        for (int index = 0; index < 100; ++index) {
            dense.append(-0.75);
            dense.append(0.75);
        }
        item.setChannelPeaks({QVariant(dense), QVariant(dense)});

        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        QVERIFY(item.generatedPointCount() <= 2);
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

    void exposesSharedPlayerStyleInputs()
    {
        AudioEditorWaveformItem item;
        item.setDensity(1.5);
        item.setLineWidth(2.0);
        QCOMPARE(item.density(), 1.5);
        QCOMPARE(item.lineWidth(), 2.0);
        const QMetaObject& meta = AudioEditorWaveformItem::staticMetaObject;
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

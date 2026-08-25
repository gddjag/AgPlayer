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
    void rendersOneCenteredMixWhileRetainingStereoInput()
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
        QCOMPARE(item.channelPeaks().size(), 2);
        QCOMPARE(geometry_node->geometry()->vertexCount(), 4);
        QCOMPARE(node->childCount(), 0);
        QCOMPARE(item.generatedPointCount(), 4);
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

    void highDensityStereoSharesOneVisualLogicalPixelBudget()
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
        QCOMPARE(item.generatedPointCount(), 20);
        delete node;
    }

    void densityAndLineWidthConfigureTheCenteredMixBudget()
    {
        TestableAudioEditorWaveformItem item;
        item.setWidth(10.0);
        item.setHeight(80.0);
        item.setDensity(2.0);
        item.setLineWidth(3.0);
        QVariantList dense;
        for (int index = 0; index < 1'000; ++index) {
            dense.append(-0.75);
            dense.append(0.75);
        }
        item.setChannelPeaks({QVariant(dense), QVariant(dense)});

        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        QCOMPARE(item.density(), 2.0);
        QCOMPARE(item.lineWidth(), 3.0);
        QCOMPARE(item.generatedPointCount(), 40);
        const auto* geometryNode = static_cast<QSGGeometryNode*>(node);
        QCOMPARE(geometryNode->geometry()->lineWidth(), 3.0F);
        delete node;
    }

    void subpixelLineWidthAndDensityAboveTwoRemainEffective()
    {
        TestableAudioEditorWaveformItem item;
        item.setWidth(10.0);
        item.setHeight(80.0);
        item.setDensity(5.0);
        item.setLineWidth(0.3);
        QVariantList dense;
        for (int index = 0; index < 1'000; ++index) {
            dense.append(-0.75);
            dense.append(0.75);
        }
        item.setChannelPeaks({QVariant(dense), QVariant(dense)});

        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        QCOMPARE(item.density(), 5.0);
        QCOMPARE(item.lineWidth(), 0.3);
        QCOMPARE(item.generatedPointCount(), 100);
        const auto* geometryNode = static_cast<QSGGeometryNode*>(node);
        QCOMPARE(geometryNode->geometry()->lineWidth(), 0.3F);
        delete node;
    }

    void nonFiniteAppearanceValuesAreIgnored()
    {
        AudioEditorWaveformItem item;
        item.setDensity(3.0);
        item.setLineWidth(0.6);

        item.setDensity(std::numeric_limits<double>::quiet_NaN());
        item.setLineWidth(std::numeric_limits<double>::infinity());

        QCOMPARE(item.density(), 3.0);
        QCOMPARE(item.lineWidth(), 0.6);
    }

    void onePixelStereoRendersOneMixedEnvelope()
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
        QCOMPARE(item.generatedPointCount(), 2);
        delete node;
    }

    void oppositeStereoSamplesMixAtTheVisualCenter()
    {
        TestableAudioEditorWaveformItem item;
        item.setWidth(20.0);
        item.setHeight(100.0);
        item.setChannelPeaks({
            QVariant(QVariantList{-1.0, -1.0, 0.5, 0.5}),
            QVariant(QVariantList{1.0, 1.0, -0.5, -0.5})});

        QSGNode* node = item.updatePaintNode(nullptr, nullptr);
        QVERIFY(node != nullptr);
        const auto* geometryNode = static_cast<QSGGeometryNode*>(node);
        QCOMPARE(geometryNode->geometry()->vertexCount(), 4);
        const auto* vertices = geometryNode->geometry()->vertexDataAsPoint2D();
        for (int index = 0; index < 4; ++index) {
            QCOMPARE(vertices[index].y, 50.0F);
        }
        delete node;
    }

    void sceneGraphAntialiasingIsEnabled()
    {
        AudioEditorWaveformItem item;
        QVERIFY(item.antialiasing());
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
                 unsigned{QSGGeometry::DrawLines});
        QCOMPARE(geometryNode->geometry()->vertexCount(), 4);
        const auto* vertices = geometryNode->geometry()->vertexDataAsPoint2D();
        QCOMPARE(vertices[0].x, 0.0F);
        QCOMPARE(vertices[1].x, 50.0F);
        QCOMPARE(vertices[2].x, 50.0F);
        QCOMPARE(vertices[3].x, 100.0F);
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
        QCOMPARE(geometryNode->geometry()->vertexCount(), 4);
        QCOMPARE(item.generatedPointCount(), 4);
        delete node;
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

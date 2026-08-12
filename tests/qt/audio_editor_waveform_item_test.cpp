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
    void rendersBoundedStereoPeakGeometry()
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

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    objectName: "losslessEvidencePanel"
    property var result: ({})
    property int chartMode: 0
    signal spectrogramRequested()

    function numberValue(key, fallback) {
        if (!result || result[key] === undefined || result[key] === null)
            return fallback
        const value = Number(result[key])
        return isFinite(value) ? value : fallback
    }

    function frequencyText(value) {
        const hz = Number(value || 0)
        if (hz < 0)
            return qsTr("--")
        if (hz === 0)
            return qsTr("0 Hz")
        if (hz >= 1000)
            return (hz / 1000).toLocaleString(Qt.locale(), "f", hz % 1000 ? 1 : 0)
                    + " kHz"
        return Math.round(hz) + " Hz"
    }

    color: Theme.losslessWorkspaceSurface
    border.color: Theme.opaqueBorder
    border.width: 1
    radius: Theme.radiusSm
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            color: Theme.losslessPanelHeaderSurface
            border.color: Theme.opaqueBorder
            border.width: 1

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingMd
                height: parent.height
                spacing: Theme.spacingXs

                ThemedButton {
                    id: spectrumTab
                    objectName: "losslessSpectrumTab"
                    height: parent.height
                    compact: true
                    labelPixelSize: Theme.losslessFontSizeBody
                    text: qsTr("频谱证据")
                    onClicked: root.chartMode = 0
                    background: Rectangle {
                        color: spectrumTab.hovered ? Theme.surfaceHover : "transparent"
                        border.color: spectrumTab.activeFocus ? Theme.focus : "transparent"
                        border.width: spectrumTab.activeFocus ? 2 : 0
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 2
                            visible: root.chartMode === 0
                            color: Theme.accent
                        }
                    }
                }
                ThemedButton {
                    id: spectrogramTab
                    objectName: "losslessSpectrogramTab"
                    height: parent.height
                    compact: true
                    labelPixelSize: Theme.losslessFontSizeBody
                    text: qsTr("时频图")
                    onClicked: {
                        root.chartMode = 1
                        root.spectrogramRequested()
                    }
                    background: Rectangle {
                        color: spectrogramTab.hovered
                               ? Theme.surfaceHover : "transparent"
                        border.color: spectrogramTab.activeFocus
                                      ? Theme.focus : "transparent"
                        border.width: spectrogramTab.activeFocus ? 2 : 0
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 2
                            visible: root.chartMode === 1
                            color: Theme.accent
                        }
                    }
                }
            }
        }

        Flickable {
            id: evidenceViewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 190
            contentWidth: width
            contentHeight: Math.max(height, 360)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ThemedScrollBar {
                width: 8
                implicitWidth: 8
                visible: evidenceViewport.contentHeight
                         > evidenceViewport.height + 0.5
            }

            ColumnLayout {
                x: Theme.spacingMd
                y: Theme.spacingMd
                width: evidenceViewport.width - Theme.spacingMd * 2
                height: evidenceViewport.contentHeight - Theme.spacingLg * 2
                spacing: Theme.spacingSm

                Item {
                    id: chartFrame
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 150

                    Item {
                        id: plotArea
                        anchors.left: parent.left
                        anchors.leftMargin: 52
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.top: parent.top
                        anchors.topMargin: 34
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 32

                        LosslessEvidenceItem {
                            id: spectrumChart
                            objectName: "losslessSpectrumChart"
                            anchors.fill: parent
                            visible: root.chartMode === 0
                            mode: LosslessEvidenceItem.Spectrum
                            spectrum: root.result && root.result.spectrum
                                      ? root.result.spectrum : []
                            sampleRate: root.numberValue("sampleRate", 0)
                            cutoffHz: root.numberValue("cutoffHz", 0)
                            gridColor: Theme.losslessGrid
                            traceColor: Theme.losslessSpectrum
                            fillColor: Theme.losslessSpectrumFill
                        }

                        LosslessEvidenceItem {
                            id: spectrogramChart
                            objectName: "losslessSpectrogramChart"
                            anchors.fill: parent
                            visible: root.chartMode === 1
                            mode: LosslessEvidenceItem.Spectrogram
                            spectrogram: visible && root.result
                                         && root.result.spectrogram
                                         ? root.result.spectrogram : []
                            sampleRate: root.numberValue("sampleRate", 0)
                            gridColor: Theme.losslessGrid
                            traceColor: Theme.losslessSpectrum
                            fillColor: Theme.losslessSpectrumFill
                        }

                        Repeater {
                            model: 7
                            delegate: Text {
                                required property int index
                                x: -42
                                y: Math.max(-height / 2,
                                            plotArea.height * index / 6 - height / 2)
                                width: 36
                                horizontalAlignment: Text.AlignRight
                                text: (-20 * index) + " dB"
                                color: Theme.textSecondary
                                font.family: Theme.fontPrimary
                                font.pixelSize: Theme.losslessFontSizeMeta
                            }
                        }

                        Repeater {
                            model: 5
                            delegate: Text {
                                required property int index
                                x: Math.max(0, Math.min(plotArea.width - width,
                                    plotArea.width * index / 4 - width / 2))
                                y: plotArea.height + 5
                                text: index === 0 ? qsTr("0 Hz")
                                      : root.numberValue("sampleRate", 0) > 0
                                        ? root.frequencyText(
                                          root.numberValue("sampleRate", 0)
                                          * 0.5 * index / 4)
                                        : qsTr("--")
                                color: Theme.textSecondary
                                font.family: Theme.fontPrimary
                                font.pixelSize: Theme.losslessFontSizeMeta
                            }
                        }

                        Rectangle {
                            visible: root.chartMode === 0
                                     && root.numberValue("cutoffHz", 0) > 0
                                     && root.numberValue("sampleRate", 0) > 0
                            x: plotArea.width * root.numberValue("cutoffHz", 0)
                               / (root.numberValue("sampleRate", 0) * 0.5)
                            y: 0
                            width: 1
                            height: parent.height
                            color: Theme.losslessSpectrum
                            opacity: 0.72
                        }

                        Text {
                            visible: root.chartMode === 0
                                     && root.numberValue("cutoffHz", 0) > 0
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.topMargin: -20
                            text: qsTr("%1 截止").arg(root.frequencyText(
                                  root.numberValue("cutoffHz", 0)))
                            color: Theme.textPrimary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.losslessFontSizeMeta
                        }
                    }

                    Column {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - Theme.spacing2Xl, 320)
                        spacing: Theme.spacingSm
                        visible: root.chartMode === 0
                                 ? spectrumChart.spectrum.length === 0
                                 : spectrogramChart.spectrogram.length === 0

                        ThemedIcon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: Theme.iconSizeLg
                            height: width
                            source: Theme.icon("bar-chart-line")
                            tint: Theme.textTertiary
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: root.result && root.result.taskId
                                  ? qsTr("此项证据尚不可用")
                                  : qsTr("选择已完成任务以查看频谱证据")
                            color: Theme.textTertiary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.losslessFontSizeBody
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(188,
                                                     Math.max(140, root.height * 0.32))
                    color: Theme.losslessPanelHeaderSurface
                    border.color: Theme.opaqueBorder
                    border.width: 0
                    radius: Theme.radiusXs

                    Column {
                        anchors.fill: parent
                        Repeater {
                            model: [
                                { label: qsTr("高频硬截止"), value: root.numberValue("cutoffHz", 0) > 0 ? root.frequencyText(root.numberValue("cutoffHz", 0)) : qsTr("--") },
                                { label: qsTr("编码频谱空洞"), value: root.result && root.result.holesText ? root.result.holesText : qsTr("--") },
                                { label: qsTr("有效位深"), value: root.numberValue("effectiveBits", 0) > 0 ? qsTr("约 %1 bit").arg(root.numberValue("effectiveBits", 0).toLocaleString(Qt.locale(), "f", 1)) : qsTr("--") },
                                { label: qsTr("重采样痕迹"), value: root.result && root.result.resamplingText ? root.result.resamplingText : qsTr("--") }
                            ]
                            delegate: Rectangle {
                                required property int index
                                required property var modelData
                                width: parent.width
                                height: parent.height / 4
                                color: "transparent"
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 1
                                    visible: index < 3
                                    color: Theme.opaqueDivider
                                }
                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: Theme.spacingMd
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.label
                                    color: Theme.textSecondary
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: Theme.losslessFontSizeMeta
                                }
                                Text {
                                    anchors.right: parent.right
                                    anchors.rightMargin: Theme.spacingMd
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width * 0.31
                                    horizontalAlignment: Text.AlignLeft
                                    wrapMode: Text.Wrap
                                    text: modelData.value
                                    color: Theme.textPrimary
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: Theme.losslessFontSizeMeta
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

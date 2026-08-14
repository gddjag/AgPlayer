import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import AgPlayer

Rectangle {
    id: root
    objectName: "voiceCloneReferencePanel"
    color: Theme.panel
    border.color: Theme.border
    radius: Theme.radiusSm

    property alias referencePath: pathField.text
    property string previousReferencePath: ""
    property var waveformLayers: ({})
    property int waveformDurationMs: 0
    readonly property bool hasReference: pathField.text !== ""
    readonly property bool waveformReady: !!(waveformDurationMs > 0
                                             && waveformLayers.mix
                                             && waveformLayers.mix.length > 0)
    property url referenceUrl: ""
    readonly property bool previewingThis: hasReference
                                            && AudioPreviewController.isCurrentSource(
                                                referenceUrl)
    readonly property real waveformScale: scaleForPeaks(waveformLayers.mix || [])

    function formatTime(ms) {
        var seconds = Math.max(0, Math.floor(Number(ms) / 1000))
        var minutes = Math.floor(seconds / 60)
        seconds %= 60
        return (minutes < 10 ? "0" : "") + minutes + ":"
                + (seconds < 10 ? "0" : "") + seconds
    }

    function fileName(path) {
        const normalized = String(path).replace(/\\/g, "/")
        return normalized.substring(normalized.lastIndexOf("/") + 1)
    }

    function sameLocalPath(left, right) {
        return String(left).replace(/\\/g, "/").toLowerCase()
                === String(right).replace(/\\/g, "/").toLowerCase()
    }

    function scaleForPeaks(peaks) {
        var maximum = 0
        for (var index = 0; index < peaks.length; ++index)
            maximum = Math.max(maximum, Math.abs(Number(peaks[index]) || 0))
        return maximum > 0 ? Math.max(1, Math.min(12, 0.72 / maximum)) : 1
    }

    function loadWaveform() {
        waveformLayers = ({})
        waveformDurationMs = 0
        if (hasReference)
            WaveformProvider.loadForTrack(pathField.text)
    }

    onReferencePathChanged: {
        if (root.previousReferencePath !== ""
                && root.sameLocalPath(AudioPreviewController.sourcePath,
                                      root.previousReferencePath))
            AudioPreviewController.stop()
        root.previousReferencePath = pathField.text
        root.loadWaveform()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        Label {
            text: qsTr("1  参考人声")
            color: Theme.primaryText
            font.pixelSize: 17
            font.weight: Font.DemiBold
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.elevated
            border.color: Theme.border
            radius: Theme.radiusSm

            ColumnLayout {
                visible: !root.hasReference
                anchors.centerIn: parent
                width: parent.width - 28
                spacing: 8
                ThemedIcon {
                    Layout.alignment: Qt.AlignHCenter
                    source: Theme.icon("music-2-line")
                    tint: Theme.iconAccent
                    sourceSize.width: 30
                    sourceSize.height: 30
                }
                Label {
                    Layout.fillWidth: true
                    text: pathField.text === "" ? qsTr("选择参考音频文件") : qsTr("已选择真实文件路径")
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.primaryText
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("建议 3–30 秒；格式约束由当前模型提供")
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.secondaryText
                    font.pixelSize: 11
                }
            }

            ColumnLayout {
                visible: root.hasReference
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        objectName: "voiceCloneReferencePlayButton"
                        Layout.preferredWidth: 42
                        Layout.preferredHeight: 38
                        icon.source: Theme.icon(root.previewingThis
                                                && AudioPreviewController.playing
                                                ? "pause-fill" : "play-fill")
                        Accessible.name: root.previewingThis
                                         && AudioPreviewController.playing
                                         ? qsTr("暂停参考人声")
                                         : qsTr("播放参考人声")
                        onClicked: AudioPreviewController.toggle(root.referenceUrl)
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Label {
                            Layout.fillWidth: true
                            text: root.fileName(pathField.text)
                            color: Theme.primaryText
                            font.weight: Font.DemiBold
                            elide: Text.ElideMiddle
                        }
                        Label {
                            text: qsTr("已加载参考人声 · %1").arg(
                                      root.formatTime(root.waveformDurationMs))
                            color: Theme.secondaryText
                            font.pixelSize: 11
                        }
                    }
                }

                WaveformItem {
                    id: referenceWaveform
                    objectName: "voiceCloneReferenceWaveform"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 60
                    layers: root.waveformLayers
                    position: 0
                    cursorPosition: root.previewingThis
                                    ? AudioPreviewController.positionMs : 0
                    duration: root.waveformDurationMs > 0
                              ? root.waveformDurationMs
                              : (root.previewingThis
                                 ? AudioPreviewController.durationMs : 0)
                    analysisProgress: WaveformProvider.analysisProgress
                    baseColor: Theme.waveformViolet
                    progressColor: Theme.waveformCyan
                    amplitudeScale: root.waveformScale
                    onSeekRequested: positionMs => {
                        if (root.previewingThis)
                            AudioPreviewController.seek(positionMs)
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("建议 3–30 秒；格式约束由当前模型提供")
                    color: Theme.secondaryText
                    font.pixelSize: 11
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: pathField
                objectName: "voiceCloneReferencePathField"
                Layout.fillWidth: true
                placeholderText: qsTr("输入或粘贴本地音频路径")
                Accessible.name: qsTr("参考音频路径")
                onTextChanged: if (!activeFocus) cursorPosition = 0
            }
            ToolButton {
                objectName: "voiceCloneReferenceBrowseButton"
                icon.source: Theme.icon("folder-open-line")
                icon.color: Theme.iconPrimary
                Accessible.name: qsTr("浏览参考音频")
                onClicked: referenceDialog.open()
            }
        }
    }

    Connections {
        target: WaveformProvider
        function onWaveformReady(path, layers) {
            if (!root.sameLocalPath(path, pathField.text))
                return
            root.waveformLayers = {
                mix: layers.mix || [],
                _sampleRate: Number(layers._sampleRate) || 0,
                _totalSamples: Number(layers._totalSamples) || 0,
                _peakCount: Number(layers._peakCount) || 0
            }
            root.waveformDurationMs = Math.max(
                        0, Number(layers._durationMs) || 0)
        }
    }

    Component.onCompleted: {
        root.previousReferencePath = pathField.text
        root.loadWaveform()
    }
    Component.onDestruction: {
        if (AudioPreviewController.isCurrentSource(root.referenceUrl))
            AudioPreviewController.stop()
    }

    FileDialog {
        id: referenceDialog
        title: qsTr("选择参考音频")
        nameFilters: [qsTr("音频文件 (*.wav *.mp3 *.flac *.m4a)"), qsTr("所有文件 (*)")]
        onAccepted: pathField.text = decodeURIComponent(
                        selectedFile.toString().replace(/^file:\/\/\//, ""))
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import AgPlayer

Rectangle {
    id: root
    objectName: "voiceCloneResultPanel"
    color: Theme.panel
    border.color: Theme.border
    radius: Theme.radiusSm
    implicitHeight: 190

    property string resultPath: ""
    property string waveformRequestId: ""
    property string previousResultPath: ""
    property url resultUrl: ""
    property var waveformLayers: ({})
    property int waveformDurationMs: 0
    readonly property bool hasResult: resultPath !== ""
    readonly property bool waveformReady: !!(waveformDurationMs > 0
                                             && waveformLayers
                                             && waveformLayers.mix
                                             && waveformLayers.mix.length > 0)
    readonly property bool previewingThis: root.hasResult
                                            && AudioPreviewController.isCurrentSource(
                                                root.resultUrl)
    readonly property real waveformScale: root.scaleForPeaks(
                                               root.waveformLayers.mix || [])
    signal saveRequested(string path, string destinationPath)
    signal deleteRequested(string path)
    signal sendToEditorRequested(string path)

    function formatTime(ms) {
        var seconds = Math.max(0, Math.floor(Number(ms) / 1000))
        var minutes = Math.floor(seconds / 60)
        seconds %= 60
        return (minutes < 10 ? "0" : "") + minutes + ":"
                + (seconds < 10 ? "0" : "") + seconds
    }

    function scaleForPeaks(peaks) {
        var maximum = 0
        for (var index = 0; index < peaks.length; ++index)
            maximum = Math.max(maximum, Math.abs(Number(peaks[index]) || 0))
        return maximum > 0 ? Math.max(1, Math.min(12, 0.72 / maximum)) : 1
    }

    function sameLocalPath(left, right) {
        return String(left).replace(/\\/g, "/").toLowerCase()
                === String(right).replace(/\\/g, "/").toLowerCase()
    }

    function loadWaveform() {
        WaveformProvider.cancelRequest(root.waveformRequestId)
        root.waveformLayers = ({})
        root.waveformDurationMs = 0
        if (root.resultPath !== "")
            WaveformProvider.loadForRequest(root.waveformRequestId,
                                            root.resultPath)
    }

    onResultPathChanged: {
        if (root.previousResultPath !== ""
                && root.sameLocalPath(AudioPreviewController.sourcePath,
                                      root.previousResultPath))
            AudioPreviewController.stop()
        root.previousResultPath = root.resultPath
        root.loadWaveform()
    }

    function saveTo(destinationPath) {
        if (!hasResult || destinationPath === "") return
        saveRequested(resultPath, destinationPath)
        saveDialog.close()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10
        Label {
            text: qsTr("5  生成结果")
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
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Button {
                        id: playButton
                        objectName: "voiceCloneResultPlayButton"
                        visible: root.hasResult
                        enabled: root.resultUrl.toString() !== ""
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 40
                        icon.source: Theme.icon(root.previewingThis
                                                && AudioPreviewController.playing
                                                ? "pause-fill" : "play-fill")
                        Accessible.name: root.previewingThis
                                         && AudioPreviewController.playing
                                         ? qsTr("暂停生成结果")
                                         : qsTr("播放生成结果")
                        onClicked: AudioPreviewController.toggle(root.resultUrl)
                    }
                    ThemedIcon {
                        visible: !root.hasResult
                        source: Theme.icon("information-line")
                        tint: Theme.iconSecondary
                        sourceSize.width: 28
                        sourceSize.height: 28
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: root.hasResult ? qsTr("已生成真实音频文件") : qsTr("暂无生成结果")
                            color: Theme.primaryText
                            font.weight: Font.DemiBold
                        }
                        Label {
                            Layout.fillWidth: true
                            text: root.hasResult ? root.resultPath : qsTr("生成成功后在此显示实际文件；不绘制占位波形")
                            color: Theme.secondaryText
                            elide: Text.ElideMiddle
                        }
                    }
                    Button {
                        objectName: "voiceCloneSendToEditorButton"
                        visible: root.hasResult
                        text: qsTr("发送到剪辑")
                        onClicked: root.sendToEditorRequested(root.resultPath)
                    }
                    Button {
                        objectName: "voiceCloneSaveResultButton"
                        visible: root.hasResult
                        text: qsTr("保存文件")
                        icon.source: Theme.icon("download-line")
                        onClicked: saveDialog.open()
                    }
                    Button {
                        objectName: "voiceCloneDeleteResultButton"
                        visible: root.hasResult
                        text: qsTr("删除")
                        icon.source: Theme.icon("delete-bin-line")
                        onClicked: root.deleteRequested(root.resultPath)
                    }
                }

                Item {
                    visible: root.hasResult
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 42

                    WaveformItem {
                        id: resultWaveform
                        objectName: "voiceCloneResultWaveform"
                        anchors.fill: parent
                        layers: root.waveformLayers
                        position: 0
                        cursorPosition: root.previewingThis
                                        ? AudioPreviewController.positionMs : 0
                        duration: root.waveformDurationMs > 0
                                  ? root.waveformDurationMs
                                  : (root.previewingThis
                                     ? AudioPreviewController.durationMs : 0)
                        analysisProgress: root.waveformReady ? 1 : 0
                        baseColor: Theme.waveformViolet
                        progressColor: Theme.waveformCyan
                        gradientStartColor: Theme.waveformCyan
                        gradientMiddleColor: Theme.waveformViolet
                        gradientEndColor: Theme.waveformMagenta
                        amplitudeScale: root.waveformScale
                        onSeekRequested: positionMs => {
                            if (root.previewingThis)
                                AudioPreviewController.seek(positionMs)
                        }
                    }
                }

                Label {
                    visible: root.hasResult
                    Layout.alignment: Qt.AlignRight
                    text: root.formatTime(root.previewingThis
                                          ? AudioPreviewController.positionMs : 0)
                          + " / " + root.formatTime(root.waveformDurationMs)
                    color: Theme.secondaryText
                    font.pixelSize: 11
                }
            }
        }
    }

    Connections {
        target: WaveformProvider
        function onRequestWaveformReady(requestId, path, layers) {
            if (requestId !== root.waveformRequestId
                    || !root.sameLocalPath(path, root.resultPath))
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
        root.waveformRequestId = root.objectName + ":" + String(root)
        root.previousResultPath = root.resultPath
        root.loadWaveform()
    }
    Component.onDestruction: {
        if (root.waveformRequestId !== "")
            WaveformProvider.cancelRequest(root.waveformRequestId)
        if (AudioPreviewController.isCurrentSource(root.resultUrl))
            AudioPreviewController.stop()
    }

    FileDialog {
        id: saveDialog
        objectName: "voiceCloneSaveDialog"
        title: qsTr("保存生成人声")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "wav"
        nameFilters: [qsTr("WAV 音频 (*.wav)")]
        onAccepted: root.saveTo(decodeURIComponent(
                        selectedFile.toString().replace(/^file:\/\/\//, "")))
    }
}

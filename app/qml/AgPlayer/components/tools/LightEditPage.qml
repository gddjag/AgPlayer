import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: page
    objectName: "lightEditPage"
    color: Theme.background

    property var editor: LightEditor
    property real zoomScale: 1.0
    property int viewportOffsetMs: 0
    property int playheadMs: 0
    property bool keepPitch: editor.keepPitch
    property real pixelsPerMs: 0.0035
    property int selectedTrack: editor.selectedTrack

    readonly property var trackColors: [
        Theme.waveformRed,
        Theme.waveformBlue,
        Theme.waveformGreen,
        Theme.waveformCyan,
        Theme.waveformViolet,
        Theme.waveformMagenta
    ]

    function formatTime(ms) {
        const total = Math.max(0, Math.floor(ms / 1000))
        const minutes = Math.floor(total / 60)
        const seconds = total % 60
        return (minutes < 10 ? "0" : "") + minutes + ":"
               + (seconds < 10 ? "0" : "") + seconds
    }

    function projectDurationMs() {
        let duration = 0
        const values = editor.tracks
        for (let i = 0; i < values.length; ++i) {
            const track = values[i]
            if (!track.hasFile) continue
            duration = Math.max(duration, track.timelineStartMs
                                + Math.max(0, track.outMs - track.inMs))
        }
        return Math.max(duration, 300000)
    }

    function applyWheelZoom(delta, anchorX) {
        const oldScale = zoomScale
        const anchorTime = viewportOffsetMs
                         + anchorX / (pixelsPerMs * oldScale)
        const factor = delta > 0 ? 1.15 : 1 / 1.15
        zoomScale = Math.max(0.35, Math.min(12.0, oldScale * factor))
        viewportOffsetMs = Math.max(0, Math.round(anchorTime
                           - anchorX / (pixelsPerMs * zoomScale)))
    }

    function panTimeline(deltaPixels) {
        viewportOffsetMs = Math.max(0, Math.round(viewportOffsetMs
                           + deltaPixels / (pixelsPerMs * zoomScale)))
    }

    Component {
        id: fileDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFile
            nameFilters: [qsTr("音频文件 (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)")]
            onAccepted: editor.loadFileToTrack(editor.selectedTrack, selectedFile)
        }
    }

    Component {
        id: outputDirDialogComponent
        FolderDialog {
            onAccepted: outputDirectory.text = selectedFolder.toString().replace(/^file:\/+/, "")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10

                Button {
                    objectName: "addFileButton"
                    text: qsTr("添加文件")
                    icon.source: Theme.icon("add-line")
                    onClicked: {
                        const dialog = fileDialogComponent.createObject(page)
                        dialog.open()
                    }
                }

                ComboBox {
                    Layout.preferredWidth: 175
                    model: Array.from({length: editor.trackCount},
                                      function(_, index) { return qsTr("主轨道：轨道 %1").arg(index + 1) })
                    currentIndex: editor.selectedTrack
                    onActivated: editor.selectedTrack = currentIndex
                }

                Text {
                    text: qsTr("目标 BPM")
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                }

                ToolButton {
                    icon.source: Theme.icon("subtract-line")
                    icon.color: Theme.iconPrimary
                    onClicked: editor.targetBpm = Math.max(40, editor.targetBpm - 1)
                }

                TextField {
                    id: targetBpmField
                    objectName: "targetBpmField"
                    Layout.preferredWidth: 58
                    horizontalAlignment: Text.AlignHCenter
                    text: Math.round(editor.targetBpm).toString()
                    validator: IntValidator { bottom: 40; top: 240 }
                    onEditingFinished: editor.targetBpm = Number(text)
                }

                ToolButton {
                    icon.source: Theme.icon("add-line")
                    icon.color: Theme.iconPrimary
                    onClicked: editor.targetBpm = Math.min(240, editor.targetBpm + 1)
                }

                Text {
                    text: qsTr("保持音高")
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                }

                Switch {
                    id: keepPitchSwitch
                    objectName: "keepPitchSwitch"
                    checked: page.keepPitch
                    onToggled: editor.keepPitch = checked
                }

                Text {
                    text: qsTr("吸附网格")
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                }

                ComboBox {
                    id: snapGridBox
                    objectName: "snapGridBox"
                    Layout.preferredWidth: 95
                    model: [qsTr("1/4 拍"), qsTr("1/2 拍"), qsTr("1 拍")]
                    currentIndex: 0
                    onActivated: editor.snapEnabled = true
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: unifyBpmButton
                    objectName: "unifyBpmButton"
                    text: qsTr("统一 BPM")
                    enabled: !editor.busy
                    onClicked: editor.unifyBpm(false)
                }

                Button {
                    id: alignBpmButton
                    objectName: "alignBpmButton"
                    text: qsTr("BPM + 节拍对齐")
                    highlighted: true
                    enabled: !editor.busy
                    onClicked: editor.unifyBpm(true)
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: Theme.panel
            border.color: Theme.border
            border.width: 1

            Item {
                anchors.fill: parent
                anchors.leftMargin: 214
                clip: true

                Canvas {
                    id: ruler
                    anchors.fill: parent
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.fillStyle = Theme.primaryText
                        ctx.strokeStyle = Theme.border
                        ctx.font = "11px '" + Theme.fontFallback + "'"
                        const interval = 30000
                        const end = page.viewportOffsetMs
                                  + width / (page.pixelsPerMs * page.zoomScale)
                        let start = Math.floor(page.viewportOffsetMs / interval) * interval
                        for (let time = start; time <= end; time += interval) {
                            const x = (time - page.viewportOffsetMs)
                                    * page.pixelsPerMs * page.zoomScale
                            ctx.beginPath()
                            ctx.moveTo(x, 19)
                            ctx.lineTo(x, height)
                            ctx.stroke()
                            ctx.fillText(page.formatTime(time), x + 4, 14)
                        }
                    }
                    Connections {
                        target: page
                        function onZoomScaleChanged() { ruler.requestPaint() }
                        function onViewportOffsetMsChanged() { ruler.requestPaint() }
                    }
                }

                Rectangle {
                    x: (page.playheadMs - page.viewportOffsetMs)
                       * page.pixelsPerMs * page.zoomScale
                    y: 0
                    width: 2
                    height: parent.height
                    color: Theme.cyan
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 330
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm
            clip: true

            Flickable {
                id: tracksViewport
                anchors.fill: parent
                anchors.margins: 6
                contentWidth: width
                contentHeight: tracksColumn.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: tracksColumn
                    width: tracksViewport.width
                    height: childrenRect.height
                    spacing: 6

                    Repeater {
                        model: editor.tracks

                        MultiTrackWaveform {
                            width: tracksColumn.width
                            trackIndex: index
                            track: modelData
                            trackColor: page.trackColors[index]
                            pixelsPerMs: page.pixelsPerMs
                            zoomScale: page.zoomScale
                            viewportOffsetMs: page.viewportOffsetMs
                            playheadMs: page.playheadMs
                            selected: editor.selectedTrack === index

                            onTrackClicked: editor.selectedTrack = index
                            onClipMoveRequested: function(trackIndex, timelineStartMs) {
                                editor.moveClip(trackIndex, timelineStartMs)
                            }
                            onClipTrimRequested: function(trackIndex, inMs, outMs) {
                                editor.trimClip(trackIndex, inMs, outMs)
                            }
                            onSeekRequested: function(positionMs) {
                                page.playheadMs = Math.max(0, positionMs)
                            }

                            DropArea {
                                anchors.fill: parent
                                keys: ["text/uri-list"]
                                onDropped: function(drop) {
                                    if (drop.hasUrls && drop.urls.length > 0)
                                        editor.loadFileToTrack(index, drop.urls[0])
                                }
                            }
                        }
                    }
                }

                WheelHandler {
                    target: null
                    onWheel: function(event) {
                        if ((event.modifiers & Qt.ShiftModifier) !== 0)
                            page.panTimeline(-event.angleDelta.y)
                        else
                            page.applyWheelZoom(event.angleDelta.y,
                                                event.x - 214)
                        event.accepted = true
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 76
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 4

                Repeater {
                    model: [
                        { text: qsTr("撤销"), icon: "arrow-go-back-line", enabled: editor.canUndo,
                          action: function() { editor.undo() } },
                        { text: qsTr("重做"), icon: "arrow-go-forward-line", enabled: editor.canRedo,
                          action: function() { editor.redo() } },
                        { text: qsTr("剪切"), icon: "scissors-cut-line", enabled: false,
                          action: function() {} },
                        { text: qsTr("复制"), icon: "file-copy-line", enabled: false,
                          action: function() {} },
                        { text: qsTr("删除"), icon: "delete-bin-line", enabled: false,
                          action: function() {} },
                        { text: qsTr("分割"), icon: "split-cells-horizontal", enabled: false,
                          action: function() {} },
                        { text: qsTr("合并"), icon: "merge-cells-horizontal", enabled: false,
                          action: function() {} },
                        { text: qsTr("静音"), icon: "volume-mute-line", enabled: true,
                          action: function() {
                              const track = editor.tracks[editor.selectedTrack]
                              editor.setTrackMuted(editor.selectedTrack, !track.muted)
                          } },
                        { text: qsTr("裁剪"), icon: "crop-line", enabled: false,
                          action: function() {} }
                    ]

                    ToolButton {
                        enabled: modelData.enabled && !editor.busy
                        implicitWidth: 62
                        implicitHeight: 60
                        icon.source: Theme.icon(modelData.icon)
                        icon.color: enabled ? Theme.iconPrimary : Theme.iconSecondary
                        icon.width: 20
                        icon.height: 20
                        text: modelData.text
                        display: AbstractButton.TextUnderIcon
                        onClicked: modelData.action()
                    }
                }

                Item { Layout.fillWidth: true }

                ToolButton {
                    icon.source: Theme.icon("volume-up-fill")
                    icon.color: Theme.iconPrimary
                }
                Slider {
                    Layout.preferredWidth: 160
                    from: 0
                    to: 1
                    value: 0.68
                }
                Text {
                    text: page.formatTime(page.playheadMs) + " / "
                          + page.formatTime(page.projectDurationMs())
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 130
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm

                GridLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    columns: 6
                    rowSpacing: 8
                    columnSpacing: 10

                    Text { text: qsTr("导出设置"); color: Theme.primaryText; font.pixelSize: 14; Layout.columnSpan: 6 }
                    Text { text: qsTr("输出格式"); color: Theme.secondaryText }
                    ComboBox {
                        id: outputFormatBox
                        objectName: "outputFormatBox"
                        model: ["MP3", "WAV", "FLAC"]
                        currentIndex: 0
                        Layout.preferredWidth: 150
                    }
                    Text { text: qsTr("采样率"); color: Theme.secondaryText }
                    ComboBox {
                        id: outputSampleRateBox
                        objectName: "outputSampleRateBox"
                        model: ["44,100 Hz", "48,000 Hz", "96,000 Hz"]
                        Layout.preferredWidth: 160
                    }
                    Text { text: qsTr("声道"); color: Theme.secondaryText }
                    ComboBox {
                        id: outputChannelBox
                        objectName: "outputChannelBox"
                        model: [qsTr("立体声"), qsTr("单声道")]
                        Layout.preferredWidth: 130
                    }
                    Text { text: qsTr("输出目录"); color: Theme.secondaryText }
                    TextField {
                        id: outputDirectory
                        text: SettingsController.defaultOutputDirectory
                        Layout.columnSpan: 4
                        Layout.fillWidth: true
                    }
                    Button {
                        text: qsTr("浏览")
                        onClicked: {
                            const dialog = outputDirDialogComponent.createObject(page)
                            dialog.open()
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 360
                Layout.fillHeight: true
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 14

                    ColumnLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("项目信息"); color: Theme.primaryText; font.pixelSize: 14 }
                        Text {
                            text: qsTr("轨道数量：%1 / 6").arg(editor.tracks.filter(
                                function(track) { return track.hasFile }).length)
                            color: Theme.secondaryText
                        }
                        Text {
                            text: qsTr("项目时长：%1").arg(page.formatTime(page.projectDurationMs()))
                            color: Theme.secondaryText
                        }
                        Text {
                            text: qsTr("目标 BPM：%1").arg(Math.round(editor.targetBpm))
                            color: Theme.secondaryText
                        }
                    }

                    Button {
                        objectName: "exportAudioButton"
                        text: editor.busy ? qsTr("处理中…") : qsTr("导出音频")
                        icon.source: Theme.icon("download-line")
                        highlighted: true
                        enabled: !editor.busy && editor.tracks.some(
                            function(track) { return track.hasFile })
                        onClicked: {
                            const rates = [44100, 48000, 96000]
                            editor.exportProject(
                                outputDirectory.text,
                                outputFormatBox.currentText.toLowerCase(),
                                rates[outputSampleRateBox.currentIndex],
                                outputChannelBox.currentIndex === 0 ? 2 : 1)
                        }
                    }
                }
            }
        }
    }
}

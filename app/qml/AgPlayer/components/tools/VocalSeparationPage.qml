import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: page
    objectName: "vocalSeparationPage"
    color: "#000E1D"
    focus: true

    readonly property bool fullDesktop: width >= 1440
    readonly property bool desktop: width >= 1100
    readonly property bool compact: !desktop
    readonly property color surface: "#001122"
    readonly property color raised: "#001426"
    readonly property color input: "#001020"
    readonly property color border: "#123047"
    readonly property color divider: "#0B273B"
    readonly property color textPrimary: "#E5EEF7"
    readonly property color muted: "#8C9DAE"
    readonly property color cyan: "#00C7F4"
    readonly property color primary: "#075EED"
    readonly property color success: "#00D978"
    readonly property bool hasInput: VocalSeparationController.inputInfo.name !== undefined
                                  && VocalSeparationController.inputInfo.name.length > 0
    property int compactTab: 0
    property string playlistDiagnostic: ""

    function modelSupports(modelId, stem) {
        const expected = stem === "vocals" ? VocalSeparationController.Vocals
                       : stem === "instrumental" ? VocalSeparationController.Accompaniment
                       : stem === "drums" ? VocalSeparationController.Drums
                       : stem === "bass" ? VocalSeparationController.Bass
                       : VocalSeparationController.Other
        for (let index = 0; index < VocalSeparationController.models.length; ++index) {
            const model = VocalSeparationController.models[index]
            if (model.id === modelId)
                return model.stems.indexOf(expected) >= 0
        }
        return false
    }

    function stemInfo(kind) {
        for (let index = 0; index < VocalSeparationController.stems.length; ++index) {
            const stem = VocalSeparationController.stems[index]
            if (stem.kind === kind)
                return stem
        }
        return ({ supported: false, selected: false, available: false,
                    waveform: [], path: "", derived: false })
    }

    function formatBytes(bytes) {
        if (!bytes || bytes <= 0) return ""
        if (bytes >= 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MB"
        return Math.ceil(bytes / 1024) + " KB"
    }

    function modelStateText(state) {
        switch (state) {
        case VocalSeparationController.NotInstalled: return qsTr("未下载")
        case VocalSeparationController.Downloading: return qsTr("下载中")
        case VocalSeparationController.Paused: return qsTr("已暂停")
        case VocalSeparationController.Verifying: return qsTr("校验中")
        case VocalSeparationController.Installed: return qsTr("已安装")
        case VocalSeparationController.Failed: return qsTr("下载失败")
        }
        return qsTr("未知状态")
    }

    function jobStateText() {
        switch (VocalSeparationController.jobState) {
        case VocalSeparationController.Probing: return qsTr("正在探测设备")
        case VocalSeparationController.Running: return qsTr("正在分离")
        case VocalSeparationController.Cancelling: return qsTr("正在取消")
        case VocalSeparationController.Completed: return qsTr("分离完成")
        case VocalSeparationController.Cancelled: return qsTr("已取消")
        case VocalSeparationController.Failed: return qsTr("分离失败")
        }
        return qsTr("等待输入")
    }

    FileDialog {
        id: inputDialog
        title: qsTr("选择音频文件")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("音频文件 (*.wav *.flac *.m4a *.aac *.mp3 *.ogg *.opus)")]
        onAccepted: VocalSeparationController.selectInput(selectedFile)
    }

    FolderDialog {
        id: outputDialog
        title: qsTr("选择输出目录")
        onAccepted: VocalSeparationController.selectOutputDirectory(selectedFolder)
    }

    Connections {
        target: VocalSeparationController
        function onPlaylistOperationFinished(success, diagnostic) {
            page.playlistDiagnostic = diagnostic
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TabBar {
            id: compactTabs
            objectName: "separationCompactTabs"
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 8
            visible: page.compact
            currentIndex: page.compactTab
            onCurrentIndexChanged: page.compactTab = currentIndex
            background: Rectangle { color: page.raised; radius: 6 }
            TabButton { text: qsTr("工作台") ; Accessible.name: text }
            TabButton { text: qsTr("输出设置") ; Accessible.name: text }
            TabButton { text: qsTr("分离记录") ; Accessible.name: text }
        }

        Flickable {
            id: scroll
            objectName: "separationScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: page.compact ? 8 : 0
            clip: true
            contentWidth: width
            contentHeight: workbench.implicitHeight + 16
            boundsBehavior: Flickable.StopAtBounds

            RowLayout {
                id: workbench
                width: scroll.width - 28
                x: 14
                y: 8
                spacing: 8

                ColumnLayout {
                    id: workspace
                    Layout.fillWidth: true
                    Layout.preferredWidth: page.desktop
                                           ? workbench.width - (page.fullDesktop ? 474 : 336) - 8
                                           : workbench.width
                    visible: !page.compact || page.compactTab === 0
                    spacing: 8

                    Rectangle {
                        objectName: "separationErrorPanel"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        visible: VocalSeparationController.error.length > 0
                        color: "#2B1220"
                        border.color: "#8F3550"
                        radius: 6
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            Label {
                                Layout.fillWidth: true
                                text: VocalSeparationController.error
                                color: page.textPrimary
                                elide: Text.ElideRight
                            }
                            Button {
                                text: qsTr("重试")
                                enabled: VocalSeparationController.jobState === VocalSeparationController.Failed
                                Accessible.name: text
                                Accessible.role: Accessible.Button
                                ToolTip.visible: hovered && !enabled
                                ToolTip.text: qsTr("当前错误不可重试")
                                onClicked: VocalSeparationController.retry()
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 184
                        spacing: 8

                        Rectangle {
                            id: inputPanel
                            objectName: "separationInputPanel"
                            Layout.preferredWidth: page.fullDesktop ? 296 : 250
                            Layout.fillHeight: true
                            color: page.surface
                            border.color: page.border
                            radius: 7

                            FileDropArea {
                                anchors.fill: parent
                                anchors.margins: 8
                                onUrlsDropped: function(urls) {
                                    VocalSeparationController.dropInput(urls)
                                }
                            }

                            ColumnLayout {
                                anchors.centerIn: parent
                                width: parent.width - 26
                                spacing: 8
                                ThemedIcon {
                                    source: Theme.icon("folder-open-line")
                                    tint: page.cyan
                                    sourceSize.width: 34; sourceSize.height: 34
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.preferredWidth: 34; Layout.preferredHeight: 34
                                }
                                Label {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: VocalSeparationController.inputInfo.name
                                          ? qsTr("已选择输入") : qsTr("拖拽音频文件到此处")
                                    color: page.textPrimary
                                    font.pixelSize: 15
                                    wrapMode: Text.Wrap
                                }
                                Label {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: qsTr("WAV / FLAC / M4A / MP3")
                                    color: page.muted
                                    font.pixelSize: 12
                                }
                                Button {
                                    text: qsTr("选择文件")
                                    Layout.alignment: Qt.AlignHCenter
                                    focusPolicy: Qt.StrongFocus
                                    Accessible.name: text
                                    Accessible.role: Accessible.Button
                                    onClicked: inputDialog.open()
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("选择一个本地音频文件")
                                }
                            }
                        }

                        Rectangle {
                            id: inputPreview
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: page.input
                            border.color: page.border
                            radius: 7

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 12
                                Image {
                                    Layout.preferredWidth: 82; Layout.preferredHeight: 82
                                    source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
                                    fillMode: Image.PreserveAspectFit
                                    visible: page.hasInput
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    spacing: 5
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            Layout.fillWidth: true
                                            text: VocalSeparationController.inputInfo.name
                                                  || qsTr("尚未选择输入文件")
                                            color: page.textPrimary
                                            elide: Text.ElideRight
                                            font.pixelSize: 18
                                        }
                                        Button {
                                            visible: page.hasInput
                                            flat: true
                                            text: qsTr("清除")
                                            Accessible.name: text
                                            Accessible.role: Accessible.Button
                                            onClicked: VocalSeparationController.clearInput()
                                        }
                                    }
                                    Label {
                                        text: VocalSeparationController.inputInfo.name
                                              ? page.formatBytes(VocalSeparationController.inputInfo.bytes)
                                                + "  ·  "
                                                + Math.floor(VocalSeparationController.inputInfo.durationMs / 60000)
                                                + ":" + String(Math.floor((VocalSeparationController.inputInfo.durationMs / 1000) % 60)).padStart(2, "0")
                                              : qsTr("选择文件后将分析真实波形")
                                        color: page.muted
                                        font.pixelSize: 12
                                    }
                                    Item {
                                        id: inputWaveform
                                        objectName: "separationInputWaveform"
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        property var peaks: VocalSeparationController.inputInfo.waveform || []
                                        Repeater {
                                            model: Math.min(inputWaveform.peaks.length, 220)
                                            Rectangle {
                                                readonly property int peakIndex: Math.floor(index * inputWaveform.peaks.length / Math.max(1, model))
                                                width: Math.max(1, inputWaveform.width / Math.max(1, model) - 1)
                                                height: Math.max(2, inputWaveform.height * Math.min(1, Number(inputWaveform.peaks[peakIndex] || 0)))
                                                x: index * (inputWaveform.width / Math.max(1, model))
                                                anchors.verticalCenter: parent.verticalCenter
                                                color: page.cyan
                                                opacity: 0.85
                                            }
                                        }
                                        Label {
                                            anchors.centerIn: parent
                                            visible: inputWaveform.peaks.length === 0
                                            text: VocalSeparationController.inputInfo.name
                                                  ? qsTr("正在分析真实波形…") : qsTr("输入波形")
                                            color: page.muted
                                        }
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        id: modelDeck
                        objectName: "separationModelDeck"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 233
                        spacing: 8
                        Repeater {
                            model: VocalSeparationController.models
                            Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: VocalSeparationController.selectedModelId === modelData.id
                                       ? "#062039" : page.surface
                                border.color: VocalSeparationController.selectedModelId === modelData.id
                                              ? page.cyan : page.border
                                radius: 7
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: 12
                                    spacing: 5
                                    Label { text: modelData.id; color: page.textPrimary; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Label { text: modelData.family.toUpperCase() + " · " + page.formatBytes(modelData.bytes); color: page.muted; font.pixelSize: 12; Layout.fillWidth: true }
                                    Label { text: modelData.provenance; color: page.muted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.fillHeight: true }
                                    Label { text: page.modelStateText(modelData.state); color: modelData.state === VocalSeparationController.Installed ? page.success : page.cyan; font.pixelSize: 12 }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Button {
                                            text: VocalSeparationController.selectedModelId === modelData.id ? qsTr("当前模型") : qsTr("选择")
                                            enabled: VocalSeparationController.selectedModelId !== modelData.id
                                            Accessible.name: text; Accessible.role: Accessible.Button
                                            onClicked: VocalSeparationController.selectModel(modelData.id)
                                        }
                                        Button {
                                            visible: modelData.state !== VocalSeparationController.Installed
                                            text: modelData.state === VocalSeparationController.Paused ? qsTr("继续") : qsTr("下载")
                                            Accessible.name: text; Accessible.role: Accessible.Button
                                            onClicked: modelData.state === VocalSeparationController.Paused
                                                       ? VocalSeparationController.resumeDownload()
                                                       : VocalSeparationController.downloadModel(modelData.id)
                                        }
                                    }
                                }
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            color: page.raised; border.color: page.border; radius: 7
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 12
                                Label { text: qsTr("自定义模型"); color: page.cyan; font.bold: true }
                                Label { Layout.fillWidth: true; Layout.fillHeight: true; wrapMode: Text.Wrap; color: page.muted; font.pixelSize: 12; text: qsTr("仅接受已签名或白名单的 MDX / Demucs ONNX 清单。此版本不会导入未验证的文件。") }
                                Button { text: qsTr("打开模型目录"); Accessible.name: text; Accessible.role: Accessible.Button; onClicked: VocalSeparationController.openModelDirectory() }
                            }
                        }
                    }

                    Rectangle {
                        id: stemSelector
                        objectName: "separationStemSelector"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 72
                        color: page.surface; border.color: page.border; radius: 7
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 10; spacing: 7
                            Label { text: qsTr("输出音轨"); color: page.textPrimary; font.bold: true }
                            Repeater {
                                model: [
                                    { kind: VocalSeparationController.Vocals, text: qsTr("人声"), color: "#1688FF" },
                                    { kind: VocalSeparationController.Accompaniment, text: qsTr("伴奏"), color: "#00C7A4" },
                                    { kind: VocalSeparationController.Drums, text: qsTr("鼓组"), color: "#54B948" },
                                    { kind: VocalSeparationController.Bass, text: qsTr("贝斯"), color: "#FF9400" },
                                    { kind: VocalSeparationController.Other, text: qsTr("其他"), color: "#A960FF" }
                                ]
                                Button {
                                    readonly property var info: page.stemInfo(modelData.kind)
                                    text: modelData.text + (info.derived ? qsTr("（派生）") : "")
                                    checkable: true; checked: info.selected; enabled: info.supported
                                    Accessible.name: text; Accessible.role: Accessible.CheckBox
                                    ToolTip.visible: hovered && !enabled
                                    ToolTip.text: qsTr("当前模型不支持此音轨")
                                    onClicked: VocalSeparationController.setStemSelected(modelData.kind, checked)
                                    background: Rectangle { color: Qt.rgba(modelData.color.r, modelData.color.g, modelData.color.b, parent.checked ? 0.24 : 0.1); border.color: modelData.color; radius: 5 }
                                }
                            }
                        }
                    }

                    Rectangle {
                        id: timeline
                        objectName: "separationTimeline"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 211
                        color: page.input; border.color: page.border; radius: 7
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 10; spacing: 5
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: page.jobStateText(); color: VocalSeparationController.jobState === VocalSeparationController.Completed ? page.success : page.textPrimary; font.bold: true }
                                Item { Layout.fillWidth: true }
                                Label { text: Math.round(VocalSeparationController.progress * 100) + "%"; color: page.muted }
                            }
                            Slider {
                                Layout.fillWidth: true
                                from: 0; to: Math.max(1, AudioPreviewController.durationMs)
                                value: AudioPreviewController.positionMs
                                enabled: AudioPreviewController.durationMs > 0
                                Accessible.name: qsTr("预览播放位置")
                                onMoved: AudioPreviewController.seek(value)
                            }
                            Repeater {
                                model: VocalSeparationController.stems
                                Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true; Layout.fillHeight: true
                                    color: "transparent"; border.color: page.divider; radius: 3
                                    RowLayout {
                                        anchors.fill: parent; anchors.margins: 4
                                        Label { Layout.preferredWidth: 64; text: modelData.name; color: page.textPrimary }
                                        Item {
                                            Layout.fillWidth: true; Layout.fillHeight: true
                                            Repeater {
                                                model: Math.min(modelData.waveform.length, 160)
                                                Rectangle {
                                                    readonly property int peakIndex: Math.floor(index * modelData.waveform.length / Math.max(1, model))
                                                    width: Math.max(1, parent.width / Math.max(1, model) - 1)
                                                    height: Math.max(1, parent.height * Math.min(1, Number(modelData.waveform[peakIndex] || 0)))
                                                    x: index * (parent.width / Math.max(1, model)); anchors.verticalCenter: parent.verticalCenter
                                                    color: page.cyan
                                                }
                                            }
                                        }
                                        Button { text: qsTr("试听"); enabled: modelData.available; Accessible.name: modelData.name + text; Accessible.role: Accessible.Button; onClicked: VocalSeparationController.previewStem(modelData.kind) }
                                        Button { text: qsTr("导出"); enabled: modelData.available; Accessible.name: modelData.name + text; Accessible.role: Accessible.Button; onClicked: { exportDialog.kind = modelData.kind; exportDialog.open() } }
                                    }
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    id: sideColumn
                    Layout.preferredWidth: page.compact ? workbench.width
                                                       : page.fullDesktop ? 474 : 336
                    Layout.fillHeight: true
                    visible: page.desktop || page.compactTab !== 0
                    spacing: 8
                    Rectangle {
                        id: settingsPanel
                        objectName: "separationSettingsPanel"
                        Layout.fillWidth: true; Layout.preferredHeight: 184
                        visible: page.desktop || page.compactTab === 1
                        color: page.surface; border.color: page.border; radius: 7
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 12; spacing: 6
                            Label { text: qsTr("输出设置"); color: page.textPrimary; font.bold: true }
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: qsTr("格式"); color: page.muted; Layout.preferredWidth: 64 }
                                ComboBox {
                                    Layout.fillWidth: true; model: ["WAV", "FLAC"]
                                    currentIndex: VocalSeparationController.outputFormat === "flac" ? 1 : 0
                                    Accessible.name: qsTr("输出格式")
                                    onActivated: VocalSeparationController.selectOutputFormat(currentText.toLowerCase())
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: qsTr("目录"); color: page.muted; Layout.preferredWidth: 64 }
                                Label { Layout.fillWidth: true; text: VocalSeparationController.outputDirectory; color: page.textPrimary; elide: Text.ElideMiddle }
                                Button { text: qsTr("选择"); Accessible.name: qsTr("选择输出目录"); Accessible.role: Accessible.Button; onClicked: outputDialog.open() }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: qsTr("设备"); color: page.muted; Layout.preferredWidth: 64 }
                                Repeater {
                                    model: VocalSeparationController.availableDevices
                                    Button {
                                        required property var modelData
                                        text: modelData.name; checkable: true
                                        checked: VocalSeparationController.deviceMode === modelData.mode
                                        enabled: modelData.available || modelData.mode === VocalSeparationController.Auto
                                        Accessible.name: qsTr("处理设备：") + text; Accessible.role: Accessible.RadioButton
                                        ToolTip.visible: hovered && !enabled; ToolTip.text: modelData.reason
                                        onClicked: VocalSeparationController.selectDevice(modelData.mode)
                                    }
                                }
                            }
                        }
                    }
                    Rectangle {
                        id: historyPanel
                        objectName: "separationHistoryPanel"
                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 240
                        visible: page.desktop || page.compactTab === 2
                        color: page.surface; border.color: page.border; radius: 7
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 12; spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: qsTr("分离记录"); color: page.textPrimary; font.bold: true }
                                Item { Layout.fillWidth: true }
                                Button {
                                    text: qsTr("打开输出目录")
                                    Accessible.name: text
                                    Accessible.role: Accessible.Button
                                    onClicked: VocalSeparationController.openOutputDirectory()
                                }
                            }
                            ListView {
                                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                                model: VocalSeparationController.history
                                delegate: RowLayout {
                                    width: ListView.view.width; height: 44
                                    Label { Layout.fillWidth: true; text: modelData.inputName || modelData.name || modelData.path || qsTr("已完成的分离"); color: page.textPrimary; elide: Text.ElideRight }
                                    Label { text: modelData.modelId || ""; color: page.muted; font.pixelSize: 11 }
                                }
                                Label { anchors.centerIn: parent; visible: parent.count === 0; text: qsTr("暂无真实分离记录"); color: page.muted }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: bottomBar
            objectName: "separationBottomBar"
            Layout.fillWidth: true
            Layout.preferredHeight: page.compact ? 96 : 66
            color: page.surface; border.color: page.border
            GridLayout {
                anchors.fill: parent; anchors.margins: 10; rowSpacing: 8; columnSpacing: 8
                columns: page.compact ? 4 : 8
                Button { text: AudioPreviewController.playing ? qsTr("暂停预览") : qsTr("预览输入"); enabled: page.hasInput; Accessible.name: text; Accessible.role: Accessible.Button; onClicked: VocalSeparationController.previewInput() }
                Button { text: qsTr("重新分离"); enabled: VocalSeparationController.jobState === VocalSeparationController.Completed; Accessible.name: text; Accessible.role: Accessible.Button; onClicked: VocalSeparationController.retry() }
                Button { text: qsTr("导出伴奏"); enabled: page.stemInfo(VocalSeparationController.Accompaniment).available; Accessible.name: text; Accessible.role: Accessible.Button; onClicked: { exportDialog.kind = VocalSeparationController.Accompaniment; exportDialog.open() } }
                Button { text: qsTr("导出人声"); enabled: page.stemInfo(VocalSeparationController.Vocals).available; Accessible.name: text; Accessible.role: Accessible.Button; onClicked: { exportDialog.kind = VocalSeparationController.Vocals; exportDialog.open() } }
                ComboBox {
                    id: playlistBox; Layout.preferredWidth: 130; visible: PlaylistModel.count > 0
                    model: PlaylistModel; textRole: "name"; valueRole: "playlistId"
                    Accessible.name: qsTr("目标播放列表")
                }
                Button {
                    text: qsTr("加入播放列表"); enabled: PlaylistModel.count > 0 && VocalSeparationController.stems.length > 0
                    Accessible.name: text; Accessible.role: Accessible.Button
                    ToolTip.visible: hovered && !enabled; ToolTip.text: qsTr("请先创建播放列表并完成分离")
                    onClicked: VocalSeparationController.addSelectedToPlaylist(playlistBox.currentValue)
                }
                Item { Layout.fillWidth: true; visible: !page.compact }
                Button {
                    id: primaryAction
                    objectName: "separationPrimaryAction"
                    Layout.columnSpan: page.compact ? 3 : 1
                    text: VocalSeparationController.jobState === VocalSeparationController.Running
                          || VocalSeparationController.jobState === VocalSeparationController.Cancelling
                          ? qsTr("取消分离") : qsTr("开始分离")
                    enabled: (VocalSeparationController.jobState === VocalSeparationController.Running
                              || VocalSeparationController.jobState === VocalSeparationController.Cancelling)
                             || (page.hasInput
                                 && VocalSeparationController.selectedModelId.length > 0)
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: text; Accessible.role: Accessible.Button
                    ToolTip.visible: hovered && !enabled
                    ToolTip.text: qsTr("请选择输入文件和模型")
                    onClicked: VocalSeparationController.jobState === VocalSeparationController.Running
                               || VocalSeparationController.jobState === VocalSeparationController.Cancelling
                               ? VocalSeparationController.cancel() : VocalSeparationController.start()
                    background: Rectangle { color: parent.enabled ? page.primary : page.divider; border.color: parent.activeFocus ? page.cyan : page.primary; radius: 6 }
                    contentItem: Text { text: parent.text; color: page.textPrimary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }
            Label { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 2; visible: page.playlistDiagnostic.length > 0; text: page.playlistDiagnostic; color: page.muted; font.pixelSize: 11 }
        }
    }

    FolderDialog {
        id: exportDialog
        property int kind: VocalSeparationController.Vocals
        title: qsTr("选择导出目录")
        onAccepted: VocalSeparationController.exportStem(kind, selectedFolder)
    }
}

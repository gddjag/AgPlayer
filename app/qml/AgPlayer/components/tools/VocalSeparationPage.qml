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
    readonly property int sidePanelWidth: desktop
                                         ? Math.max(336, Math.min(560,
                                                                  Math.round((width - 28) * 0.29)))
                                         : Math.max(0, width - 28)
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
    readonly property bool contextLocked: VocalSeparationController.jobState === VocalSeparationController.Probing
                                        || VocalSeparationController.jobState === VocalSeparationController.Running
                                        || VocalSeparationController.jobState === VocalSeparationController.Cancelling
    readonly property string contextLockReason: VocalSeparationController.jobState === VocalSeparationController.Probing
                                             ? qsTr("正在探测设备，请稍候")
                                             : qsTr("分离任务进行中，暂不能更改输入或设置")
    property int compactTab: 0
    property string playlistDiagnostic: ""

    component WorkbenchButton: Button {
        id: control
        property bool primaryAction: false
        implicitHeight: 30
        leftPadding: 10
        rightPadding: 10
        topPadding: 5
        bottomPadding: 5
        font.pixelSize: 12
        focusPolicy: Qt.StrongFocus
        background: Rectangle {
            radius: 5
            color: control.primaryAction
                   ? (control.enabled ? page.primary : page.divider)
                   : control.down ? "#123A59"
                                  : control.checked ? "#0A3451"
                                                    : (control.enabled ? "#06253B" : "#041725")
            border.color: control.activeFocus ? page.cyan
                                              : (control.primaryAction ? page.primary : page.border)
            border.width: control.activeFocus ? 2 : 1
            opacity: control.enabled ? 1.0 : 0.62
        }
        contentItem: Text {
            text: control.text
            color: control.enabled ? page.textPrimary : page.muted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

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

    function stemLabel(kind) {
        switch (kind) {
        case VocalSeparationController.Vocals: return qsTr("人声")
        case VocalSeparationController.Accompaniment: return qsTr("伴奏")
        case VocalSeparationController.Drums: return qsTr("鼓组")
        case VocalSeparationController.Bass: return qsTr("贝斯")
        case VocalSeparationController.Other: return qsTr("其他")
        }
        return qsTr("音轨")
    }

    function previewActionText(path) {
        if (path.length > 0 && AudioPreviewController.sourcePath === path)
            return AudioPreviewController.playing ? qsTr("暂停") : qsTr("继续")
        return qsTr("试听")
    }

    function allSelectedStemsAvailable(stems) {
        let selectedCount = 0
        for (let index = 0; index < stems.length; ++index) {
            const stem = stems[index]
            if (!stem.selected)
                continue
            ++selectedCount
            if (!stem.available)
                return false
        }
        return selectedCount > 0
    }

    function hasAvailableSelectedStems() {
        return page.allSelectedStemsAvailable(VocalSeparationController.stems)
    }

    function formatBytes(bytes) {
        if (!bytes || bytes <= 0) return ""
        if (bytes >= 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MB"
        return Math.ceil(bytes / 1024) + " KB"
    }

    function modelStateText(state) {
        switch (state) {
        case VocalSeparationController.NotInstalled: return qsTr("未下载")
        case VocalSeparationController.PendingVerification: return qsTr("待校验")
        case VocalSeparationController.Downloading: return qsTr("下载中")
        case VocalSeparationController.Paused: return qsTr("已暂停")
        case VocalSeparationController.Verifying: return qsTr("校验中")
        case VocalSeparationController.Installed: return qsTr("已安装")
        case VocalSeparationController.ModelFailed: return qsTr("下载失败")
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
        case VocalSeparationController.JobFailed: return qsTr("分离失败")
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
            TabButton { text: qsTr("工作台"); focusPolicy: Qt.StrongFocus; Accessible.name: text; Accessible.role: Accessible.PageTab }
            TabButton { text: qsTr("输出设置"); focusPolicy: Qt.StrongFocus; Accessible.name: text; Accessible.role: Accessible.PageTab }
            TabButton { text: qsTr("分离记录"); focusPolicy: Qt.StrongFocus; Accessible.name: text; Accessible.role: Accessible.PageTab }
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
                                           ? workbench.width - page.sidePanelWidth - 8
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
                            WorkbenchButton {
                                text: qsTr("重试")
                                enabled: VocalSeparationController.canRetry
                                Accessible.name: text
                                Accessible.role: Accessible.Button
                                ToolTip.visible: hovered && !enabled
                                ToolTip.text: VocalSeparationController.error.length > 0
                                              ? VocalSeparationController.error : qsTr("当前错误不可重试")
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
                                enabled: !page.contextLocked
                                Accessible.name: qsTr("音频文件拖放区域")
                                Accessible.role: Accessible.Button
                                ToolTip.visible: !enabled
                                ToolTip.text: page.contextLockReason
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
                                WorkbenchButton {
                                    text: qsTr("选择文件")
                                    Layout.alignment: Qt.AlignHCenter
                                    focusPolicy: Qt.StrongFocus
                                    enabled: !page.contextLocked
                                    Accessible.name: text
                                    Accessible.role: Accessible.Button
                                    onClicked: inputDialog.open()
                                    ToolTip.visible: hovered && !enabled
                                    ToolTip.text: page.contextLocked ? page.contextLockReason : qsTr("选择一个本地音频文件")
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
                                        WorkbenchButton {
                                            visible: page.hasInput
                                            flat: true
                                            text: qsTr("清除")
                                            enabled: !page.contextLocked
                                            Accessible.name: text
                                            Accessible.role: Accessible.Button
                                            ToolTip.visible: hovered && !enabled
                                            ToolTip.text: page.contextLockReason
                                            onClicked: VocalSeparationController.clearInput()
                                        }
                                        WorkbenchButton {
                                            visible: page.hasInput
                                            text: page.previewActionText(VocalSeparationController.inputInfo.path || "")
                                            Accessible.name: qsTr("输入预览：") + text
                                            Accessible.role: Accessible.Button
                                            ToolTip.visible: hovered
                                            ToolTip.text: qsTr("播放或暂停当前输入预览")
                                            onClicked: VocalSeparationController.previewInput()
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
                                    Label { text: modelData.name; color: page.textPrimary; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Label { text: modelData.family.toUpperCase() + " · " + page.formatBytes(modelData.bytes); color: page.muted; font.pixelSize: 12; Layout.fillWidth: true }
                                    Label { text: modelData.useCase; color: page.textPrimary; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                    Label { text: modelData.provenance; color: page.muted; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.fillHeight: true }
                                    Label { text: page.modelStateText(modelData.state); color: modelData.state === VocalSeparationController.Installed ? page.success : page.cyan; font.pixelSize: 12 }
                                    ProgressBar { visible: modelData.id === VocalSeparationController.downloadingModelId; from: 0; to: 1; value: VocalSeparationController.downloadProgress; Layout.fillWidth: true }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        WorkbenchButton {
                                            text: VocalSeparationController.selectedModelId === modelData.id ? qsTr("当前模型") : qsTr("选择")
                                            enabled: !page.contextLocked
                                                     && VocalSeparationController.selectedModelId !== modelData.id
                                            Accessible.name: text; Accessible.role: Accessible.Button
                                            ToolTip.visible: hovered && !enabled
                                            ToolTip.text: page.contextLocked ? page.contextLockReason : qsTr("该模型已是当前选择")
                                            onClicked: VocalSeparationController.selectModel(modelData.id)
                                        }
                                        WorkbenchButton {
                                            visible: modelData.state === VocalSeparationController.Installed
                                                     || modelData.state === VocalSeparationController.ModelFailed
                                            text: qsTr("删除模型")
                                            enabled: !page.contextLocked && !VocalSeparationController.downloadBusy
                                            Accessible.name: text; Accessible.role: Accessible.Button
                                            ToolTip.visible: hovered && !enabled
                                            ToolTip.text: page.contextLocked ? page.contextLockReason
                                                                              : qsTr("下载或校验完成后才能删除模型")
                                            onClicked: VocalSeparationController.deleteModel(modelData.id)
                                        }
                                        WorkbenchButton {
                                            visible: modelData.state !== VocalSeparationController.Installed
                                            text: modelData.state === VocalSeparationController.Verifying ? qsTr("校验中")
                                                : modelData.state === VocalSeparationController.Downloading ? qsTr("暂停")
                                                : modelData.state === VocalSeparationController.Paused ? qsTr("继续")
                                                : modelData.state === VocalSeparationController.ModelFailed ? qsTr("重试下载")
                                                : modelData.state === VocalSeparationController.PendingVerification ? qsTr("校验")
                                                : qsTr("下载")
                                            Accessible.name: text; Accessible.role: Accessible.Button
                                            enabled: modelData.state !== VocalSeparationController.Verifying
                                                     && !page.contextLocked
                                                     && (!VocalSeparationController.downloadBusy
                                                         || modelData.id === VocalSeparationController.downloadingModelId)
                                            ToolTip.visible: hovered && !enabled
                                            ToolTip.text: modelData.state === VocalSeparationController.Verifying
                                                          ? qsTr("正在校验模型")
                                                          : page.contextLocked ? page.contextLockReason
                                                                              : VocalSeparationController.downloadBusy
                                                                                ? qsTr("另一模型正在下载或校验") : qsTr("模型正在校验")
                                            onClicked: {
                                                if (modelData.state === VocalSeparationController.Downloading)
                                                    VocalSeparationController.pauseDownload()
                                                else if (modelData.state === VocalSeparationController.Paused)
                                                    VocalSeparationController.resumeDownload()
                                                else if (modelData.state === VocalSeparationController.PendingVerification)
                                                    VocalSeparationController.verifyInstalledModels()
                                                else
                                                    VocalSeparationController.downloadModel(modelData.id)
                                            }
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
                                Label { text: qsTr("安全清单入口"); color: page.cyan; font.bold: true }
                                Label { Layout.fillWidth: true; Layout.fillHeight: true; wrapMode: Text.Wrap; color: page.muted; font.pixelSize: 12; text: qsTr("自定义模型选择尚未启用。仅已签名或白名单的 MDX / Demucs 清单可由后续版本处理；此页不会导入本地文件。") }
                                WorkbenchButton {
                                    text: qsTr("打开共享模型目录")
                                    enabled: !page.contextLocked
                                    Accessible.name: text; Accessible.role: Accessible.Button
                                    ToolTip.visible: hovered && !enabled; ToolTip.text: page.contextLockReason
                                    onClicked: VocalSeparationController.openModelDirectory()
                                }
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
                                WorkbenchButton {
                                    readonly property var info: page.stemInfo(modelData.kind)
                                    text: modelData.text + (info.derived ? qsTr("（派生）") : "")
                                    checkable: true; checked: info.selected
                                    enabled: !page.contextLocked && info.supported
                                    focusPolicy: Qt.StrongFocus
                                    Accessible.name: text; Accessible.role: Accessible.CheckBox
                                    ToolTip.visible: hovered && !enabled
                                    ToolTip.text: page.contextLocked ? page.contextLockReason
                                                                      : qsTr("当前模型不支持此音轨")
                                    onClicked: VocalSeparationController.setStemSelected(modelData.kind, checked)
                                    background: Rectangle {
                                        color: Qt.rgba(modelData.color.r, modelData.color.g, modelData.color.b,
                                                       parent.checked ? 0.24 : 0.1)
                                        border.color: parent.activeFocus ? page.cyan : modelData.color
                                        border.width: parent.activeFocus ? 2 : 1
                                        radius: 5
                                    }
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
                                Label {
                                    visible: VocalSeparationController.jobState === VocalSeparationController.Running
                                             || VocalSeparationController.jobState === VocalSeparationController.Cancelling
                                    text: Math.round(VocalSeparationController.progress * 100) + "%"
                                    color: page.muted
                                }
                            }
                            Slider {
                                Layout.fillWidth: true
                                from: 0; to: Math.max(1, AudioPreviewController.durationMs)
                                value: AudioPreviewController.positionMs
                                enabled: AudioPreviewController.durationMs > 0
                                focusPolicy: Qt.StrongFocus
                                Accessible.name: qsTr("预览播放位置")
                                onMoved: AudioPreviewController.seek(value)
                            }
                            Repeater {
                                model: VocalSeparationController.stems
                                Rectangle {
                                    required property var modelData
                                    objectName: "separationTimelineStem-" + modelData.kind
                                    Layout.fillWidth: true; Layout.fillHeight: true
                                    color: "transparent"; border.color: page.divider; radius: 3
                                    RowLayout {
                                        anchors.fill: parent; anchors.margins: 4
                                        Label { Layout.preferredWidth: 64; text: page.stemLabel(modelData.kind); color: modelData.supported ? page.textPrimary : page.muted }
                                        Item {
                                            Layout.fillWidth: true; Layout.fillHeight: true
                                            visible: modelData.supported
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
                                        Label { visible: !modelData.supported; Layout.fillWidth: true; text: qsTr("当前模型不支持"); color: page.muted; font.pixelSize: 11 }
                                        WorkbenchButton {
                                            text: page.previewActionText(modelData.path || "")
                                            enabled: modelData.available
                                            Accessible.name: page.stemLabel(modelData.kind) + qsTr("预览：") + text
                                            Accessible.role: Accessible.Button
                                            ToolTip.visible: hovered && !enabled
                                            ToolTip.text: qsTr("输出尚不可用")
                                            onClicked: VocalSeparationController.previewStem(modelData.kind)
                                        }
                                        WorkbenchButton {
                                            text: qsTr("导出")
                                            enabled: modelData.available && !page.contextLocked
                                            Accessible.name: page.stemLabel(modelData.kind) + text
                                            Accessible.role: Accessible.Button
                                            ToolTip.visible: hovered && !enabled
                                            ToolTip.text: page.contextLocked ? page.contextLockReason : qsTr("输出尚不可用")
                                            onClicked: { exportDialog.kind = modelData.kind; exportDialog.open() }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    id: sideColumn
                    objectName: "separationSideColumn"
                    Layout.preferredWidth: page.compact ? workbench.width : page.sidePanelWidth
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
                                    enabled: !page.contextLocked
                                    focusPolicy: Qt.StrongFocus
                                    Accessible.name: qsTr("输出格式")
                                    ToolTip.visible: hovered && !enabled; ToolTip.text: page.contextLockReason
                                    onActivated: VocalSeparationController.selectOutputFormat(currentText.toLowerCase())
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: qsTr("目录"); color: page.muted; Layout.preferredWidth: 64 }
                                Label { Layout.fillWidth: true; text: VocalSeparationController.outputDirectory; color: page.textPrimary; elide: Text.ElideMiddle }
                                WorkbenchButton {
                                    text: qsTr("选择")
                                    enabled: !page.contextLocked
                                    Accessible.name: qsTr("选择输出目录"); Accessible.role: Accessible.Button
                                    ToolTip.visible: hovered && !enabled; ToolTip.text: page.contextLockReason
                                    onClicked: outputDialog.open()
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: qsTr("设备"); color: page.muted; Layout.preferredWidth: 64 }
                                Repeater {
                                    model: VocalSeparationController.availableDevices
                                    WorkbenchButton {
                                        required property var modelData
                                        text: modelData.name; checkable: true
                                        checked: VocalSeparationController.deviceMode === modelData.mode
                                        enabled: !page.contextLocked && modelData.available
                                        Accessible.name: qsTr("处理设备：") + text; Accessible.role: Accessible.RadioButton
                                        ToolTip.visible: hovered && !enabled
                                        ToolTip.text: page.contextLocked ? page.contextLockReason : modelData.reason
                                        onClicked: VocalSeparationController.selectDevice(modelData.mode)
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Item { Layout.fillWidth: true }
                                WorkbenchButton {
                                    text: qsTr("探测设备")
                                    enabled: !page.contextLocked
                                    Accessible.name: text; Accessible.role: Accessible.Button
                                    ToolTip.visible: hovered && !enabled
                                    ToolTip.text: page.contextLocked ? page.contextLockReason : qsTr("设备探测正在进行")
                                    onClicked: VocalSeparationController.probeDevices()
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
                                WorkbenchButton {
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
                                    width: ListView.view.width; height: 54
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Label { Layout.fillWidth: true; text: modelData.inputName || modelData.inputPath || ""; color: page.textPrimary; elide: Text.ElideRight }
                                        Label { Layout.fillWidth: true; text: (modelData.createdAt || "") + " · " + (modelData.status || ""); color: page.muted; font.pixelSize: 10; elide: Text.ElideRight }
                                        Label { Layout.fillWidth: true; text: modelData.outputPath || ""; color: page.muted; font.pixelSize: 10; elide: Text.ElideMiddle }
                                    }
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
                columns: page.compact ? 4 : 10
                WorkbenchButton {
                    text: page.previewActionText(VocalSeparationController.inputInfo.path || "")
                    enabled: page.hasInput
                    Accessible.name: qsTr("输入预览：") + text; Accessible.role: Accessible.Button
                    ToolTip.visible: hovered && !enabled; ToolTip.text: qsTr("请先选择输入文件")
                    onClicked: VocalSeparationController.previewInput()
                }
                WorkbenchButton {
                    visible: !page.compact
                    text: qsTr("重新分离"); enabled: !page.contextLocked && VocalSeparationController.jobState === VocalSeparationController.Completed
                    Accessible.name: text; Accessible.role: Accessible.Button
                    ToolTip.visible: hovered && !enabled; ToolTip.text: page.contextLocked ? page.contextLockReason : qsTr("分离完成后可重新开始")
                    onClicked: VocalSeparationController.start()
                }
                WorkbenchButton {
                    visible: !page.compact
                    text: qsTr("导出伴奏"); enabled: !page.contextLocked && page.stemInfo(VocalSeparationController.Accompaniment).available
                    Accessible.name: text; Accessible.role: Accessible.Button
                    ToolTip.visible: hovered && !enabled; ToolTip.text: page.contextLocked ? page.contextLockReason : qsTr("伴奏输出尚不可用")
                    onClicked: { exportDialog.kind = VocalSeparationController.Accompaniment; exportDialog.open() }
                }
                WorkbenchButton {
                    visible: !page.compact
                    text: qsTr("导出人声"); enabled: !page.contextLocked && page.stemInfo(VocalSeparationController.Vocals).available
                    Accessible.name: text; Accessible.role: Accessible.Button
                    ToolTip.visible: hovered && !enabled; ToolTip.text: page.contextLocked ? page.contextLockReason : qsTr("人声输出尚不可用")
                    onClicked: { exportDialog.kind = VocalSeparationController.Vocals; exportDialog.open() }
                }
                WorkbenchButton {
                    visible: !page.compact
                    text: qsTr("导出所选轨")
                    enabled: !page.contextLocked && page.hasAvailableSelectedStems()
                    Accessible.name: text; Accessible.role: Accessible.Button
                    ToolTip.visible: hovered && !enabled; ToolTip.text: page.contextLocked ? page.contextLockReason : qsTr("所选输出尚不可用")
                    onClicked: exportSelectedDialog.open()
                }
                ComboBox {
                    id: playlistBox; Layout.preferredWidth: 130; Layout.columnSpan: 1; visible: !page.compact && PlaylistModel.count > 0
                    model: PlaylistModel; textRole: "name"; valueRole: "playlistId"
                    enabled: !page.contextLocked
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: qsTr("目标播放列表"); Accessible.role: Accessible.ComboBox
                    ToolTip.visible: hovered && !enabled; ToolTip.text: page.contextLockReason
                }
                WorkbenchButton {
                    objectName: "separationPlaylistAction"
                    visible: !page.compact
                    text: qsTr("加入播放列表"); Layout.columnSpan: 1
                    enabled: !page.contextLocked && PlaylistModel.count > 0 && page.hasAvailableSelectedStems()
                    Accessible.name: text; Accessible.role: Accessible.Button
                    ToolTip.visible: hovered && !enabled; ToolTip.text: page.contextLocked ? page.contextLockReason : qsTr("请选择播放列表并完成至少一条已选输出")
                    onClicked: VocalSeparationController.addSelectedToPlaylist(playlistBox.currentValue)
                }
                Item {
                    Layout.fillWidth: true
                    Layout.columnSpan: page.compact ? 3 : 1
                    visible: true
                }
                WorkbenchButton {
                    id: primaryAction
                    objectName: "separationPrimaryAction"
                    Layout.columnSpan: page.compact ? 2 : 3
                    Layout.preferredWidth: page.compact ? -1 : bottomBar.width * 0.30
                    text: VocalSeparationController.jobState === VocalSeparationController.Running
                          ? qsTr("取消分离")
                          : VocalSeparationController.jobState === VocalSeparationController.Cancelling
                            ? qsTr("正在取消") : qsTr("开始分离")
                    enabled: VocalSeparationController.jobState === VocalSeparationController.Running
                             || (VocalSeparationController.jobState !== VocalSeparationController.Cancelling
                                 && VocalSeparationController.canStart)
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: text; Accessible.role: Accessible.Button
                    ToolTip.visible: hovered && !enabled
                    ToolTip.text: VocalSeparationController.jobState === VocalSeparationController.Cancelling
                                  ? qsTr("正在取消分离任务") : VocalSeparationController.startDisabledReason
                    onClicked: VocalSeparationController.jobState === VocalSeparationController.Running
                               ? VocalSeparationController.cancel() : VocalSeparationController.start()
                    primaryAction: true
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

    FolderDialog {
        id: exportSelectedDialog
        title: qsTr("选择所选音轨导出目录")
        onAccepted: VocalSeparationController.exportSelected(selectedFolder)
    }
}

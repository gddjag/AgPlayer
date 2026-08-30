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
    property bool autoAddToPlaylist: false
    property bool autoOpenOutputDirectory: true
    property int previousJobState: VocalSeparationController.jobState

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

    component TransportIconButton: Button {
        id: iconButton
        property string iconName: "play-fill"
        property color iconTint: page.textPrimary
        implicitWidth: 38
        implicitHeight: 38
        focusPolicy: Qt.StrongFocus
        background: Rectangle {
            radius: width / 2
            color: iconButton.down ? "#0D3A58" : "#06253B"
            border.color: iconButton.activeFocus ? page.cyan
                                                   : (iconButton.hovered ? page.cyan : page.border)
            border.width: iconButton.activeFocus ? 2 : 1
            opacity: iconButton.enabled ? 1 : 0.5
        }
        contentItem: ThemedIcon {
            source: Theme.icon(iconButton.iconName)
            tint: iconButton.iconTint
            sourceSize.width: 20
            sourceSize.height: 20
        }
    }

    component WorkbenchCheckBox: CheckBox {
        id: checkControl
        implicitHeight: 22
        spacing: 6
        focusPolicy: Qt.StrongFocus
        indicator: ThemedIcon {
            x: 0
            y: Math.round((checkControl.height - height) / 2)
            width: 17
            height: 17
            source: Theme.icon(checkControl.checked
                               ? "checkbox-circle-fill" : "checkbox-blank-line")
            tint: checkControl.checked ? page.primary : page.muted
            opacity: checkControl.enabled ? 1 : 0.5
        }
        contentItem: Text {
            leftPadding: 23
            text: checkControl.text
            color: checkControl.enabled ? page.textPrimary : page.muted
            font.family: Theme.fontPrimary
            font.pixelSize: 10
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    component WorkbenchComboBox: ComboBox {
        id: comboControl
        implicitHeight: 28
        leftPadding: 10
        rightPadding: 28
        font.pixelSize: 11
        focusPolicy: Qt.StrongFocus
        background: Rectangle {
            radius: 5
            color: comboControl.enabled ? page.input : "#041725"
            border.color: comboControl.activeFocus ? page.cyan
                                                   : (comboControl.hovered ? "#1A5272" : page.border)
            border.width: comboControl.activeFocus ? 2 : 1
            opacity: comboControl.enabled ? 1 : 0.62
        }
        contentItem: Text {
            leftPadding: 0
            rightPadding: 0
            text: comboControl.displayText
            color: comboControl.enabled ? page.textPrimary : page.muted
            font: comboControl.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: ThemedIcon {
            x: comboControl.width - width - 8
            y: Math.round((comboControl.height - height) / 2)
            width: 14
            height: 14
            source: Theme.icon("arrow-down-s-line")
            tint: comboControl.enabled ? page.muted : page.divider
        }
        delegate: ItemDelegate {
            id: comboDelegate
            required property int index
            width: comboControl.width - 2
            height: 28
            highlighted: comboControl.highlightedIndex === index
            contentItem: Text {
                text: comboControl.textAt(comboDelegate.index)
                color: page.textPrimary
                font: comboControl.font
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                radius: 3
                color: comboDelegate.highlighted ? "#0A3451" : page.surface
            }
        }
        popup: Popup {
            y: comboControl.height + 2
            width: comboControl.width
            implicitHeight: Math.min(180, contentItem.implicitHeight + 2)
            padding: 1
            background: Rectangle {
                color: page.surface
                border.color: page.border
                radius: 5
            }
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: comboControl.popup.visible ? comboControl.delegateModel : null
                currentIndex: comboControl.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator { }
            }
        }
    }

    component UnifiedWaveform: WaveformItem {
        id: unifiedWaveform
        property bool useTrackAccent: false
        property color trackAccent: page.cyan
        visualMode: SettingsController.waveformMode
        baseColor: useTrackAccent
                   ? Qt.rgba(trackAccent.r, trackAccent.g, trackAccent.b, 0.55)
                   : (SettingsController.waveformMode === 0
                      ? SettingsController.waveformSolidBaseColor
                      : (SettingsController.waveformMode === 2
                         ? SettingsController.spectrumSolidColor
                         : SettingsController.waveformRgbBaseColor))
        progressColor: useTrackAccent ? trackAccent
                                      : SettingsController.waveformSolidProgressColor
        gradientStartColor: useTrackAccent ? trackAccent
                            : (SettingsController.waveformMode === 2
                               && SettingsController.spectrumColorMode === 0
                               ? SettingsController.spectrumSolidColor
                               : SettingsController.spectrumRgbStartColor)
        gradientMiddleColor: useTrackAccent ? trackAccent
                             : (SettingsController.waveformMode === 2
                                && SettingsController.spectrumColorMode === 0
                                ? SettingsController.spectrumSolidColor
                                : SettingsController.spectrumRgbMiddleColor)
        gradientEndColor: useTrackAccent ? trackAccent
                          : (SettingsController.waveformMode === 2
                             && SettingsController.spectrumColorMode === 0
                             ? SettingsController.spectrumSolidColor
                             : SettingsController.spectrumRgbEndColor)
        rgbProgress: !useTrackAccent && SettingsController.waveformRgbProgress
        amplitudeScale: SettingsController.waveformMode === 2
                        ? 1.0 : SettingsController.waveformHeight
        density: SettingsController.waveformMode === 2
                 ? 1.0 : SettingsController.waveformDensity
        lineWidth: SettingsController.waveformMode === 2
                   ? 3.0 : SettingsController.waveformThickness
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

    function stemSummary(kinds) {
        const labels = []
        for (let index = 0; index < kinds.length; ++index)
            labels.push(stemLabel(kinds[index]))
        return labels.join(" / ")
    }

    function previewActionText(path) {
        if (path.length > 0 && AudioPreviewController.sourcePath === path)
            return AudioPreviewController.playing ? qsTr("暂停") : qsTr("继续")
        return qsTr("试听")
    }

    function toggleSharedPreview() {
        if (!page.hasInput && !AudioPreviewController.hasSource)
            return
        if (!AudioPreviewController.hasSource) {
            VocalSeparationController.previewInput()
        } else if (AudioPreviewController.playing) {
            AudioPreviewController.pause()
        } else {
            AudioPreviewController.resume()
        }
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

    function formatTime(milliseconds) {
        const totalSeconds = Math.max(0, Math.floor(Number(milliseconds || 0) / 1000))
        const hours = Math.floor(totalSeconds / 3600)
        const minutes = Math.floor((totalSeconds % 3600) / 60)
        const seconds = totalSeconds % 60
        return hours > 0
                ? String(hours).padStart(2, "0") + ":"
                  + String(minutes).padStart(2, "0") + ":"
                  + String(seconds).padStart(2, "0")
                : String(minutes).padStart(2, "0") + ":"
                  + String(seconds).padStart(2, "0")
    }

    function stemColor(kind) {
        switch (kind) {
        case VocalSeparationController.Vocals: return "#1688FF"
        case VocalSeparationController.Accompaniment: return "#00C7A4"
        case VocalSeparationController.Drums: return "#73C447"
        case VocalSeparationController.Bass: return "#FF9400"
        case VocalSeparationController.Other: return "#A960FF"
        }
        return page.cyan
    }

    function stemIcon(kind) {
        switch (kind) {
        case VocalSeparationController.Vocals: return "lucide-mic"
        case VocalSeparationController.Accompaniment: return "music-2-line"
        case VocalSeparationController.Drums: return "lucide-drum"
        case VocalSeparationController.Bass: return "lucide-guitar"
        case VocalSeparationController.Other: return "lucide-audio-lines"
        }
        return "music-2-line"
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

    function historyStatusText(status) {
        switch (String(status || "").toLowerCase()) {
        case "completed": return qsTr("已完成")
        case "failed": return qsTr("失败")
        case "cancelled": return qsTr("已取消")
        }
        return status || qsTr("未知")
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

    WorkbenchComboBox {
        id: playlistBox
        visible: false
        model: PlaylistModel
        textRole: "name"
        valueRole: "playlistId"
    }

    Shortcut {
        sequence: "Space"
        context: Qt.WindowShortcut
        enabled: page.visible && (page.hasInput || AudioPreviewController.hasSource)
        onActivated: page.toggleSharedPreview()
    }

    Dialog {
        id: backupModelDialog
        objectName: "separationBackupModelDialog"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(520, page.width - 48)
        title: qsTr("备用模型下载地址")
        padding: 14
        background: Rectangle {
            color: page.surface
            border.color: page.border
            radius: 8
        }
        contentItem: ColumnLayout {
            spacing: 10
            TextArea {
                id: backupModelText
                objectName: "separationBackupModelText"
                Layout.fillWidth: true
                Layout.preferredHeight: 132
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                color: page.textPrimary
                selectionColor: page.primary
                selectedTextColor: "white"
                text: qsTr("暂无可信备用模型下载地址。\n\n为避免模型被篡改，AgPlayer 只会在这里提供经过核验、并与官方文件哈希一致的备用链接。获得可信网盘地址后再补充。")
                background: Rectangle {
                    color: page.input
                    border.color: page.border
                    radius: 5
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                WorkbenchButton {
                    text: qsTr("复制说明")
                    Accessible.name: text
                    Accessible.role: Accessible.Button
                    onClicked: {
                        backupModelText.selectAll()
                        backupModelText.copy()
                        backupModelText.deselect()
                    }
                }
                WorkbenchButton {
                    text: qsTr("关闭")
                    Accessible.name: text
                    Accessible.role: Accessible.Button
                    onClicked: backupModelDialog.close()
                }
            }
        }
    }

    Connections {
        target: VocalSeparationController
        function onJobStateChanged() {
            const current = VocalSeparationController.jobState
            if (current === VocalSeparationController.Completed
                    && page.previousJobState !== VocalSeparationController.Completed) {
                if (page.autoAddToPlaylist && PlaylistModel.count > 0
                        && playlistBox.currentValue !== undefined)
                    VocalSeparationController.addSelectedToPlaylist(playlistBox.currentValue)
                if (page.autoOpenOutputDirectory)
                    VocalSeparationController.openOutputDirectory()
            }
            page.previousJobState = current
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
                        Layout.preferredHeight: 116
                        spacing: 8

                        Rectangle {
                            id: inputPanel
                            objectName: "separationInputPanel"
                            Layout.preferredWidth: page.fullDesktop ? 230 : 210
                            Layout.fillHeight: true
                            color: page.surface
                            border.color: page.border
                            radius: 7

                            FileDropArea {
                                anchors.fill: parent
                                anchors.margins: 8
                                enabled: !page.contextLocked
                                Accessible.name: qsTr("音频文件拖放区域；键盘用户请使用选择文件按钮")
                                Accessible.role: Accessible.Pane
                                ToolTip.visible: !enabled
                                ToolTip.text: page.contextLockReason
                                onUrlsDropped: function(urls) {
                                    VocalSeparationController.dropInput(urls)
                                }
                            }

                            ColumnLayout {
                                anchors.centerIn: parent
                                width: parent.width - 26
                                spacing: 2
                                ThemedIcon {
                                    source: Theme.icon("folder-open-line")
                                    tint: page.cyan
                                    sourceSize.width: 22; sourceSize.height: 22
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.preferredWidth: 22; Layout.preferredHeight: 22
                                }
                                Label {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: VocalSeparationController.inputInfo.name
                                          ? qsTr("已选择输入") : qsTr("拖拽音频文件到此处")
                                    color: page.textPrimary
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                }
                                Label {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: qsTr("WAV / FLAC / M4A / MP3")
                                    color: page.muted
                                    font.pixelSize: 10
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
                            objectName: "separationInputPreview"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: page.input
                            border.color: page.border
                            radius: 7

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 0
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    spacing: 1
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            Layout.fillWidth: true
                                            text: VocalSeparationController.inputInfo.name
                                                  || qsTr("尚未选择输入文件")
                                            color: page.textPrimary
                                            elide: Text.ElideRight
                                            font.pixelSize: 14
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
                                    }
                                    Label {
                                        text: VocalSeparationController.inputInfo.name
                                              ? page.formatBytes(VocalSeparationController.inputInfo.bytes)
                                                + "  ·  "
                                                + Math.floor(VocalSeparationController.inputInfo.durationMs / 60000)
                                                + ":" + String(Math.floor((VocalSeparationController.inputInfo.durationMs / 1000) % 60)).padStart(2, "0")
                                              : qsTr("选择文件后将分析真实波形")
                                        color: page.muted
                                        font.pixelSize: 11
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        spacing: 8
                                        TransportIconButton {
                                            id: inputPreviewPlay
                                            objectName: "separationInputPreviewPlay"
                                            Layout.preferredWidth: 34
                                            Layout.preferredHeight: 34
                                            enabled: page.hasInput
                                            iconName: AudioPreviewController.sourcePath
                                                      === (VocalSeparationController.inputInfo.path || "")
                                                      && AudioPreviewController.playing
                                                      ? "pause-fill" : "play-fill"
                                            iconTint: page.cyan
                                            Accessible.name: qsTr("输入播放或暂停")
                                            Accessible.role: Accessible.Button
                                            ToolTip.visible: hovered
                                            ToolTip.text: enabled ? qsTr("播放或暂停输入音频")
                                                                      : qsTr("请先选择输入文件")
                                            onClicked: VocalSeparationController.previewInput()
                                        }
                                        Item {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            clip: true
                                            UnifiedWaveform {
                                                id: inputWaveform
                                                objectName: "separationInputWaveform"
                                                anchors.fill: parent
                                                peaks: VocalSeparationController.inputInfo.waveform || []
                                                position: AudioPreviewController.sourcePath
                                                          === (VocalSeparationController.inputInfo.path || "")
                                                          ? AudioPreviewController.positionMs : 0
                                                duration: VocalSeparationController.inputInfo.durationMs || 0
                                                pointerInteractionEnabled: page.hasInput
                                                onSeekRequested: function(positionMs) {
                                                    if (AudioPreviewController.sourcePath
                                                            !== (VocalSeparationController.inputInfo.path || ""))
                                                        VocalSeparationController.previewInput()
                                                    AudioPreviewController.seek(positionMs)
                                                }
                                            }
                                            Label {
                                                anchors.centerIn: parent
                                                visible: inputWaveform.peaks.length === 0
                                                text: page.hasInput ? qsTr("正在分析真实波形…")
                                                                    : qsTr("输入波形")
                                                color: page.muted
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        id: modelDeck
                        objectName: "separationModelDeck"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 174
                        color: "transparent"

                        ListView {
                            id: modelList
                            objectName: "separationModelList"
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.bottom: modelScrollBar.top
                            anchors.bottomMargin: 4
                            orientation: ListView.Horizontal
                            spacing: 8
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: VocalSeparationController.models.length + 1
                            delegate: Rectangle {
                                required property int index
                                readonly property bool customEntry:
                                    index === VocalSeparationController.models.length
                                readonly property var cardData: customEntry
                                    ? ({ id: "custom", tierLabel: qsTr("自定义模型"),
                                         badgeLabel: qsTr("安全清单"),
                                         name: qsTr("兼容 ONNX 模型"),
                                         provider: qsTr("本地安全 manifest"),
                                         description: qsTr("仅接受通过校验的 MDX / Demucs ONNX 模型"),
                                         repositoryUrl: "", bytes: 0, stems: [] })
                                    : VocalSeparationController.models[index]
                                width: page.fullDesktop ? 304 : 286
                                height: modelList.height
                                objectName: customEntry ? "separationCustomModelCard"
                                                        : "separationModelCard-" + cardData.id
                                color: customEntry ? page.raised
                                      : VocalSeparationController.selectedModelId === cardData.id
                                        ? "#062039" : page.surface
                                border.color: customEntry ? "#006C78"
                                              : VocalSeparationController.selectedModelId === cardData.id
                                                ? page.cyan : page.border
                                radius: 7

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: !page.contextLocked
                                    onClicked: cardData.id === "custom"
                                               ? VocalSeparationController.openModelDirectory()
                                               : VocalSeparationController.selectModel(cardData.id)
                                }

                                ColumnLayout {
                                    z: 1
                                    anchors.fill: parent
                                    anchors.margins: 9
                                    spacing: 2
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            Layout.fillWidth: true
                                            text: cardData.tierLabel
                                            color: customEntry ? "#00DFC7" : page.textPrimary
                                            font.bold: true
                                            font.pixelSize: 14
                                            elide: Text.ElideRight
                                        }
                                        Rectangle {
                                            visible: cardData.badgeLabel.length > 0
                                            implicitWidth: badgeText.implicitWidth + 14
                                            implicitHeight: 22
                                            radius: 3
                                            color: customEntry ? "#063A3B" : "#0A2D49"
                                            border.color: customEntry ? "#00A98F" : page.border
                                            Label {
                                                id: badgeText
                                                anchors.centerIn: parent
                                                text: cardData.badgeLabel
                                                color: customEntry ? "#00DFC7" : page.cyan
                                                font.pixelSize: 10
                                            }
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: qsTr("模型名称："); color: page.muted; font.pixelSize: 10; Layout.preferredWidth: 62 }
                                        Label { Layout.fillWidth: true; text: cardData.name; color: page.textPrimary; font.pixelSize: 10; elide: Text.ElideRight }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: qsTr("提供商："); color: page.muted; font.pixelSize: 10; Layout.preferredWidth: 62 }
                                        Label { Layout.fillWidth: true; text: cardData.provider; color: page.textPrimary; font.pixelSize: 10; elide: Text.ElideRight }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: qsTr("模型介绍："); color: page.muted; font.pixelSize: 10; Layout.preferredWidth: 62 }
                                        Label {
                                            objectName: customEntry ? "customModelSummary"
                                                                    : "modelStemSummary-" + cardData.id
                                            Layout.fillWidth: true
                                            text: customEntry ? cardData.description
                                                  : cardData.description + qsTr("；输出：")
                                                    + page.stemSummary(cardData.stems)
                                            color: page.textPrimary
                                            font.pixelSize: 10
                                            elide: Text.ElideRight
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: qsTr("官方仓库："); color: page.muted; font.pixelSize: 10; Layout.preferredWidth: 62 }
                                        WorkbenchButton {
                                            Layout.fillWidth: true
                                            text: customEntry ? qsTr("打开模型目录") : cardData.repositoryUrl
                                            implicitHeight: 18
                                            leftPadding: 0
                                            rightPadding: 0
                                            font.pixelSize: 10
                                            Accessible.name: customEntry ? qsTr("打开自定义模型目录")
                                                                         : qsTr("打开模型官方仓库")
                                            Accessible.role: Accessible.Button
                                            background: Rectangle {
                                                color: "transparent"
                                                radius: 2
                                                border.width: parent.activeFocus ? 1 : 0
                                                border.color: page.cyan
                                            }
                                            contentItem: Text {
                                                text: parent.text
                                                color: page.cyan
                                                font: parent.font
                                                verticalAlignment: Text.AlignVCenter
                                                horizontalAlignment: Text.AlignLeft
                                                elide: Text.ElideMiddle
                                            }
                                            onClicked: customEntry
                                                       ? VocalSeparationController.openModelDirectory()
                                                       : Qt.openUrlExternally(cardData.repositoryUrl)
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: qsTr("文件大小："); color: page.muted; font.pixelSize: 10; Layout.preferredWidth: 62 }
                                        Label { Layout.fillWidth: true; text: customEntry ? qsTr("按模型文件") : page.formatBytes(cardData.bytes); color: page.textPrimary; font.pixelSize: 10 }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: qsTr("状态："); color: page.muted; font.pixelSize: 10; Layout.preferredWidth: 62 }
                                        Label {
                                            Layout.fillWidth: true
                                            text: customEntry ? qsTr("等待安全清单") : page.modelStateText(cardData.state)
                                            color: customEntry ? "#00DFC7"
                                                  : cardData.state === VocalSeparationController.Installed
                                                    ? page.success : page.cyan
                                            font.pixelSize: 10
                                        }
                                    }
                                    ProgressBar {
                                        visible: !customEntry
                                                 && cardData.id === VocalSeparationController.downloadingModelId
                                        from: 0
                                        to: 1
                                        value: VocalSeparationController.downloadProgress
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 4
                                    }
                                    Item { Layout.fillHeight: true }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Item { Layout.fillWidth: true }
                                        WorkbenchButton {
                                            visible: !customEntry
                                                     && (cardData.state === VocalSeparationController.Installed
                                                         || cardData.state === VocalSeparationController.ModelFailed)
                                            text: qsTr("删除")
                                            enabled: !page.contextLocked && !VocalSeparationController.downloadBusy
                                            Accessible.name: qsTr("删除模型")
                                            Accessible.role: Accessible.Button
                                            onClicked: VocalSeparationController.deleteModel(cardData.id)
                                        }
                                        WorkbenchButton {
                                            visible: !customEntry
                                                     && cardData.state !== VocalSeparationController.Installed
                                            text: cardData.state === VocalSeparationController.Verifying ? qsTr("校验中")
                                                : cardData.state === VocalSeparationController.Downloading ? qsTr("暂停")
                                                : cardData.state === VocalSeparationController.Paused ? qsTr("继续")
                                                : cardData.state === VocalSeparationController.ModelFailed ? qsTr("重试")
                                                : cardData.state === VocalSeparationController.PendingVerification ? qsTr("校验")
                                                : qsTr("下载")
                                            Accessible.name: text
                                            Accessible.role: Accessible.Button
                                            enabled: cardData.state !== VocalSeparationController.Verifying
                                                     && !page.contextLocked
                                                     && (!VocalSeparationController.downloadBusy
                                                         || cardData.id === VocalSeparationController.downloadingModelId)
                                            onClicked: {
                                                if (cardData.state === VocalSeparationController.Downloading)
                                                    VocalSeparationController.pauseDownload()
                                                else if (cardData.state === VocalSeparationController.Paused)
                                                    VocalSeparationController.resumeDownload()
                                                else if (cardData.state === VocalSeparationController.PendingVerification)
                                                    VocalSeparationController.verifyInstalledModels()
                                                else
                                                    VocalSeparationController.downloadModel(cardData.id)
                                            }
                                        }
                                    }
                                }
                            }

                        }
                        ScrollBar {
                            id: modelScrollBar
                            objectName: "separationModelScrollBar"
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 8
                            orientation: Qt.Horizontal
                            policy: ScrollBar.AlwaysOn
                            active: true
                            size: modelList.contentWidth > 0
                                  ? Math.min(1, modelList.width / modelList.contentWidth) : 1
                            position: modelList.contentWidth > 0
                                      ? modelList.contentX / modelList.contentWidth : 0
                            onPositionChanged: {
                                if (pressed)
                                    modelList.contentX = position * modelList.contentWidth
                            }
                        }
                    }

                    Rectangle {
                        id: stemSelector
                        objectName: "separationStemSelector"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 76
                        color: page.surface; border.color: page.border; radius: 7
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: qsTr("输出音轨"); color: page.textPrimary; font.bold: true }
                                Label {
                                    text: qsTr("请选择要生成的音轨（至少选择一个）")
                                    color: page.muted
                                    font.pixelSize: 10
                                }
                                Item { Layout.fillWidth: true }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 7
                                Repeater {
                                    model: [
                                        { kind: VocalSeparationController.Vocals, text: qsTr("人声"), description: qsTr("主唱与和声") },
                                        { kind: VocalSeparationController.Accompaniment, text: qsTr("伴奏"), description: qsTr("无人声混音") },
                                        { kind: VocalSeparationController.Drums, text: qsTr("鼓组"), description: qsTr("鼓与打击乐") },
                                        { kind: VocalSeparationController.Bass, text: qsTr("贝斯"), description: qsTr("低频乐器") },
                                        { kind: VocalSeparationController.Other, text: qsTr("其他"), description: qsTr("其余乐器") }
                                    ]
                                    WorkbenchButton {
                                        id: stemOption
                                        readonly property var info: page.stemInfo(modelData.kind)
                                        readonly property color accent: page.stemColor(modelData.kind)
                                        objectName: "separationStemOption-" + modelData.kind
                                        Layout.preferredWidth: page.fullDesktop ? 154 : 108
                                        Layout.fillHeight: true
                                        checkable: true
                                        checked: info.selected
                                        enabled: !page.contextLocked && info.supported
                                        focusPolicy: Qt.StrongFocus
                                        Accessible.name: modelData.text + ": " + modelData.description
                                        Accessible.role: Accessible.CheckBox
                                        ToolTip.visible: hovered
                                        ToolTip.text: page.contextLocked ? page.contextLockReason
                                                      : info.supported ? modelData.description
                                                                       : qsTr("当前模型不支持此音轨")
                                        onClicked: VocalSeparationController.setStemSelected(
                                                       modelData.kind, checked)
                                        background: Rectangle {
                                            color: Qt.rgba(stemOption.accent.r,
                                                           stemOption.accent.g,
                                                           stemOption.accent.b,
                                                           stemOption.checked ? 0.22 : 0.08)
                                            border.color: stemOption.activeFocus ? page.cyan
                                                                                 : stemOption.accent
                                            border.width: stemOption.activeFocus ? 2 : 1
                                            radius: 5
                                            opacity: stemOption.enabled ? 1 : 0.38
                                        }
                                        contentItem: RowLayout {
                                            spacing: 7
                                            ThemedIcon {
                                                objectName: "separationStemIcon-" + modelData.kind
                                                source: Theme.icon(page.stemIcon(modelData.kind))
                                                tint: stemOption.accent
                                                sourceSize.width: 18
                                                sourceSize.height: 18
                                                Layout.preferredWidth: 18
                                                Layout.preferredHeight: 18
                                            }
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 0
                                                Label {
                                                    text: modelData.text
                                                          + (stemOption.info.derived ? qsTr("（派生）") : "")
                                                    color: stemOption.enabled ? page.textPrimary : page.muted
                                                    font.pixelSize: 11
                                                }
                                                Label {
                                                    text: stemOption.info.supported
                                                          ? modelData.description : qsTr("当前模型不可用")
                                                    color: page.muted
                                                    font.pixelSize: 9
                                                    elide: Text.ElideRight
                                                    Layout.fillWidth: true
                                                }
                                            }
                                            ThemedIcon {
                                                objectName: "separationStemCheck-" + modelData.kind
                                                visible: true
                                                source: Theme.icon(stemOption.checked
                                                                   ? "checkbox-circle-fill"
                                                                   : "checkbox-blank-line")
                                                tint: stemOption.info.supported
                                                      ? stemOption.accent : page.muted
                                                opacity: stemOption.info.supported ? 1 : 0.55
                                                sourceSize.width: 15
                                                sourceSize.height: 15
                                                Layout.preferredWidth: 15
                                                Layout.preferredHeight: 15
                                            }
                                        }
                                    }
                                }
                                Item { Layout.fillWidth: true }
                                WorkbenchButton {
                                    objectName: "separationBackupModelAction"
                                    Layout.preferredWidth: page.fullDesktop ? 154 : 132
                                    Layout.fillHeight: true
                                    text: qsTr("备用模型下载地址")
                                    Accessible.name: text
                                    Accessible.role: Accessible.Button
                                    onClicked: backupModelDialog.open()
                                }
                            }
                        }
                    }

                    Rectangle {
                        id: timeline
                        objectName: "separationTimeline"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 350
                        color: page.input; border.color: page.border; radius: 7
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 8; spacing: 3
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: page.jobStateText(); color: VocalSeparationController.jobState === VocalSeparationController.Completed ? page.success : page.textPrimary; font.bold: true }
                                Label {
                                    text: VocalSeparationController.stage.length > 0
                                          ? " · " + VocalSeparationController.stage : ""
                                    color: page.muted
                                    font.pixelSize: 10
                                }
                                Item { Layout.fillWidth: true }
                                Label {
                                    visible: VocalSeparationController.jobState === VocalSeparationController.Running
                                             || VocalSeparationController.jobState === VocalSeparationController.Cancelling
                                    text: Math.round(VocalSeparationController.progress * 100) + "%"
                                    color: page.muted
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 72
                                Layout.rightMargin: 76
                                Repeater {
                                    model: 5
                                    Label {
                                        required property int index
                                        Layout.fillWidth: index < 4
                                        horizontalAlignment: index === 0 ? Text.AlignLeft
                                                             : index === 4 ? Text.AlignRight
                                                                           : Text.AlignHCenter
                                        text: page.formatTime(
                                                  (VocalSeparationController.inputInfo.durationMs || 0)
                                                  * index / 4)
                                        color: page.muted
                                        font.pixelSize: 9
                                    }
                                }
                            }
                            Repeater {
                                model: VocalSeparationController.stems
                                Rectangle {
                                    required property var modelData
                                    readonly property color accent: page.stemColor(modelData.kind)
                                    objectName: "separationTimelineStem-" + modelData.kind
                                    Layout.fillWidth: true; Layout.fillHeight: true
                                    Layout.minimumHeight: 38
                                    color: Qt.rgba(accent.r, accent.g, accent.b, 0.07)
                                    border.color: page.divider; radius: 3
                                    RowLayout {
                                        anchors.fill: parent; anchors.margins: 3
                                        Rectangle {
                                            Layout.preferredWidth: 58
                                            Layout.fillHeight: true
                                            radius: 3
                                            color: Qt.rgba(accent.r, accent.g, accent.b,
                                                           modelData.supported ? 0.22 : 0.06)
                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.margins: 5
                                                ThemedIcon {
                                                    source: Theme.icon(page.stemIcon(modelData.kind))
                                                    tint: modelData.supported ? accent : page.muted
                                                    sourceSize.width: 14
                                                    sourceSize.height: 14
                                                    Layout.preferredWidth: 14
                                                    Layout.preferredHeight: 14
                                                }
                                                Label {
                                                    Layout.fillWidth: true
                                                    text: page.stemLabel(modelData.kind)
                                                    color: modelData.supported ? page.textPrimary : page.muted
                                                    font.pixelSize: 10
                                                }
                                            }
                                        }
                                        Slider {
                                            id: stemVolume
                                            objectName: "stemPreviewVolume-" + modelData.kind
                                            Layout.preferredWidth: 72
                                            Layout.preferredHeight: 22
                                            implicitHeight: 22
                                            from: 0; to: 1; stepSize: 0.05
                                            value: Number(modelData.previewVolume === undefined
                                                          ? 0.8 : modelData.previewVolume)
                                            enabled: modelData.supported && !page.contextLocked
                                            focusPolicy: Qt.StrongFocus
                                            Accessible.name: page.stemLabel(modelData.kind) + qsTr("预览音量")
                                            Accessible.role: Accessible.Slider
                                            ToolTip.visible: hovered
                                            ToolTip.text: page.contextLocked ? page.contextLockReason
                                                                                  : qsTr("调整此音轨的试听音量")
                                            background: Rectangle {
                                                x: stemVolume.leftPadding
                                                y: stemVolume.topPadding
                                                   + stemVolume.availableHeight / 2 - height / 2
                                                implicitWidth: 72
                                                implicitHeight: 4
                                                width: stemVolume.availableWidth
                                                height: implicitHeight
                                                radius: 2
                                                color: "#0B2A3B"
                                                Rectangle {
                                                    width: stemVolume.visualPosition * parent.width
                                                    height: parent.height
                                                    radius: parent.radius
                                                    color: stemVolume.enabled ? page.cyan : page.muted
                                                    opacity: stemVolume.enabled ? 0.9 : 0.45
                                                }
                                            }
                                            handle: Rectangle {
                                                x: stemVolume.leftPadding
                                                   + stemVolume.visualPosition
                                                     * (stemVolume.availableWidth - width)
                                                y: stemVolume.topPadding
                                                   + stemVolume.availableHeight / 2 - height / 2
                                                implicitWidth: 10
                                                implicitHeight: 10
                                                radius: 5
                                                color: stemVolume.enabled ? page.cyan : page.muted
                                                border.color: stemVolume.activeFocus
                                                              ? page.textPrimary : "transparent"
                                                border.width: stemVolume.activeFocus ? 2 : 0
                                            }
                                            onMoved: VocalSeparationController.setStemPreviewVolume(
                                                         modelData.kind, value)
                                        }
                                        Item {
                                            id: waveformTrack
                                            Layout.fillWidth: true; Layout.fillHeight: true
                                            visible: modelData.supported
                                            clip: true
                                            UnifiedWaveform {
                                                id: stemWaveform
                                                objectName: "separationStemWaveform-" + modelData.kind
                                                anchors.fill: parent
                                                peaks: modelData.waveform || []
                                                position: AudioPreviewController.sourcePath
                                                          === (modelData.path || "")
                                                          ? AudioPreviewController.positionMs : 0
                                                duration: VocalSeparationController.inputInfo.durationMs || 0
                                                pointerInteractionEnabled: modelData.available
                                                useTrackAccent: true
                                                trackAccent: accent
                                                onSeekRequested: function(positionMs) {
                                                    if (AudioPreviewController.sourcePath
                                                            !== (modelData.path || ""))
                                                        VocalSeparationController.previewStem(modelData.kind)
                                                    AudioPreviewController.seek(positionMs)
                                                }
                                            }
                                            Rectangle {
                                                visible: stemWaveform.duration > 0
                                                x: Math.max(0, Math.min(parent.width - width,
                                                                       stemWaveform.waveformCursorX))
                                                width: 1
                                                anchors.top: parent.top
                                                anchors.bottom: parent.bottom
                                                color: "#FFFFFF"
                                                opacity: 0.7
                                            }
                                        }
                                        Label { visible: !modelData.supported; Layout.fillWidth: true; text: qsTr("当前模型不支持"); color: page.muted; font.pixelSize: 11 }
                                        TransportIconButton {
                                            Layout.preferredWidth: 26
                                            Layout.preferredHeight: 26
                                            implicitWidth: 26
                                            implicitHeight: 26
                                            enabled: modelData.available && !page.contextLocked
                                            iconName: "download-line"
                                            iconTint: accent
                                            Accessible.name: page.stemLabel(modelData.kind) + qsTr("导出")
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
                        Layout.fillWidth: true; Layout.preferredHeight: 150
                        visible: page.desktop || page.compactTab === 1
                        color: page.surface; border.color: page.border; radius: 7
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 10; spacing: 4
                            Label { text: qsTr("输出设置"); color: page.textPrimary; font.bold: true; font.pixelSize: 13 }
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: qsTr("格式"); color: page.muted; Layout.preferredWidth: 68; font.pixelSize: 11 }
                                WorkbenchComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTr("WAV（无损）"), qsTr("FLAC（无损）"), qsTr("MP3（兼容）")]
                                    implicitHeight: 24
                                    currentIndex: VocalSeparationController.outputFormat === "flac" ? 1
                                                  : VocalSeparationController.outputFormat === "mp3" ? 2 : 0
                                    enabled: !page.contextLocked
                                    focusPolicy: Qt.StrongFocus
                                    Accessible.name: qsTr("输出格式")
                                    ToolTip.visible: hovered && !enabled; ToolTip.text: page.contextLockReason
                                    onActivated: VocalSeparationController.selectOutputFormat(
                                                     currentIndex === 1 ? "flac"
                                                                       : currentIndex === 2 ? "mp3" : "wav")
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: qsTr("输出目录"); color: page.muted; Layout.preferredWidth: 68; font.pixelSize: 11 }
                                Label { Layout.fillWidth: true; text: VocalSeparationController.outputDirectory; color: page.textPrimary; elide: Text.ElideMiddle; font.pixelSize: 10 }
                                WorkbenchButton {
                                    text: qsTr("选择")
                                    implicitHeight: 24
                                    enabled: !page.contextLocked
                                    Accessible.name: qsTr("选择输出目录"); Accessible.role: Accessible.Button
                                    ToolTip.visible: hovered && !enabled; ToolTip.text: page.contextLockReason
                                    onClicked: outputDialog.open()
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: qsTr("处理设备"); color: page.muted; Layout.preferredWidth: 68; font.pixelSize: 11 }
                                Repeater {
                                    model: VocalSeparationController.availableDevices
                                    WorkbenchButton {
                                        required property var modelData
                                        implicitHeight: 24
                                        text: modelData.mode === VocalSeparationController.Auto
                                              ? qsTr("自动")
                                              : modelData.mode === VocalSeparationController.GPU
                                                ? qsTr("GPU") : qsTr("CPU")
                                        checkable: true
                                        checked: VocalSeparationController.deviceMode === modelData.mode
                                        enabled: !page.contextLocked && modelData.available
                                        Accessible.name: qsTr("处理设备：") + text; Accessible.role: Accessible.RadioButton
                                        ToolTip.visible: hovered && !enabled
                                        ToolTip.text: page.contextLocked ? page.contextLockReason : modelData.reason
                                        onClicked: VocalSeparationController.selectDevice(modelData.mode)
                                    }
                                }
                                Item { Layout.fillWidth: true }
                                WorkbenchButton {
                                    text: qsTr("探测")
                                    implicitHeight: 24
                                    enabled: !page.contextLocked
                                    Accessible.name: qsTr("探测处理设备"); Accessible.role: Accessible.Button
                                    ToolTip.visible: hovered && !enabled
                                    ToolTip.text: page.contextLocked ? page.contextLockReason : qsTr("设备探测正在进行")
                                    onClicked: VocalSeparationController.probeDevices()
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                WorkbenchCheckBox {
                                    id: autoPlaylistCheck
                                    objectName: "separationAutoPlaylist"
                                    text: qsTr("导出后加入播放列表")
                                    implicitHeight: 22
                                    checked: page.autoAddToPlaylist
                                    enabled: !page.contextLocked && PlaylistModel.count > 0
                                    focusPolicy: Qt.StrongFocus
                                    Accessible.name: text
                                    Accessible.role: Accessible.CheckBox
                                    ToolTip.visible: hovered && !enabled
                                    ToolTip.text: page.contextLocked ? page.contextLockReason
                                                  : qsTr("请先创建播放列表")
                                    onToggled: page.autoAddToPlaylist = checked
                                }
                                WorkbenchCheckBox {
                                    id: autoOpenDirectoryCheck
                                    objectName: "separationAutoOpenDirectory"
                                    text: qsTr("完成后打开目录")
                                    implicitHeight: 22
                                    checked: page.autoOpenOutputDirectory
                                    enabled: !page.contextLocked
                                    focusPolicy: Qt.StrongFocus
                                    Accessible.name: text
                                    Accessible.role: Accessible.CheckBox
                                    onToggled: page.autoOpenOutputDirectory = checked
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
                            anchors.fill: parent; anchors.margins: 10; spacing: 5
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
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: page.divider }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 22
                                spacing: 8
                                Label {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 160
                                    text: page.fullDesktop ? qsTr("文件名") : qsTr("文件 / 模型")
                                    color: page.muted
                                    font.pixelSize: 10
                                }
                                Label {
                                    objectName: "separationHistoryHeaderModel"
                                    Layout.preferredWidth: 110
                                    text: qsTr("模型")
                                    color: page.muted
                                    font.pixelSize: 10
                                    visible: page.fullDesktop
                                }
                                Label {
                                    Layout.preferredWidth: 92
                                    text: qsTr("时间")
                                    color: page.muted
                                    font.pixelSize: 10
                                    visible: page.fullDesktop
                                }
                                Label {
                                    objectName: "separationHistoryHeaderDetail"
                                    Layout.preferredWidth: 100
                                    text: qsTr("时间 / 状态")
                                    color: page.muted
                                    font.pixelSize: 10
                                    visible: !page.fullDesktop
                                }
                                Label {
                                    Layout.preferredWidth: 48
                                    text: qsTr("状态")
                                    color: page.muted
                                    font.pixelSize: 10
                                    visible: page.fullDesktop
                                }
                                Label { Layout.preferredWidth: 62; text: qsTr("操作"); color: page.muted; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter }
                            }
                            ListView {
                                id: historyList
                                objectName: "separationHistoryList"
                                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                                model: VocalSeparationController.history
                                delegate: RowLayout {
                                    required property var modelData
                                    width: ListView.view.width; height: 48
                                    spacing: 8
                                    ColumnLayout {
                                        objectName: "separationHistoryFileCell"
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 160
                                        Label { Layout.fillWidth: true; text: modelData.inputName || modelData.inputPath || ""; color: page.textPrimary; elide: Text.ElideRight; font.pixelSize: 10 }
                                        Label {
                                            Layout.fillWidth: true
                                            text: page.fullDesktop
                                                  ? (modelData.outputPath || "")
                                                  : (modelData.modelId || "") + " · "
                                                    + (modelData.outputPath || "")
                                            color: page.muted
                                            font.pixelSize: 9
                                            elide: Text.ElideMiddle
                                        }
                                    }
                                    Label {
                                        objectName: "separationHistoryModelCell"
                                        Layout.preferredWidth: 110
                                        text: modelData.modelId || ""
                                        color: page.muted
                                        font.pixelSize: 9
                                        wrapMode: Text.Wrap
                                        visible: page.fullDesktop
                                    }
                                    Label {
                                        Layout.preferredWidth: 92
                                        text: modelData.createdAt || ""
                                        color: page.muted
                                        font.pixelSize: 9
                                        wrapMode: Text.Wrap
                                        visible: page.fullDesktop
                                    }
                                    Label {
                                        Layout.preferredWidth: 48
                                        text: page.historyStatusText(modelData.status)
                                        color: (modelData.status || "").toLowerCase() === "completed"
                                               ? page.success : page.cyan
                                        font.pixelSize: 9
                                        visible: page.fullDesktop
                                    }
                                    ColumnLayout {
                                        objectName: "separationHistoryDetailCell"
                                        Layout.preferredWidth: 100
                                        spacing: 1
                                        visible: !page.fullDesktop
                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.createdAt || ""
                                            color: page.muted
                                            font.pixelSize: 9
                                            elide: Text.ElideRight
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: page.historyStatusText(modelData.status)
                                            color: (modelData.status || "").toLowerCase() === "completed"
                                                   ? page.success : page.cyan
                                            font.pixelSize: 9
                                        }
                                    }
                                    RowLayout {
                                        Layout.preferredWidth: 62
                                        spacing: 4
                                        TransportIconButton {
                                            implicitWidth: 26
                                            implicitHeight: 26
                                            Layout.preferredWidth: 26
                                            Layout.preferredHeight: 26
                                            iconName: "folder-open-line"
                                            iconTint: page.cyan
                                            Accessible.name: qsTr("打开该记录输出目录")
                                            Accessible.role: Accessible.Button
                                            onClicked: VocalSeparationController.openHistoryOutputDirectory(
                                                           modelData.outputPath || "")
                                        }
                                        TransportIconButton {
                                            implicitWidth: 26
                                            implicitHeight: 26
                                            Layout.preferredWidth: 26
                                            Layout.preferredHeight: 26
                                            iconName: "more-2-line"
                                            iconTint: page.textPrimary
                                            enabled: !page.contextLocked
                                                     && (modelData.inputPath || "").length > 0
                                            Accessible.name: qsTr("记录操作")
                                            Accessible.role: Accessible.Button
                                            onClicked: historyMenu.popup()
                                            Menu {
                                                id: historyMenu
                                                MenuItem {
                                                    text: qsTr("重新使用此输入")
                                                    onTriggered: VocalSeparationController.selectHistoryInput(
                                                                     modelData.inputPath || "")
                                                }
                                                MenuItem {
                                                    text: qsTr("打开输出目录")
                                                    onTriggered: VocalSeparationController.openHistoryOutputDirectory(
                                                                     modelData.outputPath || "")
                                                }
                                            }
                                        }
                                    }
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
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.bottomMargin: 10
            color: "transparent"
            border.width: 0
            GridLayout {
                anchors.fill: parent; anchors.margins: 10; rowSpacing: 8; columnSpacing: 8
                columns: page.compact ? 4 : 14
                Rectangle {
                    id: transport
                    objectName: "separationTransport"
                    Layout.columnSpan: page.compact ? 4 : 3
                    Layout.preferredWidth: page.compact ? bottomBar.width * 0.58 : 310
                    Layout.fillHeight: true
                    color: "transparent"
                    border.width: 0
                    radius: 6
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 7
                        spacing: 8
                        TransportIconButton {
                            implicitWidth: 32
                            implicitHeight: 32
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            enabled: AudioPreviewController.hasSource
                            iconName: "skip-back-fill"
                            iconTint: page.textPrimary
                            Accessible.name: qsTr("返回预览起点")
                            Accessible.role: Accessible.Button
                            onClicked: AudioPreviewController.seek(0)
                        }
                        TransportIconButton {
                            id: transportPlay
                            objectName: "separationTransportPlay"
                            implicitWidth: 38
                            implicitHeight: 38
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 38
                            enabled: page.hasInput || AudioPreviewController.hasSource
                            iconName: AudioPreviewController.playing ? "pause-fill" : "play-fill"
                            iconTint: AudioPreviewController.playing ? page.success : page.textPrimary
                            Accessible.name: AudioPreviewController.playing
                                             ? qsTr("暂停预览") : qsTr("播放预览")
                            Accessible.role: Accessible.Button
                            onClicked: page.toggleSharedPreview()
                        }
                        Label {
                            id: transportTime
                            objectName: "separationTransportTime"
                            Layout.fillWidth: true
                            text: page.formatTime(AudioPreviewController.positionMs)
                                  + " / "
                                  + page.formatTime(AudioPreviewController.durationMs
                                                    || VocalSeparationController.inputInfo.durationMs)
                            color: page.textPrimary
                            font.pixelSize: page.compact ? 14 : 18
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                    }
                }
                WorkbenchButton {
                    text: qsTr("重新分离"); enabled: !page.contextLocked && VocalSeparationController.jobState === VocalSeparationController.Completed
                    Accessible.name: text; Accessible.role: Accessible.Button
                    ToolTip.visible: hovered && !enabled; ToolTip.text: page.contextLocked ? page.contextLockReason : qsTr("分离完成后可重新开始")
                    onClicked: VocalSeparationController.start()
                }
                WorkbenchButton {
                    text: qsTr("导出伴奏"); enabled: !page.contextLocked && page.stemInfo(VocalSeparationController.Accompaniment).available
                    Accessible.name: text; Accessible.role: Accessible.Button
                    ToolTip.visible: hovered && !enabled; ToolTip.text: page.contextLocked ? page.contextLockReason : qsTr("伴奏输出尚不可用")
                    onClicked: { exportDialog.kind = VocalSeparationController.Accompaniment; exportDialog.open() }
                }
                WorkbenchButton {
                    text: qsTr("导出人声"); enabled: !page.contextLocked && page.stemInfo(VocalSeparationController.Vocals).available
                    Accessible.name: text; Accessible.role: Accessible.Button
                    ToolTip.visible: hovered && !enabled; ToolTip.text: page.contextLocked ? page.contextLockReason : qsTr("人声输出尚不可用")
                    onClicked: { exportDialog.kind = VocalSeparationController.Vocals; exportDialog.open() }
                }
                WorkbenchButton {
                    text: qsTr("导出所有音轨")
                    enabled: !page.contextLocked
                             && VocalSeparationController.jobState === VocalSeparationController.Completed
                    Accessible.name: text; Accessible.role: Accessible.Button
                    ToolTip.visible: hovered && !enabled; ToolTip.text: page.contextLocked ? page.contextLockReason : qsTr("完成分离后可导出全部可用音轨")
                    onClicked: exportSelectedDialog.open()
                }
                Item {
                    Layout.fillWidth: true
                    Layout.columnSpan: page.compact ? 2 : 1
                    visible: true
                }
                WorkbenchButton {
                    id: primaryAction
                    objectName: "separationPrimaryAction"
                    Layout.columnSpan: page.compact ? 2 : 4
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
        title: qsTr("选择所有音轨导出目录")
        onAccepted: VocalSeparationController.exportAll(selectedFolder)
    }
}

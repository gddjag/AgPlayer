import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: page
    color: Theme.background
    focus: true

    property var converter: FormatConverter
    property var selectedIndices: []
    property int selectionAnchor: -1
    property string statusFilter: "all"
    property string searchText: ""
    property var pendingConversion: null
    readonly property bool losslessOutput:
        formatCombo.currentValue === "wav"
        || formatCombo.currentValue === "flac"

    function addCurrentPlayerTrack() {
        const ids = PlaybackController.queueTrackIds.length > 0
            ? PlaybackController.queueTrackIds : [PlaybackController.currentTrackId]
        const urls = []
        for (let index = 0; index < ids.length; ++index) {
            const track = LibraryModel.trackForId(ids[index])
            if (track && track.path)
                urls.push(Qt.resolvedUrl("file:///" + encodeURI(String(track.path).replace(/\\/g, "/"))))
        }
        if (urls.length > 0)
            converter.loadFiles(urls)
    }

    function selectFile(index, modifiers) {
        let next = selectedIndices.slice()
        if ((modifiers & Qt.ShiftModifier) !== 0 && selectionAnchor >= 0) {
            next = []
            const first = Math.min(selectionAnchor, index)
            const last = Math.max(selectionAnchor, index)
            for (let row = first; row <= last; ++row)
                next.push(row)
        } else if ((modifiers & Qt.ControlModifier) !== 0) {
            const position = next.indexOf(index)
            if (position >= 0)
                next.splice(position, 1)
            else
                next.push(index)
            selectionAnchor = index
        } else {
            next = [index]
            selectionAnchor = index
        }
        selectedIndices = next
    }

    function deleteSelection() {
        const ordered = selectedIndices.slice().sort(function(a, b) { return b - a })
        for (let row = 0; row < ordered.length; ++row)
            converter.removeFile(ordered[row])
        selectedIndices = []
        selectionAnchor = -1
    }

    function matchesFilter(entry) {
        const query = searchText.trim().toLowerCase()
        if (query.length > 0
                && String(entry.fileName || "").toLowerCase().indexOf(query) < 0
                && String(entry.format || "").toLowerCase().indexOf(query) < 0
                && String(entry.outputFormat || "").toLowerCase().indexOf(query) < 0)
            return false
        if (statusFilter === "all")
            return true
        if (statusFilter === "waiting")
            return entry.status === "Pending" || entry.status === "Ready"
        if (statusFilter === "done")
            return entry.status === "Done"
        if (statusFilter === "failed")
            return entry.status === "Error"
        if (statusFilter === "cancelled")
            return entry.status === "Cancelled"
        return true
    }

    function statusCount(filter) {
        let count = 0
        const files = converter.files || []
        for (let row = 0; row < files.length; ++row) {
            const entry = files[row]
            if (filter === "all"
                    || (filter === "waiting"
                        && (entry.status === "Pending" || entry.status === "Ready"))
                    || (filter === "done" && entry.status === "Done")
                    || (filter === "failed" && entry.status === "Error")
                    || (filter === "cancelled" && entry.status === "Cancelled"))
                ++count
        }
        return count
    }

    function startConversion(selectedOnly) {
        converter.bitrateMode = bitrateModeCombo.currentIndex === 0 ? "cbr" : "vbr"
        converter.conflictPolicy = conflictPolicyBox.currentValue
        const args = [formatCombo.currentValue,
                      bitRateCombo.currentValue,
                      sampleRateCombo.currentValue,
                      channelsCombo.currentValue,
                      outputDirField.text.trim(),
                      keepMetadataCheck.checked,
                      volumeNormalizeCheck.checked,
                      extractAudioCheck.checked]
        const plannedIndices = selectedOnly ? selectedIndices : Array.from(
                    {length: converter.fileCount}, function(_, row) { return row })
        const preview = converter.previewSelected(plannedIndices, args[0], args[1],
                                                  args[2], args[3], args[4], args[7])
        if (!preview.ready)
            return
        if (preview.requiresConfirmation) {
            pendingConversion = { selectedOnly: selectedOnly, args: args,
                                  profile: preview.resolvedProfile }
            preflightDialog.open()
            return
        }
        runConversion(selectedOnly, args)
    }

    function runConversion(selectedOnly, args) {
        if (selectedOnly)
            converter.startSelected(selectedIndices,
                                    args[0], args[1], args[2], args[3],
                                    args[4], args[5], args[6], args[7])
        else
            converter.start(args[0], args[1], args[2], args[3],
                            args[4], args[5], args[6], args[7])
    }

    Dialog {
        id: preflightDialog
        objectName: "formatPreflightDialog"
        modal: true
        title: qsTr("转换参数确认")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            if (pendingConversion)
                page.runConversion(pendingConversion.selectedOnly,
                                   pendingConversion.args)
            pendingConversion = null
        }
        onRejected: pendingConversion = null
        contentItem: Text {
            width: 320
            wrapMode: Text.WordWrap
            color: Theme.primaryText
            text: pendingConversion
                ? (pendingConversion.profile.conflictCount > 0
                    ? qsTr("检测到 %1 个同名输出。确认后将采用自动编号，原文件不会被覆盖。")
                        .arg(pendingConversion.profile.conflictCount)
                    : qsTr("请求参数与编码器实际参数不同。采样率将使用 %1 Hz。")
                        .arg(pendingConversion.profile.sampleRate))
                : ""
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Delete) {
            deleteSelection()
            event.accepted = true
        } else if ((event.modifiers & Qt.ControlModifier) !== 0
                   && event.key === Qt.Key_A) {
            selectedIndices = Array.from({length: converter.fileCount},
                                         function(_, row) { return row })
            event.accepted = true
        }
    }

    Component {
        id: fileDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFiles
            nameFilters: [qsTr("音频与视频文件 (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma *.mp4 *.mkv *.avi *.mov *.webm)")]
            onAccepted: {
                converter.loadFiles(selectedFiles)
                destroy()
            }
            onRejected: destroy()
        }
    }

    Component {
        id: folderDialogComponent
        FolderDialog {
            onAccepted: {
                converter.loadFiles([selectedFolder])
                destroy()
            }
            onRejected: destroy()
        }
    }

    Component {
        id: outputDirDialogComponent
        FolderDialog {
            onAccepted: {
                outputDirField.text = selectedFolder.toString().replace(/^file:\/+/, "")
                destroy()
            }
            onRejected: destroy()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        Rectangle {
            objectName: "formatToolbar"
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 4

                Button {
                    text: qsTr("添加文件")
                    icon.source: Theme.icon("add-line")
                    enabled: !converter.busy
                    onClicked: {
                        const dialog = fileDialogComponent.createObject(page)
                        dialog.open()
                    }
                }
                Button {
                    text: qsTr("添加文件夹")
                    icon.source: Theme.icon("folder-add-line")
                    enabled: !converter.busy
                    onClicked: {
                        const dialog = folderDialogComponent.createObject(page)
                        dialog.open()
                    }
                }
                Button {
                    text: qsTr("从播放器添加")
                    icon.source: Theme.icon("music-2-line")
                    enabled: !converter.busy
                             && PlaybackController.currentTrackId.length > 0
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("需要先在播放器中选择歌曲")
                    onClicked: page.addCurrentPlayerTrack()
                }
                Button {
                    text: qsTr("移除选中")
                    icon.source: Theme.icon("delete-bin-line")
                    enabled: selectedIndices.length > 0 && !converter.busy
                    onClicked: page.deleteSelection()
                }
                Button {
                    text: qsTr("清空列表")
                    icon.source: Theme.icon("delete-bin-line")
                    enabled: converter.fileCount > 0 && !converter.busy
                    onClicked: {
                        converter.clear()
                        selectedIndices = []
                    }
                }
                Item { Layout.fillWidth: true }
                TextField {
                    id: formatSearchField
                    objectName: "formatSearchField"
                    Layout.preferredWidth: 240
                    placeholderText: qsTr("搜索文件名、格式或标签…")
                    leftPadding: 10
                    onTextChanged: page.searchText = text
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            Rectangle {
                id: taskPanel
                objectName: "formatTaskPanel"
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    RowLayout {
                        objectName: "formatStatusFilters"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8
                        spacing: 4

                        Text {
                            text: qsTr("任务列表")
                            color: Theme.primaryText
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Repeater {
                            model: [
                                { key: "all", text: qsTr("全部") },
                                { key: "waiting", text: qsTr("等待中") },
                                { key: "done", text: qsTr("已完成") },
                                { key: "failed", text: qsTr("失败") },
                                { key: "cancelled", text: qsTr("已取消") }
                            ]
                            Button {
                                text: modelData.text + " " + page.statusCount(modelData.key)
                                checkable: true
                                checked: page.statusFilter === modelData.key
                                onClicked: page.statusFilter = modelData.key
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: qsTr("已选择 %1 个").arg(selectedIndices.length)
                            color: Theme.secondaryText
                            font.pixelSize: 11
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8
                        spacing: 6
                        Item { Layout.preferredWidth: 24 }
                        Text { text: qsTr("文件名"); color: Theme.secondaryText; font.pixelSize: 11; Layout.fillWidth: true }
                        Text { text: qsTr("源格式"); color: Theme.secondaryText; font.pixelSize: 11; Layout.preferredWidth: 54 }
                        Text { text: qsTr("时长"); color: Theme.secondaryText; font.pixelSize: 11; Layout.preferredWidth: 54 }
                        Text { text: qsTr("采样率"); color: Theme.secondaryText; font.pixelSize: 11; Layout.preferredWidth: 62 }
                        Text { text: qsTr("码率"); color: Theme.secondaryText; font.pixelSize: 11; Layout.preferredWidth: 62 }
                        Text { text: qsTr("输出"); color: Theme.secondaryText; font.pixelSize: 11; Layout.preferredWidth: 48 }
                        Text { text: qsTr("状态"); color: Theme.secondaryText; font.pixelSize: 11; Layout.preferredWidth: 58 }
                        Text { text: qsTr("进度"); color: Theme.secondaryText; font.pixelSize: 11; Layout.preferredWidth: 96 }
                        Item { Layout.preferredWidth: 30 }
                    }

                    ListView {
                        id: fileList
                        objectName: "formatFileList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: converter.files
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            readonly property bool included: page.matchesFilter(modelData)
                            width: fileList.width
                            height: included ? 38 : 0
                            visible: included
                            color: page.selectedIndices.indexOf(index) >= 0
                                   ? Qt.rgba(Theme.activeSelection.r,
                                             Theme.activeSelection.g,
                                             Theme.activeSelection.b, 0.22)
                                   : (rowHover.hovered ? Theme.hoverSurface
                                                       : "transparent")

                            HoverHandler { id: rowHover }
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: function(eventPoint, button) {
                                    page.forceActiveFocus()
                                    page.selectFile(index, eventPoint.modifiers)
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 6
                                CheckBox {
                                    Layout.preferredWidth: 24
                                    checked: page.selectedIndices.indexOf(index) >= 0
                                    onClicked: page.selectFile(index, Qt.ControlModifier)
                                }
                                Text { text: modelData.fileName; color: Theme.primaryText; font.pixelSize: 11; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                Text { text: modelData.format; color: Theme.secondaryText; font.pixelSize: 10; Layout.preferredWidth: 54 }
                                Text { text: converter.formatDuration(modelData.durationMs); color: Theme.secondaryText; font.pixelSize: 10; Layout.preferredWidth: 54 }
                                Text { text: modelData.sampleRate > 0 ? (modelData.sampleRate / 1000).toFixed(1) + " kHz" : "--"; color: Theme.secondaryText; font.pixelSize: 10; Layout.preferredWidth: 62 }
                                Text { text: modelData.bitRate > 0 ? Math.round(modelData.bitRate / 1000) + " kbps" : "--"; color: Theme.secondaryText; font.pixelSize: 10; Layout.preferredWidth: 62 }
                                Text { text: modelData.outputFormat || "--"; color: Theme.secondaryText; font.pixelSize: 10; Layout.preferredWidth: 48 }
                                Text {
                                    text: modelData.status === "Done" ? qsTr("已完成")
                                          : modelData.status === "Error" ? qsTr("失败")
                                          : modelData.status === "Converting" ? qsTr("转换中")
                                          : modelData.status === "Cancelled" ? qsTr("已取消")
                                          : qsTr("就绪")
                                    color: modelData.status === "Done" ? Theme.waveformGreen
                                          : modelData.status === "Error" ? Theme.favoriteRed
                                          : modelData.status === "Converting" ? Theme.cyan
                                          : Theme.secondaryText
                                    font.pixelSize: 10
                                    Layout.preferredWidth: 58
                                }
                                RowLayout {
                                    Layout.preferredWidth: 96
                                    spacing: 4
                                    ProgressBar { Layout.fillWidth: true; from: 0; to: 1; value: modelData.progress || 0 }
                                    Text { text: Math.round((modelData.progress || 0) * 100) + "%"; color: Theme.secondaryText; font.pixelSize: 9 }
                                }
                                ToolButton {
                                    Layout.preferredWidth: 30
                                    icon.source: Theme.icon("delete-bin-line")
                                    icon.color: Theme.iconSecondary
                                    ToolTip.visible: hovered
                                    ToolTip.text: converter.busy ? qsTr("取消任务") : qsTr("移除")
                                    onClicked: converter.busy
                                               ? converter.cancelEntry(index)
                                               : converter.removeFile(index)
                                }
                            }
                        }

                        ColumnLayout {
                            anchors.centerIn: parent
                            visible: converter.fileCount === 0
                            spacing: 8
                            ThemedIcon {
                                Layout.alignment: Qt.AlignHCenter
                                source: Theme.icon("folder-open-line")
                                tint: Theme.iconSecondary
                                sourceSize.width: 28
                                sourceSize.height: 28
                            }
                            Text {
                                text: qsTr("拖入音频文件，或使用上方添加命令")
                                color: Theme.secondaryText
                                font.pixelSize: 12
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }
                    }
                }
            }

            Rectangle {
                id: settingsPanel
                objectName: "formatSettingsPanel"
                Layout.preferredWidth: Math.max(360, page.width * 0.265)
                Layout.fillHeight: true
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm

                Flickable {
                    anchors.fill: parent
                    anchors.margins: 10
                    clip: true
                    contentWidth: width
                    contentHeight: settingsColumn.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    ColumnLayout {
                        id: settingsColumn
                        width: parent.width - 8
                        spacing: 7

                        Text { text: qsTr("转换设置"); color: Theme.primaryText; font.pixelSize: 15; font.weight: Font.DemiBold }
                        Text { text: qsTr("A. 输出格式"); color: Theme.secondaryText; font.pixelSize: 11 }
                        GridLayout {
                            objectName: "formatOutputFormatGroup"
                            Layout.fillWidth: true
                            columns: 4
                            rowSpacing: 4
                            columnSpacing: 4
                            Repeater {
                                model: converter.supportedOutputFormats
                                Button {
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    checkable: true
                                    enabled: modelData.available
                                    checked: formatCombo.currentIndex === index
                                    objectName: "formatOutputFormatButton_" + modelData.key
                                    onClicked: formatCombo.currentIndex = index
                                    ToolTip.visible: hovered && !modelData.available
                                    ToolTip.text: modelData.reason
                                }
                            }
                        }
                        ComboBox {
                            id: formatCombo
                            objectName: "converterOutputFormatBox"
                            visible: false
                            model: converter.supportedOutputFormats
                            textRole: "label"
                            valueRole: "key"
                            currentIndex: {
                                const value = SettingsController.transcodeFormat.toLowerCase()
                                for (let row = 0; row < model.length; ++row) {
                                    if (model[row].key === value)
                                        return row
                                }
                                return 0
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                        Text { text: qsTr("B. 编码参数"); color: Theme.secondaryText; font.pixelSize: 11 }
                        GridLayout {
                            objectName: "formatEncodingSettingsGroup"
                            Layout.fillWidth: true
                            columns: 2
                            rowSpacing: 6
                            columnSpacing: 6
                            Text { text: qsTr("码率模式"); color: Theme.secondaryText; font.pixelSize: 11 }
                            ComboBox {
                                id: bitrateModeCombo
                                objectName: "converterBitrateModeBox"
                                Layout.fillWidth: true
                                model: ["CBR", "VBR"]
                                enabled: !page.losslessOutput
                                currentIndex: converter.bitrateMode === "vbr" ? 1 : 0
                            }
                            Text { text: qsTr("目标码率"); color: Theme.secondaryText; font.pixelSize: 11 }
                            ComboBox {
                                id: bitRateCombo
                                Layout.fillWidth: true
                                enabled: !page.losslessOutput
                                model: [
                                    { label: "64 kbps", value: 64000 },
                                    { label: "96 kbps", value: 96000 },
                                    { label: "128 kbps", value: 128000 },
                                    { label: "192 kbps", value: 192000 },
                                    { label: "256 kbps", value: 256000 },
                                    { label: "320 kbps", value: 320000 },
                                    { label: qsTr("无损"), value: 0 }
                                ]
                                textRole: "label"
                                valueRole: "value"
                                currentIndex: SettingsController.transcodeBitrateKbps === 128 ? 2
                                              : SettingsController.transcodeBitrateKbps === 192 ? 3
                                              : SettingsController.transcodeBitrateKbps === 256 ? 4 : 5
                            }
                            Text { text: qsTr("采样率"); color: Theme.secondaryText; font.pixelSize: 11 }
                            ComboBox {
                                id: sampleRateCombo
                                Layout.fillWidth: true
                                model: [
                                    { label: qsTr("自动"), value: 0 },
                                    { label: "44.1 kHz", value: 44100 },
                                    { label: "48 kHz", value: 48000 },
                                    { label: "88.2 kHz", value: 88200 },
                                    { label: "96 kHz", value: 96000 },
                                    { label: "176.4 kHz", value: 176400 },
                                    { label: "192 kHz", value: 192000 }
                                ]
                                textRole: "label"
                                valueRole: "value"
                                currentIndex: 1
                            }
                            Text { text: qsTr("声道"); color: Theme.secondaryText; font.pixelSize: 11 }
                            ComboBox {
                                id: channelsCombo
                                Layout.fillWidth: true
                                model: [
                                    { label: qsTr("自动"), value: 0 },
                                    { label: qsTr("单声道"), value: 1 },
                                    { label: qsTr("立体声"), value: 2 }
                                ]
                                textRole: "label"
                                valueRole: "value"
                                currentIndex: SettingsController.transcodeChannels === 1 ? 1 : 2
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: page.losslessOutput
                            text: qsTr("无损格式保留采样精度，不使用目标码率")
                            color: Theme.secondaryText
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                        Text { text: qsTr("C. 输出选项"); color: Theme.secondaryText; font.pixelSize: 11 }
                        Item { objectName: "formatOutputOptionsGroup"; Layout.preferredHeight: 0 }
                        TextField {
                            id: outputDirField
                            Layout.fillWidth: true
                            text: SettingsController.defaultOutputDirectory
                            placeholderText: qsTr("输出目录")
                        }
                        Button {
                            Layout.fillWidth: true
                            text: qsTr("选择输出目录")
                            icon.source: Theme.icon("folder-open-line")
                            onClicked: {
                                const dialog = outputDirDialogComponent.createObject(page)
                                dialog.open()
                            }
                        }
                        ComboBox {
                            id: conflictPolicyBox
                            objectName: "converterConflictPolicyBox"
                            Layout.fillWidth: true
                            textRole: "label"
                            valueRole: "value"
                            model: [
                                { label: qsTr("自动序号"), value: "auto-number" },
                                { label: qsTr("跳过已存在"), value: "skip" },
                                { label: qsTr("覆盖（校验后替换）"), value: "overwrite" },
                                { label: qsTr("询问"), value: "ask" }
                            ]
                        }
                        CheckBox {
                            id: keepMetadataCheck
                            objectName: "keepMetadataCheck"
                            checked: SettingsController.preserveMetadata
                            text: qsTr("保留元数据")
                        }
                        CheckBox { id: volumeNormalizeCheck; text: qsTr("音量标准化") }
                        CheckBox {
                            id: keepCoverCheck
                            objectName: "keepCoverCheck"
                            enabled: false
                            text: qsTr("保留封面（仅支持时）")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("当前转换核心尚未提供封面流写入能力，因此此选项不可用。")
                        }
                        CheckBox {
                            id: preserveDirectoriesCheck
                            objectName: "preserveDirectoriesCheck"
                            enabled: false
                            text: qsTr("保留目录结构")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("当前文件导入未记录来源根目录，因此此选项不可用。")
                        }
                        CheckBox {
                            id: extractAudioCheck
                            objectName: "extractAudioCheck"
                            checked: false
                            text: qsTr("从视频中提取音频")
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("提示：转换任务使用实际 FFmpeg 编解码能力；不支持的参数组合会在任务行显示具体原因。")
                            color: Theme.secondaryText
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }

        Rectangle {
            id: bottomBar
            objectName: "formatBottomBar"
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                ColumnLayout {
                    Layout.preferredWidth: 260
                    spacing: 2
                    RowLayout {
                        Text { text: qsTr("总进度"); color: Theme.secondaryText; font.pixelSize: 11 }
                        Item { Layout.fillWidth: true }
                        Text { text: Math.round(converter.progress * 100) + "%"; color: Theme.primaryText; font.pixelSize: 11 }
                    }
                    ProgressBar { Layout.fillWidth: true; from: 0; to: 1; value: converter.progress }
                    Item { objectName: "formatTotalProgress"; Layout.preferredHeight: 0 }
                    Text {
                        text: qsTr("%1 个任务 / 已选择 %2 个").arg(converter.fileCount).arg(selectedIndices.length)
                        color: Theme.secondaryText
                        font.pixelSize: 9
                    }
                }
                ComboBox {
                    id: parallelJobsBox
                    objectName: "converterParallelJobsBox"
                    Layout.preferredWidth: 86
                    model: [qsTr("并发 1"), qsTr("并发 2"), qsTr("并发 4")]
                    currentIndex: converter.parallelJobs === 1 ? 0
                                  : converter.parallelJobs === 2 ? 1 : 2
                    onActivated: converter.parallelJobs = currentIndex === 0
                                 ? 1 : currentIndex === 1 ? 2 : 4
                }
                Button {
                    text: qsTr("输出目录")
                    icon.source: Theme.icon("folder-open-line")
                    onClicked: {
                        const dialog = outputDirDialogComponent.createObject(page)
                        dialog.open()
                    }
                }
                Item { Layout.fillWidth: true }
                Text { text: qsTr("已完成 %1").arg(converter.completedCount); color: Theme.waveformGreen; font.pixelSize: 11 }
                Text { text: qsTr("失败 %1").arg(converter.failedCount); color: converter.failedCount > 0 ? Theme.favoriteRed : Theme.secondaryText; font.pixelSize: 11 }
                Button {
                    id: retryFailedButton
                    objectName: "retryFailedButton"
                    text: qsTr("重试失败")
                    visible: !converter.busy && converter.failedCount > 0
                    enabled: visible
                    onClicked: {
                        converter.bitrateMode = bitrateModeCombo.currentIndex === 0
                                                ? "cbr" : "vbr"
                        converter.retryFailed(
                            formatCombo.currentValue,
                            bitRateCombo.currentValue,
                            sampleRateCombo.currentValue,
                            channelsCombo.currentValue,
                            outputDirField.text.trim(),
                            keepMetadataCheck.checked,
                            volumeNormalizeCheck.checked,
                            extractAudioCheck.checked)
                    }
                }
                Button {
                    id: convertSelectedButton
                    objectName: "convertSelectedButton"
                    text: qsTr("转换选中")
                    visible: !converter.busy
                    enabled: selectedIndices.length > 0
                    onClicked: page.startConversion(true)
                }
                Button {
                    id: convertAllButton
                    objectName: "convertAllButton"
                    text: converter.busy ? qsTr("取消全部") : qsTr("开始处理")
                    highlighted: true
                    enabled: converter.busy || converter.fileCount > 0
                    onClicked: converter.busy ? converter.cancel()
                                              : page.startConversion(false)
                }
            }
        }
    }

    FileDropArea {
        objectName: "formatDropArea"
        anchors.fill: parent
        z: 100
        onUrlsDropped: function(urls) { converter.loadFiles(urls) }
    }

    Connections {
        target: converter
        function onTranscodeCompleted(successCount, failureCount) {
            statusText.text = failureCount === 0
                              ? qsTr("已完成 %1 个任务").arg(successCount)
                              : qsTr("%1 个成功，%2 个失败").arg(successCount).arg(failureCount)
            statusText.color = failureCount === 0 ? Theme.waveformGreen
                                                   : Theme.favoriteRed
            statusText.opacity = 1
            statusTimer.restart()
        }
        function onErrorOccurred(message) {
            statusText.text = message
            statusText.color = Theme.favoriteRed
            statusText.opacity = 1
            statusTimer.restart()
        }
        function onWarningOccurred(message) {
            statusText.text = message
            statusText.color = Theme.ratingGold
            statusText.opacity = 1
            statusTimer.restart()
        }
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 74
        color: Theme.elevated
        border.color: Theme.border
        border.width: 1
        radius: Theme.radiusSm
        visible: statusText.opacity > 0
        width: Math.min(parent.width - 32, statusText.implicitWidth + 28)
        height: 34
        opacity: statusText.opacity
        Text {
            id: statusText
            anchors.centerIn: parent
            color: Theme.secondaryText
            font.pixelSize: 11
            opacity: 0
        }
    }
    Timer { id: statusTimer; interval: 5000; onTriggered: statusText.opacity = 0 }
}

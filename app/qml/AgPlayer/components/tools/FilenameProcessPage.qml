import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: page
    objectName: "filenameProcessPage"
    color: Theme.background
    focus: true

    property var selectedIndices: []
    property var previewRows: []
    property int selectionAnchor: -1
    property int entryRevision: 0
    property string searchText: ""
    property bool issueFilterEnabled: false
    property bool qaReferenceMode: false

    onQaReferenceModeChanged: {
        if (!qaReferenceMode) return
        prefixField.text = "[Live]_"
        suffixField.text = "_Remaster"
        replaceSpacesCheck.checked = true
        preserveExtensionCheck.checked = true
        removeAffixesWhenBlankCheck.checked = false
        autoNumberCheck.checked = true
        numberStartSpin.value = 1
        numberDigitsSpin.value = 2
        numberPositionBox.currentIndex = 1
        numberSeparatorField.text = "_"
        refreshPreview()
    }

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
            FilenameProcessor.loadFiles(urls)
    }

    readonly property int warningCount: {
        let count = 0
        for (let i = 0; i < previewRows.length; ++i)
            if (previewRows[i].severity === 1) ++count
        return count
    }
    readonly property int errorCount: {
        let count = 0
        for (let i = 0; i < previewRows.length; ++i)
            if (previewRows[i].severity === 2) ++count
        return count
    }
    readonly property int conflictCount: warningCount + errorCount
    readonly property int unchangedCount: {
        let count = 0
        for (let i = 0; i < previewRows.length; ++i)
            if (previewRows[i].preview === previewRows[i].original) ++count
        return count
    }
    readonly property int readyCount: Math.max(0, previewRows.length - warningCount - errorCount)
    readonly property int runnableCount: {
        let count = 0
        for (let i = 0; i < previewRows.length; ++i)
            if (previewRows[i].severity !== 2
                    && previewRows[i].preview !== previewRows[i].original) ++count
        return count
    }

    function isSelected(index) { return selectedIndices.indexOf(index) >= 0 }
    function entry(index) {
        entryRevision
        return FilenameProcessor.entryAt(index)
    }
    function matchesSearch(index) {
        if (issueFilterEnabled) {
            const preview = previewForIndex(index)
            if (!preview || preview.severity === 0) return false
        }
        const query = searchText.trim().toLowerCase()
        if (query.length === 0) return true
        const item = entry(index)
        return String(item.fileName || "").toLowerCase().indexOf(query) >= 0
                || String(item.path || "").toLowerCase().indexOf(query) >= 0
    }
    function selectIndex(index, modifiers) {
        let next = selectedIndices.slice()
        if ((modifiers & Qt.ShiftModifier) !== 0 && selectionAnchor >= 0) {
            next = []
            for (let i = Math.min(selectionAnchor, index);
                 i <= Math.max(selectionAnchor, index); ++i) next.push(i)
        } else if ((modifiers & Qt.ControlModifier) !== 0) {
            const position = next.indexOf(index)
            if (position >= 0) next.splice(position, 1)
            else next.push(index)
            selectionAnchor = index
        } else {
            next = [index]
            selectionAnchor = index
        }
        selectedIndices = next
        refreshPreview()
    }
    function toggleIndex(index) {
        const next = selectedIndices.slice()
        const position = next.indexOf(index)
        if (position >= 0) next.splice(position, 1)
        else next.push(index)
        selectedIndices = next
        selectionAnchor = index
        refreshPreview()
    }
    function rules() {
        return {
            prefix: prefixField.text,
            suffix: suffixField.text,
            removePrefix: removePrefixField.text,
            removeSuffix: removeSuffixField.text,
            replaceSpaces: replaceSpacesCheck.checked,
            spaceReplacement: spaceReplacementField.text,
            caseMode: caseBox.currentValue,
            preserveExtension: preserveExtensionCheck.checked,
            removePrefixWhenEmpty: removeAffixesWhenBlankCheck.checked,
            removeSuffixWhenEmpty: removeAffixesWhenBlankCheck.checked,
            removeSequenceWhenEmpty: removeAffixesWhenBlankCheck.checked,
            removeSequenceAtStart: removeLeadingSequenceCheck.checked,
            removeSequenceAtEnd: removeTrailingSequenceCheck.checked,
            autoNumber: autoNumberCheck.checked,
            numberStart: numberStartSpin.value,
            numberDigits: numberDigitsSpin.value,
            numberPosition: numberPositionBox.currentValue,
            numberSeparator: numberSeparatorField.text
        }
    }
    function refreshPreview() {
        if (FilenameProcessor.fileCount > 0 && selectedIndices.length === 0) {
            previewRows = []
            return
        }
        previewRows = FilenameProcessor.preview(
                    rules(), selectedIndices, conflictBox.currentValue)
    }
    function previewForIndex(index) {
        for (let i = 0; i < previewRows.length; ++i)
            if (previewRows[i].index === index) return previewRows[i]
        const item = entry(index)
        return { index: index, original: item.fileName,
                 preview: item.fileName, conflict: false }
    }
    function deleteSelection() {
        FilenameProcessor.removeFiles(selectedIndices)
        selectedIndices = []
        selectionAnchor = -1
        refreshPreview()
    }
    function formatSize(bytes) {
        const value = Number(bytes || 0)
        return value >= 1048576
                ? (value / 1048576).toFixed(1) + " MB"
                : Math.max(1, Math.round(value / 1024)) + " KB"
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Delete) {
            deleteSelection()
            event.accepted = true
        } else if ((event.modifiers & Qt.ControlModifier) !== 0
                   && event.key === Qt.Key_A) {
            selectedIndices = Array.from({length: FilenameProcessor.fileCount},
                                         function(_, index) { return index })
            refreshPreview()
            event.accepted = true
        }
    }

    FileDialog {
        id: audioDialog
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("音频文件 (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)")]
        onAccepted: FilenameProcessor.loadFiles(selectedFiles)
    }
    FolderDialog {
        id: folderDialog
        onAccepted: FilenameProcessor.loadFiles([selectedFolder])
    }
    Dialog {
        id: overwriteConfirmation
        modal: true
        anchors.centerIn: parent
        title: qsTr("确认覆盖现有文件")
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: FilenameProcessor.apply(
            page.rules(), page.selectedIndices, conflictBox.currentValue)
        ColumnLayout {
            width: 420
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("覆盖策略会先备份目标文件；重命名成功后仍可安全撤销。是否继续？")
                color: Theme.primaryText
            }
        }
    }

    FileDropArea {
        objectName: "filenameDropArea"
        anchors.fill: parent
        z: 20
        onUrlsDropped: function(urls) { FilenameProcessor.loadFiles(urls) }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 0
        anchors.rightMargin: 0
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        spacing: 7

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 6
                spacing: 16
            Button {
                Layout.preferredHeight: 40
                text: qsTr("添加文件")
                icon.source: Theme.icon("add-line")
                onClicked: audioDialog.open()
            }
            Button {
                Layout.preferredHeight: 40
                text: qsTr("添加文件夹")
                icon.source: Theme.icon("folder-add-line")
                onClicked: folderDialog.open()
            }
            Button {
                Layout.preferredHeight: 40
                text: qsTr("从播放列表添加")
                icon.source: Theme.icon("music-2-line")
                enabled: !FilenameProcessor.busy
                         && PlaybackController.currentTrackId.length > 0
                ToolTip.visible: hovered
                ToolTip.text: qsTr("需要先在播放器中选择歌曲")
                onClicked: page.addCurrentPlayerTrack()
            }
            Button {
                Layout.preferredHeight: 40
                text: qsTr("移除选中")
                icon.source: Theme.icon("delete-bin-line")
                enabled: selectedIndices.length > 0 && !FilenameProcessor.busy
                onClicked: deleteSelection()
            }
            Button {
                Layout.preferredHeight: 40
                text: qsTr("清空列表")
                icon.source: Theme.icon("delete-bin-line")
                enabled: FilenameProcessor.fileCount > 0 && !FilenameProcessor.busy
                onClicked: FilenameProcessor.clear()
            }
            Item { Layout.fillWidth: true }
            TextField {
                Layout.preferredWidth: 330
                Layout.preferredHeight: 40
                leftPadding: 38
                rightPadding: 40
                placeholderText: qsTr("搜索文件名、所在目录...")
                onTextChanged: page.searchText = text
                Item {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 18
                    height: 18
                    Rectangle {
                        width: 12
                        height: 12
                        radius: 6
                        color: "transparent"
                        border.color: Theme.iconSecondary
                        border.width: 1.5
                    }
                    Rectangle {
                        x: 11
                        y: 11
                        width: 7
                        height: 1.5
                        radius: 1
                        rotation: 45
                        transformOrigin: Item.Left
                        color: Theme.iconSecondary
                    }
                }
                ToolButton {
                    objectName: "filenameIssueFilterButton"
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 34
                    height: 34
                    checkable: true
                    checked: page.issueFilterEnabled
                    icon.source: Theme.icon("equalizer-line")
                    icon.color: checked ? Theme.cyan : Theme.iconPrimary
                    ToolTip.visible: hovered
                    ToolTip.text: checked ? qsTr("显示全部文件")
                                              : qsTr("仅显示警告和错误")
                    onToggled: page.issueFilterEnabled = checked
                }
            }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            Rectangle {
                id: filePanel
                objectName: "filenameFilePanel"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: Math.max(380, page.width * 0.378)
                Layout.maximumWidth: page.width * 0.40
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 46
                        color: Theme.panel
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            text: qsTr("已选择的文件（%1）").arg(FilenameProcessor.fileCount)
                            color: Theme.primaryText
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        color: Theme.background
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 7
                            CheckBox {
                                Layout.preferredWidth: 24
                                checked: FilenameProcessor.fileCount > 0
                                         && selectedIndices.length === FilenameProcessor.fileCount
                                onClicked: {
                                    if (checked) {
                                        selectedIndices = Array.from({length: FilenameProcessor.fileCount},
                                                                     function(_, index) { return index })
                                    } else selectedIndices = []
                                    refreshPreview()
                                }
                            }
                            Label { text: qsTr("文件名"); color: Theme.secondaryText; Layout.preferredWidth: 152 }
                            Label { text: qsTr("所在目录"); color: Theme.secondaryText; Layout.fillWidth: true }
                            Label { text: qsTr("扩展名"); color: Theme.secondaryText; Layout.preferredWidth: 62 }
                            Label { text: qsTr("大小"); color: Theme.secondaryText; Layout.preferredWidth: 72 }
                            Label { text: qsTr("状态"); color: Theme.secondaryText; Layout.preferredWidth: 54 }
                        }
                    }

                    ListView {
                        id: fileList
                        objectName: "filenameFileList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: FilenameProcessor.fileCount
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        delegate: Rectangle {
                            required property int index
                            readonly property var fileEntry: page.entry(index)
                            readonly property bool searchMatch: page.matchesSearch(index)
                            width: fileList.width
                            height: searchMatch ? 42 : 0
                            visible: searchMatch
                            color: rowHover.hovered ? Theme.hoverSurface : "transparent"
                            HoverHandler { id: rowHover }
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: function(eventPoint, button) {
                                    page.selectIndex(index, button.modifiers)
                                    page.forceActiveFocus()
                                }
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 7
                                CheckBox {
                                    Layout.preferredWidth: 24
                                    checked: page.isSelected(index)
                                    onClicked: page.toggleIndex(index)
                                }
                                ThemedIcon {
                                    source: Theme.icon("music-2-line")
                                    tint: Theme.iconSecondary
                                    sourceSize.width: 16
                                    sourceSize.height: 16
                                }
                                Label {
                                    text: fileEntry.fileName || ""
                                    color: Theme.primaryText
                                    Layout.preferredWidth: 130
                                    elide: Text.ElideRight
                                }
                                Label {
                                    text: fileEntry.path ? String(fileEntry.path).replace(/[^\\/]+$/, "") : ""
                                    color: Theme.secondaryText
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    text: String(fileEntry.extension || "").toUpperCase()
                                    color: Theme.secondaryText
                                    Layout.preferredWidth: 62
                                }
                                Label {
                                    text: page.formatSize(fileEntry.fileSize)
                                    color: Theme.secondaryText
                                    Layout.preferredWidth: 72
                                }
                                Label {
                                    text: qsTr("就绪")
                                    color: Theme.waveformGreen
                                    Layout.preferredWidth: 54
                                }
                            }
                        }

                        ColumnLayout {
                            anchors.centerIn: parent
                            visible: FilenameProcessor.fileCount === 0
                            spacing: 8
                            ThemedIcon {
                                Layout.alignment: Qt.AlignHCenter
                                source: Theme.icon("folder-open-line")
                                tint: Theme.iconSecondary
                                sourceSize.width: 28
                                sourceSize.height: 28
                            }
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("拖入音频文件，或使用上方添加命令")
                                color: Theme.secondaryText
                            }
                        }
                    }

                }
            }

            ColumnLayout {
                id: rulesColumn
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: Math.max(620, page.width * 0.61)
                spacing: 8

                Rectangle {
                    id: rulesPanel
                    objectName: "filenameRulesPanel"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 258
                    color: Theme.panel
                    border.color: Theme.border
                    border.width: 1
                    radius: Theme.radiusSm

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 7
                        Label {
                            text: qsTr("批量文件名处理")
                            color: Theme.primaryText
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            GridLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 320
                                columns: 2
                                rowSpacing: 6
                                columnSpacing: 8
                                Label { text: qsTr("前缀"); color: Theme.secondaryText }
                                TextField {
                                    id: prefixField
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("留空：删除识别到的原前缀/序号")
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("填写则添加；留空则删除文件名开头的标签和序号")
                                    onTextChanged: page.refreshPreview()
                                }
                                Label { text: qsTr("后缀"); color: Theme.secondaryText }
                                TextField {
                                    id: suffixField
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("留空：删除识别到的原后缀")
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("填写则添加；留空则删除文件名末尾的尾标和标签")
                                    onTextChanged: page.refreshPreview()
                                }
                                Label { text: qsTr("删除前缀"); color: Theme.secondaryText }
                                TextField {
                                    id: removePrefixField
                                    objectName: "filenameRemovePrefixField"
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("精确匹配文件名开头")
                                    onTextChanged: page.refreshPreview()
                                }
                                Label { text: qsTr("删除后缀"); color: Theme.secondaryText }
                                TextField {
                                    id: removeSuffixField
                                    objectName: "filenameRemoveSuffixField"
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("精确匹配扩展名前的结尾")
                                    onTextChanged: page.refreshPreview()
                                }
                                Label { text: qsTr("大小写规则"); color: Theme.secondaryText }
                                ComboBox {
                                    id: caseBox
                                    objectName: "filenameCaseBox"
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 112
                                    model: [
                                        { text: qsTr("保持不变"), value: "keep" },
                                        { text: qsTr("全部小写"), value: "lower" },
                                        { text: qsTr("全部大写"), value: "upper" },
                                        { text: qsTr("标题格式"), value: "title" }
                                    ]
                                    textRole: "text"
                                    valueRole: "value"
                                    onCurrentValueChanged: page.refreshPreview()
                                }
                            }
                            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.border }
                            ColumnLayout {
                                Layout.fillWidth: true
                                CheckBox {
                                    id: replaceSpacesCheck
                                    text: qsTr("替换空格为下划线")
                                    checked: false
                                    onToggled: page.refreshPreview()
                                }
                                CheckBox {
                                    id: preserveExtensionCheck
                                    text: qsTr("保留扩展名")
                                    checked: true
                                    onToggled: page.refreshPreview()
                                }
                                CheckBox {
                                    id: removeAffixesWhenBlankCheck
                                    text: qsTr("自动识别并删除常见前后缀")
                                    checked: false
                                    onToggled: page.refreshPreview()
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("前缀或后缀留空时，自动清理文件名中可识别的标签、尾标和序号")
                                }
                                CheckBox {
                                    id: removeLeadingSequenceCheck
                                    objectName: "filenameRemoveLeadingSequence"
                                    text: qsTr("删除开头序号")
                                    checked: false
                                    onToggled: page.refreshPreview()
                                }
                                CheckBox {
                                    id: removeTrailingSequenceCheck
                                    objectName: "filenameRemoveTrailingSequence"
                                    text: qsTr("删除结尾序号")
                                    checked: false
                                    onToggled: page.refreshPreview()
                                }
                                RowLayout {
                                    visible: false
                                    Label { text: qsTr("空格替换字符"); color: Theme.secondaryText }
                                    TextField {
                                        id: spaceReplacementField
                                        text: "_"
                                        enabled: replaceSpacesCheck.checked
                                        Layout.fillWidth: true
                                        onTextChanged: page.refreshPreview()
                                    }
                                }
                                Label { text: qsTr("冲突策略"); color: Theme.secondaryText }
                                ComboBox {
                                    id: conflictBox
                                    objectName: "filenameConflictBox"
                                    Layout.fillWidth: true
                                    model: [
                                        { text: qsTr("自动重命名（添加序号）"), value: "autoNumber" },
                                        { text: qsTr("跳过冲突文件"), value: "skip" },
                                        { text: qsTr("覆盖现有文件"), value: "overwrite" },
                                        { text: qsTr("遇到冲突停止"), value: "stop" }
                                    ]
                                    textRole: "text"
                                    valueRole: "value"
                                    onCurrentValueChanged: page.refreshPreview()
                                }
                            }
                            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.border }
                            GridLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 292
                                columns: 2
                                rowSpacing: 6
                                columnSpacing: 7
                                ThemedSwitch {
                                    id: autoNumberCheck
                                    Layout.columnSpan: 2
                                    Layout.alignment: Qt.AlignRight
                                    checked: false
                                    text: qsTr("自动序号")
                                    onToggled: page.refreshPreview()
                                }
                                Label { text: qsTr("起始序号"); color: Theme.secondaryText }
                                SpinBox {
                                    id: numberStartSpin
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 999999
                                    value: 1
                                    enabled: autoNumberCheck.checked
                                    onValueChanged: page.refreshPreview()
                                }
                                Label { text: qsTr("位数"); color: Theme.secondaryText }
                                SpinBox {
                                    id: numberDigitsSpin
                                    Layout.fillWidth: true
                                    from: 1
                                    to: 9
                                    value: 2
                                    enabled: autoNumberCheck.checked
                                    onValueChanged: page.refreshPreview()
                                }
                                Label { text: qsTr("编号位置"); color: Theme.secondaryText }
                                ComboBox {
                                    id: numberPositionBox
                                    Layout.fillWidth: true
                                    enabled: autoNumberCheck.checked
                                    model: [
                                        { text: qsTr("文件名最前"), value: "beginning" },
                                        { text: qsTr("前缀末尾"), value: "afterPrefix" },
                                        { text: qsTr("后缀之前"), value: "beforeSuffix" },
                                        { text: qsTr("后缀末尾"), value: "afterSuffix" }
                                    ]
                                    textRole: "text"
                                    valueRole: "value"
                                    currentIndex: 1
                                    onCurrentValueChanged: page.refreshPreview()
                                }
                                Label { text: qsTr("分隔符"); color: Theme.secondaryText }
                                TextField {
                                    id: numberSeparatorField
                                    Layout.fillWidth: true
                                    text: "_"
                                    enabled: autoNumberCheck.checked
                                    onTextChanged: page.refreshPreview()
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8

                    Rectangle {
                        id: previewPanel
                        objectName: "filenamePreviewPanel"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.panel
                        border.color: Theme.border
                        border.width: 1
                        radius: Theme.radiusSm

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 44
                                color: Theme.panel
                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 12
                                    text: qsTr("重命名预览（%1）").arg(previewRows.length)
                                    color: Theme.primaryText
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                color: Theme.background
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 8
                                    Label { text: qsTr("#"); color: Theme.secondaryText; Layout.preferredWidth: 28 }
                                    Label { text: qsTr("原文件名"); color: Theme.secondaryText; Layout.fillWidth: true }
                                    Label { text: qsTr("新文件名（预览）"); color: Theme.secondaryText; Layout.fillWidth: true }
                                    Label { text: qsTr("状态"); color: Theme.secondaryText; Layout.preferredWidth: 62 }
                                    Label { text: qsTr("说明"); color: Theme.secondaryText; Layout.preferredWidth: 70 }
                                }
                            }
                            ListView {
                                id: previewList
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: page.previewRows
                                boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                delegate: Rectangle {
                                    required property int index
                                    required property var modelData
                                    width: previewList.width
                                    height: 38
                                    color: index % 2 === 0 ? "transparent" : Theme.background
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        spacing: 8
                                        Label { text: index + 1; color: Theme.secondaryText; Layout.preferredWidth: 28 }
                                        Label { text: modelData.original || ""; color: Theme.primaryText; Layout.fillWidth: true; elide: Text.ElideRight }
                                        Label { text: modelData.preview || ""; color: modelData.conflict ? Theme.ratingGold : Theme.primaryText; Layout.fillWidth: true; elide: Text.ElideRight }
                                        Label {
                                            text: modelData.severity === 2 ? qsTr("错误")
                                                  : modelData.conflict ? qsTr("警告") : qsTr("就绪")
                                            color: modelData.severity === 2 ? Theme.favoriteRed
                                                  : modelData.conflict ? Theme.ratingGold : Theme.waveformGreen
                                            Layout.preferredWidth: 62
                                        }
                                        Label {
                                            text: modelData.reason || (modelData.preview === modelData.original
                                                  ? qsTr("无需修改") : qsTr("可执行"))
                                            color: modelData.severity === 2 ? Theme.favoriteRed : Theme.secondaryText
                                            Layout.preferredWidth: 70
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        objectName: "filenameValidationPanel"
                        visible: page.width >= 1500
                        Layout.preferredWidth: 280
                        Layout.fillHeight: true
                        color: Theme.panel
                        border.color: Theme.border
                        border.width: 1
                        radius: Theme.radiusSm
                        RowLayout {
                            visible: false
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8
                            Label {
                                text: qsTr("冲突与验证")
                                color: Theme.primaryText
                                font.weight: Font.DemiBold
                            }
                            Label { text: qsTr("全部文件  %1").arg(previewRows.length); color: Theme.secondaryText }
                            Label { text: qsTr("就绪  %1").arg(readyCount); color: Theme.waveformGreen }
                            Label { text: qsTr("警告  %1").arg(warningCount); color: warningCount > 0 ? Theme.ratingGold : Theme.secondaryText }
                            Label { text: qsTr("错误  %1").arg(errorCount); color: errorCount > 0 ? Theme.favoriteRed : Theme.secondaryText }
                            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.border }
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("提示\n文件名长度建议不超过 255 个字符；\n某些字符在 Windows 系统中不可用：\n\\ / : * ? \" < > |")
                                color: Theme.secondaryText
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                            }
                        }
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8
                            Label { text: qsTr("冲突与验证"); color: Theme.primaryText; font.weight: Font.DemiBold }
                            Label { text: qsTr("全部文件  %1").arg(previewRows.length); color: Theme.secondaryText }
                            Label { text: qsTr("就绪  %1").arg(readyCount); color: Theme.waveformGreen }
                            Label { text: qsTr("警告  %1").arg(warningCount); color: warningCount > 0 ? Theme.ratingGold : Theme.secondaryText }
                            Label { text: qsTr("错误  %1").arg(errorCount); color: errorCount > 0 ? Theme.favoriteRed : Theme.secondaryText }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("重命名先进入临时路径，再一次性提交；失败时回滚，不覆盖音频内容。")
                                color: Theme.secondaryText
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                            }
                            Item { Layout.fillHeight: true }
                        }
                    }
                }

                Rectangle {
                    visible: page.width < 1500
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 56 : 0
                    color: Theme.panel
                    border.color: Theme.border
                    border.width: 1
                    radius: Theme.radiusSm
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10
                        Label { text: qsTr("冲突与验证"); color: Theme.primaryText; font.weight: Font.DemiBold }
                        Label { text: qsTr("全部 %1").arg(previewRows.length); color: Theme.secondaryText }
                        Label { text: qsTr("就绪 %1").arg(readyCount); color: Theme.waveformGreen }
                        Label { text: qsTr("警告 %1").arg(warningCount); color: warningCount > 0 ? Theme.ratingGold : Theme.secondaryText }
                        Label { text: qsTr("错误 %1").arg(errorCount); color: errorCount > 0 ? Theme.favoriteRed : Theme.secondaryText }
                        Item { Layout.fillWidth: true }
                        Label { text: qsTr("事务提交，失败回滚"); color: Theme.secondaryText; font.pixelSize: 10 }
                    }
                }
            }
        }

        Rectangle {
            id: bottomBar
            objectName: "filenameBottomBar"
            Layout.fillWidth: true
            Layout.preferredHeight: 132
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.topMargin: 32
                anchors.bottomMargin: 16
                anchors.leftMargin: 12
                anchors.rightMargin: 24
                spacing: 20
                Rectangle {
                    Layout.preferredWidth: 278
                    Layout.fillHeight: true
                    color: Theme.background
                    border.color: Theme.border
                    radius: Theme.radiusSm
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                    spacing: 1
                    Label { text: qsTr("成功预览数量"); color: Theme.secondaryText; font.pixelSize: 14 }
                    Label { text: qsTr("%1 个文件").arg(readyCount); color: Theme.waveformGreen; font.pixelSize: 24; font.weight: Font.DemiBold }
                    }
                }
                Rectangle {
                    Layout.preferredWidth: 266
                    Layout.fillHeight: true
                    color: Theme.background
                    border.color: Theme.border
                    radius: Theme.radiusSm
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                    spacing: 1
                    Label { text: qsTr("冲突数量"); color: Theme.secondaryText; font.pixelSize: 14 }
                    Label { text: qsTr("%1 个文件").arg(conflictCount); color: conflictCount > 0 ? Theme.ratingGold : Theme.secondaryText; font.pixelSize: 24; font.weight: Font.DemiBold }
                    }
                }
                Rectangle {
                    Layout.preferredWidth: 244
                    Layout.fillHeight: true
                    color: Theme.background
                    border.color: Theme.border
                    radius: Theme.radiusSm
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                    spacing: 1
                    Label { text: qsTr("可撤销本次重命名"); color: Theme.secondaryText; font.pixelSize: 14 }
                    Label { text: FilenameProcessor.canUndo ? qsTr("是") : qsTr("否"); color: Theme.cyan; font.pixelSize: 24 }
                    }
                    TapHandler {
                        enabled: FilenameProcessor.canUndo && !FilenameProcessor.busy
                        cursorShape: Qt.PointingHandCursor
                        onTapped: FilenameProcessor.undoLast()
                    }
                }
                ProgressBar {
                    Layout.fillWidth: true
                    visible: FilenameProcessor.busy
                    value: FilenameProcessor.progress
                }
                Item { Layout.fillWidth: !FilenameProcessor.busy }
                Label {
                    Layout.preferredWidth: 300
                    visible: !FilenameProcessor.busy
                    text: qsTr("重命名操作将在处理后生成日志，\n如需退回，可通过撤销恢复原名列表进行还原。")
                    color: Theme.secondaryText
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                Button {
                    Layout.preferredWidth: 184
                    Layout.fillHeight: true
                    text: qsTr("取消")
                    visible: true
                    enabled: FilenameProcessor.busy
                    onClicked: FilenameProcessor.cancel()
                }
                Button {
                    Layout.preferredWidth: 220
                    Layout.fillHeight: true
                    text: qsTr("开始重命名")
                    icon.source: Theme.icon("play-fill")
                    highlighted: true
                    enabled: FilenameProcessor.fileCount > 0 && runnableCount > 0
                             && !(conflictBox.currentValue === "stop"
                                  && errorCount > 0)
                             && !FilenameProcessor.busy
                    onClicked: {
                        if (conflictBox.currentValue === "overwrite")
                            overwriteConfirmation.open()
                        else
                            FilenameProcessor.apply(
                                page.rules(), page.selectedIndices,
                                conflictBox.currentValue)
                    }
                }
            }
            Label {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 18
                anchors.topMargin: 10
                text: qsTr("处理摘要")
                color: Theme.primaryText
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
        }
    }

    Connections {
        target: FilenameProcessor
        function onEntriesLoaded() {
            page.selectedIndices = Array.from(
                {length: FilenameProcessor.fileCount},
                function(_, index) { return index })
            page.selectionAnchor = -1
            ++page.entryRevision
            page.refreshPreview()
        }
        function onEntriesChanged() {
            ++page.entryRevision
            page.refreshPreview()
        }
    }
}

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

    readonly property int conflictCount: {
        let count = 0
        for (let i = 0; i < previewRows.length; ++i)
            if (previewRows[i].conflict) ++count
        return count
    }
    readonly property int unchangedCount: {
        let count = 0
        for (let i = 0; i < previewRows.length; ++i)
            if (previewRows[i].preview === previewRows[i].original) ++count
        return count
    }
    readonly property int readyCount: Math.max(0, previewRows.length - conflictCount)

    function isSelected(index) { return selectedIndices.indexOf(index) >= 0 }
    function entry(index) {
        entryRevision
        return FilenameProcessor.entryAt(index)
    }
    function matchesSearch(index) {
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
            replaceSpaces: replaceSpacesCheck.checked,
            spaceReplacement: spaceReplacementField.text,
            caseMode: caseBox.currentValue,
            preserveExtension: preserveExtensionCheck.checked,
            autoNumber: autoNumberCheck.checked,
            numberStart: numberStartSpin.value,
            numberDigits: numberDigitsSpin.value,
            numberPosition: numberPositionBox.currentValue,
            numberSeparator: numberSeparatorField.text
        }
    }
    function refreshPreview() {
        previewRows = FilenameProcessor.preview(rules(), selectedIndices)
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

    FileDropArea {
        objectName: "filenameDropArea"
        anchors.fill: parent
        z: 20
        onUrlsDropped: function(urls) { FilenameProcessor.loadFiles(urls) }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            spacing: 7
            Button {
                text: qsTr("添加文件")
                icon.source: Theme.icon("add-line")
                onClicked: audioDialog.open()
            }
            Button {
                text: qsTr("添加文件夹")
                icon.source: Theme.icon("folder-add-line")
                onClicked: folderDialog.open()
            }
            Button {
                text: qsTr("从播放器添加")
                icon.source: Theme.icon("music-2-line")
                enabled: !FilenameProcessor.busy
                         && PlaybackController.currentTrackId.length > 0
                ToolTip.visible: hovered
                ToolTip.text: qsTr("需要先在播放器中选择歌曲")
                onClicked: page.addCurrentPlayerTrack()
            }
            Button {
                text: qsTr("移除选中")
                icon.source: Theme.icon("delete-bin-line")
                enabled: selectedIndices.length > 0 && !FilenameProcessor.busy
                onClicked: deleteSelection()
            }
            Button {
                text: qsTr("清空")
                enabled: FilenameProcessor.fileCount > 0 && !FilenameProcessor.busy
                onClicked: FilenameProcessor.clear()
            }
            Button {
                text: qsTr("撤销上次")
                icon.source: Theme.icon("arrow-go-back-line")
                enabled: FilenameProcessor.canUndo && !FilenameProcessor.busy
                onClicked: FilenameProcessor.undoLast()
            }
            Item { Layout.fillWidth: true }
            TextField {
                Layout.preferredWidth: 250
                placeholderText: qsTr("搜索文件名或所在目录")
                onTextChanged: page.searchText = text
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            Rectangle {
                id: filePanel
                objectName: "filenameFilePanel"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: Math.max(380, page.width * 0.38)
                Layout.maximumWidth: page.width * 0.45
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
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
                            Label { text: qsTr("文件名"); color: Theme.secondaryText; Layout.fillWidth: true }
                            Label { text: qsTr("所在目录"); color: Theme.secondaryText; Layout.preferredWidth: 130 }
                            Label { text: qsTr("扩展名"); color: Theme.secondaryText; Layout.preferredWidth: 54 }
                            Label { text: qsTr("大小"); color: Theme.secondaryText; Layout.preferredWidth: 64 }
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
                            height: searchMatch ? 40 : 0
                            visible: searchMatch
                            color: page.isSelected(index) ? Theme.activeSelection
                                  : (rowHover.hovered ? Theme.hoverSurface : "transparent")
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
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                                Label {
                                    text: fileEntry.path ? String(fileEntry.path).replace(/[^\\/]+$/, "") : ""
                                    color: Theme.secondaryText
                                    Layout.preferredWidth: 130
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    text: String(fileEntry.extension || "").toUpperCase()
                                    color: Theme.secondaryText
                                    Layout.preferredWidth: 54
                                }
                                Label {
                                    text: page.formatSize(fileEntry.fileSize)
                                    color: Theme.secondaryText
                                    Layout.preferredWidth: 64
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

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        color: Theme.background
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            Label {
                                text: qsTr("已选择 %1 个文件").arg(selectedIndices.length)
                                color: Theme.secondaryText
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: qsTr("共 %1 个文件").arg(FilenameProcessor.fileCount)
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
                Layout.preferredWidth: Math.max(620, page.width * 0.52)
                spacing: 8

                Rectangle {
                    id: rulesPanel
                    objectName: "filenameRulesPanel"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 252
                    color: Theme.panel
                    border.color: Theme.border
                    border.width: 1
                    radius: Theme.radiusSm

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 7
                        Label {
                            text: qsTr("批量文件名规则")
                            color: Theme.primaryText
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            GridLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 252
                                columns: 2
                                rowSpacing: 6
                                columnSpacing: 8
                                Label { text: qsTr("前缀"); color: Theme.secondaryText }
                                TextField { id: prefixField; Layout.fillWidth: true; onTextChanged: page.refreshPreview() }
                                Label { text: qsTr("后缀"); color: Theme.secondaryText }
                                TextField { id: suffixField; Layout.fillWidth: true; onTextChanged: page.refreshPreview() }
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
                                CheckBox {
                                    id: preserveExtensionCheck
                                    text: qsTr("保留扩展名")
                                    checked: true
                                    Layout.columnSpan: 2
                                    onToggled: page.refreshPreview()
                                }
                                CheckBox {
                                    id: replaceSpacesCheck
                                    text: qsTr("替换空格")
                                    onToggled: page.refreshPreview()
                                }
                                TextField {
                                    id: spaceReplacementField
                                    text: "_"
                                    enabled: replaceSpacesCheck.checked
                                    Layout.fillWidth: true
                                    onTextChanged: page.refreshPreview()
                                }
                            }
                            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.border }
                            GridLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 292
                                columns: 2
                                rowSpacing: 6
                                columnSpacing: 7
                                CheckBox {
                                    id: autoNumberCheck
                                    text: qsTr("自动编号")
                                    Layout.columnSpan: 2
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
                                Label { text: qsTr("冲突策略"); color: Theme.secondaryText }
                                ComboBox {
                                    id: conflictBox
                                    objectName: "filenameConflictBox"
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 180
                                    model: [
                                        { text: qsTr("自动编号避让"), value: "autoNumber" },
                                        { text: qsTr("跳过冲突文件"), value: "skip" },
                                        { text: qsTr("遇到冲突停止"), value: "stop" }
                                    ]
                                    textRole: "text"
                                    valueRole: "value"
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
                                    height: 36
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
                            Label { text: qsTr("冲突  %1").arg(conflictCount); color: conflictCount > 0 ? Theme.ratingGold : Theme.secondaryText }
                            Label { text: qsTr("未更改  %1").arg(unchangedCount); color: Theme.secondaryText }
                            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.border }
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("重命名先进入临时路径，再一次性提交；失败时回滚，不覆盖音频内容。")
                                color: Theme.secondaryText
                                font.pixelSize: 10
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
                            Label { text: qsTr("冲突  %1").arg(conflictCount); color: conflictCount > 0 ? Theme.ratingGold : Theme.secondaryText }
                            Label { text: qsTr("未更改 %1").arg(unchangedCount); color: Theme.secondaryText }
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
                        Label { text: qsTr("冲突 %1").arg(conflictCount); color: conflictCount > 0 ? Theme.ratingGold : Theme.secondaryText }
                        Label { text: qsTr("未更改 %1").arg(unchangedCount); color: Theme.secondaryText }
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
            Layout.preferredHeight: 118
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 10
                Rectangle {
                    Layout.preferredWidth: 210
                    Layout.fillHeight: true
                    color: Theme.background
                    border.color: Theme.border
                    radius: Theme.radiusSm
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                    spacing: 1
                    Label { text: qsTr("成功预览数量"); color: Theme.secondaryText; font.pixelSize: 10 }
                    Label { text: readyCount; color: Theme.waveformGreen; font.pixelSize: 18; font.weight: Font.DemiBold }
                    }
                }
                Rectangle {
                    Layout.preferredWidth: 190
                    Layout.fillHeight: true
                    color: Theme.background
                    border.color: Theme.border
                    radius: Theme.radiusSm
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                    spacing: 1
                    Label { text: qsTr("冲突数量"); color: Theme.secondaryText; font.pixelSize: 10 }
                    Label { text: conflictCount; color: conflictCount > 0 ? Theme.ratingGold : Theme.secondaryText; font.pixelSize: 18; font.weight: Font.DemiBold }
                    }
                }
                Rectangle {
                    Layout.preferredWidth: 190
                    Layout.fillHeight: true
                    color: Theme.background
                    border.color: Theme.border
                    radius: Theme.radiusSm
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                    spacing: 1
                    Label { text: qsTr("可撤销事务"); color: Theme.secondaryText; font.pixelSize: 10 }
                    Label { text: FilenameProcessor.canUndo ? qsTr("可撤销") : qsTr("无"); color: Theme.primaryText; font.pixelSize: 14 }
                    }
                }
                ProgressBar {
                    Layout.fillWidth: true
                    visible: FilenameProcessor.busy
                    value: FilenameProcessor.progress
                }
                Item { Layout.fillWidth: !FilenameProcessor.busy }
                Button {
                    text: qsTr("取消")
                    visible: true
                    enabled: FilenameProcessor.busy
                    onClicked: FilenameProcessor.cancel()
                }
                Button {
                    text: qsTr("开始重命名")
                    icon.source: Theme.icon("play-fill")
                    highlighted: true
                    enabled: FilenameProcessor.fileCount > 0 && readyCount > 0
                             && !FilenameProcessor.busy
                    onClicked: FilenameProcessor.apply(
                        page.rules(), page.selectedIndices, conflictBox.currentValue)
                }
            }
        }
    }

    Connections {
        target: FilenameProcessor
        function onEntriesLoaded() {
            page.selectedIndices = []
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

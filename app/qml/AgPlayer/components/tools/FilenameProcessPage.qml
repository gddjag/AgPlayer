import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Control {
    id: page
    objectName: "filenameProcessPage"
    padding: 0
    clip: true
    font.pixelSize: Theme.fontSizeBody
    focus: true
    background: Rectangle { color: Theme.background }

    readonly property color panelColor: Theme.panel
    readonly property color neutralActionColor: Theme.elevated
    readonly property color actionBlue: Theme.accent
    readonly property bool compactLayout: width < 1500
    readonly property real desktopWorkspaceWidth: 1012

    component AccentCheckBox: ThemedCheckBox {}

    component StatusGlyph: Item {
        id: glyph
        property int status: 0 // 0 ready, 1 warning, 2 error
        property bool square: false
        property real markSize: 15
        implicitWidth: Theme.iconSizeMd
        implicitHeight: Theme.iconSizeMd
        Rectangle {
            anchors.centerIn: parent
            width: glyph.markSize
            height: glyph.markSize
            radius: glyph.square ? 2 : glyph.markSize / 2
            visible: glyph.status !== 1
            color: "transparent"
            border.width: 1.5
            border.color: glyph.status === 2 ? Theme.error : Theme.success
            ThemedIcon {
                anchors.centerIn: parent
                width: glyph.markSize * 0.72
                height: width
                sourceSize.width: width
                sourceSize.height: height
                source: Theme.icon(glyph.status === 2 ? "close-line" : "check-line")
                tint: parent.border.color
            }
        }
        ThemedIcon {
            anchors.centerIn: parent
            visible: glyph.status === 1
            width: glyph.markSize
            height: width
            sourceSize.width: width
            sourceSize.height: height
            source: Theme.icon("error-warning-line")
            tint: Theme.warning
        }
    }

    component CompactSpinBox: SpinBox {
        id: compactSpin
        editable: true
        implicitHeight: Theme.controlHeight
        contentItem: TextInput {
            z: 2
            text: Number(compactSpin.value).toLocaleString(
                      compactSpin.locale, "f", 0)
            color: Theme.primaryText
            selectionColor: Theme.accent
            selectedTextColor: Theme.onCyanText
            horizontalAlignment: Qt.AlignLeft
            verticalAlignment: Qt.AlignVCenter
            leftPadding: 10
            rightPadding: 30
            readOnly: !compactSpin.editable
            validator: compactSpin.validator
            inputMethodHints: Qt.ImhFormattedNumbersOnly
        }
        up.indicator: Rectangle {
            x: compactSpin.width - width - 1
            y: 1
            width: 26
            height: compactSpin.height / 2 - 1
            color: compactSpin.up.pressed ? Theme.hoverSurface : "transparent"
            ThemedIcon {
                anchors.centerIn: parent
                source: Theme.icon("arrow-up-s-line")
                tint: Theme.iconSecondary
                sourceSize.width: 14
                sourceSize.height: 14
            }
        }
        down.indicator: Rectangle {
            x: compactSpin.width - width - 1
            y: compactSpin.height / 2
            width: 26
            height: compactSpin.height / 2 - 1
            color: compactSpin.down.pressed ? Theme.hoverSurface : "transparent"
            ThemedIcon {
                anchors.centerIn: parent
                source: Theme.icon("arrow-down-s-line")
                tint: Theme.iconSecondary
                sourceSize.width: 14
                sourceSize.height: 14
            }
        }
        background: Rectangle {
            color: Theme.background
            radius: Theme.radiusSm
            border.width: 1
            border.color: compactSpin.activeFocus ? Theme.focus : Theme.border
        }
    }

    component CompactComboBox: ThemedComboBox {}

    component ActionButton: ThemedButton {
        id: actionButton
        property real labelPixelSize: Theme.fontSizeBody

        contentItem: RowLayout {
            spacing: Theme.spacingSm
            ThemedIcon {
                visible: String(actionButton.icon.source).length > 0
                source: actionButton.icon.source
                tint: !actionButton.enabled ? Theme.textDisabled
                      : actionButton.primary ? Theme.accentText
                                             : Theme.textPrimary
                sourceSize.width: Theme.iconSizeMd
                sourceSize.height: Theme.iconSizeMd
            }
            Text {
                text: actionButton.text
                color: !actionButton.enabled ? Theme.textDisabled
                       : actionButton.primary ? Theme.accentText
                                              : Theme.textPrimary
                font.family: Theme.fontPrimary
                font.pixelSize: actionButton.labelPixelSize
                font.weight: actionButton.primary ? Font.Medium : Font.Normal
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }

    property var selectedIndices: []
    property var previewRows: []
    property int selectionAnchor: -1
    property int entryRevision: 0
    property bool qaReferenceMode: false

    onQaReferenceModeChanged: {
        if (!qaReferenceMode) return
        prefixAddRadio.checked = true
        suffixAddRadio.checked = true
        prefixField.text = "[Live]_"
        suffixField.text = "_Remaster"
        replaceSpacesCheck.checked = true
        preserveExtensionCheck.checked = true
        autoNumberCheck.checked = false
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
            prefix: prefixAddRadio.checked ? prefixField.text : "",
            suffix: suffixAddRadio.checked ? suffixField.text : "",
            removePrefix: prefixRemoveRadio.checked ? removePrefixField.text : "",
            removeSuffix: suffixRemoveRadio.checked ? removeSuffixField.text : "",
            replaceSpaces: replaceSpacesCheck.checked,
            spaceReplacement: spaceReplacementField.text,
            caseMode: caseBox.currentValue,
            preserveExtension: preserveExtensionCheck.checked,
            removePrefixWhenEmpty: prefixAddRadio.checked && prefixField.text.length === 0,
            removeSuffixWhenEmpty: suffixAddRadio.checked && suffixField.text.length === 0,
            removeSequenceWhenEmpty: false,
            removeSequenceAtStart: removeSequenceCheck.checked,
            removeSequenceAtEnd: removeSequenceCheck.checked,
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
    ThemedDialog {
        id: overwriteConfirmation
        modal: true
        anchors.centerIn: parent
        title: qsTr("确认覆盖现有文件")
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: FilenameProcessor.apply(
            page.rules(), page.selectedIndices, conflictBox.currentValue)
        ColumnLayout {
            width: Math.min(420, page.width - 2 * Theme.spacing2Xl)
            spacing: Theme.spacingMd
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("覆盖策略会先备份目标文件；重命名成功后仍可安全撤销。是否继续？")
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeBody
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
        anchors.leftMargin: Theme.spacingXs
        anchors.rightMargin: Theme.spacingSm
        anchors.topMargin: Theme.spacingSm
        anchors.bottomMargin: Theme.spacingXs
        spacing: Theme.spacingXs

        Rectangle {
            objectName: "filenameCommandBar"
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.settingsRowHeight + Theme.spacingSm
            color: page.panelColor
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingLg
                anchors.rightMargin: Theme.spacingLg
                spacing: Theme.spacingMd
            ActionButton {
                Layout.preferredWidth: 124
                Layout.preferredHeight: Theme.controlHeightProminent
                text: qsTr("添加文件")
                icon.source: Theme.icon("add-line")
                onClicked: audioDialog.open()
            }
            ActionButton {
                Layout.preferredWidth: 142
                Layout.preferredHeight: Theme.controlHeightProminent
                text: qsTr("添加文件夹")
                icon.source: Theme.icon("folder-add-line")
                onClicked: folderDialog.open()
            }
            ActionButton {
                Layout.preferredWidth: 170
                Layout.preferredHeight: Theme.controlHeightProminent
                text: qsTr("从播放列表添加")
                icon.source: Theme.icon("music-2-line")
                enabled: !FilenameProcessor.busy
                         && PlaybackController.currentTrackId.length > 0
                ToolTip.visible: hovered
                ToolTip.text: qsTr("需要先在播放器中选择歌曲")
                onClicked: page.addCurrentPlayerTrack()
            }
            ActionButton {
                Layout.preferredWidth: 126
                Layout.preferredHeight: Theme.controlHeightProminent
                text: qsTr("移除选中")
                icon.source: Theme.icon("delete-bin-line")
                enabled: selectedIndices.length > 0 && !FilenameProcessor.busy
                onClicked: deleteSelection()
            }
            ActionButton {
                Layout.preferredWidth: 126
                Layout.preferredHeight: Theme.controlHeightProminent
                text: qsTr("清空列表")
                icon.source: Theme.icon("delete-bin-line")
                enabled: FilenameProcessor.fileCount > 0 && !FilenameProcessor.busy
                onClicked: FilenameProcessor.clear()
            }
            Item { Layout.fillWidth: true }
            }
        }

        Flickable {
            id: workspaceScroller
            objectName: "filenameWorkspaceScroller"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: Math.max(width, page.desktopWorkspaceWidth)
            contentHeight: height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ScrollBar {
                policy: page.compactLayout ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }

            RowLayout {
                width: workspaceScroller.contentWidth
                height: workspaceScroller.height
                spacing: Theme.spacingXs

            Rectangle {
                id: filePanel
                objectName: "filenameFilePanel"
                Layout.fillWidth: false
                Layout.fillHeight: true
                Layout.preferredWidth: Math.max(380, Math.round(page.width * 0.3711))
                Layout.maximumWidth: Layout.preferredWidth
                color: page.panelColor
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.listRowHeight
                        color: page.panelColor
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 18
                            text: qsTr("已选择的文件（%1）").arg(FilenameProcessor.fileCount)
                            color: Theme.primaryText
                            font.pixelSize: Theme.fontSizeBody
                            font.weight: Font.DemiBold
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.tableHeaderHeight
                        color: Theme.background
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 8
                            spacing: 7
                            AccentCheckBox {
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
                            Label { text: qsTr("所在目录"); color: Theme.secondaryText; Layout.preferredWidth: 164 }
                            Label { text: qsTr("扩展名"); color: Theme.secondaryText; Layout.preferredWidth: 78 }
                            Label { text: qsTr("大小"); color: Theme.secondaryText; Layout.preferredWidth: 75 }
                            Label { text: qsTr("状态"); color: Theme.secondaryText; Layout.preferredWidth: 62 }
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
                            width: fileList.width
                            height: Theme.listRowHeight
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
                                anchors.leftMargin: 16
                                anchors.rightMargin: 8
                                spacing: 7
                                AccentCheckBox {
                                    Layout.preferredWidth: 24
                                    checked: page.isSelected(index)
                                    onClicked: page.toggleIndex(index)
                                }
                                Rectangle {
                                    Layout.preferredWidth: 20
                                    Layout.preferredHeight: 20
                                    radius: 3
                                    color: {
                                        const ext = String(fileEntry.extension || "").toLowerCase()
                                        if (ext === "flac" || ext === "wav") return Theme.waveformGreen
                                        if (ext === "m4a" || ext === "opus") return Theme.waveformViolet
                                        return Theme.waveformBlue
                                    }
                                    ThemedIcon {
                                        anchors.centerIn: parent
                                        source: Theme.icon("music-2-line")
                                        tint: Theme.onBrandGradientText
                                        sourceSize.width: 14
                                        sourceSize.height: 14
                                    }
                                }
                                Label {
                                    text: fileEntry.fileName || ""
                                    color: Theme.primaryText
                                    Layout.preferredWidth: 125
                                    elide: Text.ElideRight
                                }
                                Label {
                                    text: fileEntry.path ? String(fileEntry.path).replace(/[^\\/]+$/, "") : ""
                                    color: Theme.secondaryText
                                    Layout.preferredWidth: 164
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    text: fileEntry.extension ? "." + String(fileEntry.extension).toLowerCase() : ""
                                    color: Theme.secondaryText
                                    Layout.preferredWidth: 78
                                }
                                Label {
                                    text: page.formatSize(fileEntry.fileSize)
                                    color: Theme.secondaryText
                                    Layout.preferredWidth: 75
                                }
                                RowLayout {
                                    Layout.preferredWidth: 62
                                    spacing: 5
                                    StatusGlyph { status: 0 }
                                    Label { text: qsTr("就绪"); color: Theme.secondaryText }
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
                spacing: 6

                Rectangle {
                    id: rulesPanel
                    objectName: "filenameRulesPanel"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 259
                    color: page.panelColor
                    border.color: Theme.border
                    border.width: 1
                    radius: Theme.radiusSm

                    ButtonGroup { id: prefixModeGroup }
                    ButtonGroup { id: suffixModeGroup }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 46
                            color: page.panelColor
                            Label {
                                anchors.left: parent.left
                                anchors.leftMargin: 22
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("批量文件名处理")
                                color: Theme.primaryText
                                font.pixelSize: Theme.fontSizeBody
                                font.weight: Font.DemiBold
                            }
                        }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.leftMargin: 22
                            Layout.rightMargin: 12
                            Layout.topMargin: 8
                            Layout.bottomMargin: 12
                            spacing: 12

                            ColumnLayout {
                                Layout.preferredWidth: 220
                                Layout.fillHeight: true
                                spacing: 5
                                Label { text: qsTr("前缀"); color: Theme.primaryText; font.weight: Font.DemiBold }
                                ThemedRadioButton {
                                    id: prefixAddRadio
                                    objectName: "filenamePrefixAddRadio"
                                    text: qsTr("添加前缀")
                                    checked: true
                                    ButtonGroup.group: prefixModeGroup
                                    onToggled: page.refreshPreview()
                                }
                                ThemedTextField {
                                    id: prefixField
                                    objectName: "filenamePrefixField"
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 28
                                    Layout.rightMargin: 9
                                    Layout.preferredHeight: Theme.controlHeight
                                    enabled: prefixAddRadio.checked
                                    placeholderText: qsTr("输入要添加的前缀")
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("填写时添加；留空时删除可识别的原前缀和序号")
                                    onTextChanged: page.refreshPreview()
                                }
                                ThemedRadioButton {
                                    id: prefixRemoveRadio
                                    objectName: "filenamePrefixRemoveRadio"
                                    text: qsTr("删除前缀")
                                    ButtonGroup.group: prefixModeGroup
                                    onToggled: page.refreshPreview()
                                }
                                ThemedTextField {
                                    id: removePrefixField
                                    objectName: "filenameRemovePrefixField"
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 28
                                    Layout.rightMargin: 9
                                    Layout.preferredHeight: Theme.controlHeight
                                    enabled: prefixRemoveRadio.checked
                                    opacity: enabled ? 1.0 : 0.55
                                    placeholderText: qsTr("输入要删除的前缀")
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("仅删除文件名开头完全匹配的文字，不区分大小写")
                                    onTextChanged: page.refreshPreview()
                                }
                            }

                            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.border }

                            ColumnLayout {
                                Layout.preferredWidth: 220
                                Layout.fillHeight: true
                                spacing: 5
                                Label { text: qsTr("后缀"); color: Theme.primaryText; font.weight: Font.DemiBold }
                                ThemedRadioButton {
                                    id: suffixAddRadio
                                    objectName: "filenameSuffixAddRadio"
                                    text: qsTr("添加后缀")
                                    checked: true
                                    ButtonGroup.group: suffixModeGroup
                                    onToggled: page.refreshPreview()
                                }
                                ThemedTextField {
                                    id: suffixField
                                    objectName: "filenameSuffixField"
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 28
                                    Layout.rightMargin: 9
                                    Layout.preferredHeight: Theme.controlHeight
                                    enabled: suffixAddRadio.checked
                                    placeholderText: qsTr("输入要添加的后缀")
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("填写时添加；留空时删除可识别的原后缀")
                                    onTextChanged: page.refreshPreview()
                                }
                                ThemedRadioButton {
                                    id: suffixRemoveRadio
                                    objectName: "filenameSuffixRemoveRadio"
                                    text: qsTr("删除后缀")
                                    ButtonGroup.group: suffixModeGroup
                                    onToggled: page.refreshPreview()
                                }
                                ThemedTextField {
                                    id: removeSuffixField
                                    objectName: "filenameRemoveSuffixField"
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 28
                                    Layout.rightMargin: 9
                                    Layout.preferredHeight: Theme.controlHeight
                                    enabled: suffixRemoveRadio.checked
                                    opacity: enabled ? 1.0 : 0.55
                                    placeholderText: qsTr("输入要删除的后缀")
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("仅删除扩展名前完全匹配的文字，不区分大小写")
                                    onTextChanged: page.refreshPreview()
                                }
                            }

                            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.border }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 194
                                Layout.fillHeight: true
                                spacing: 3
                                AccentCheckBox {
                                    id: replaceSpacesCheck
                                    text: qsTr("替换空格为下划线")
                                    checked: false
                                    onToggled: page.refreshPreview()
                                }
                                AccentCheckBox {
                                    id: preserveExtensionCheck
                                    text: qsTr("保留扩展名")
                                    checked: true
                                    onToggled: page.refreshPreview()
                                }
                                RowLayout {
                                    visible: false
                                    Label { text: qsTr("空格替换字符"); color: Theme.secondaryText }
                                    ThemedTextField {
                                        id: spaceReplacementField
                                        text: "_"
                                        enabled: replaceSpacesCheck.checked
                                        Layout.fillWidth: true
                                        onTextChanged: page.refreshPreview()
                                    }
                                }
                                Label { text: qsTr("冲突策略"); color: Theme.secondaryText }
                                CompactComboBox {
                                    id: conflictBox
                                    objectName: "filenameConflictBox"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Theme.controlHeight
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
                                Label { text: qsTr("大小写规则"); color: Theme.secondaryText }
                                CompactComboBox {
                                    id: caseBox
                                    objectName: "filenameCaseBox"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Theme.controlHeight
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

                            GridLayout {
                                Layout.preferredWidth: 280
                                Layout.fillHeight: true
                                columns: 2
                                rowSpacing: 4
                                columnSpacing: 8
                                RowLayout {
                                    Layout.columnSpan: 2
                                    Layout.fillWidth: true
                                    Label { text: qsTr("自动序号"); color: Theme.primaryText; font.weight: Font.DemiBold; Layout.leftMargin: 10 }
                                    ThemedSwitch {
                                        id: autoNumberCheck
                                        objectName: "filenameAutoNumberCheck"
                                        checked: false
                                        text: ""
                                        enabled: !removeSequenceCheck.checked
                                        onCheckedChanged: {
                                            if (checked)
                                                removeSequenceCheck.checked = false
                                            page.refreshPreview()
                                        }
                                    }
                                    Item { Layout.fillWidth: true }
                                    Label { text: qsTr("删除序号"); color: Theme.primaryText; font.weight: Font.DemiBold }
                                    ThemedSwitch {
                                        id: removeSequenceCheck
                                        objectName: "filenameRemoveSequenceCheck"
                                        Layout.rightMargin: 14
                                        checked: false
                                        text: ""
                                        enabled: !autoNumberCheck.checked
                                        onCheckedChanged: {
                                            if (checked)
                                                autoNumberCheck.checked = false
                                            page.refreshPreview()
                                        }
                                    }
                                }
                                Label { text: qsTr("起始序号"); color: Theme.secondaryText; Layout.preferredWidth: 108 }
                                CompactSpinBox {
                                    id: numberStartSpin
                                    Layout.preferredWidth: 150
                                    from: 0
                                    to: 999999
                                    value: 1
                                    enabled: autoNumberCheck.checked
                                    onValueChanged: page.refreshPreview()
                                }
                                Label { text: qsTr("位数"); color: Theme.secondaryText; Layout.preferredWidth: 108 }
                                CompactSpinBox {
                                    id: numberDigitsSpin
                                    Layout.preferredWidth: 150
                                    from: 1
                                    to: 9
                                    value: 2
                                    enabled: autoNumberCheck.checked
                                    onValueChanged: page.refreshPreview()
                                }
                                Label { text: qsTr("编号位置"); color: Theme.secondaryText; Layout.preferredWidth: 108 }
                                CompactComboBox {
                                    id: numberPositionBox
                                    Layout.preferredWidth: 150
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
                                Label { text: qsTr("分隔符"); color: Theme.secondaryText; Layout.preferredWidth: 108 }
                                ThemedTextField {
                                    id: numberSeparatorField
                                    Layout.preferredWidth: 150
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
                    spacing: 6

                    Rectangle {
                        id: previewPanel
                        objectName: "filenamePreviewPanel"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: page.panelColor
                        border.color: Theme.border
                        border.width: 1
                        radius: Theme.radiusSm

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0
                            Rectangle {
                                Layout.fillWidth: true
                            Layout.preferredHeight: Theme.listRowHeight
                                color: page.panelColor
                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 22
                                    text: qsTr("重命名预览（%1）").arg(previewRows.length)
                                    color: Theme.primaryText
                                    font.pixelSize: Theme.fontSizeBody
                                    font.weight: Font.DemiBold
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.tableHeaderHeight
                                color: Theme.background
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 24
                                    anchors.rightMargin: 10
                                    spacing: 8
                                    Label { text: qsTr("#"); color: Theme.secondaryText; Layout.preferredWidth: 46 }
                                    Label { text: qsTr("原文件名"); color: Theme.secondaryText; Layout.preferredWidth: 186 }
                                    Label { text: qsTr("新文件名（预览）"); color: Theme.secondaryText; Layout.fillWidth: true }
                                    Label { text: qsTr("状态"); color: Theme.secondaryText; Layout.preferredWidth: 62 }
                                    Label { text: qsTr("说明"); color: Theme.secondaryText; Layout.preferredWidth: 83 }
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
                                    height: Theme.listRowHeight
                                    color: index % 2 === 0 ? "transparent" : Theme.background
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 24
                                        anchors.rightMargin: 10
                                        spacing: 8
                                        Label { text: index + 1; color: Theme.secondaryText; Layout.preferredWidth: 46 }
                                        Label { text: modelData.original || ""; color: Theme.primaryText; Layout.preferredWidth: 186; elide: Text.ElideRight }
                                        Label { text: modelData.preview || ""; color: modelData.conflict ? Theme.warning : Theme.primaryText; Layout.fillWidth: true; elide: Text.ElideRight }
                                        Item {
                                            Layout.preferredWidth: 62
                                            StatusGlyph {
                                                anchors.centerIn: parent
                                                status: modelData.severity === 2 ? 2
                                                        : modelData.conflict ? 1 : 0
                                            }
                                        }
                                        Label {
                                            text: modelData.reason || (modelData.preview === modelData.original
                                                  ? qsTr("无需修改") : qsTr("就绪"))
                                            color: modelData.severity === 2 ? Theme.error : Theme.secondaryText
                                            Layout.preferredWidth: 83
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        objectName: "filenameValidationPanel"
                        visible: page.width >= 1500
                        Layout.preferredWidth: 285
                        Layout.fillHeight: true
                        color: page.panelColor
                        border.color: Theme.border
                        border.width: 1
                        radius: Theme.radiusSm
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 7
                            Label {
                                text: qsTr("冲突与验证")
                                color: Theme.primaryText
                                font.pixelSize: Theme.fontSizeBody
                                font.weight: Font.DemiBold
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                            Repeater {
                                model: [
                                    { label: qsTr("全部文件"), value: previewRows.length,
                                      icon: "file-copy-line", status: -1, tint: Theme.primaryText },
                                    { label: qsTr("就绪"), value: readyCount,
                                      icon: "", status: 0, tint: Theme.success },
                                    { label: qsTr("警告"), value: warningCount,
                                      icon: "", status: 1, tint: Theme.warning },
                                    { label: qsTr("错误"), value: errorCount,
                                      icon: "", status: 2, tint: Theme.error }
                                ]
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Theme.tableHeaderHeight
                                    spacing: 9
                                    Item {
                                        visible: modelData.status < 0
                                        Layout.preferredWidth: 18
                                        Layout.preferredHeight: 18
                                        ThemedIcon {
                                            anchors.centerIn: parent
                                            source: Theme.icon("file-copy-line")
                                            tint: Theme.iconPrimary
                                            sourceSize.width: Theme.iconSizeMd
                                            sourceSize.height: Theme.iconSizeMd
                                        }
                                    }
                                    StatusGlyph {
                                        visible: modelData.status >= 0
                                        status: modelData.status
                                        square: modelData.status === 0
                                    }
                                    Label { text: modelData.label; color: Theme.primaryText }
                                    Item { Layout.fillWidth: true }
                                    Label { text: modelData.value; color: Theme.primaryText; font.pixelSize: Theme.fontSizeSection }
                                }
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                            Label {
                                text: qsTr("提示")
                                color: Theme.primaryText
                                font.pixelSize: Theme.fontSizeBody
                                font.weight: Font.DemiBold
                            }
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("文件名长度建议不超过 255 个字符；\n某些字符在 Windows 系统中不可用：\n\\ / : * ? \" < > |")
                                color: Theme.secondaryText
                                font.pixelSize: Theme.fontSizeCaption
                                wrapMode: Text.WordWrap
                                lineHeight: 1.4
                            }
                            Item { Layout.fillHeight: true }
                        }
                    }
                }

                Rectangle {
                    visible: page.width < 1500
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? Theme.settingsRowHeight : 0
                    color: page.panelColor
                    border.color: Theme.border
                    border.width: 1
                    radius: Theme.radiusSm
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 24
                        anchors.rightMargin: 10
                        anchors.topMargin: 10
                        anchors.bottomMargin: 10
                        spacing: 10
                        Label { text: qsTr("冲突与验证"); color: Theme.primaryText; font.weight: Font.DemiBold }
                        Label { text: qsTr("全部 %1").arg(previewRows.length); color: Theme.secondaryText }
                        Label { text: qsTr("就绪 %1").arg(readyCount); color: Theme.success }
                        Label { text: qsTr("警告 %1").arg(warningCount); color: warningCount > 0 ? Theme.warning : Theme.secondaryText }
                        Label { text: qsTr("错误 %1").arg(errorCount); color: errorCount > 0 ? Theme.error : Theme.secondaryText }
                        Item { Layout.fillWidth: true }
                        Label { text: qsTr("事务提交，失败回滚"); color: Theme.secondaryText; font.pixelSize: Theme.fontSizeCaption }
                    }
                }
            }
            }
        }

        Rectangle {
            id: bottomBar
            objectName: "filenameBottomBar"
            Layout.fillWidth: true
            Layout.preferredHeight: page.compactLayout ? 112 : 124
            color: page.panelColor
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.topMargin: 32
                anchors.bottomMargin: Theme.spacingSm
                anchors.leftMargin: page.compactLayout ? Theme.spacingMd
                                                       : Theme.spacingLg
                anchors.rightMargin: page.compactLayout ? Theme.spacingMd
                                                        : Theme.spacingLg
                spacing: page.compactLayout ? Theme.spacingSm
                                            : Theme.spacingLg
                Rectangle {
                    objectName: "filenameReadySummaryCard"
                    Layout.preferredWidth: page.compactLayout ? 150 : 236
                    Layout.preferredHeight: page.compactLayout ? 64 : 72
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                    color: Theme.background
                    border.color: Theme.border
                    radius: Theme.radiusSm
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: page.compactLayout ? Theme.spacingMd
                                                           : Theme.spacingLg
                        spacing: Theme.spacingSm
                        ColumnLayout {
                            spacing: 1
                            Label { text: qsTr("成功预览数量"); color: Theme.secondaryText; font.pixelSize: Theme.fontSizeBody }
                            RowLayout {
                                spacing: 6
                                Label { text: readyCount; color: Theme.success; font.pixelSize: Theme.fontSizePageTitle; font.weight: Font.DemiBold }
                                Label { text: qsTr("个文件"); color: Theme.secondaryText; font.pixelSize: Theme.fontSizeBody }
                            }
                        }
                        Item { Layout.fillWidth: true }
                        StatusGlyph {
                            status: 0
                            markSize: page.compactLayout ? 24 : 30
                            Layout.preferredWidth: markSize + 4
                            Layout.preferredHeight: markSize + 4
                        }
                    }
                }
                Rectangle {
                    objectName: "filenameConflictSummaryCard"
                    Layout.preferredWidth: page.compactLayout ? 145 : 226
                    Layout.preferredHeight: page.compactLayout ? 64 : 72
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                    color: Theme.background
                    border.color: Theme.border
                    radius: Theme.radiusSm
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: page.compactLayout ? Theme.spacingMd
                                                           : Theme.spacingLg
                        spacing: Theme.spacingSm
                        ColumnLayout {
                            spacing: 1
                            Label { text: qsTr("冲突数量"); color: Theme.secondaryText; font.pixelSize: Theme.fontSizeBody }
                            RowLayout {
                                spacing: 6
                                Label { text: conflictCount; color: conflictCount > 0 ? Theme.error : Theme.secondaryText; font.pixelSize: Theme.fontSizePageTitle; font.weight: Font.DemiBold }
                                Label { text: qsTr("个文件"); color: Theme.secondaryText; font.pixelSize: Theme.fontSizeBody }
                            }
                        }
                        Item { Layout.fillWidth: true }
                        StatusGlyph {
                            status: 1
                            markSize: page.compactLayout ? 24 : 30
                            Layout.preferredWidth: markSize + 4
                            Layout.preferredHeight: markSize + 4
                        }
                    }
                }
                Rectangle {
                    objectName: "filenameUndoSummaryCard"
                    Layout.preferredWidth: page.compactLayout ? 140 : 208
                    Layout.preferredHeight: page.compactLayout ? 64 : 72
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                    color: Theme.background
                    border.color: Theme.border
                    radius: Theme.radiusSm
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: page.compactLayout ? Theme.spacingMd
                                                           : Theme.spacingLg
                        spacing: Theme.spacingSm
                        ColumnLayout {
                            spacing: 1
                            Label { text: qsTr("可撤销本次重命名"); color: Theme.secondaryText; font.pixelSize: Theme.fontSizeBody }
                            Label { text: FilenameProcessor.canUndo ? qsTr("是") : qsTr("否"); color: Theme.cyan; font.pixelSize: Theme.fontSizePageTitle }
                        }
                        Item { Layout.fillWidth: true }
                        ThemedIcon {
                            source: Theme.icon("restore-line")
                            tint: Theme.cyan
                            sourceSize: Qt.size(page.compactLayout ? 28 : 32,
                                                page.compactLayout ? 28 : 32)
                        }
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
                Label {
                    Layout.preferredWidth: page.compactLayout ? 0 : 260
                    Layout.leftMargin: page.compactLayout ? 0 : Theme.spacingSm
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 4
                    visible: !page.compactLayout && !FilenameProcessor.busy
                    text: qsTr("重命名操作将在处理后生成日志，\n如需退回，可通过撤销恢复原名列表进行还原。")
                    color: Theme.secondaryText
                    font.pixelSize: Theme.fontSizeCaption
                    wrapMode: Text.WordWrap
                }
                ActionButton {
                    id: startRenameButton
                    objectName: "filenameStartButton"
                    Layout.preferredWidth: page.compactLayout ? 130 : 180
                    Layout.preferredHeight: Theme.controlHeightProminent
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("开始重命名")
                    labelPixelSize: Theme.fontSizeBody
                    icon.source: Theme.icon("play-fill")
                    primary: true
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
                ActionButton {
                    id: cancelRenameButton
                    objectName: "filenameCancelButton"
                    visible: true
                    enabled: FilenameProcessor.busy
                    Layout.leftMargin: -1
                    Layout.preferredWidth: page.compactLayout ? 100 : 150
                    Layout.preferredHeight: Theme.controlHeightProminent
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("取消")
                    labelPixelSize: Theme.fontSizeBody
                    icon.source: Theme.icon("close-fill")
                    onClicked: FilenameProcessor.cancel()
                }
            }
            Label {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 18
                anchors.topMargin: 10
                text: qsTr("处理摘要")
                color: Theme.primaryText
                font.pixelSize: Theme.fontSizeBody
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

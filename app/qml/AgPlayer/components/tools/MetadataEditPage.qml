import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: page
    objectName: "metadataEditPage"
    color: "#081b29"
    focus: true

    readonly property real desktopMinimumWidth: 1206
    readonly property bool compactLayout: width < desktopMinimumWidth
    readonly property real inspectorRatio: 0.44
    readonly property color canvasColor: "#071925"
    readonly property color panelColor: "#0a1d2b"
    readonly property color inputColor: "#081622"
    readonly property color borderColor: "#162b3a"
    readonly property color lineColor: "#132938"
    readonly property color mutedColor: "#91a0ad"

    property var selectedIndices: []
    property int selectionAnchor: -1
    property int entryRevision: 0
    property string searchText: ""
    property string sortKey: "fileName"
    property bool sortAscending: true
    property string statusFilter: "all"
    property string exportMode: "results"
    property string coverAction: "keep"
    property string errorMessage: ""
    property var scopeAggregate: ({})
    readonly property var displayedIndices: filteredSortedIndices()
    readonly property string coverMode: coverAction
    readonly property var fieldDefinitions: [
        { key: "title", label: qsTr("标题") },
        { key: "artist", label: qsTr("艺术家") },
        { key: "album", label: qsTr("专辑") },
        { key: "albumArtist", label: qsTr("专辑艺术家") },
        { key: "genre", label: qsTr("流派") },
        { key: "composer", label: qsTr("作曲") },
        { key: "date", label: qsTr("日期") },
        { key: "customTag", label: qsTr("自定义标签") },
        { key: "bpm", label: qsTr("BPM") }
    ]

    component ToolbarAction: Button {
        Layout.preferredHeight: 40
        leftPadding: 14
        rightPadding: 14
        spacing: 8
        font.pixelSize: 14
        palette.buttonText: Theme.primaryText
        background: Rectangle {
            radius: 5
            color: parent.down ? "#173248"
                               : parent.hovered ? "#122b3d" : "#0d2231"
            border.width: 1
            border.color: page.borderColor
        }
    }

    component ColumnHeader: ToolButton {
        property string sortField: ""
        flat: true
        font.pixelSize: 13
        palette.buttonText: page.mutedColor
        background: Rectangle { color: "transparent" }
        onClicked: if (sortField !== "") page.sortBy(sortField)
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
            MetadataEditor.loadFiles(urls)
    }

    function entry(index) {
        entryRevision
        return MetadataEditor.entryAt(index)
    }

    function isSelected(index) {
        return selectedIndices.indexOf(index) >= 0
    }

    function resultForPath(path) {
        const results = MetadataEditor.results || []
        for (let index = 0; index < results.length; ++index) {
            if (results[index].path === path)
                return results[index]
        }
        return null
    }

    function rowStatus(index) {
        const item = entry(index)
        const result = resultForPath(item.path)
        if (item.hasError || (result && !result.success)) return "failed"
        if (result && result.stage === "verified") return "modified"
        if (result && result.stage === "preflight")
            return result.success ? "supported" : "failed"
        return "ready"
    }

    function matchesSearch(index) {
        const query = searchText.trim().toLowerCase()
        if (query.length === 0) return true
        const item = entry(index)
        return String(item.fileName || "").toLowerCase().indexOf(query) >= 0
                || String(item.path || "").toLowerCase().indexOf(query) >= 0
                || String(item.title || "").toLowerCase().indexOf(query) >= 0
                || String(item.artist || "").toLowerCase().indexOf(query) >= 0
                || String(item.album || "").toLowerCase().indexOf(query) >= 0
    }

    function filteredSortedIndices() {
        entryRevision
        const rows = []
        for (let index = 0; index < MetadataEditor.fileCount; ++index) {
            if (!matchesSearch(index)) continue
            if (statusFilter !== "all" && rowStatus(index) !== statusFilter) continue
            rows.push(index)
        }
        rows.sort(function(left, right) {
            const a = entry(left)[sortKey]
            const b = entry(right)[sortKey]
            let comparison = 0
            if (sortKey === "durationMs")
                comparison = Number(a || 0) - Number(b || 0)
            else
                comparison = String(a || "").localeCompare(String(b || ""))
            return sortAscending ? comparison : -comparison
        })
        return rows
    }

    function sortBy(key) {
        if (sortKey === key)
            sortAscending = !sortAscending
        else {
            sortKey = key
            sortAscending = true
        }
    }

    function selectIndex(index, modifiers) {
        let next = selectedIndices.slice()
        if ((modifiers & Qt.ShiftModifier) !== 0 && selectionAnchor >= 0) {
            const anchorPosition = displayedIndices.indexOf(selectionAnchor)
            const targetPosition = displayedIndices.indexOf(index)
            if (anchorPosition >= 0 && targetPosition >= 0) {
                const first = Math.min(anchorPosition, targetPosition)
                const last = Math.max(anchorPosition, targetPosition)
                next = displayedIndices.slice(first, last + 1)
            } else {
                next = [index]
                selectionAnchor = index
            }
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

    function toggleIndex(index) {
        const next = selectedIndices.slice()
        const position = next.indexOf(index)
        if (position >= 0)
            next.splice(position, 1)
        else
            next.push(index)
        selectedIndices = next
        selectionAnchor = index
    }

    function deleteSelection() {
        MetadataEditor.removeFiles(selectedIndices)
        selectedIndices = []
        selectionAnchor = -1
    }

    function rowForField(key) {
        for (let index = 0; index < fieldRepeater.count; ++index) {
            const row = fieldRepeater.itemAt(index)
            if (row && row.fieldKey === key)
                return row
        }
        return null
    }

    function fieldPayload() {
        const result = {}
        for (let index = 0; index < fieldRepeater.count; ++index) {
            const row = fieldRepeater.itemAt(index)
            result[row.fieldKey] = row.descriptor()
        }
        return result
    }

    function setFieldValue(key, value) {
        const row = rowForField(key)
        if (row)
            row.setValue(value)
    }

    function setFieldMode(key, mode) {
        const row = rowForField(key)
        if (row)
            row.selectMode(mode)
    }

    function fieldMode(key) {
        const row = rowForField(key)
        return row ? row.selectedMode : ""
    }

    function fieldValue(key) {
        const row = rowForField(key)
        return row ? row.valueText : ""
    }

    function aggregateTargetIndices() {
        return targetIndices()
    }

    function refreshFields() {
        scopeAggregate = MetadataEditor.aggregateMetadata(aggregateTargetIndices())
        for (let index = 0; index < fieldRepeater.count; ++index) {
            const row = fieldRepeater.itemAt(index)
            if (row)
                row.reset(scopeAggregate[row.fieldKey] || { value: "", multiple: false })
        }
    }

    function activeCoverDetails() {
        MetadataEditor.coverImage
        if (coverMode === "set" && MetadataEditor.coverImage !== "") {
            const selected = MetadataEditor.replacementCoverDetails()
            selected.state = "single"
            selected.preview = MetadataEditor.coverImage
            return selected
        }
        return scopeAggregate.cover || { state: "none" }
    }

    function formatFileSize(sizeBytes) {
        const bytes = Number(sizeBytes || 0)
        if (bytes >= 1024 * 1024)
            return qsTr("%1 MB").arg((bytes / (1024 * 1024)).toFixed(1))
        if (bytes >= 1024)
            return qsTr("%1 KB").arg(Math.max(1, Math.round(bytes / 1024)))
        return qsTr("%1 B").arg(bytes)
    }

    function configuredEditCount() {
        let count = 0
        for (let index = 0; index < fieldRepeater.count; ++index) {
            if (fieldRepeater.itemAt(index).selectedMode !== "keep")
                ++count
        }
        if (coverMode !== "keep")
            ++count
        return count
    }

    function targetCount() {
        if (metadataScopeBox.currentValue === "current")
            return selectionAnchor >= 0 ? 1 : 0
        if (metadataScopeBox.currentValue === "selected")
            return selectedIndices.length
        return MetadataEditor.fileCount
    }

    function targetIndices() {
        if (metadataScopeBox.currentValue === "current")
            return selectionAnchor >= 0 ? [selectionAnchor] : []
        if (metadataScopeBox.currentValue === "selected")
            return selectedIndices.slice()
        return Array.from({length: MetadataEditor.fileCount}, function(_, index) { return index })
    }

    onSelectedIndicesChanged: Qt.callLater(refreshFields)
    onSelectionAnchorChanged: {
        if (metadataScopeBox.currentValue === "current")
            Qt.callLater(refreshFields)
    }

    function planLines() {
        const lines = []
        const count = targetCount()
        for (let index = 0; index < fieldRepeater.count; ++index) {
            const row = fieldRepeater.itemAt(index)
            if (row.selectedMode === "set")
                lines.push(row.fieldLabel + qsTr("：统一为 “%1” · %2 个文件")
                           .arg(row.valueText).arg(count))
            else if (row.selectedMode === "clear")
                lines.push(row.fieldLabel + qsTr("：清除 · %1 个文件").arg(count))
            else
                lines.push(row.fieldLabel + qsTr("：保留原值"))
        }
        if (coverMode === "set")
            lines.push(qsTr("封面：替换当前封面 · %1 个文件").arg(count))
        else if (coverMode === "clear")
            lines.push(qsTr("封面：移除 · %1 个文件").arg(count))
        return lines
    }

    function resetEdits() {
        refreshFields()
        MetadataEditor.clearCoverImage()
        coverAction = "keep"
    }

    function conversionTargetUrls() {
        const urls = []
        const targets = targetIndices()
        for (let index = 0; index < targets.length; ++index) {
            const item = entry(targets[index])
            if (item.path) {
                const localPath = String(item.path).replace(/\\/g, "/")
                // encodeURI deliberately leaves '#' untouched, but QUrl treats
                // it as a fragment and would truncate a legal Windows filename.
                const encodedPath = encodeURI(localPath).replace(/#/g, "%23")
                urls.push(Qt.resolvedUrl("file:///" + encodedPath))
            }
        }
        return urls
    }

    function openConversionWorkflow() {
        const urls = conversionTargetUrls()
        if (urls.length === 0)
            return
        const payload = fieldPayload()
        payload.coverMode = coverMode
        if (!FormatConverter.setMetadataEditPlanForFiles(
                    payload, MetadataEditor.coverImage, urls))
            return
        FormatConverter.loadFiles(urls)
        AudioToolsController.selectTool(1)
    }

    function applyEdits() {
        if (configuredEditCount() === 0) {
            errorMessage = qsTr("请先直接编辑至少一项元数据，或选择新的封面图片。")
            metadataErrorDialog.open()
            return
        }
        const payload = fieldPayload()
        payload.coverMode = coverMode
        const targets = targetIndices()
        if (metadataProcessingModeBox.currentValue === "metadataOnly")
            MetadataEditor.applyMetadata(payload, targets)
        else
            openConversionWorkflow()
    }

    function formatDuration(durationMs) {
        const totalSeconds = Math.max(0, Math.round(Number(durationMs || 0) / 1000))
        const minutes = Math.floor(totalSeconds / 60)
        const seconds = totalSeconds % 60
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    function fileAccent(fileName) {
        const suffix = String(fileName || "").split(".").pop().toLowerCase()
        if (suffix === "mp3") return "#19a56f"
        if (suffix === "flac") return "#ea5f32"
        if (suffix === "m4a") return "#805fd0"
        if (suffix === "opus") return "#2cb3bd"
        if (suffix === "wav") return "#159ec7"
        return "#6d7f8c"
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Delete) {
            deleteSelection()
            event.accepted = true
        } else if ((event.modifiers & Qt.ControlModifier) !== 0
                   && event.key === Qt.Key_A) {
            selectedIndices = displayedIndices.slice()
            selectionAnchor = selectedIndices.length > 0 ? selectedIndices[0] : -1
            event.accepted = true
        }
    }

    FileDialog {
        id: audioDialog
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("音频文件 (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)")]
        onAccepted: MetadataEditor.loadFiles(selectedFiles)
    }

    FolderDialog {
        id: folderDialog
        onAccepted: MetadataEditor.loadFiles([selectedFolder])
    }

    FileDialog {
        id: coverDialog
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("图片文件 (*.png *.jpg *.jpeg *.bmp)")]
        onAccepted: {
            MetadataEditor.setCoverImage(selectedFile)
            if (MetadataEditor.coverImage !== "")
                page.coverAction = "set"
        }
    }

    FileDialog {
        id: exportResultsDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("JSON 报告 (*.json)"), qsTr("CSV 报告 (*.csv)")]
        onAccepted: {
            if (page.exportMode === "currentList")
                MetadataEditor.exportCurrentList(selectedFile, page.displayedIndices)
            else
                MetadataEditor.exportResults(selectedFile)
            page.exportMode = "results"
        }
    }

    Dialog {
        id: threeStateHelpDialog
        modal: true
        title: qsTr("三态编辑说明")
        anchors.centerIn: parent
        width: 440
        standardButtons: Dialog.Ok
        contentItem: Label {
            width: 400
            padding: 16
            wrapMode: Text.WordWrap
            text: qsTr("保留：每个文件保持原值，不写入。\n\n设为：将输入值统一写入目标文件；空值无效，请使用清除。\n\n清除：删除该字段的所有已知别名标签。")
            color: Theme.primaryText
        }
    }

    Dialog {
        id: metadataErrorDialog
        objectName: "metadataErrorDialog"
        modal: true
        title: qsTr("元数据修改失败")
        anchors.centerIn: parent
        width: 480
        standardButtons: Dialog.Ok
        contentItem: Label {
            width: 440
            padding: 18
            text: page.errorMessage
            color: Theme.primaryText
            wrapMode: Text.WordWrap
        }
    }

    Dialog {
        id: preflightDecisionDialog
        objectName: "metadataPreflightDecisionDialog"
        modal: true
        title: qsTr("预检发现不支持项")
        anchors.centerIn: parent
        width: 500
        closePolicy: Popup.NoAutoClose
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.primaryText
                text: qsTr("%1 个文件可安全修改，%2 个文件不支持。")
                      .arg(MetadataEditor.supportedCount)
                      .arg(MetadataEditor.unsupportedCount)
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    text: qsTr("取消")
                    onClicked: {
                        MetadataEditor.applyPreflightDecision("cancel")
                        preflightDecisionDialog.close()
                    }
                }
                Button {
                    text: qsTr("跳过不支持项继续")
                    enabled: MetadataEditor.supportedCount > 0
                    onClicked: {
                        MetadataEditor.applyPreflightDecision("skipUnsupported")
                        preflightDecisionDialog.close()
                    }
                }
            }
        }
    }

    FileDropArea {
        objectName: "metadataDropArea"
        anchors.fill: parent
        z: 20
        onUrlsDropped: function(urls) { MetadataEditor.loadFiles(urls) }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            id: toolbar
            objectName: "metadataToolbar"
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            Layout.leftMargin: 12
            spacing: 14

            ToolbarAction {
                Layout.preferredWidth: 136
                text: qsTr("添加文件")
                icon.source: Theme.icon("add-line")
                onClicked: audioDialog.open()
            }
            ToolbarAction {
                Layout.preferredWidth: 151
                text: qsTr("添加文件夹")
                icon.source: Theme.icon("folder-add-line")
                onClicked: folderDialog.open()
            }
            ToolbarAction {
                Layout.preferredWidth: 184
                text: qsTr("从播放列表添加")
                icon.source: Theme.icon("music-2-line")
                enabled: !MetadataEditor.busy && PlaybackController.currentTrackId.length > 0
                onClicked: page.addCurrentPlayerTrack()
            }
            ToolbarAction {
                Layout.preferredWidth: 142
                text: qsTr("移除选中")
                icon.source: Theme.icon("delete-bin-line")
                enabled: selectedIndices.length > 0 && !MetadataEditor.busy
                onClicked: page.deleteSelection()
            }
            ToolbarAction {
                Layout.preferredWidth: 140
                text: qsTr("清空列表")
                icon.source: Theme.icon("delete-bin-line")
                enabled: MetadataEditor.fileCount > 0 && !MetadataEditor.busy
                onClicked: {
                    MetadataEditor.clear()
                    page.selectedIndices = []
                    page.selectionAnchor = -1
                    page.resetEdits()
                }
            }
            Item { Layout.fillWidth: true }
            TextField {
                objectName: "metadataSearchField"
                visible: false
                Layout.preferredWidth: 244
                Layout.preferredHeight: 38
                leftPadding: 14
                rightPadding: 12
                placeholderText: qsTr("搜索文件名、标签或路径...")
                color: Theme.primaryText
                font.pixelSize: 13
                background: Rectangle {
                    color: page.inputColor
                    radius: 5
                    border.width: 1
                    border.color: parent.activeFocus ? Theme.accent : page.borderColor
                }
                onTextChanged: page.searchText = text
            }
            ComboBox {
                objectName: "metadataStatusFilter"
                visible: false
                Layout.preferredWidth: 110
                Layout.preferredHeight: 38
                textRole: "text"
                valueRole: "value"
                model: [
                    { text: qsTr("全部状态"), value: "all" },
                    { text: qsTr("就绪"), value: "ready" },
                    { text: qsTr("待处理"), value: "supported" },
                    { text: qsTr("已修改"), value: "modified" },
                    { text: qsTr("失败"), value: "failed" }
                ]
                background: Rectangle {
                    color: page.inputColor
                    radius: 5
                    border.width: 1
                    border.color: page.borderColor
                }
                onCurrentValueChanged: page.statusFilter = currentValue || "all"
            }
        }

        TabBar {
            id: compactMetadataTabs
            objectName: "metadataCompactTabs"
            visible: page.compactLayout
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 36 : 0
            TabButton { text: qsTr("文件列表") }
            TabButton { text: qsTr("编辑元数据") }
        }

        Rectangle {
            id: workbench
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: page.canvasColor
            radius: 6

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 10
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 8

                Rectangle {
                    id: filePanel
                    objectName: "metadataFilePanel"
                    visible: !page.compactLayout || compactMetadataTabs.currentIndex === 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: page.compactLayout
                                           ? (visible ? workbench.width - 18 : 0)
                                           : workbench.width - 26
                                             - (page.width * page.inspectorRatio - 12)
                    Layout.minimumWidth: page.compactLayout ? 0 : 560
                    color: page.panelColor
                    border.width: 1
                    border.color: page.borderColor
                    radius: 5

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        RowLayout {
                            objectName: "metadataFileHeader"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 43
                            Layout.leftMargin: 16
                            Layout.rightMargin: 12
                            spacing: 10
                            Label {
                                text: qsTr("文件列表")
                                color: Theme.primaryText
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }
                            Rectangle {
                                implicitWidth: selectedChipText.implicitWidth + 24
                                implicitHeight: 28
                                radius: 5
                                color: "#103557"
                                border.color: "#1c4d75"
                                Label {
                                    id: selectedChipText
                                    anchors.centerIn: parent
                                    text: qsTr("已选 %1").arg(page.selectedIndices.length)
                                    color: "#cce8ff"
                                    font.pixelSize: 13
                                }
                            }
                            Rectangle {
                                implicitWidth: totalChipText.implicitWidth + 24
                                implicitHeight: 28
                                radius: 5
                                color: "#0d2130"
                                Label {
                                    id: totalChipText
                                    anchors.centerIn: parent
                                    text: qsTr("总计 %1").arg(MetadataEditor.fileCount)
                                    color: page.mutedColor
                                    font.pixelSize: 13
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            color: "#091b29"
                            border.width: 1
                            border.color: page.lineColor
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 8
                                CheckBox {
                                    Layout.preferredWidth: 26
                                    checked: page.displayedIndices.length > 0
                                             && page.displayedIndices.every(function(index) {
                                                 return page.isSelected(index)
                                             })
                                    onClicked: {
                                        page.selectedIndices = checked
                                            ? page.displayedIndices.slice() : []
                                        page.selectionAnchor = page.selectedIndices.length > 0
                                            ? page.selectedIndices[0] : -1
                                    }
                                }
                                ColumnHeader { text: qsTr("文件名"); sortField: "fileName"; Layout.preferredWidth: 184 }
                                ColumnHeader { text: qsTr("标题"); sortField: "title"; Layout.preferredWidth: 124 }
                                ColumnHeader { text: qsTr("艺术家"); sortField: "artist"; Layout.preferredWidth: 122 }
                                ColumnHeader { text: qsTr("专辑"); sortField: "album"; Layout.preferredWidth: 122 }
                                ColumnHeader { objectName: "metadataTableTagHeader"; text: qsTr("标签"); sortField: "customTag"; Layout.preferredWidth: 48 }
                                ColumnHeader { text: qsTr("时长"); sortField: "durationMs"; Layout.preferredWidth: 54 }
                                Label { text: qsTr("封面"); color: page.mutedColor; Layout.preferredWidth: 46; horizontalAlignment: Text.AlignHCenter }
                                Label { text: qsTr("状态"); color: page.mutedColor; Layout.preferredWidth: 52; horizontalAlignment: Text.AlignHCenter }
                            }
                        }

                        ListView {
                            id: fileList
                            objectName: "metadataFileList"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: page.displayedIndices
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                            delegate: Rectangle {
                                required property int index
                                required property int modelData
                                readonly property int sourceIndex: modelData
                                readonly property var metadata: page.entry(sourceIndex)
                                width: fileList.width
                                height: 52
                                color: page.isSelected(sourceIndex) ? "#0d2b42"
                                      : (rowHover.hovered ? "#0c2638" : "transparent")
                                border.width: 1
                                border.color: page.lineColor
                                HoverHandler { id: rowHover }
                                TapHandler {
                                    acceptedButtons: Qt.LeftButton
                                    onTapped: function(eventPoint, button) {
                                        page.selectIndex(sourceIndex, button.modifiers)
                                        page.forceActiveFocus()
                                    }
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 8
                                    CheckBox {
                                        Layout.preferredWidth: 26
                                        checked: page.isSelected(sourceIndex)
                                        onClicked: page.toggleIndex(sourceIndex)
                                    }
                                    RowLayout {
                                        Layout.preferredWidth: 184
                                        spacing: 8
                                        Rectangle {
                                            Layout.preferredWidth: 22
                                            Layout.preferredHeight: 22
                                            radius: 3
                                            color: page.fileAccent(metadata.fileName)
                                            ThemedIcon {
                                                anchors.centerIn: parent
                                                source: Theme.icon("music-2-fill")
                                                tint: "white"
                                                sourceSize.width: 14
                                                sourceSize.height: 14
                                            }
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: metadata.fileName || ""
                                            color: Theme.primaryText
                                            font.pixelSize: 13
                                            elide: Text.ElideRight
                                            ToolTip.visible: fileNameHover.hovered
                                            ToolTip.text: text
                                            HoverHandler { id: fileNameHover }
                                        }
                                    }
                                    Label { text: metadata.title || "—"; color: Theme.primaryText; Layout.preferredWidth: 124; elide: Text.ElideRight }
                                    Label { text: metadata.artist || "—"; color: Theme.primaryText; Layout.preferredWidth: 122; elide: Text.ElideRight }
                                    Label { text: metadata.album || "—"; color: Theme.primaryText; Layout.preferredWidth: 122; elide: Text.ElideRight }
                                    Label { text: metadata.customTag || "—"; color: page.mutedColor; Layout.preferredWidth: 48; horizontalAlignment: Text.AlignHCenter }
                                    Label { text: page.formatDuration(metadata.durationMs); color: page.mutedColor; Layout.preferredWidth: 54; horizontalAlignment: Text.AlignHCenter }
                                    Rectangle {
                                        Layout.preferredWidth: 46
                                        Layout.preferredHeight: 42
                                        radius: 4
                                        color: page.inputColor
                                        Image {
                                            anchors.fill: parent
                                            visible: metadata.coverPreview !== ""
                                            source: metadata.coverPreview || ""
                                            fillMode: Image.PreserveAspectCrop
                                        }
                                        ThemedIcon {
                                            anchors.centerIn: parent
                                            visible: metadata.coverPreview === ""
                                            source: Theme.icon("picture-in-picture-2-line")
                                            tint: page.mutedColor
                                            sourceSize.width: 17
                                            sourceSize.height: 17
                                        }
                                    }
                                    Label {
                                        readonly property var applyResult: page.resultForPath(metadata.path)
                                        text: metadata.hasError ? qsTr("错误")
                                              : applyResult
                                                ? (applyResult.stage === "preflight"
                                                   ? (applyResult.success
                                                      ? qsTr("待处理") : qsTr("不支持"))
                                                   : applyResult.success
                                                     ? qsTr("已完成")
                                                     : applyResult.status === "cancelled"
                                                       ? qsTr("已取消") : qsTr("失败"))
                                                : qsTr("就绪")
                                        color: metadata.hasError || (applyResult && !applyResult.success)
                                               ? Theme.favoriteRed : Theme.accent
                                        Layout.preferredWidth: 52
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                }
                            }

                            ColumnLayout {
                                anchors.centerIn: parent
                                visible: MetadataEditor.fileCount === 0
                                spacing: 8
                                ThemedIcon {
                                    Layout.alignment: Qt.AlignHCenter
                                    source: Theme.icon("folder-open-line")
                                    tint: page.mutedColor
                                    sourceSize.width: 30
                                    sourceSize.height: 30
                                }
                                Label {
                                    text: qsTr("拖入音频文件，或使用上方按钮添加")
                                    color: page.mutedColor
                                }
                            }
                        }

                        RowLayout {
                            objectName: "metadataFileFooter"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 43
                            Layout.leftMargin: 10
                            Layout.rightMargin: 14
                            spacing: 10
                            CheckBox {
                                text: qsTr("全选")
                                checked: page.displayedIndices.length > 0
                                         && page.selectedIndices.length === page.displayedIndices.length
                                onClicked: {
                                    page.selectedIndices = checked
                                        ? page.displayedIndices.slice() : []
                                    page.selectionAnchor = page.selectedIndices.length > 0
                                        ? page.selectedIndices[0] : -1
                                }
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: qsTr("已选 %1 个文件，共 %2 个文件")
                                      .arg(page.selectedIndices.length)
                                      .arg(MetadataEditor.fileCount)
                                color: page.mutedColor
                                font.pixelSize: 13
                            }
                            ToolbarAction {
                                objectName: "metadataExportCurrentListButton"
                                visible: false
                                Layout.preferredHeight: 32
                                text: qsTr("导出当前列表")
                                enabled: page.displayedIndices.length > 0
                                onClicked: {
                                    page.exportMode = "currentList"
                                    exportResultsDialog.open()
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    id: inspectorPanel
                    objectName: "metadataInspectorPanel"
                    visible: !page.compactLayout || compactMetadataTabs.currentIndex === 1
                    Layout.fillHeight: true
                    Layout.preferredWidth: page.compactLayout
                                           ? (visible ? workbench.width - 18 : 0)
                                           : page.width * page.inspectorRatio - 12
                    Layout.minimumWidth: page.compactLayout ? 0 : 620
                    color: page.panelColor
                    border.width: 1
                    border.color: page.borderColor
                    radius: 5

                    ScrollView {
                        id: inspectorScroll
                        anchors.fill: parent
                        anchors.margins: 14
                        clip: true
                        contentWidth: availableWidth
                        ScrollBar.vertical.policy: page.compactLayout
                                                   ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff

                        ColumnLayout {
                            width: inspectorScroll.availableWidth
                            spacing: 5

                            RowLayout {
                                objectName: "metadataEditorHeader"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 28
                                Label {
                                    text: qsTr("批量元数据编辑")
                                    color: Theme.primaryText
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                }
                                Label {
                                    text: qsTr("已选 %1 个文件").arg(page.selectedIndices.length)
                                    color: page.mutedColor
                                    font.pixelSize: 13
                                }
                                Item { Layout.fillWidth: true }
                            }

                            // Advanced scope and conversion options remain available to
                            // the workflow and automated tests, but do not displace the
                            // direct-entry reference form.
                            RowLayout {
                                visible: false
                                Layout.fillWidth: true
                                Layout.preferredHeight: 0
                                spacing: 8
                                Label { text: qsTr("作用范围"); color: page.mutedColor; font.pixelSize: 12 }
                                ComboBox {
                                    id: metadataScopeBox
                                    objectName: "metadataScopeBox"
                                    Layout.preferredWidth: 132
                                    Layout.preferredHeight: 32
                                    textRole: "text"
                                    valueRole: "value"
                                    model: [
                                        { text: qsTr("当前文件"), value: "current" },
                                        { text: qsTr("已选文件"), value: "selected" },
                                        { text: qsTr("全部文件"), value: "all" }
                                    ]
                                    currentIndex: 1
                                    onCurrentValueChanged: Qt.callLater(page.refreshFields)
                                    background: Rectangle {
                                        color: page.inputColor
                                        radius: 4
                                        border.width: 1
                                        border.color: page.borderColor
                                    }
                                }
                                Label { text: qsTr("处理方式"); color: page.mutedColor; font.pixelSize: 12 }
                                ComboBox {
                                    id: metadataProcessingModeBox
                                    objectName: "metadataProcessingModeBox"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 32
                                    textRole: "text"
                                    valueRole: "value"
                                    model: [
                                        { text: qsTr("仅修改元数据（流复制）"), value: "metadataOnly" },
                                        { text: qsTr("转换时写入新文件"), value: "convert" }
                                    ]
                                    background: Rectangle {
                                        color: page.inputColor
                                        radius: 4
                                        border.width: 1
                                        border.color: page.borderColor
                                    }
                                }
                                ToolButton {
                                    objectName: "metadataThreeStateHelp"
                                    icon.source: Theme.icon("information-line")
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("三态编辑说明")
                                    onClicked: threeStateHelpDialog.open()
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 24
                                ThemedIcon {
                                    source: Theme.icon("information-line")
                                    tint: page.mutedColor
                                    sourceSize.width: 16
                                    sourceSize.height: 16
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("直接编辑以下信息，留空表示保持原值不变。")
                                    color: page.mutedColor
                                    font.pixelSize: 12
                                }
                                ToolButton {
                                    objectName: "metadataConversionSettingsButton"
                                    visible: metadataProcessingModeBox.currentValue === "convert"
                                    text: qsTr("修改转换设置")
                                    enabled: page.targetCount() > 0
                                             && page.configuredEditCount() > 0
                                    onClicked: page.openConversionWorkflow()
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 372
                                spacing: 22

                                ColumnLayout {
                                    objectName: "metadataReferenceFieldForm"
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    spacing: 10

                                    Repeater {
                                        id: fieldRepeater
                                        model: page.fieldDefinitions
                                        delegate: RowLayout {
                                            id: fieldRow
                                            required property var modelData
                                            property string fieldKey: modelData.key
                                            property string fieldLabel: modelData.label
                                            property string selectedMode: "keep"
                                            property string sourceValue: ""
                                            property bool sourceMultiple: false
                                            readonly property string valueText: valueField.text
                                            function setValue(value) { valueField.text = value }
                                            function selectMode(mode) {
                                                selectedMode = mode
                                                if (mode === "clear")
                                                    valueField.clear()
                                                else if (mode === "keep")
                                                    valueField.text = sourceMultiple ? "" : sourceValue
                                            }
                                            function reset(summary) {
                                                sourceValue = String(summary.value || "")
                                                sourceMultiple = Boolean(summary.multiple)
                                                selectedMode = !sourceMultiple && sourceValue !== ""
                                                               ? "set" : "keep"
                                                valueField.text = sourceMultiple ? "" : sourceValue
                                            }
                                            function descriptor() {
                                                return { mode: selectedMode, value: valueField.text }
                                            }
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 32
                                            spacing: 8
                                            Label {
                                                text: fieldRow.fieldLabel
                                                color: Theme.primaryText
                                                Layout.preferredWidth: 92
                                                font.pixelSize: 14
                                            }
                                            Item {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 30
                                                TextField {
                                                    id: valueField
                                                    objectName: "metadataValueField_" + fieldRow.fieldKey
                                                    anchors.fill: parent
                                                    enabled: fieldRow.selectedMode !== "clear"
                                                    rightPadding: fieldRow.fieldKey === "date" ? 38 : 12
                                                    leftPadding: 14
                                                    color: Theme.primaryText
                                                    placeholderText: fieldRow.selectedMode === "clear"
                                                                       ? qsTr("将清除")
                                                                       : fieldRow.sourceMultiple
                                                                         ? qsTr("多种值")
                                                                         : qsTr("保留原值")
                                                    font.pixelSize: 14
                                                    background: Rectangle {
                                                        color: valueField.enabled ? page.inputColor : "#091825"
                                                        radius: 4
                                                        border.width: valueField.activeFocus ? 1.5 : 1
                                                        border.color: valueField.activeFocus
                                                                      ? Theme.accent : page.borderColor
                                                    }
                                                    onTextEdited: fieldRow.selectedMode = text.length > 0
                                                                                  ? "set" : "keep"
                                                }
                                                ThemedIcon {
                                                    visible: fieldRow.fieldKey === "date"
                                                    anchors.right: parent.right
                                                    anchors.rightMargin: 12
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    source: Theme.icon("time-line")
                                                    tint: page.mutedColor
                                                    sourceSize.width: 17
                                                    sourceSize.height: 17
                                                }
                                            }
                                            Item {
                                                visible: false
                                                Layout.preferredWidth: 0
                                                Layout.preferredHeight: 0
                                                ButtonGroup { id: fieldModeGroup }
                                                Repeater {
                                                    model: [
                                                        { text: qsTr("保留"), value: "keep" },
                                                        { text: qsTr("设为"), value: "set" },
                                                        { text: qsTr("清除"), value: "clear" }
                                                    ]
                                                    delegate: ToolButton {
                                                        required property var modelData
                                                        objectName: "metadataModeButton_" + fieldRow.fieldKey
                                                                    + "_" + modelData.value
                                                        visible: false
                                                        checkable: true
                                                        checked: fieldRow.selectedMode === modelData.value
                                                        ButtonGroup.group: fieldModeGroup
                                                        onClicked: fieldRow.selectMode(modelData.value)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                ColumnLayout {
                                    objectName: "metadataCoverSection"
                                    Layout.preferredWidth: 212
                                    Layout.minimumWidth: 212
                                    Layout.maximumWidth: 212
                                    Layout.fillHeight: true
                                    spacing: 7
                                    Label {
                                        text: qsTr("封面（Cover Art）")
                                        color: Theme.primaryText
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                    }
                                    Rectangle {
                                        objectName: "metadataCoverPreview"
                                        Layout.preferredWidth: 212
                                        Layout.preferredHeight: 210
                                        color: page.inputColor
                                        border.width: 1
                                        border.color: "#314254"
                                        radius: 5
                                        clip: true
                                        Image {
                                            anchors.fill: parent
                                            source: page.activeCoverDetails().state === "single"
                                                ? page.activeCoverDetails().preview || "" : ""
                                            fillMode: Image.PreserveAspectCrop
                                        }
                                        ThemedIcon {
                                            anchors.centerIn: parent
                                            visible: page.activeCoverDetails().state !== "single"
                                            source: Theme.icon("picture-in-picture-2-line")
                                            tint: page.mutedColor
                                            sourceSize.width: 36
                                            sourceSize.height: 36
                                        }
                                    }
                                    Label {
                                        objectName: "metadataCoverSummaryLabel"
                                        Layout.fillWidth: true
                                        readonly property var details: page.activeCoverDetails()
                                        text: details.state === "multiple"
                                            ? qsTr("当前封面：多种封面")
                                            : details.state === "single"
                                              ? qsTr("当前封面：%1").arg(
                                                    details.fileName || qsTr("内嵌封面"))
                                              : qsTr("当前封面：无")
                                        color: page.mutedColor
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        readonly property var details: page.activeCoverDetails()
                                        visible: details.state === "single"
                                        text: qsTr("%1 × %2 · %3 · %4")
                                              .arg(details.width || 0)
                                              .arg(details.height || 0)
                                              .arg(details.mimeType || qsTr("未知格式"))
                                              .arg(page.formatFileSize(details.sizeBytes))
                                        color: page.mutedColor
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                    ToolbarAction {
                                        Layout.preferredWidth: 116
                                        Layout.preferredHeight: 34
                                        text: qsTr("选择图片...")
                                        icon.source: Theme.icon("picture-in-picture-2-line")
                                        onClicked: {
                                            page.coverAction = "set"
                                            coverDialog.open()
                                        }
                                        Accessible.description: qsTr("单击选择图片；聚焦后按 Delete 可移除封面")
                                        Keys.onDeletePressed: {
                                            MetadataEditor.clearCoverImage()
                                            page.coverAction = "clear"
                                        }
                                    }
                                    Item { Layout.fillHeight: true }
                                }
                            }

                            Rectangle {
                                objectName: "metadataChangePreview"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 223
                                color: "#081b28"
                                border.width: 1
                                border.color: page.borderColor
                                radius: 6

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 4
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            text: qsTr("修改摘要（预览结果）")
                                            color: Theme.primaryText
                                            font.pixelSize: 15
                                            font.weight: Font.DemiBold
                                        }
                                        Item { Layout.fillWidth: true }
                                        ToolButton {
                                            objectName: "metadataExportResultsButton"
                                            text: qsTr("导出结果")
                                            visible: MetadataEditor.results.length > 0
                                            enabled: !MetadataEditor.busy
                                            onClicked: {
                                                page.exportMode = "results"
                                                exportResultsDialog.open()
                                            }
                                        }
                                    }
                                    Label {
                                        visible: page.planLines().length === 0
                                        text: qsTr("填写字段后将在此显示预计修改。")
                                        color: page.mutedColor
                                    }
                                    Repeater {
                                        model: MetadataEditor.results.length === 0
                                               ? page.planLines() : []
                                        delegate: RowLayout {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            spacing: 8
                                            ThemedIcon {
                                                source: Theme.icon("checkbox-blank-circle-fill")
                                                tint: "#39bd72"
                                                sourceSize.width: 16
                                                sourceSize.height: 16
                                            }
                                            Label {
                                                Layout.fillWidth: true
                                                text: modelData
                                                color: page.mutedColor
                                                font.pixelSize: 12
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        visible: MetadataEditor.results.length > 0
                                        text: qsTr("成功 %1 · 失败 %2 · 取消 %3")
                                              .arg(MetadataEditor.successCount)
                                              .arg(MetadataEditor.failedCount)
                                              .arg(MetadataEditor.cancelledCount)
                                        color: Theme.accent
                                        font.pixelSize: 12
                                    }
                                    Repeater {
                                        model: MetadataEditor.results
                                        delegate: Label {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            text: modelData.fileName + qsTr("：")
                                                  + (modelData.success
                                                     ? (modelData.message || modelData.stage || qsTr("完成"))
                                                     : (modelData.preflightReason || modelData.message
                                                        || modelData.errorCode || qsTr("失败")))
                                            color: modelData.success ? "#39bd72" : Theme.favoriteRed
                                            font.pixelSize: 11
                                            elide: Text.ElideMiddle
                                        }
                                    }
                                    Item { Layout.fillHeight: true }
                                }
                            }

                            RowLayout {
                                objectName: "metadataActionBar"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 48
                                spacing: 12
                                ProgressBar {
                                    Layout.fillWidth: true
                                    visible: MetadataEditor.busy
                                    from: 0
                                    to: 1
                                    value: MetadataEditor.progress
                                }
                                Item { Layout.fillWidth: !MetadataEditor.busy }
                                Button {
                                    id: applyButton
                                    objectName: "metadataApplyButton"
                                    Layout.preferredWidth: 214
                                    Layout.preferredHeight: 42
                                    text: qsTr("应用修改")
                                    enabled: page.targetCount() > 0
                                             && page.configuredEditCount() > 0
                                             && !MetadataEditor.busy
                                    palette.buttonText: "white"
                                    background: Rectangle {
                                        radius: 5
                                        color: !applyButton.enabled ? "#28465e"
                                             : applyButton.down ? "#0055d8"
                                             : applyButton.hovered ? "#087cff" : "#086bf2"
                                    }
                                    onClicked: page.applyEdits()
                                }
                                Button {
                                    id: cancelButton
                                    objectName: "metadataCancelButton"
                                    Layout.preferredWidth: 198
                                    Layout.preferredHeight: 42
                                    text: qsTr("取消")
                                    icon.source: Theme.icon("restore-line")
                                    palette.buttonText: Theme.primaryText
                                    background: Rectangle {
                                        radius: 5
                                        color: cancelButton.down ? "#173248"
                                             : cancelButton.hovered ? "#122b3d" : "#0d2231"
                                        border.width: 1
                                        border.color: page.borderColor
                                    }
                                    onClicked: {
                                        if (MetadataEditor.busy)
                                            MetadataEditor.cancel()
                                        else
                                            page.resetEdits()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: MetadataEditor
        function onEntriesLoaded() {
            page.selectedIndices = Array.from({length: MetadataEditor.fileCount},
                                              function(_, index) { return index })
            page.selectionAnchor = page.selectedIndices.length > 0 ? page.selectedIndices[0] : -1
            ++page.entryRevision
            Qt.callLater(page.refreshFields)
        }
        function onEntriesChanged() {
            ++page.entryRevision
            Qt.callLater(page.refreshFields)
        }
        function onPreflightDecisionRequired() { preflightDecisionDialog.open() }
        function onErrorOccurred(message) {
            page.errorMessage = message
            metadataErrorDialog.open()
        }
    }

    Component.onCompleted: Qt.callLater(refreshFields)
}

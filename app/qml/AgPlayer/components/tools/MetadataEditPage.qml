import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: page
    objectName: "metadataEditPage"
    color: Theme.background
    focus: true
    readonly property bool compactLayout: width < 1100

    property var selectedIndices: []
    property int selectionAnchor: -1
    property int entryRevision: 0
    property string sortKey: "fileName"
    property bool sortAscending: true
    property string statusFilter: "all"
    property string exportMode: "results"
    readonly property string coverMode: MetadataEditor.coverImage !== "" ? "set" : "keep"
    readonly property var displayedIndices: filteredSortedIndices()
    readonly property var fieldDefinitions: [
        { key: "title", label: qsTr("标题") },
        { key: "artist", label: qsTr("艺术家") },
        { key: "album", label: qsTr("专辑") },
        { key: "albumArtist", label: qsTr("专辑艺术家") },
        { key: "genre", label: qsTr("流派") },
        { key: "year", label: qsTr("年份") },
        { key: "date", label: qsTr("日期") },
        { key: "composer", label: qsTr("作曲家") },
        { key: "bpm", label: qsTr("BPM") }
    ]

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

    function isSelected(index) { return selectedIndices.indexOf(index) >= 0 }
    function isMixedField(key) {
        if (selectedIndices.length < 2)
            return false
        const first = String(entry(selectedIndices[0])[key] || "")
        for (let i = 1; i < selectedIndices.length; ++i) {
            if (String(entry(selectedIndices[i])[key] || "") !== first)
                return true
        }
        return false
    }
    function entry(index) {
        entryRevision
        return MetadataEditor.entryAt(index)
    }
    function resultForPath(path) {
        const results = MetadataEditor.results || []
        for (let row = 0; row < results.length; ++row) {
            if (results[row].path === path)
                return results[row]
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
    function filteredSortedIndices() {
        entryRevision
        const rows = []
        for (let index = 0; index < MetadataEditor.fileCount; ++index) {
            if (statusFilter !== "all" && rowStatus(index) !== statusFilter) continue
            rows.push(index)
        }
        rows.sort(function(left, right) {
            const a = entry(left)[sortKey]
            const b = entry(right)[sortKey]
            let comparison = 0
            if (sortKey === "durationMs" || sortKey === "fileSize")
                comparison = Number(a || 0) - Number(b || 0)
            else
                comparison = String(a || "").localeCompare(String(b || ""))
            return sortAscending ? comparison : -comparison
        })
        return rows
    }
    function sortBy(key) {
        if (sortKey === key) sortAscending = !sortAscending
        else {
            sortKey = key
            sortAscending = true
        }
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
    }
    function toggleIndex(index) {
        const next = selectedIndices.slice()
        const position = next.indexOf(index)
        if (position >= 0) next.splice(position, 1)
        else next.push(index)
        selectedIndices = next
        selectionAnchor = index
    }
    function deleteSelection() {
        MetadataEditor.removeFiles(selectedIndices)
        selectedIndices = []
        selectionAnchor = -1
    }
    function fieldPayload() {
        const result = {}
        for (let i = 0; i < fieldRepeater.count; ++i) {
            const row = fieldRepeater.itemAt(i)
            result[row.fieldKey] = row.descriptor()
        }
        return result
    }
    function rowForField(key) {
        for (let index = 0; index < fieldRepeater.count; ++index) {
            const row = fieldRepeater.itemAt(index)
            if (row && row.fieldKey === key)
                return row
        }
        return null
    }
    function setFieldValue(key, value) {
        const row = rowForField(key)
        if (row) row.setValue(value)
    }
    function setFieldMode(key, mode) {
        const row = rowForField(key)
        if (row) row.selectMode(mode)
    }
    function fieldMode(key) {
        const row = rowForField(key)
        return row ? row.selectedMode : ""
    }
    function fieldValue(key) {
        const row = rowForField(key)
        return row ? row.valueText : ""
    }
    function fieldSourceIndices() {
        if (selectedIndices.length > 0)
            return selectedIndices.slice()
        if (selectionAnchor >= 0)
            return [selectionAnchor]
        return Array.from({length: MetadataEditor.fileCount},
                          function(_, index) { return index })
    }
    function refreshFields() {
        const indexes = fieldSourceIndices()
        for (let index = 0; index < fieldRepeater.count; ++index) {
            const row = fieldRepeater.itemAt(index)
            if (row)
                row.loadValues(indexes)
        }
    }
    function conversionTargetUrls() {
        const urls = []
        let indexes = []
        if (metadataScopeBox.currentValue === "current")
            indexes = selectionAnchor >= 0 ? [selectionAnchor] : []
        else if (metadataScopeBox.currentValue === "selected")
            indexes = selectedIndices
        else
            indexes = Array.from({length: MetadataEditor.fileCount}, function(_, index) { return index })
        for (let index = 0; index < indexes.length; ++index) {
            const item = entry(indexes[index])
            if (item.path)
                urls.push(Qt.resolvedUrl("file:///" + encodeURI(String(item.path).replace(/\\/g, "/"))))
        }
        return urls
    }
    function openConversionWorkflow() {
        const payload = fieldPayload()
        payload.coverMode = page.coverMode
        if (!FormatConverter.setMetadataEditPlan(payload,
                                                 MetadataEditor.coverImage))
            return
        const urls = conversionTargetUrls()
        if (urls.length === 0)
            return
        FormatConverter.loadFiles(urls)
        AudioToolsController.selectTool(1)
    }
    function configuredEditCount() {
        let count = 0
        for (let i = 0; i < fieldRepeater.count; ++i) {
            if (fieldRepeater.itemAt(i).selectedMode !== "keep") ++count
        }
        if (page.coverMode !== "keep") ++count
        return count
    }
    function targetCount() {
        if (metadataScopeBox.currentValue === "current")
            return selectionAnchor >= 0 ? 1 : 0
        if (metadataScopeBox.currentValue === "selected")
            return selectedIndices.length
        return MetadataEditor.fileCount
    }
    function planLines() {
        const lines = []
        for (let i = 0; i < fieldRepeater.count; ++i) {
            const row = fieldRepeater.itemAt(i)
            if (row.selectedMode === "keep") continue
            const action = row.selectedMode === "set" ? qsTr("设为 “%1”").arg(row.valueText)
                                                       : qsTr("清除")
            lines.push(row.fieldLabel + qsTr("：") + action)
        }
        if (page.coverMode === "set")
            lines.push(qsTr("封面：替换"))
        return lines
    }

    onSelectedIndicesChanged: Qt.callLater(refreshFields)
    onSelectionAnchorChanged: Qt.callLater(refreshFields)
    function formatDuration(durationMs) {
        const totalSeconds = Math.max(0, Math.round(Number(durationMs || 0) / 1000))
        const minutes = Math.floor(totalSeconds / 60)
        const seconds = totalSeconds % 60
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Delete) {
            deleteSelection()
            event.accepted = true
        } else if ((event.modifiers & Qt.ControlModifier) !== 0
                   && event.key === Qt.Key_A) {
            selectedIndices = Array.from({length: MetadataEditor.fileCount},
                                         function(_, index) { return index })
            event.accepted = true
        }
    }

    FileDialog {
        id: audioDialog
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("音频文件 (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)")]
        onAccepted: MetadataEditor.loadFiles(selectedFiles)
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
                text: qsTr("%1 个文件可安全流复制，%2 个文件不支持。源文件尚未修改。")
                      .arg(MetadataEditor.supportedCount)
                      .arg(MetadataEditor.unsupportedCount)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.secondaryText
                text: qsTr("可只处理完全支持的文件，或返回检查逐文件原因。")
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
                Button {
                    highlighted: true
                    text: qsTr("只处理完全支持的文件")
                    enabled: MetadataEditor.supportedCount > 0
                    onClicked: {
                        MetadataEditor.applyPreflightDecision("supportedOnly")
                        preflightDecisionDialog.close()
                    }
                }
            }
        }
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
                enabled: !MetadataEditor.busy
                         && PlaybackController.currentTrackId.length > 0
                ToolTip.visible: hovered
                ToolTip.text: qsTr("需要先在播放器中选择歌曲")
                onClicked: page.addCurrentPlayerTrack()
            }
            Button {
                text: qsTr("移除选中")
                icon.source: Theme.icon("delete-bin-line")
                enabled: selectedIndices.length > 0 && !MetadataEditor.busy
                onClicked: deleteSelection()
            }
            Button {
                text: qsTr("清空")
                enabled: MetadataEditor.fileCount > 0 && !MetadataEditor.busy
                onClicked: MetadataEditor.clear()
            }
            Item { Layout.fillWidth: true }
        }

        TabBar {
            id: compactMetadataTabs
            objectName: "metadataCompactTabs"
            visible: page.compactLayout
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 36 : 0
            TabButton { text: qsTr("任务列表") }
            TabButton { text: qsTr("编辑元数据") }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            Rectangle {
                id: filePanel
                objectName: "metadataFilePanel"
                visible: !page.compactLayout
                         || compactMetadataTabs.currentIndex === 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: page.compactLayout
                                       ? (visible ? page.width - 28 : 0)
                                       : Math.max(620, page.width * 0.58)
                Layout.minimumWidth: page.compactLayout ? 0 : 560
                Layout.maximumWidth: page.compactLayout
                                     ? (visible ? 16777215 : 0)
                                     : 16777215
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    RowLayout {
                        objectName: "metadataStatusFilter"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        spacing: 6
                        Label {
                            text: qsTr("任务列表：")
                            color: Theme.primaryText
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                        ButtonGroup { id: metadataStatusFilterGroup }
                        Repeater {
                            model: [
                                { text: qsTr("全部"), value: "all" },
                                { text: qsTr("就绪"), value: "ready" },
                                { text: qsTr("支持"), value: "supported" },
                                { text: qsTr("已修改"), value: "modified" },
                                { text: qsTr("失败"), value: "failed" }
                            ]
                            Button {
                                checkable: true
                                checked: page.statusFilter === modelData.value
                                ButtonGroup.group: metadataStatusFilterGroup
                                text: modelData.text
                                onClicked: page.statusFilter = modelData.value
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        color: Theme.background
                        border.color: Theme.border
                        border.width: 0
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 8
                            CheckBox {
                                Layout.preferredWidth: 24
                                checked: page.displayedIndices.length > 0
                                         && page.displayedIndices.every(function(index) {
                                             return page.isSelected(index)
                                         })
                                onClicked: {
                                    selectedIndices = checked ? page.displayedIndices.slice() : []
                                }
                            }
                            ToolButton { text: qsTr("文件名") + (page.sortKey === "fileName" ? (page.sortAscending ? " ↑" : " ↓") : ""); Layout.fillWidth: true; onClicked: page.sortBy("fileName") }
                            ToolButton { text: qsTr("标题") + (page.sortKey === "title" ? (page.sortAscending ? " ↑" : " ↓") : ""); Layout.preferredWidth: 120; onClicked: page.sortBy("title") }
                            ToolButton { text: qsTr("艺术家") + (page.sortKey === "artist" ? (page.sortAscending ? " ↑" : " ↓") : ""); Layout.preferredWidth: 110; onClicked: page.sortBy("artist") }
                            ToolButton { text: qsTr("专辑") + (page.sortKey === "album" ? (page.sortAscending ? " ↑" : " ↓") : ""); Layout.preferredWidth: 110; onClicked: page.sortBy("album") }
                            ToolButton { text: qsTr("年份") + (page.sortKey === "year" ? (page.sortAscending ? " ↑" : " ↓") : ""); Layout.preferredWidth: 50; onClicked: page.sortBy("year") }
                            ToolButton { text: qsTr("时长") + (page.sortKey === "durationMs" ? (page.sortAscending ? " ↑" : " ↓") : ""); Layout.preferredWidth: 48; onClicked: page.sortBy("durationMs") }
                            Label { text: qsTr("封面"); color: Theme.secondaryText; Layout.preferredWidth: 42 }
                            Label { text: qsTr("状态"); color: Theme.secondaryText; Layout.preferredWidth: 56 }
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
                            height: 50
                            visible: true
                            clip: true
                            color: page.isSelected(sourceIndex) ? Theme.activeSelection
                                  : (rowHover.hovered ? Theme.hoverSurface : "transparent")
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
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 8
                                CheckBox {
                                    Layout.preferredWidth: 24
                                    checked: page.isSelected(sourceIndex)
                                    onClicked: page.toggleIndex(sourceIndex)
                                }
                                Rectangle {
                                    Layout.preferredWidth: 34
                                    Layout.preferredHeight: 34
                                    color: Theme.background
                                    radius: 4
                                    ThemedIcon {
                                        anchors.centerIn: parent
                                        source: Theme.icon("music-2-line")
                                        tint: Theme.iconSecondary
                                        sourceSize.width: 18
                                        sourceSize.height: 18
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Label {
                                        Layout.fillWidth: true
                                        text: metadata.title || metadata.fileName || ""
                                        color: Theme.primaryText
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: metadata.fileName || ""
                                        color: Theme.secondaryText
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                    }
                                }
                                Label {
                                    text: metadata.title || "—"
                                    color: Theme.primaryText
                                    Layout.preferredWidth: 120
                                    elide: Text.ElideRight
                                }
                                Label {
                                    text: metadata.artist || qsTr("未知")
                                    color: Theme.primaryText
                                    Layout.preferredWidth: 110
                                    elide: Text.ElideRight
                                }
                                Label {
                                    text: metadata.album || qsTr("未知")
                                    color: Theme.primaryText
                                    Layout.preferredWidth: 110
                                    elide: Text.ElideRight
                                }
                                Label {
                                    text: metadata.year || "—"
                                    color: Theme.secondaryText
                                    Layout.preferredWidth: 50
                                }
                                 Label {
                                     text: page.formatDuration(metadata.durationMs)
                                     color: Theme.secondaryText
                                     Layout.preferredWidth: 48
                                 }
                                Rectangle {
                                    Layout.preferredWidth: 34
                                    Layout.preferredHeight: 34
                                    color: metadata.hasCover ? Theme.elevated : Theme.background
                                    radius: 4
                                    Image {
                                        anchors.fill: parent
                                        anchors.margins: 2
                                        visible: metadata.coverPreview !== ""
                                        source: metadata.coverPreview || ""
                                        fillMode: Image.PreserveAspectCrop
                                    }
                                    ThemedIcon {
                                        anchors.centerIn: parent
                                        visible: metadata.coverPreview === ""
                                        source: Theme.icon("picture-in-picture-2-line")
                                        tint: metadata.hasCover ? Theme.waveformGreen : Theme.iconSecondary
                                        sourceSize.width: 17
                                        sourceSize.height: 17
                                    }
                                }
                                Label {
                                    readonly property var applyResult:
                                        page.resultForPath(metadata.path)
                                    text: metadata.hasError ? qsTr("错误")
                                          : applyResult
                                            ? (applyResult.stage === "preflight"
                                               ? (applyResult.success
                                                  ? qsTr("支持") : qsTr("不支持"))
                                               : applyResult.stage === "cancelled"
                                                 ? qsTr("已取消")
                                                 : (applyResult.success
                                                    ? qsTr("已修改") : qsTr("失败")))
                                            : qsTr("就绪")
                                    color: metadata.hasError
                                           || (applyResult && !applyResult.success)
                                           ? Theme.favoriteRed : Theme.waveformGreen
                                    Layout.preferredWidth: 56
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
                        objectName: "metadataFileFooter"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        color: Theme.background
                        border.color: Theme.border
                        border.width: 1
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 8
                            Label {
                                text: qsTr("已显示 %1 / %2，已选 %3")
                                      .arg(page.displayedIndices.length)
                                      .arg(MetadataEditor.fileCount)
                                      .arg(page.selectedIndices.length)
                                color: Theme.secondaryText
                                font.pixelSize: 10
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                objectName: "metadataExportCurrentListButton"
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
            }

            Rectangle {
                id: inspectorPanel
                objectName: "metadataInspectorPanel"
                visible: !page.compactLayout
                         || compactMetadataTabs.currentIndex === 1
                Layout.fillWidth: page.compactLayout
                Layout.preferredWidth: page.compactLayout
                                       ? (visible ? page.width - 28 : 0)
                                       : Math.max(480, page.width * 0.36)
                Layout.minimumWidth: page.compactLayout ? 0 : 460
                Layout.maximumWidth: page.compactLayout
                                     ? (visible ? 16777215 : 0)
                                     : 640
                Layout.fillHeight: true
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 12
                    clip: true
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    ColumnLayout {
                        width: parent.width
                        spacing: 5

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: qsTr("批量元数据编辑")
                                color: Theme.primaryText
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: qsTr("直接编辑；留空即清除")
                                color: Theme.secondaryText
                                font.pixelSize: 10
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("作用范围"); color: Theme.secondaryText }
                            ComboBox {
                                id: metadataScopeBox
                                objectName: "metadataScopeBox"
                                Layout.fillWidth: true
                                model: [
                                    { text: qsTr("当前文件"), value: "current" },
                                    { text: qsTr("已选文件"), value: "selected" },
                                    { text: qsTr("全部文件"), value: "all" }
                                ]
                                textRole: "text"
                                valueRole: "value"
                                currentIndex: 2
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("处理方式"); color: Theme.secondaryText }
                            ComboBox {
                                id: metadataProcessingModeBox
                                objectName: "metadataProcessingModeBox"
                                Layout.fillWidth: true
                                model: [
                                    { text: qsTr("仅修改元数据（流复制）"), value: "metadataOnly" },
                                    { text: qsTr("转换时写入新文件"), value: "convert" }
                                ]
                                textRole: "text"
                                valueRole: "value"
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("编辑框自动显示原值；不改动即保留，删除内容留空即清除。")
                            color: Theme.secondaryText
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.elevated
                            border.color: Theme.border
                            radius: Theme.radiusSm
                            implicitHeight: 32
                            Label {
                                anchors.fill: parent
                                anchors.margins: 8
                                text: metadataProcessingModeBox.currentValue === "metadataOnly"
                                      ? qsTr("流复制：不解码、不重编码；验证后替换原文件。")
                                      : qsTr("将复用格式转换器：转换完成后写入并验证元数据，源文件不覆盖。")
                                color: metadataProcessingModeBox.currentValue === "metadataOnly"
                                       ? Theme.secondaryText : Theme.favoriteRed
                                font.pixelSize: 10
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                        }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

                        Repeater {
                            id: fieldRepeater
                            model: page.fieldDefinitions
                            delegate: RowLayout {
                                id: fieldRow
                                required property var modelData
                                property string fieldKey: modelData.key
                                property string fieldLabel: modelData.label
                                property string selectedMode: "keep"
                                property string valueText: valueField.text
                                property bool loadingValue: false
                                function selectMode(mode) {
                                    selectedMode = mode
                                    if (mode === "clear")
                                        valueField.clear()
                                }
                                function setValue(value) {
                                    valueField.text = value
                                    selectedMode = value === "" ? "clear" : "set"
                                }
                                function loadValues(indexes) {
                                    loadingValue = true
                                    if (!indexes || indexes.length === 0) {
                                        valueField.text = ""
                                        valueField.placeholderText = qsTr("选择文件后显示原值")
                                    } else {
                                        const first = String(page.entry(indexes[0])[fieldKey] || "")
                                        let mixed = false
                                        for (let i = 1; i < indexes.length; ++i) {
                                            if (String(page.entry(indexes[i])[fieldKey] || "") !== first) {
                                                mixed = true
                                                break
                                            }
                                        }
                                        valueField.text = mixed ? "" : first
                                        valueField.placeholderText = mixed ? qsTr("多个值") : qsTr("留空即清除")
                                    }
                                    selectedMode = "keep"
                                    loadingValue = false
                                }
                                function descriptor() {
                                    return { mode: fieldRow.selectedMode,
                                             value: valueField.text }
                                }
                                Layout.fillWidth: true
                                Layout.preferredHeight: 24
                                spacing: 4
                                Label {
                                    text: fieldRow.fieldLabel
                                    color: Theme.secondaryText
                                    Layout.preferredWidth: 72
                                    elide: Text.ElideRight
                                }
                                TextField {
                                    id: valueField
                                    objectName: "metadataValueField_" + fieldRow.fieldKey
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 28
                                    font.pixelSize: 12
                                    placeholderText: qsTr("选择文件后显示原值")
                                    onTextEdited: {
                                        if (!fieldRow.loadingValue)
                                            fieldRow.selectedMode = text === "" ? "clear" : "set"
                                    }
                                }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                        ColumnLayout {
                            objectName: "metadataCoverSection"
                            Layout.fillWidth: true
                            spacing: 6
                            Label {
                                text: qsTr("封面（Cover Art）")
                                color: Theme.primaryText
                                font.weight: Font.DemiBold
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Rectangle {
                                    Layout.preferredWidth: 92
                                    Layout.preferredHeight: 92
                                    color: Theme.background
                                    border.color: Theme.border
                                    radius: Theme.radiusSm
                                    Image {
                                        anchors.fill: parent
                                        anchors.margins: 4
                                        source: MetadataEditor.coverImage !== ""
                                            ? MetadataEditor.coverImage
                                            : (page.selectionAnchor >= 0
                                               ? page.entry(page.selectionAnchor).coverPreview || "" : "")
                                        fillMode: Image.PreserveAspectFit
                                    }
                                    ThemedIcon {
                                        anchors.centerIn: parent
                                        visible: MetadataEditor.coverImage === ""
                                            && (page.selectionAnchor < 0
                                                || !(page.entry(page.selectionAnchor).coverPreview || ""))
                                    source: Theme.icon("picture-in-picture-2-line")
                                        tint: Theme.iconSecondary
                                        sourceSize.width: 22
                                        sourceSize.height: 22
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        Layout.fillWidth: true
                                        text: MetadataEditor.coverImage !== ""
                                            ? qsTr("新封面：已选择图片")
                                            : (page.selectionAnchor >= 0
                                               ? (page.entry(page.selectionAnchor).coverInfo || qsTr("当前文件无封面"))
                                               : qsTr("选择文件以查看当前封面"))
                                        color: Theme.secondaryText
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                    }
                                    Button {
                                        Layout.fillWidth: true
                                        text: qsTr("选择图片...")
                                        onClicked: coverDialog.open()
                                    }
                                    Button {
                                        Layout.fillWidth: true
                                        text: qsTr("移除选择")
                                        enabled: MetadataEditor.coverImage !== ""
                                        onClicked: {
                                            MetadataEditor.clearCoverImage()
                                        }
                                    }
                                }
                            }
                        }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                        ColumnLayout {
                            objectName: "metadataChangePreview"
                            Layout.fillWidth: true
                            spacing: 3
                            Label {
                                text: qsTr("修改预览（预估）")
                                color: Theme.primaryText
                                font.weight: Font.DemiBold
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: MetadataEditor.results.length > 0
                                text: qsTr("支持 %1 · 不支持 %2 · 成功 %3 · 失败 %4 · 取消 %5")
                                      .arg(MetadataEditor.supportedCount)
                                      .arg(MetadataEditor.unsupportedCount)
                                      .arg(MetadataEditor.successCount)
                                      .arg(MetadataEditor.failedCount)
                                      .arg(MetadataEditor.cancelledCount)
                                color: Theme.accent
                                font.pixelSize: 10
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: MetadataEditor.results.length === 0
                                text: metadataProcessingModeBox.currentValue === "convert"
                                      ? qsTr("将复用格式转换设置并输出新文件；原文件不会被覆盖。")
                                      : (metadataScopeBox.currentValue === "current"
                                         ? qsTr("将对当前焦点文件执行预检和流复制写入。")
                                         : (metadataScopeBox.currentValue === "selected"
                                            ? qsTr("将对 %1 个已选文件执行预检。").arg(selectedIndices.length)
                                            : qsTr("将对列表中的全部 %1 个文件执行预检。").arg(MetadataEditor.fileCount)))
                                color: Theme.secondaryText
                                wrapMode: Text.WordWrap
                            }
                            Repeater {
                                model: page.planLines()
                                delegate: Label {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: "✓ " + modelData
                                    color: Theme.waveformGreen
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }
                             Repeater {
                                 model: MetadataEditor.results
                                 delegate: Label {
                                     required property var modelData
                                     Layout.fillWidth: true
                                     text: (modelData.success ? "✓ " : "✕ ")
                                           + modelData.fileName + " · "
                                           + (modelData.success
                                              ? (modelData.message || modelData.stage || "")
                                              : (modelData.preflightReason
                                                 || modelData.errorCode
                                                 || modelData.message
                                                 || modelData.stage || ""))
                                     color: modelData.success
                                            ? Theme.waveformGreen
                                            : Theme.favoriteRed
                                    font.pixelSize: 10
                                    elide: Text.ElideMiddle
                                }
                            }
                        }
                        Item { Layout.preferredHeight: 2 }
                    }
                }
            }
        }

        Rectangle {
            id: bottomBar
            objectName: "metadataBottomBar"
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 10
                ColumnLayout {
                    Layout.preferredWidth: page.compactLayout ? 190 : 260
                    spacing: 2
                    Label {
                        text: metadataScopeBox.currentValue === "current"
                              ? qsTr("将修改当前文件")
                              : (metadataScopeBox.currentValue === "selected"
                                 ? qsTr("将修改 %1 个已选文件").arg(selectedIndices.length)
                                 : qsTr("将修改全部 %1 个文件").arg(MetadataEditor.fileCount))
                        color: Theme.primaryText
                    }
                    Label {
                        visible: !page.compactLayout
                        text: qsTr("写入采用临时文件和原子替换，不直接覆盖源文件。")
                        color: Theme.secondaryText
                        font.pixelSize: 10
                    }
                }
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    value: MetadataEditor.progress
                    visible: MetadataEditor.busy
                }
                Item { Layout.fillWidth: !MetadataEditor.busy }
                 Button {
                     objectName: "metadataPreflightButton"
                     text: qsTr("预检修改")
                    enabled: page.targetCount() > 0 && page.configuredEditCount() > 0
                             && !MetadataEditor.busy
                    onClicked: {
                        const payload = page.fieldPayload()
                        payload.coverMode = page.coverMode
                        let targets = metadataScopeBox.currentValue === "selected"
                                      ? page.selectedIndices : []
                        if (metadataScopeBox.currentValue === "current")
                            targets = page.selectionAnchor >= 0 ? [page.selectionAnchor] : []
                        if (metadataProcessingModeBox.currentValue === "metadataOnly")
                            MetadataEditor.preflightMetadata(payload, targets)
                        else
                            openConversionWorkflow()
                    }
                }
                 Button {
                     objectName: "metadataExportResultsButton"
                     text: qsTr("导出结果")
                    enabled: MetadataEditor.results.length > 0 && !MetadataEditor.busy
                    onClicked: {
                        page.exportMode = "results"
                        exportResultsDialog.open()
                    }
                }
                Button {
                    text: qsTr("取消")
                    visible: true
                    enabled: MetadataEditor.busy
                    onClicked: MetadataEditor.cancel()
                }
                 Button {
                     objectName: "metadataApplyButton"
                     text: qsTr("应用修改")
                    highlighted: true
                    enabled: page.targetCount() > 0 && page.configuredEditCount() > 0
                             && !MetadataEditor.busy
                    onClicked: {
                        const payload = page.fieldPayload()
                        payload.coverMode = page.coverMode
                        let targets = []
                        if (metadataScopeBox.currentValue === "current") {
                            if (page.selectionAnchor >= 0) targets = [page.selectionAnchor]
                        } else if (metadataScopeBox.currentValue === "selected") {
                            targets = page.selectedIndices
                        }
                        if (metadataProcessingModeBox.currentValue === "metadataOnly")
                            MetadataEditor.applyMetadata(payload, targets)
                        else
                            openConversionWorkflow()
                    }
                }
            }
        }
    }

    Connections {
        target: MetadataEditor
        function onEntriesLoaded() {
            page.selectedIndices = []
            page.selectionAnchor = -1
            ++page.entryRevision
            Qt.callLater(page.refreshFields)
        }
        function onEntriesChanged() {
            ++page.entryRevision
            Qt.callLater(page.refreshFields)
        }
        function onPreflightDecisionRequired() { preflightDecisionDialog.open() }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: page
    objectName: "metadataEditPage"
    color: Theme.background
    clip: true
    focus: true

    readonly property real desktopMinimumWidth: 1206
    readonly property bool compactLayout: width < desktopMinimumWidth
    readonly property real inspectorRatio: 0.44
    readonly property color canvasColor: Theme.editorCanvas
    readonly property color panelColor: Theme.panel
    readonly property color inputColor: Theme.elevated
    readonly property color borderColor: Theme.border
    readonly property color lineColor: Theme.border
    readonly property color mutedColor: Theme.secondaryText

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

    component ToolbarAction: ThemedButton {
        id: toolbarAction
        Layout.preferredHeight: Theme.controlHeightProminent
        leftPadding: 14
        rightPadding: 14
        spacing: 8

        contentItem: RowLayout {
            spacing: toolbarAction.spacing
            ThemedIcon {
                visible: String(toolbarAction.icon.source).length > 0
                source: toolbarAction.icon.source
                tint: toolbarAction.enabled ? Theme.textPrimary
                                             : Theme.textDisabled
                sourceSize.width: Theme.iconSizeMd
                sourceSize.height: Theme.iconSizeMd
            }
            Text {
                text: toolbarAction.text
                color: toolbarAction.enabled ? Theme.textPrimary
                                             : Theme.textDisabled
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeBody
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }

    component ColumnHeader: ToolButton {
        property string sortField: ""
        flat: true
        font.family: Theme.fontPrimary
        font.pixelSize: Theme.fontSizeBody
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

    function resultStageLabel(stage) {
        switch (String(stage || "")) {
        case "preflight": return qsTr("预检")
        case "prepare": return qsTr("准备")
        case "write": return qsTr("写入")
        case "verified": return qsTr("回读验证")
        case "cancelled": return qsTr("取消")
        default: return qsTr("处理")
        }
    }

    function metadataErrorInfo(errorCode) {
        switch (Number(errorCode)) {
        case 1: return { name: "InvalidEditPlan", cause: qsTr("修改内容无效或字段值不符合要求。"), action: qsTr("检查输入内容后重试。") }
        case 2: return { name: "PhysicalTagConflict", cause: qsTr("多个字段映射到同一个物理标签，无法同时写入。"), action: qsTr("只保留其中一个冲突字段后重试。") }
        case 3: return { name: "PermissionDenied", cause: qsTr("目标目录没有写入权限。"), action: qsTr("更换可写目录，或为当前用户授予写入权限。") }
        case 4: return { name: "ReadOnlyFile", cause: qsTr("音频文件是只读文件。"), action: qsTr("取消文件的只读属性后重试。") }
        case 5: return { name: "FileInUse", cause: qsTr("文件正在被占用，无法安全替换。"), action: qsTr("关闭正在播放该文件的播放器或其他占用程序后重试。") }
        case 6: return { name: "InsufficientDiskSpace", cause: qsTr("磁盘空间不足，无法创建安全临时副本。"), action: qsTr("释放目标磁盘空间后重试。") }
        case 7: return { name: "UnsupportedContainer", cause: qsTr("当前音频容器不支持所选元数据修改。"), action: qsTr("减少不支持的字段，或先转换为 MP3、FLAC 等受支持格式。") }
        case 8: return { name: "UnsupportedMuxer", cause: qsTr("当前格式没有可用的安全写入器。"), action: qsTr("转换为受支持格式后再修改。") }
        case 9: return { name: "UnsupportedField", cause: qsTr("当前格式不支持至少一个所选字段。"), action: qsTr("根据预检提示取消不支持的字段后重试。") }
        case 10: return { name: "UnsupportedCover", cause: qsTr("当前格式或封面类型不支持写入。"), action: qsTr("改用 JPG/PNG 封面，或转换为支持封面的格式。") }
        case 11: return { name: "UnsupportedStructure", cause: qsTr("文件包含暂不支持安全保留的流或结构。"), action: qsTr("先备份文件，再转换为标准音频结构后修改。") }
        case 12: return { name: "InputOpenFailed", cause: qsTr("无法读取音频文件或其元数据。"), action: qsTr("确认文件存在、可读取且未损坏。") }
        case 13: return { name: "OutputCreateFailed", cause: qsTr("无法在目标目录创建临时输出文件。"), action: qsTr("检查目录权限、文件占用和可用空间。") }
        case 14: return { name: "HeaderWriteFailed", cause: qsTr("写入容器头或标签头失败。"), action: qsTr("检查文件是否损坏，或改用受支持格式。") }
        case 15: return { name: "PacketReadFailed", cause: qsTr("读取原音频数据包失败。"), action: qsTr("文件可能损坏；请先确认它能完整播放。") }
        case 16: return { name: "PacketWriteFailed", cause: qsTr("复制音频数据包到临时文件时失败。"), action: qsTr("检查磁盘、文件系统和剩余空间后重试。") }
        case 17: return { name: "TrailerWriteFailed", cause: qsTr("完成临时文件封装时失败。"), action: qsTr("检查磁盘状态，或转换为标准格式后重试。") }
        case 18: return { name: "VerificationFailed", cause: qsTr("写入后的元数据或音频流回读验证失败，原文件已尽量恢复。"), action: qsTr("不要继续批量处理；检查该文件后单独重试。") }
        case 19: return { name: "SourceChanged", cause: qsTr("源文件在预检后发生变化。"), action: qsTr("重新加载文件后再试。") }
        case 20: return { name: "AtomicReplaceFailed", cause: qsTr("临时文件验证通过，但无法安全替换原文件。"), action: qsTr("关闭文件占用程序并检查目录权限后重试。") }
        case 21: return { name: "Cancelled", cause: qsTr("操作已被取消。"), action: qsTr("需要时重新应用修改。") }
        case 22: return { name: "InternalError", cause: qsTr("元数据处理发生内部错误。"), action: qsTr("保留此错误信息并重新启动 AgPlayer 后重试。") }
        default: return { name: "Unknown", cause: qsTr("未返回可识别的失败原因。"), action: qsTr("请保留文件名、阶段和错误码用于排查。") }
        }
    }

    function resultDetailText(result) {
        if (!result)
            return qsTr("原因：未返回处理结果。")
        if (result.success)
            return result.message || qsTr("处理完成。")

        const info = metadataErrorInfo(result.errorCode)
        const backendReason = String(result.preflightReason
                                     || result.message || "").trim()
        const reason = info.name === "Unknown" && backendReason.length > 0
                     ? backendReason : info.cause
        const numericCode = Number(result.errorCode)
        const codeText = isNaN(numericCode)
                       ? info.name : info.name + " (" + numericCode + ")"
        let details = qsTr("失败阶段：%1").arg(resultStageLabel(result.stage))
                + "\n" + qsTr("原因：%1").arg(reason)
        if (backendReason.length > 0 && backendReason !== reason)
            details += "\n" + qsTr("技术详情：%1").arg(backendReason)
        return details + "\n" + qsTr("错误码：%1").arg(codeText)
                + "\n" + qsTr("建议：%1").arg(info.action)
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
        if (suffix === "mp3") return "#19a56f" // theme-color-allow: file format badge
        if (suffix === "flac") return "#ea5f32" // theme-color-allow: file format badge
        if (suffix === "m4a") return "#805fd0" // theme-color-allow: file format badge
        if (suffix === "opus") return "#2cb3bd" // theme-color-allow: file format badge
        if (suffix === "wav") return "#159ec7" // theme-color-allow: file format badge
        return "#6d7f8c" // theme-color-allow: file format badge
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

    ThemedDialog {
        id: threeStateHelpDialog
        modal: true
        title: qsTr("三态编辑说明")
        anchors.centerIn: parent
        width: Math.min(440, page.width - 2 * Theme.spacing2Xl)
        contentWidth: Math.max(0, width - leftPadding - rightPadding)
        contentHeight: threeStateHelpContent.implicitHeight
        standardButtons: Dialog.Ok
        contentItem: Label {
            id: threeStateHelpContent
            width: threeStateHelpDialog.contentWidth
            wrapMode: Text.WordWrap
            text: qsTr("保留：每个文件保持原值，不写入。\n\n设为：将输入值统一写入目标文件；空值无效，请使用清除。\n\n清除：删除该字段的所有已知别名标签。")
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeBody
        }
    }

    ThemedDialog {
        id: metadataErrorDialog
        objectName: "metadataErrorDialog"
        modal: true
        title: qsTr("元数据处理提示")
        anchors.centerIn: parent
        width: Math.min(360, page.width - 2 * Theme.spacing2Xl)
        contentWidth: Math.max(0, width - leftPadding - rightPadding)
        contentHeight: metadataErrorContent.implicitHeight
        standardButtons: Dialog.Ok
        contentItem: Label {
            id: metadataErrorContent
            width: metadataErrorDialog.contentWidth
            text: page.errorMessage
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.WordWrap
        }
    }

    ThemedDialog {
        id: preflightDecisionDialog
        objectName: "metadataPreflightDecisionDialog"
        modal: true
        title: qsTr("预检发现不支持项")
        anchors.centerIn: parent
        width: Math.min(500, page.width - 2 * Theme.spacing2Xl)
        contentWidth: Math.max(0, width - leftPadding - rightPadding)
        contentHeight: metadataPreflightContent.implicitHeight
        closePolicy: Popup.NoAutoClose
        contentItem: ColumnLayout {
            id: metadataPreflightContent
            width: preflightDecisionDialog.contentWidth
            implicitWidth: preflightDecisionDialog.contentWidth
            spacing: Theme.spacingMd
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeBody
                text: qsTr("%1 个文件可安全修改，%2 个文件不支持。")
                      .arg(MetadataEditor.supportedCount)
                      .arg(MetadataEditor.unsupportedCount)
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                ThemedButton {
                    text: qsTr("取消")
                    onClicked: {
                        MetadataEditor.applyPreflightDecision("cancel")
                        preflightDecisionDialog.close()
                    }
                }
                ThemedButton {
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
            Layout.preferredHeight: Theme.controlHeightProminent
            spacing: page.compactLayout ? Theme.spacingSm : Theme.spacingMd

            ToolbarAction {
                Layout.preferredWidth: page.compactLayout ? 104 : 136
                Layout.minimumWidth: page.compactLayout ? 92 : 110
                text: qsTr("添加文件")
                icon.source: Theme.icon("add-line")
                onClicked: audioDialog.open()
            }
            ToolbarAction {
                Layout.preferredWidth: page.compactLayout ? 112 : 151
                Layout.minimumWidth: page.compactLayout ? 100 : 120
                text: qsTr("添加文件夹")
                icon.source: Theme.icon("folder-add-line")
                onClicked: folderDialog.open()
            }
            ToolbarAction {
                Layout.preferredWidth: page.compactLayout ? 134 : 184
                Layout.minimumWidth: page.compactLayout ? 118 : 145
                text: qsTr("从播放列表添加")
                icon.source: Theme.icon("music-2-line")
                enabled: !MetadataEditor.busy && PlaybackController.currentTrackId.length > 0
                onClicked: page.addCurrentPlayerTrack()
            }
            ToolbarAction {
                Layout.preferredWidth: page.compactLayout ? 104 : 142
                Layout.minimumWidth: page.compactLayout ? 92 : 112
                text: qsTr("移除选中")
                icon.source: Theme.icon("delete-bin-line")
                enabled: selectedIndices.length > 0 && !MetadataEditor.busy
                onClicked: page.deleteSelection()
            }
            ToolbarAction {
                Layout.preferredWidth: page.compactLayout ? 104 : 140
                Layout.minimumWidth: page.compactLayout ? 92 : 112
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
            ThemedTextField {
                objectName: "metadataSearchField"
                visible: false
                Layout.preferredWidth: 244
                Layout.preferredHeight: Theme.controlHeight
                leftPadding: 14
                rightPadding: 12
                placeholderText: qsTr("搜索文件名、标签或路径...")
                onTextChanged: page.searchText = text
            }
            ThemedComboBox {
                objectName: "metadataStatusFilter"
                Layout.preferredWidth: page.compactLayout ? 96 : 110
                Layout.minimumWidth: page.compactLayout ? 86 : 100
                Layout.preferredHeight: Theme.controlHeight
                textRole: "text"
                valueRole: "value"
                model: [
                    { text: qsTr("全部状态"), value: "all" },
                    { text: qsTr("就绪"), value: "ready" },
                    { text: qsTr("待处理"), value: "supported" },
                    { text: qsTr("已修改"), value: "modified" },
                    { text: qsTr("失败"), value: "failed" }
                ]
                onCurrentValueChanged: page.statusFilter = currentValue || "all"
            }
        }

        RowLayout {
            id: compactMetadataTabs
            objectName: "metadataCompactTabs"
            property int currentIndex: 0
            visible: page.compactLayout
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 36 : 0
            Layout.maximumHeight: Layout.preferredHeight
            spacing: 0

            ThemedTabButton {
                objectName: "metadataCompactFilesTab"
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: qsTr("文件列表")
                iconSource: Theme.icon("file-copy-line")
                selected: compactMetadataTabs.currentIndex === 0
                onClicked: compactMetadataTabs.currentIndex = 0
            }
            ThemedTabButton {
                objectName: "metadataCompactEditorTab"
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: qsTr("编辑元数据")
                iconSource: Theme.icon("equalizer-line")
                selected: compactMetadataTabs.currentIndex === 1
                onClicked: compactMetadataTabs.currentIndex = 1
            }
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
                    Layout.fillWidth: page.compactLayout ? visible : true
                    Layout.fillHeight: true
                    Layout.preferredWidth: page.compactLayout
                                           ? 0
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
                            Layout.preferredHeight: Theme.listRowHeight
                            Layout.leftMargin: 16
                            Layout.rightMargin: 12
                            spacing: 10
                            Label {
                                text: qsTr("文件列表")
                                color: Theme.primaryText
                                font.pixelSize: Theme.fontSizeBody
                                font.weight: Font.DemiBold
                            }
                            Rectangle {
                                implicitWidth: selectedChipText.implicitWidth + 24
                                implicitHeight: 28
                                radius: 5
                                color: Theme.selectedTrackSelection
                                border.color: Theme.accent
                                Label {
                                    id: selectedChipText
                                    anchors.centerIn: parent
                                    text: qsTr("已选 %1").arg(page.selectedIndices.length)
                                    color: Theme.primaryText
                                    font.pixelSize: Theme.fontSizeBody
                                }
                            }
                            Rectangle {
                                implicitWidth: totalChipText.implicitWidth + 24
                                implicitHeight: 28
                                radius: 5
                                color: Theme.elevated
                                Label {
                                    id: totalChipText
                                    anchors.centerIn: parent
                                    text: qsTr("总计 %1").arg(MetadataEditor.fileCount)
                                    color: page.mutedColor
                                    font.pixelSize: Theme.fontSizeBody
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.tableHeaderHeight
                            color: Theme.background
                            border.width: 1
                            border.color: page.lineColor
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 8
                                ThemedCheckBox {
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
                                height: Theme.mediaListRowHeight
                                color: page.isSelected(sourceIndex)
                                      ? Theme.selectedTrackSelection
                                      : (rowHover.hovered ? Theme.hoverSurface
                                                          : "transparent")
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
                                    ThemedCheckBox {
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
                                                tint: "white" // theme-color-allow: fixed file badge icon
                                                sourceSize.width: 14
                                                sourceSize.height: 14
                                            }
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: metadata.fileName || ""
                                            color: Theme.primaryText
                                            font.pixelSize: Theme.fontSizeBody
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
                                               ? Theme.error : Theme.accent
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
                            Layout.preferredHeight: Theme.listRowHeight
                            Layout.leftMargin: 10
                            Layout.rightMargin: 14
                            spacing: 10
                            ThemedCheckBox {
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
                                font.pixelSize: Theme.fontSizeBody
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
                    Layout.fillWidth: page.compactLayout && visible
                    Layout.fillHeight: true
                    Layout.preferredWidth: page.compactLayout
                                           ? 0
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
                                    font.pixelSize: Theme.fontSizeSection
                                    font.weight: Font.DemiBold
                                }
                                Label {
                                    text: qsTr("已选 %1 个文件").arg(page.selectedIndices.length)
                                    color: page.mutedColor
                                    font.pixelSize: Theme.fontSizeBody
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
                                Label { text: qsTr("作用范围"); color: page.mutedColor; font.pixelSize: Theme.fontSizeCaption }
                                ThemedComboBox {
                                    id: metadataScopeBox
                                    objectName: "metadataScopeBox"
                                    Layout.preferredWidth: 132
                                    Layout.preferredHeight: Theme.controlHeight
                                    textRole: "text"
                                    valueRole: "value"
                                    model: [
                                        { text: qsTr("当前文件"), value: "current" },
                                        { text: qsTr("已选文件"), value: "selected" },
                                        { text: qsTr("全部文件"), value: "all" }
                                    ]
                                    currentIndex: 1
                                    onCurrentValueChanged: Qt.callLater(page.refreshFields)
                                }
                                Label { text: qsTr("处理方式"); color: page.mutedColor; font.pixelSize: Theme.fontSizeCaption }
                                ThemedComboBox {
                                    id: metadataProcessingModeBox
                                    objectName: "metadataProcessingModeBox"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Theme.controlHeight
                                    textRole: "text"
                                    valueRole: "value"
                                    model: [
                                        { text: qsTr("仅修改元数据（流复制）"), value: "metadataOnly" },
                                        { text: qsTr("转换时写入新文件"), value: "convert" }
                                    ]
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
                                    text: qsTr("未修改字段保持原值；点击清空会删除字段。")
                                    color: page.mutedColor
                                    font.pixelSize: Theme.fontSizeCaption
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
                                                // Loading an existing value is not an edit.  A field only
                                                // becomes set/clear after the user acts on it.
                                                selectedMode = "keep"
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
                                                font.pixelSize: Theme.fontSizeBody
                                            }
                                            Item {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: Theme.controlHeight
                                                ThemedTextField {
                                                    id: valueField
                                                    objectName: "metadataValueField_" + fieldRow.fieldKey
                                                    anchors.fill: parent
                                                    enabled: fieldRow.selectedMode !== "clear"
                                                    rightPadding: fieldRow.fieldKey === "date" ? 38 : 12
                                                    leftPadding: 14
                                                    placeholderText: fieldRow.selectedMode === "clear"
                                                                       ? qsTr("将清除")
                                                                       : fieldRow.sourceMultiple
                                                                         ? qsTr("多种值")
                                                                         : qsTr("保留原值")
                                                    onTextEdited: fieldRow.selectedMode = text.length > 0
                                                                                  ? "set"
                                                                                  : ((!fieldRow.sourceMultiple
                                                                                      && fieldRow.sourceValue !== "")
                                                                                     ? "clear" : "keep")
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
                                            ToolButton {
                                                objectName: "metadataClearButton_" + fieldRow.fieldKey
                                                text: qsTr("清空")
                                                Accessible.name: qsTr("清空") + fieldRow.fieldLabel
                                                Layout.preferredWidth: 46
                                                onClicked: fieldRow.selectMode("clear")
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
                                        font.pixelSize: Theme.fontSizeBody
                                        font.weight: Font.DemiBold
                                    }
                                    Rectangle {
                                        objectName: "metadataCoverPreview"
                                        Layout.preferredWidth: 212
                                        Layout.preferredHeight: 210
                                        color: page.inputColor
                                        border.width: 1
                                        border.color: Theme.border
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
                                        font.pixelSize: Theme.fontSizeCaption
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
                                        font.pixelSize: Theme.fontSizeCaption
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
                                color: Theme.background
                                border.width: 1
                                border.color: page.borderColor
                                radius: 6
                                clip: true

                                ScrollView {
                                    id: metadataChangePreviewScroll
                                    objectName: "metadataChangePreviewScroll"
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    clip: true
                                    contentWidth: availableWidth
                                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                                    ColumnLayout {
                                        width: metadataChangePreviewScroll.availableWidth
                                        spacing: 4
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            text: qsTr("修改摘要（预览结果）")
                                            color: Theme.primaryText
                                            font.pixelSize: Theme.fontSizeBody
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
                                                tint: Theme.success
                                                sourceSize.width: 16
                                                sourceSize.height: 16
                                            }
                                            Label {
                                                Layout.fillWidth: true
                                                text: modelData
                                                color: page.mutedColor
                                                font.pixelSize: Theme.fontSizeCaption
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                    Label {
                                        objectName: "metadataResultsSummaryLabel"
                                        Layout.fillWidth: true
                                        visible: MetadataEditor.results.length > 0
                                        text: qsTr("成功 %1 · 失败 %2 · 不支持 %3 · 取消 %4")
                                              .arg(MetadataEditor.successCount)
                                              .arg(MetadataEditor.failedCount)
                                              .arg(MetadataEditor.unsupportedCount)
                                              .arg(MetadataEditor.cancelledCount)
                                        color: Theme.accent
                                        font.pixelSize: Theme.fontSizeCaption
                                    }
                                    Repeater {
                                        model: MetadataEditor.results
                                        delegate: Rectangle {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            implicitHeight: resultColumn.implicitHeight + 16
                                            radius: 4
                                            color: modelData.success
                                                   ? Qt.rgba(0.10, 0.62, 0.39, 0.08)
                                                   : Qt.rgba(0.91, 0.25, 0.28, 0.10)
                                            border.width: 1
                                            border.color: modelData.success
                                                          ? Qt.rgba(0.10, 0.62, 0.39, 0.35)
                                                          : Qt.rgba(0.91, 0.25, 0.28, 0.45)

                                            ColumnLayout {
                                                id: resultColumn
                                                anchors.fill: parent
                                                anchors.margins: 8
                                                spacing: 3

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: (modelData.fileName || qsTr("未知文件"))
                                                          + (modelData.success
                                                             ? qsTr(" · 成功") : qsTr(" · 失败"))
                                                    color: modelData.success
                                                           ? Theme.success : Theme.error
                                                    font.pixelSize: Theme.fontSizeCaption
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideMiddle
                                                }
                                                Label {
                                                    Layout.fillWidth: true
                                                    text: page.resultDetailText(modelData)
                                                    color: modelData.success
                                                           ? page.mutedColor : Theme.primaryText
                                                    font.pixelSize: Theme.fontSizeCaption
                                                    wrapMode: Text.Wrap
                                                }
                                            }
                                        }
                                    }
                                    Item { Layout.fillHeight: true }
                                    }
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
                                ThemedButton {
                                    id: applyButton
                                    objectName: "metadataApplyButton"
                                    Layout.preferredWidth: 214
                                    Layout.preferredHeight: Theme.controlHeightProminent
                                    text: qsTr("应用修改")
                                    enabled: page.targetCount() > 0
                                             && page.configuredEditCount() > 0
                                             && !MetadataEditor.busy
                                    primary: true
                                    prominent: true
                                    onClicked: page.applyEdits()
                                }
                                ThemedButton {
                                    id: cancelButton
                                    objectName: "metadataCancelButton"
                                    visible: true
                                    enabled: MetadataEditor.busy
                                    Layout.preferredWidth: 198
                                    Layout.preferredHeight: Theme.controlHeightProminent
                                    text: qsTr("取消")
                                    icon.source: Theme.icon("restore-line")
                                    prominent: true
                                    onClicked: MetadataEditor.cancel()
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

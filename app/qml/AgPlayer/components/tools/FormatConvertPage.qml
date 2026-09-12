import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: page
    objectName: "formatConvertPage"
    color: Theme.background
    clip: true
    focus: true

    property var converter: FormatConverter
    property string outputDirectory: SettingsController.defaultOutputDirectory
    function usesCompactLayout(availableWidth) { return availableWidth < 1500 }
    readonly property bool compactLayout: usesCompactLayout(width)

    Component.onCompleted: {
        converter.parallelJobs = SettingsController.parallelJobs
        converter.selectedFormat = SettingsController.transcodeFormat.toLowerCase()
    }

    Connections {
        target: SettingsController
        function onTranscodeFormatChanged() {
            const format = SettingsController.transcodeFormat.toLowerCase()
            if (converter.selectedFormat !== format)
                converter.selectedFormat = format
        }
    }

    onOutputDirectoryChanged: {
        if (SettingsController.defaultOutputDirectory !== outputDirectory)
            SettingsController.defaultOutputDirectory = outputDirectory
    }

    function addCurrentPlayerTrack() {
        const urls = []
        const ids = PlaybackController.queueTrackIds.length > 0
                  ? PlaybackController.queueTrackIds
                  : [PlaybackController.currentTrackId]
        for (let i = 0; i < ids.length; ++i) {
            const track = LibraryModel.trackForId(ids[i])
            if (track && track.path)
                urls.push(Qt.resolvedUrl("file:///" + encodeURI(
                    String(track.path).replace(/\\/g, "/"))))
        }
        converter.loadFiles(urls)
    }

    function requestPlan() {
        converter.bitrateMode = settingsPanel.bitrateMode
        converter.conflictPolicy = settingsPanel.conflictPolicy
        const requestedBitRate = Number(settingsPanel.bitRate)
        const requestedQuality = Number(settingsPanel.quality)
        const requestedSampleRate = Number(settingsPanel.sampleRate)
        SettingsController.transcodeFormat = settingsPanel.outputFormat.toUpperCase()
        if (isFinite(requestedBitRate) && requestedBitRate > 0)
            SettingsController.transcodeBitrateKbps = Math.round(requestedBitRate / 1000)
        if (isFinite(requestedSampleRate) && requestedSampleRate > 0)
            SettingsController.transcodeSampleRateHz = requestedSampleRate
        if (settingsPanel.channels === 1 || settingsPanel.channels === 2)
            SettingsController.transcodeChannels = settingsPanel.channels
        const plan = converter.buildPreflight({
            outputFormat: settingsPanel.outputFormat,
            bitRate: isFinite(requestedBitRate) ? requestedBitRate : 0,
            bitrateMode: settingsPanel.bitrateMode,
            quality: isFinite(requestedQuality) ? requestedQuality : 75,
            sampleRate: isFinite(requestedSampleRate) ? requestedSampleRate : 0,
            channels: settingsPanel.channels,
            outputDir: outputDirectory,
            keepMetadata: settingsPanel.keepMetadata,
            volumeNormalize: settingsPanel.volumeNormalize,
            extractAudio: settingsPanel.extractAudio,
            keepCover: settingsPanel.keepCover,
            preserveDirectories: settingsPanel.preserveDirectories,
            sampleFormat: settingsPanel.sampleFormat,
            bitDepth: settingsPanel.bitDepth,
            channelLayout: settingsPanel.channelLayout,
            audioStreamIndex: -1
        })
        if (plan.ready)
            preflightDialog.open()
        else {
            const reason = plan.error || plan.reason || qsTr("转换预检失败")
            errorDialog.summary = reason
            errorDialog.detail = reason
            errorDialog.open()
        }
    }

    Component {
        id: fileDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFiles
            nameFilters: [qsTr("音频与视频文件 (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.aif *.aiff *.mp4 *.mkv *.avi *.mov *.webm)")]
            onAccepted: {
                converter.addUrls(selectedFiles)
                destroy()
            }
            onRejected: destroy()
        }
    }

    Component {
        id: folderDialogComponent
        FolderDialog {
            onAccepted: {
                converter.addFolder(selectedFolder)
                destroy()
            }
            onRejected: destroy()
        }
    }

    Component {
        id: outputDialogComponent
        FolderDialog {
            onAccepted: {
                page.outputDirectory = selectedFolder.toString().replace(/^file:\/+/, "")
                destroy()
            }
            onRejected: destroy()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingXs

        Rectangle {
            id: toolbar
            objectName: "formatToolbar"
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.settingsRowHeight + Theme.spacingSm
            color: Theme.panel
            border.color: Theme.border
            radius: Theme.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingLg
                anchors.rightMargin: Theme.spacingLg
                spacing: Theme.spacingMd

                Repeater {
                    model: [
                        { text: qsTr("添加文件"), icon: "add-line", action: "file" },
                        { text: qsTr("添加文件夹"), icon: "folder-open-line", action: "folder" },
                        { text: qsTr("从播放列表添加"), icon: "music-2-line", action: "playlist" },
                        { text: qsTr("移除选中"), icon: "delete-bin-line", action: "remove" },
                        { text: qsTr("清空列表"), icon: "delete-bin-line", action: "clear" }
                    ]
                    ThemedButton {
                        objectName: modelData.action === "file"
                                    ? "formatAddFileButton"
                                    : "formatToolbarButton-" + modelData.action
                        prominent: true
                        Layout.preferredWidth: page.compactLayout
                                               ? (modelData.action === "playlist" ? 142 : 120)
                                               : modelData.action === "playlist" ? 158
                                               : modelData.action === "file" ? 130
                                               : modelData.action === "folder" ? 142
                                               : 128
                        Layout.preferredHeight: Theme.controlHeightProminent
                        available: !converter.busy
                                 && (modelData.action !== "playlist"
                                     || PlaybackController.currentTrackId.length > 0)
                        text: modelData.text
                        icon.source: Theme.icon(modelData.icon)
                        onClicked: {
                            if (modelData.action === "file")
                                fileDialogComponent.createObject(page).open()
                            else if (modelData.action === "folder")
                                folderDialogComponent.createObject(page).open()
                            else if (modelData.action === "playlist")
                                page.addCurrentPlayerTrack()
                            else if (modelData.action === "remove")
                                converter.removeChecked()
                            else
                                converter.clear()
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            ThemedIcon {
                                objectName: "formatToolbarIcon-" + modelData.action
                                source: parent.parent.icon.source
                                tint: Theme.iconPrimary
                                sourceSize.width: 18
                                sourceSize.height: 18
                                Layout.preferredWidth: 18
                                Layout.preferredHeight: 18
                            }
                            Text {
                                text: parent.parent.text
                                color: Theme.primaryText
                                font.pixelSize: Theme.fontSizeBody
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingSm

            FormatTaskTable {
                id: taskTable
                objectName: "formatTaskPanel"
                Layout.fillWidth: true
                Layout.fillHeight: true
                converter: page.converter
                settingsPanel: settingsPanel
            }

            FormatSettingsPanel {
                id: settingsPanel
                objectName: "formatSettingsPanel"
                Layout.preferredWidth: settingsPanel.expanded
                                       ? (page.compactLayout ? 360 : 445) : 40
                Layout.minimumWidth: settingsPanel.isExpanded
                                     ? (page.compactLayout ? 340 : 420) : 40
                Layout.maximumWidth: settingsPanel.isExpanded
                                     ? (page.compactLayout ? 380 : 455) : 40
                Layout.fillHeight: true
                converter: page.converter
                forceCollapsed: page.compactLayout
                outputDirectory: page.outputDirectory
                onOutputDirectoryEdited: function(directory) { page.outputDirectory = directory }
                onChooseOutputDirectory: outputDialogComponent.createObject(page).open()
            }
        }

        Rectangle {
            id: bottomBar
            objectName: "formatBottomBar"
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.settingsRowHeight + Theme.spacingLg
            color: Theme.panel
            border.color: Theme.border
            radius: Theme.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: page.compactLayout ? Theme.spacingMd
                                                       : Theme.spacingLg
                anchors.rightMargin: page.compactLayout ? Theme.spacingMd
                                                        : Theme.spacingLg
                spacing: page.compactLayout ? Theme.spacingSm
                                            : Theme.spacingLg

                ColumnLayout {
                    Layout.preferredWidth: page.compactLayout ? 190 : 430
                    spacing: Theme.spacingXs
                    RowLayout {
                        Text { text: qsTr("总进度"); color: Theme.primaryText; font.pixelSize: Theme.fontSizeBody }
                        ProgressBar {
                            id: totalProgress
                            objectName: "formatTotalProgress"
                            Layout.preferredWidth: page.compactLayout ? 108 : 320
                            from: 0; to: 1; value: converter.progress
                            background: Rectangle { implicitHeight: 10; color: Theme.hoverSurface; radius: 5 }
                            contentItem: Item {
                                implicitHeight: 10
                                Rectangle {
                                    width: totalProgress.visualPosition * parent.width
                                    height: parent.height
                                    radius: 5
                                    color: Theme.accent
                                }
                            }
                        }
                        Text { text: Math.round(totalProgress.value * 100) + "%"; color: Theme.primaryText }
                    }
                    Text {
                        text: qsTr("%1 个任务 / 预计剩余 %2").arg(converter.fileCount)
                              .arg(converter.etaText)
                        color: Theme.secondaryText
                        font.pixelSize: Theme.fontSizeBody
                    }
                }

                Rectangle {
                    visible: !page.compactLayout
                    Layout.preferredWidth: visible ? 1 : 0
                    Layout.fillHeight: visible
                    Layout.topMargin: 18
                    Layout.bottomMargin: 18
                    color: Theme.border
                }

                Item { Layout.fillWidth: true }

                RowLayout {
                    objectName: "converterParallelJobsGroup"
                    spacing: Theme.spacingXs
                    Text {
                        text: qsTr("并发")
                        color: Theme.secondaryText
                        font.pixelSize: Theme.fontSizeBody
                    }
                    ThemedComboBox {
                        id: converterParallelJobsBox
                        objectName: "converterParallelJobsBox"
                        Layout.preferredWidth: page.compactLayout ? 64 : 72
                        model: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
                        currentIndex: Math.max(0, model.indexOf(SettingsController.parallelJobs))
                        enabled: !converter.busy
                        onActivated: SettingsController.parallelJobs = currentValue
                        background: Rectangle {
                            objectName: "converterParallelJobsBoxFrame"
                            color: parent.enabled ? Theme.elevated : Theme.background
                            border.color: Theme.border
                            border.width: 1
                            radius: 5
                        }
                    }
                }

                Rectangle {
                    objectName: "formatSummaryCard"
                    Layout.preferredWidth: page.compactLayout ? 150 : 230
                    Layout.preferredHeight: 42
                    color: Theme.elevated
                    border.color: Theme.border
                    radius: Theme.radiusSm
                    RowLayout {
                        anchors.centerIn: parent
                        spacing: page.compactLayout ? Theme.spacingSm : Theme.spacingLg
                        ThemedIcon { visible: !page.compactLayout; objectName: "formatSummaryCompleteIcon"; source: Theme.icon("checkbox-circle-line"); tint: Theme.success; sourceSize.width: 18; sourceSize.height: 18 }
                        Text { text: qsTr("已完成 %1").arg(converter.doneCount); color: Theme.success }
                        ThemedIcon { visible: !page.compactLayout; objectName: "formatSummaryFailedIcon"; source: Theme.icon("error-warning-line"); tint: Theme.error; sourceSize.width: 18; sourceSize.height: 18 }
                        Text { text: qsTr("失败 %1").arg(converter.failedCount); color: Theme.error }
                    }
                    MouseArea {
                        objectName: "formatCompletedSummaryFilter"
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width / 2
                        height: parent.height
                        onClicked: converter.filteredTaskModel.statusFilter =
                            converter.filteredTaskModel.statusFilter === "Done" ? "All" : "Done"
                    }
                    MouseArea {
                        objectName: "formatFailedSummaryFilter"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width / 2
                        height: parent.height
                        onClicked: converter.filteredTaskModel.statusFilter =
                            converter.filteredTaskModel.statusFilter === "Error" ? "All" : "Error"
                    }
                }

                Item { Layout.preferredWidth: page.compactLayout ? 0 : 159 }

                ThemedButton {
                    id: convertAllButton
                    objectName: "convertAllButton"
                    primary: true
                    prominent: true
                    Layout.preferredWidth: page.compactLayout ? 110 : 174
                    Layout.preferredHeight: Theme.controlHeightProminent
                    available: converter.checkedCount > 0 && !converter.busy
                    text: qsTr("开始处理")
                    icon.source: Theme.icon("play-fill")
                    onClicked: page.requestPlan()
                    contentItem: RowLayout {
                        spacing: 10
                        ThemedIcon { objectName: "convertAllButtonIcon"; source: convertAllButton.icon.source; tint: convertAllButton.enabled ? Theme.accentText : Theme.textDisabled; sourceSize.width: Theme.iconSizeMd; sourceSize.height: Theme.iconSizeMd }
                        Text { objectName: "convertAllButtonLabel"; text: convertAllButton.text; color: convertAllButton.enabled ? Theme.accentText : Theme.textDisabled; font.pixelSize: Theme.fontSizeBody }
                    }
                }

                ThemedButton {
                    objectName: "cancelAllButton"
                    prominent: true
                    Layout.preferredWidth: page.compactLayout ? 105 : 168
                    Layout.preferredHeight: Theme.controlHeightProminent
                    available: converter.busy
                    text: qsTr("取消全部")
                    icon.source: Theme.icon("checkbox-blank-fill")
                    onClicked: converter.cancelAll()
                    contentItem: RowLayout {
                        spacing: 10
                        ThemedIcon {
                            objectName: "cancelAllButtonStopIcon"
                            source: parent.parent.icon.source
                            tint: Theme.iconPrimary
                            sourceSize.width: Theme.iconSizeMd
                            sourceSize.height: Theme.iconSizeMd
                        }
                        Text { text: parent.parent.text; color: Theme.primaryText; font.pixelSize: Theme.fontSizeBody }
                    }
                }
            }
        }
    }

    ThemedComboBox {
        objectName: "converterOutputFormatBox"
        visible: false
        model: converter.outputCapabilities
        textRole: "label"
        valueRole: "key"
    }
    ThemedButton { objectName: "convertSelectedButton"; visible: false; onClicked: page.requestPlan() }
    ThemedButton { objectName: "retryFailedButton"; visible: false; onClicked: converter.retryFailed() }

    DropArea {
        objectName: "formatDropArea"
        anchors.fill: parent
        z: -1
        onDropped: function(drop) { converter.addUrls(drop.urls) }
    }

    FormatPreflightDialog {
        id: preflightDialog
        converter: page.converter
    }

    FormatErrorDialog {
        id: errorDialog
        converter: page.converter
    }

    Connections {
        target: converter
        function onErrorOccurred(message) {
            errorDialog.summary = message
            errorDialog.detail = message
            errorDialog.open()
        }
    }

    Connections {
        target: SettingsController
        function onDefaultOutputDirectoryChanged() {
            if (page.outputDirectory !== SettingsController.defaultOutputDirectory)
                page.outputDirectory = SettingsController.defaultOutputDirectory
        }
        function onParallelJobsChanged() {
            if (converter.parallelJobs !== SettingsController.parallelJobs)
                converter.parallelJobs = SettingsController.parallelJobs
        }
    }

    Connections {
        target: converter
        function onParallelJobsChanged() {
            if (SettingsController.parallelJobs !== converter.parallelJobs)
                SettingsController.parallelJobs = converter.parallelJobs
        }
    }
}

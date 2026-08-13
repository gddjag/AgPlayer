import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: page
    objectName: "formatConvertPage"
    color: "#071018"
    focus: true

    property var converter: FormatConverter
    property string outputDirectory: ""

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
        const plan = converter.buildPreflight({
            outputFormat: settingsPanel.outputFormat,
            bitRate: settingsPanel.bitRate,
            sampleRate: settingsPanel.sampleRate,
            channels: settingsPanel.channels,
            outputDir: outputDirectory,
            keepMetadata: settingsPanel.keepMetadata,
            volumeNormalize: settingsPanel.volumeNormalize,
            extractAudio: settingsPanel.extractAudio,
            keepCover: settingsPanel.keepCover,
            preserveDirectories: settingsPanel.preserveDirectories,
            sampleFormat: settingsPanel.sampleFormat,
            channelLayout: settingsPanel.channelLayout,
            audioStreamIndex: -1
        })
        if (plan.ready)
            preflightDialog.open()
        else if (converter.checkedCount <= 0) {
            errorDialog.summary = qsTr("请至少选择一个转换任务")
            errorDialog.detail = qsTr("任务列表中没有已勾选的文件。")
            errorDialog.open()
        }
    }

    Component {
        id: fileDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFiles
            nameFilters: [qsTr("音频与视频文件 (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.mp4 *.mkv *.avi *.mov *.webm)")]
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
        spacing: 8

        Rectangle {
            id: toolbar
            objectName: "formatToolbar"
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: "#0b1721"
            border.color: "#203340"
            radius: 6

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12

                Repeater {
                    model: [
                        { text: qsTr("添加文件"), icon: "add-line", action: "file" },
                        { text: qsTr("添加文件夹"), icon: "folder-open-line", action: "folder" },
                        { text: qsTr("从播放列表添加"), icon: "music-2-line", action: "playlist" },
                        { text: qsTr("移除选中"), icon: "delete-bin-line", action: "remove" },
                        { text: qsTr("清空列表"), icon: "delete-bin-line", action: "clear" }
                    ]
                    Button {
                        Layout.preferredWidth: modelData.action === "playlist" ? 158
                                               : modelData.action === "file" ? 130
                                               : modelData.action === "folder" ? 142
                                               : 128
                        Layout.preferredHeight: 40
                        enabled: !converter.busy
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
                        background: Rectangle {
                            color: parent.hovered ? "#172a37" : "#0c1821"
                            border.color: "#263b49"
                            radius: 6
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            ThemedIcon {
                                source: parent.parent.icon.source
                                tint: "#d7e0e6"
                                sourceSize.width: 18
                                sourceSize.height: 18
                                Layout.preferredWidth: 18
                                Layout.preferredHeight: 18
                            }
                            Text {
                                text: parent.parent.text
                                color: "#d7e0e6"
                                font.pixelSize: 14
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                TextField {
                    id: searchField
                    objectName: "formatSearchField"
                    Layout.preferredWidth: 360
                    Layout.preferredHeight: 40
                    placeholderText: qsTr("搜索文件名、格式或标签...")
                    color: "#d7e0e6"
                    onTextChanged: converter.filteredTaskModel.query = text
                    background: Rectangle {
                        color: "#09141c"
                        border.color: searchField.activeFocus ? "#1688ff" : "#263b49"
                        radius: 6
                    }
                    leftPadding: 16
                }
                ToolButton {
                    objectName: "formatFilterButton"
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    icon.source: Theme.icon("equalizer-line")
                    onClicked: filterMenu.open()
                    background: Rectangle {
                        color: parent.hovered ? "#172a37" : "#09141c"
                        border.color: "#263b49"
                        radius: 6
                    }
                }
            }
        }

        Menu {
            id: filterMenu
            Repeater {
                model: ["All", "Converting", "Done", "Error", "Cancelled"]
                MenuItem {
                    text: modelData === "All" ? qsTr("全部")
                          : modelData === "Converting" ? qsTr("转换中")
                          : modelData === "Done" ? qsTr("已完成")
                          : modelData === "Error" ? qsTr("失败") : qsTr("已取消")
                    onTriggered: converter.filteredTaskModel.statusFilter = modelData
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            FormatTaskTable {
                id: taskTable
                objectName: "formatTaskPanel"
                Layout.fillWidth: true
                Layout.fillHeight: true
                converter: page.converter
            }

            FormatSettingsPanel {
                id: settingsPanel
                objectName: "formatSettingsPanel"
                Layout.preferredWidth: 445
                Layout.minimumWidth: 420
                Layout.maximumWidth: 455
                Layout.fillHeight: true
                converter: page.converter
                outputDirectory: page.outputDirectory
                onChooseOutputDirectory: outputDialogComponent.createObject(page).open()
            }
        }

        Rectangle {
            id: bottomBar
            objectName: "formatBottomBar"
            Layout.fillWidth: true
            Layout.preferredHeight: 114
            color: "#0b1721"
            border.color: "#203340"
            radius: 6

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                ColumnLayout {
                    Layout.preferredWidth: 430
                    spacing: 8
                    RowLayout {
                        Text { text: qsTr("总进度"); color: "#d7e0e6"; font.pixelSize: 14 }
                        ProgressBar {
                            id: totalProgress
                            objectName: "formatTotalProgress"
                            Layout.preferredWidth: 320
                            from: 0; to: 1; value: converter.progress
                            background: Rectangle { implicitHeight: 10; color: "#20303b"; radius: 5 }
                            contentItem: Item {
                                implicitHeight: 10
                                Rectangle {
                                    width: totalProgress.visualPosition * parent.width
                                    height: parent.height
                                    radius: 5
                                    color: "#1688ff"
                                }
                            }
                        }
                        Text { text: Math.round(totalProgress.value * 100) + "%"; color: "#d7e0e6" }
                    }
                    Text {
                        text: qsTr("%1 个任务 / 预计剩余 %2").arg(converter.fileCount)
                              .arg(converter.etaText)
                        color: "#91a0aa"
                        font.pixelSize: 13
                    }
                }

                Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; Layout.topMargin: 22; Layout.bottomMargin: 22; color: "#263b49" }

                ComboBox {
                    id: parallelBox
                    objectName: "converterParallelJobsBox"
                    Layout.preferredWidth: 160
                    Layout.preferredHeight: 46
                    model: [1, 2, 4]
                    currentIndex: 2
                    displayText: qsTr("并发  %1").arg(currentValue)
                    onActivated: converter.parallelJobs = currentValue
                }

                Button {
                    Layout.preferredWidth: 310
                    Layout.preferredHeight: 46
                    text: page.outputDirectory.length > 0
                          ? qsTr("输出目录  %1").arg(page.outputDirectory)
                          : qsTr("选择输出目录")
                    icon.source: Theme.icon("folder-open-line")
                    onClicked: outputDialogComponent.createObject(page).open()
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    objectName: "formatSummaryCard"
                    Layout.preferredWidth: 230
                    Layout.preferredHeight: 46
                    color: "#09141c"
                    border.color: "#263b49"
                    radius: 6
                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 18
                        Text { text: qsTr("✓ 已完成 %1").arg(converter.completedCount); color: "#19c37d" }
                        Text { text: qsTr("! 失败 %1").arg(converter.failedCount); color: "#ff4d4f" }
                    }
                }

                Button {
                    id: convertAllButton
                    objectName: "convertAllButton"
                    Layout.preferredWidth: 174
                    Layout.preferredHeight: 68
                    enabled: converter.checkedCount > 0 && !converter.busy
                    text: qsTr("▶  开始处理")
                    onClicked: page.requestPlan()
                    background: Rectangle { color: parent.enabled ? "#087cf0" : "#23313b"; radius: 6 }
                    contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 17 }
                }

                Button {
                    objectName: "cancelAllButton"
                    Layout.preferredWidth: 168
                    Layout.preferredHeight: 68
                    enabled: converter.busy
                    text: qsTr("■  取消全部")
                    onClicked: converter.cancelAll()
                }
            }
        }
    }

    ComboBox {
        objectName: "converterOutputFormatBox"
        visible: false
        model: converter.outputCapabilities
        textRole: "label"
        valueRole: "key"
    }
    Button { objectName: "convertSelectedButton"; visible: false; onClicked: page.requestPlan() }
    Button { objectName: "retryFailedButton"; visible: false; onClicked: converter.retryFailed(settingsPanel.outputFormat, settingsPanel.bitRate, settingsPanel.sampleRate, settingsPanel.channels, page.outputDirectory, settingsPanel.keepMetadata, settingsPanel.volumeNormalize, settingsPanel.extractAudio) }

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
}

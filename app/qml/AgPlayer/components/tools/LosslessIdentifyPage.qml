import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: page
    objectName: "losslessIdentifyPage"
    color: Theme.losslessWorkspaceSurface
    clip: true
    focus: true

    property var controller: LosslessAnalysisController
    readonly property bool compactLayout: width < 1080 || height < 620
    readonly property real widePanelBudget: Math.max(0, content.width
                                                     - Theme.spacingXs * 2
                                                     - Theme.spacingSm * 2 - 2)
    property int compactView: 0

    component ToolbarButton: ThemedButton {
        id: toolbarButton
        property url iconSource

        background: Rectangle {
            radius: Theme.radiusSm
            color: !toolbarButton.enabled ? Theme.losslessPanelSurface
                   : toolbarButton.down ? Theme.surfacePressed
                   : toolbarButton.hovered ? Theme.surfaceHover
                                           : Theme.losslessPanelSurface
            border.color: toolbarButton.activeFocus
                          ? Theme.focus : Theme.opaqueBorder
            border.width: toolbarButton.activeFocus ? 2 : 1
        }

        contentItem: RowLayout {
            spacing: Theme.spacingSm
            ThemedIcon {
                source: toolbarButton.iconSource
                tint: !toolbarButton.enabled ? Theme.textDisabled
                      : toolbarButton.primary ? Theme.accentText
                                              : Theme.iconPrimary
                sourceSize.width: 20
                sourceSize.height: 20
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
            }
            Text {
                Layout.fillWidth: true
                text: toolbarButton.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                color: !toolbarButton.enabled ? Theme.textDisabled
                       : toolbarButton.primary ? Theme.accentText
                                               : Theme.textPrimary
                font.family: Theme.fontPrimary
                font.pixelSize: toolbarButton.labelPixelSize
                font.weight: toolbarButton.primary ? Font.Medium : Font.Normal
            }
        }
    }

    signal addPlaylistRequested()
    signal locateRequested(string path)

    function openFileDialog() {
        const dialog = fileDialogComponent.createObject(page)
        if (dialog)
            dialog.open()
    }

    function openFolderDialog() {
        const dialog = folderDialogComponent.createObject(page)
        if (dialog)
            dialog.open()
    }

    function openReportDialog() {
        const dialog = reportDialogComponent.createObject(page)
        if (dialog)
            dialog.open()
    }

    Component.onDestruction: {
        if (controller && controller.running)
            controller.cancel()
    }

    Component {
        id: fileDialogComponent
        FileDialog {
            title: qsTr("添加待鉴别文件")
            fileMode: FileDialog.OpenFiles
            nameFilters: [
                qsTr("音频与媒体文件 (*.wav *.rf64 *.bwf *.aif *.aiff *.flac *.alac *.ape *.wv *.mp3 *.aac *.m4a *.ogg *.opus *.dsf *.dff *.dsd *.dst *.iso *.mp4 *.mkv *.mov *.webm)"),
                qsTr("所有文件 (*)")
            ]
            onAccepted: {
                page.controller.loadFiles(selectedFiles)
                destroy()
            }
            onRejected: destroy()
        }
    }

    Component {
        id: folderDialogComponent
        FolderDialog {
            title: qsTr("添加待鉴别文件夹")
            onAccepted: {
                page.controller.addFolder(selectedFolder)
                destroy()
            }
            onRejected: destroy()
        }
    }

    Component {
        id: reportDialogComponent
        FileDialog {
            title: qsTr("导出鉴别报告")
            fileMode: FileDialog.SaveFile
            defaultSuffix: "json"
            nameFilters: [qsTr("JSON 报告 (*.json)"), qsTr("CSV 表格 (*.csv)")]
            onAccepted: {
                const path = String(selectedFile).toLowerCase()
                page.controller.exportReport(selectedFile,
                                             path.endsWith(".csv") ? "csv" : "json")
                destroy()
            }
            onRejected: destroy()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingXs
        spacing: Theme.spacingSm

        Text {
            objectName: "losslessExperimentalNotice"
            Layout.fillWidth: true
            text: qsTr("实验性，仅供参考；鉴别结果不代表专业认证。")
            color: Theme.textSecondary
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.losslessFontSizeBody
            wrapMode: Text.Wrap
        }

        Rectangle {
            id: toolbar
            objectName: "losslessToolbar"
            Layout.fillWidth: true
            Layout.preferredHeight: page.compactLayout
                                    ? 52 : Theme.losslessToolbarHeight
            color: Theme.losslessPanelSurface
            border.color: Theme.opaqueBorder
            border.width: 1
            radius: Theme.radiusSm

            Flickable {
                anchors.fill: parent
                contentWidth: toolbarActions.implicitWidth + Theme.spacingLg * 2
                contentHeight: height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Row {
                    id: toolbarActions
                    x: Theme.spacingMd
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingLg

                    ToolbarButton {
                        objectName: "losslessAddFilesButton"
                        width: 137
                        text: qsTr("添加文件")
                        iconSource: Theme.icon("add-line")
                        height: Theme.losslessControlHeight
                        labelPixelSize: Theme.losslessFontSizeBody
                        onClicked: page.openFileDialog()
                    }
                    ToolbarButton {
                        objectName: "losslessAddFolderButton"
                        width: 153
                        text: qsTr("添加文件夹")
                        iconSource: Theme.icon("folder-open-line")
                        height: Theme.losslessControlHeight
                        labelPixelSize: Theme.losslessFontSizeBody
                        onClicked: page.openFolderDialog()
                    }
                    ToolbarButton {
                        objectName: "losslessAddPlaylistButton"
                        width: 183
                        text: qsTr("从播放列表添加")
                        iconSource: Theme.icon("music-2-line")
                        height: Theme.losslessControlHeight
                        labelPixelSize: Theme.losslessFontSizeBody
                        onClicked: page.addPlaylistRequested()
                    }

                    Item {
                        width: 22
                        height: Theme.controlHeight
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 1
                            height: parent.height
                            color: Theme.opaqueDivider
                        }
                    }

                    ToolbarButton {
                        objectName: "losslessToolbarStartButton"
                        // Compact windows keep these actions in the bottom bar,
                        // leaving both evidence/conclusion tabs visible here.
                        visible: !page.compactLayout
                        width: 134
                        text: qsTr("开始分析")
                        iconSource: Theme.icon("play-fill")
                        height: Theme.losslessControlHeight
                        labelPixelSize: Theme.losslessFontSizeBody
                        enabled: page.controller
                                 && page.controller.selectedCount > 0
                                 && !page.controller.running
                                 && !page.controller.stopping
                        onClicked: page.controller.start()
                    }
                    ToolbarButton {
                        objectName: "losslessToolbarStopButton"
                        visible: !page.compactLayout
                        width: 100
                        text: page.controller && page.controller.stopping
                              ? qsTr("正在停止") : qsTr("停止")
                        iconSource: Theme.icon("stop-fill")
                        height: Theme.losslessControlHeight
                        labelPixelSize: Theme.losslessFontSizeBody
                        enabled: page.controller && page.controller.running
                                 && !page.controller.stopping
                        onClicked: page.controller.cancel()
                    }

                    Row {
                        id: compactViewSwitch
                        objectName: "losslessCompactViewSwitch"
                        visible: page.compactLayout
                        spacing: Theme.spacingXs
                        ThemedButton {
                            objectName: "losslessCompactEvidenceButton"
                            compact: true
                            primary: page.compactView === 0
                            text: qsTr("证据")
                            onClicked: page.compactView = 0
                        }
                        ThemedButton {
                            objectName: "losslessCompactConclusionButton"
                            compact: true
                            primary: page.compactView === 1
                            text: qsTr("结论")
                            onClicked: page.compactView = 1
                        }
                    }
                }
            }
        }

        Item {
            id: content
            objectName: "losslessContent"
            Layout.fillWidth: true
            Layout.fillHeight: true

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingXs
                anchors.rightMargin: Theme.spacingXs
                spacing: Theme.spacingSm

                LosslessTaskPanel {
                    id: taskPanel
                    controller: page.controller
                    compact: page.compactLayout
                    Layout.fillHeight: true
                    Layout.preferredWidth: page.compactLayout
                                           ? Math.max(300, content.width * 0.38)
                                           : page.widePanelBudget
                                             * 622 / 1638
                    Layout.minimumWidth: page.compactLayout ? 300 : 360
                    Layout.maximumWidth: page.compactLayout
                                         ? Math.max(300, content.width * 0.42)
                                         : 10000
                }

                LosslessEvidencePanel {
                    id: evidencePanel
                    result: page.controller ? page.controller.selectedResult : ({})
                    visible: !page.compactLayout || page.compactView === 0
                    Layout.fillHeight: true
                    Layout.fillWidth: page.compactLayout
                    Layout.preferredWidth: page.compactLayout
                                           ? Math.max(420, content.width - taskPanel.width
                                                      - Theme.spacingSm)
                                           : page.widePanelBudget
                                             * 600 / 1638
                    Layout.minimumWidth: page.compactLayout ? 420 : 350
                    onSpectrogramRequested: {
                        if (page.controller)
                            page.controller.requestSpectrogram()
                    }
                }

                LosslessConclusionPanel {
                    id: conclusionPanel
                    result: page.controller ? page.controller.selectedResult : ({})
                    visible: !page.compactLayout || page.compactView === 1
                    Layout.fillHeight: true
                    Layout.fillWidth: page.compactLayout
                    Layout.preferredWidth: page.compactLayout
                                           ? Math.max(420, content.width - taskPanel.width
                                                      - Theme.spacingSm)
                                           : page.widePanelBudget
                                             * 416 / 1638
                    Layout.minimumWidth: page.compactLayout ? 420 : 300
                    onExportRequested: page.openReportDialog()
                    onLocateRequested: function(path) {
                        page.locateRequested(path)
                    }
                }
            }
        }

        Rectangle {
            id: bottomBar
            objectName: "losslessBottomBar"
            Layout.fillWidth: true
            Layout.preferredHeight: page.compactLayout
                                    ? 70 : Theme.losslessBottomBarHeight
            color: Theme.losslessPanelSurface
            border.color: Theme.opaqueBorder
            border.width: 1
            radius: Theme.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: page.compactLayout ? Theme.spacingSm
                                                        : Theme.spacingXl
                anchors.rightMargin: page.compactLayout ? Theme.spacingSm
                                                         : Theme.spacingLg
                anchors.topMargin: page.compactLayout ? Theme.spacingSm
                                                       : Theme.spacingLg
                anchors.bottomMargin: page.compactLayout ? Theme.spacingSm
                                                          : Theme.spacingLg
                spacing: Theme.spacingLg

                RowLayout {
                    Layout.fillWidth: true
                    Layout.maximumWidth: page.compactLayout ? 250 : 480
                    spacing: Theme.spacingMd
                    Text {
                        id: totalProgressLabel
                        Layout.preferredWidth: page.compactLayout
                                               ? implicitWidth : 130
                        text: qsTr("总进度  %1 / %2")
                              .arg(page.controller
                                   ? page.controller.completedCount : 0)
                              .arg(page.controller
                                   ? page.controller.totalCount : 0)
                        color: Theme.textPrimary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.losslessFontSizeMeta
                    }
                    LosslessProgressBar {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 8
                        value: page.controller ? page.controller.progress : 0
                    }
                    Text {
                        Layout.preferredWidth: 42
                        horizontalAlignment: Text.AlignRight
                        text: Math.round(Math.max(0, Math.min(1,
                              page.controller
                              ? Number(page.controller.progress) : 0)) * 100) + "%"
                        color: Theme.textSecondary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.losslessFontSizeMeta
                    }
                }

                Rectangle {
                    visible: !page.compactLayout
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    Layout.topMargin: Theme.spacingSm
                    Layout.bottomMargin: Theme.spacingSm
                    color: Theme.opaqueDivider
                }

                Text {
                    visible: !page.compactLayout
                    Layout.fillWidth: true
                    Layout.minimumWidth: 180
                    text: page.controller && page.controller.statusText
                          ? page.controller.statusText
                          : qsTr("等待开始分析")
                    elide: Text.ElideMiddle
                    color: page.controller && page.controller.error
                           ? Theme.error : Theme.textSecondary
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.losslessFontSizeMeta
                }

                Rectangle {
                    Layout.preferredWidth: page.compactLayout ? 64 : 132
                    Layout.preferredHeight: Theme.controlHeight
                    Layout.alignment: Qt.AlignVCenter
                    color: Theme.surface
                    border.color: Theme.opaqueBorder
                    border.width: 1
                    radius: Theme.radiusSm

                    Text {
                        width: 56
                        height: parent.height
                        visible: !page.compactLayout
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("并发")
                        color: Theme.textSecondary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.losslessFontSizeMeta
                    }
                    Rectangle {
                        x: 56
                        width: 1
                        anchors.verticalCenter: parent.verticalCenter
                        height: parent.height - Theme.spacingSm * 2
                        visible: !page.compactLayout
                        color: Theme.opaqueDivider
                    }
                    ThemedComboBox {
                        id: concurrencyBox
                        objectName: "losslessConcurrencyBox"
                        x: page.compactLayout ? 0 : 57
                        width: parent.width - x
                        height: parent.height
                        model: ["1", "2", "3", "4"]
                        currentIndex: Math.max(0, Math.min(3,
                                      (page.controller
                                       ? page.controller.concurrency : 2) - 1))
                        enabled: page.controller && !page.controller.running
                        onActivated: page.controller.concurrency = currentIndex + 1
                        background: Rectangle {
                            color: !concurrencyBox.enabled
                                   ? Theme.disabled
                                   : concurrencyBox.down ? Theme.surfacePressed
                                   : concurrencyBox.hovered ? Theme.surfaceHover
                                                            : Theme.losslessWorkspaceSurface
                            border.width: 0
                            radius: Theme.radiusSm
                        }
                    }
                }

                ThemedButton {
                    id: startButton
                    objectName: "losslessStartButton"
                    Layout.preferredWidth: page.compactLayout ? 132 : 224
                    Layout.preferredHeight: page.compactLayout
                                            ? Theme.losslessControlHeight
                                            : Theme.losslessProminentControlHeight
                    prominent: true
                    primary: true
                    labelPixelSize: Theme.losslessFontSizeBody
                    text: page.controller && page.controller.running
                          ? qsTr("正在批量分析") : qsTr("▶  开始批量分析")
                    enabled: page.controller
                             && page.controller.selectedCount > 0
                             && !page.controller.running
                             && !page.controller.stopping
                    onClicked: page.controller.start()
                }
                ThemedButton {
                    id: cancelButton
                    objectName: "losslessCancelButton"
                    Layout.preferredWidth: page.compactLayout ? 104 : 214
                    Layout.preferredHeight: page.compactLayout
                                            ? Theme.losslessControlHeight
                                            : Theme.losslessProminentControlHeight
                    prominent: true
                    labelPixelSize: Theme.losslessFontSizeBody
                    text: page.controller && page.controller.stopping
                          ? qsTr("正在停止") : qsTr("取消全部")
                    enabled: page.controller && page.controller.running
                             && !page.controller.stopping
                    onClicked: page.controller.cancel()
                }
            }
        }
    }
}

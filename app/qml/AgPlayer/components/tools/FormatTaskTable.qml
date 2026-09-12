import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    property var converter
    property var settingsPanel
    color: Theme.panel
    border.color: Theme.border
    radius: 6
    clip: true

    function removeTask(taskId) {
        for (let index = 0; index < converter.files.length; ++index) {
            if (converter.files[index].taskId === taskId) {
                converter.removeFile(index)
                return
            }
        }
    }

    readonly property var columnWidths: [62, 260, 120, 100, 115, 130, 120, 120, 190]

    component ReferenceCheckBox: CheckBox {
        id: control
        indicator: Rectangle {
            objectName: control.objectName.length > 0 ? control.objectName + "Indicator" : ""
            x: control.leftPadding
            y: (control.height - height) / 2
            width: 20
            height: 20
            radius: 3
                color: control.checked ? Theme.accent
                                       : (control.enabled ? Theme.elevated
                                                          : Theme.background)
            border.color: control.checked ? Theme.accent : Theme.border
            ThemedIcon {
                objectName: control.objectName.length > 0 ? control.objectName + "Mark" : ""
                anchors.centerIn: parent
                visible: control.checked
                source: Theme.icon("check-line")
                tint: Theme.accentText
                sourceSize.width: 14
                sourceSize.height: 14
            }
        }
        contentItem: Text {
            text: control.text
            leftPadding: control.indicator.width + 8
            verticalAlignment: Text.AlignVCenter
            color: control.enabled ? Theme.primaryText : Theme.secondaryText
            font.pixelSize: Theme.fontSizeBody
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            objectName: "formatStatusFilters"
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.listRowHeight
            Layout.leftMargin: 18
            spacing: 12
            Text { text: qsTr("任务列表"); color: Theme.primaryText; font.pixelSize: Theme.fontSizeBody; font.weight: Font.DemiBold }
            Repeater {
                model: [
                    { key: "All", text: qsTr("全部"), count: converter.fileCount },
                    { key: "Converting", text: qsTr("转换中"), count: converter.convertingCount },
                    { key: "Done", text: qsTr("已完成"), count: converter.doneCount },
                    { key: "Error", text: qsTr("失败"), count: converter.failedCount },
                    { key: "Cancelled", text: qsTr("已取消"), count: converter.cancelledCount }
                ]
                Button {
                    Layout.preferredHeight: Theme.controlHeightCompact
                    text: modelData.text + "  " + modelData.count
                    checkable: true
                    checked: converter.filteredTaskModel.statusFilter === modelData.key
                             || (modelData.key === "All" && converter.filteredTaskModel.statusFilter === "")
                    onClicked: converter.filteredTaskModel.statusFilter = modelData.key
                    background: Rectangle {
                    color: parent.checked ? Theme.activeSelection : Theme.elevated
                        border.color: parent.checked ? Theme.accent : Theme.border
                        radius: 5
                    }
                    contentItem: Text {
                        text: parent.text
                        color: parent.checked ? Theme.activeSelectionText : Theme.primaryText
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: Theme.fontSizeBody
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.tableHeaderHeight
            color: Theme.elevated
            Row {
                anchors.fill: parent
                Repeater {
                    model: ["", qsTr("文件名"), qsTr("原格式"), qsTr("时长"), qsTr("采样率"), qsTr("码率"), qsTr("输出格式"), qsTr("状态"), qsTr("进度")]
                    Item {
                        objectName: "formatHeaderCell-" + index
                        width: root.columnWidths[index]
                        height: Theme.tableHeaderHeight
                        ReferenceCheckBox {
                            visible: index === 0
                            anchors.centerIn: parent
                            checked: converter.filteredTaskModel.visibleCount > 0
                                     && converter.filteredTaskModel.visibleCheckedCount
                                        === converter.filteredTaskModel.visibleCount
                            onClicked: converter.setAllVisibleChecked(checked)
                            objectName: index === 0 ? "formatSelectAllCheck" : ""
                        }
                        Text {
                            visible: index !== 0
                            anchors.fill: parent
                            anchors.leftMargin: index === 1 ? 30 : 10
                            verticalAlignment: Text.AlignVCenter
                            text: modelData
                            color: Theme.secondaryText
                            font.pixelSize: Theme.fontSizeBody
                        }
                    }
                }
            }
        }

        TableView {
            id: table
            objectName: "formatTaskTableView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: converter.filteredTaskModel
            clip: true
            columnWidthProvider: function(column) { return root.columnWidths[column] }
            rowHeightProvider: function() { return Theme.listRowHeight }
            delegate: Rectangle {
                objectName: column === 1 && row === 0 ? "formatTaskFirstFilenameCell" : ""
                implicitWidth: root.columnWidths[column]
                implicitHeight: Theme.listRowHeight
                color: row % 2 ? Theme.panel : Theme.background
                border.color: Theme.border
                border.width: 1

                ReferenceCheckBox {
                    objectName: column === 0 && row === 0 ? "formatTaskFirstCheck" : ""
                    visible: column === 0
                    anchors.centerIn: parent
                    checked: model.checked
                    onClicked: converter.setTaskChecked(model.taskId, checked)
                }
                Rectangle {
                    objectName: column === 1 && row === 0 ? "formatTaskFirstFileIconBadge" : ""
                    visible: column === 1
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28
                    height: 28
                    radius: 6
                    color: model.sourceFormat === "WAV" ? "#e9964a" /* theme-color-allow: file format badge */
                         : model.sourceFormat === "MP3" ? "#35b8e7" /* theme-color-allow: file format badge */
                         : model.sourceFormat === "FLAC" ? "#9c6ade" /* theme-color-allow: file format badge */
                                                        : "#35bc85" /* theme-color-allow: file format badge */
                    ThemedIcon {
                        objectName: column === 1 && row === 0 ? "formatTaskFirstFileIcon" : ""
                        anchors.centerIn: parent
                        source: Theme.icon("file-music-fill")
                        tint: "#ffffff" // theme-color-allow: file format badge text
                        sourceSize.width: 18
                        sourceSize.height: 18
                    }
                }
                Text {
                    id: statusText
                    objectName: column === 7 && row === 0
                                ? "formatTaskFirstStatusText" : ""
                    visible: column > 0 && column < 8
                    anchors.fill: parent
                    anchors.leftMargin: column === 1 ? 48 : 10
                    anchors.rightMargin: 6
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    text: column === 1 ? model.fileName
                        : column === 2 ? model.sourceFormat
                        : column === 3 ? converter.formatDuration(model.durationMs)
                        : column === 4 ? (model.sampleRate > 0 ? (model.sampleRate / 1000) + " kHz" : "--")
                        : column === 5 ? (model.bitRate > 0 ? Math.round(model.bitRate / 1000) + " kbps" : "--")
                        : column === 6 ? model.outputFormat
                        : model.status === "Converting" ? qsTr("转换中")
                        : model.status === "Done" ? qsTr("已完成")
                        : model.status === "Error"
                          ? (model.errorDetail.length > 0
                             ? model.errorDetail
                             : qsTr("失败"))
                        : model.status === "Cancelled" ? qsTr("已取消") : qsTr("就绪")
                    color: column === 7 && model.status === "Done" ? Theme.success
                         : column === 7 && model.status === "Error" ? Theme.error
                         : column === 7 && model.status === "Converting" ? Theme.accent
                         : Theme.primaryText
                    font.pixelSize: Theme.fontSizeBody
                    ToolTip.visible: column === 7
                                         && model.status === "Error"
                                         && model.errorDetail.length > 0
                                         && errorHover.hovered
                    ToolTip.text: model.errorDetail
                    ToolTip.delay: 300
                    ToolTip.timeout: 10000
                    HoverHandler {
                        id: errorHover
                        enabled: column === 7 && model.status === "Error"
                    }
                }
                ProgressBar {
                    id: rowProgress
                    visible: column === 8
                    anchors.left: parent.left
                    anchors.right: percent.left
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    value: model.progress
                    background: Rectangle { implicitHeight: 8; color: Theme.hoverSurface; radius: 4 }
                    contentItem: Item {
                        implicitHeight: 8
                        Rectangle {
                            width: rowProgress.visualPosition * parent.width
                            height: parent.height
                            radius: 4
                            color: model.status === "Done" ? Theme.success : Theme.accent
                        }
                    }
                }
                Text {
                    id: percent
                    objectName: column === 8 && row === 0 ? "formatTaskFirstProgressPercent" : ""
                    visible: column === 8
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    text: Math.round(model.progress * 100) + "%"
                    color: Theme.primaryText
                    horizontalAlignment: Text.AlignRight
                    font.pixelSize: Theme.fontSizeCaption
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: column === 1
                    acceptedButtons: Qt.RightButton
                    onClicked: {
                        taskContextMenu.taskId = model.taskId
                        taskContextMenu.status = model.status
                        taskContextMenu.errorDetail = model.errorDetail
                        taskContextMenu.popup()
                    }
                }
            }
        }

        Text {
            Layout.preferredHeight: 34
            Layout.leftMargin: 18
            verticalAlignment: Text.AlignVCenter
            text: qsTr("共 %1 个任务 / 已选择 %2 个").arg(converter.fileCount).arg(converter.checkedCount)
            color: Theme.secondaryText
            font.pixelSize: Theme.fontSizeCaption
        }
    }

    Menu {
        id: taskContextMenu
        objectName: "formatTaskContextMenu"
        property string taskId: ""
        property string status: ""
        property string errorDetail: ""

        MenuItem {
            objectName: "formatTaskRemoveMenuItem"
            text: qsTr("移除任务")
            onTriggered: root.removeTask(taskContextMenu.taskId)
        }
        MenuItem {
            text: qsTr("重试失败任务")
            enabled: taskContextMenu.status === "Error" && !converter.busy
            onTriggered: converter.retryTask(taskContextMenu.taskId)
        }
        MenuItem {
            text: qsTr("取消任务")
            enabled: taskContextMenu.status === "Converting"
            onTriggered: converter.cancelTask(taskContextMenu.taskId)
        }
        MenuItem {
            text: qsTr("复制错误详情")
            enabled: taskContextMenu.errorDetail.length > 0
            onTriggered: converter.copyText(taskContextMenu.errorDetail)
        }
    }
}

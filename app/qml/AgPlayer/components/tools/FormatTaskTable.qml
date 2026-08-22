import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    property var converter
    property var settingsPanel
    color: "#101a21"
    border.color: "#203340"
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
            color: control.checked ? "#1688ff" : (control.enabled ? "#0c1821" : "#10181e")
            border.color: control.checked ? "#1688ff" : (control.enabled ? "#3a4a53" : "#26343c")
            ThemedIcon {
                objectName: control.objectName.length > 0 ? control.objectName + "Mark" : ""
                anchors.centerIn: parent
                visible: control.checked
                source: Theme.icon("check-line")
                tint: "#ffffff"
                sourceSize.width: 14
                sourceSize.height: 14
            }
        }
        contentItem: Text {
            text: control.text
            leftPadding: control.indicator.width + 8
            verticalAlignment: Text.AlignVCenter
            color: control.enabled ? "#d7e0e6" : "#667782"
            font.pixelSize: 13
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            objectName: "formatStatusFilters"
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.leftMargin: 18
            spacing: 12
            Text { text: qsTr("任务列表"); color: "#eef3f6"; font.pixelSize: 15; font.weight: Font.DemiBold }
            Repeater {
                model: [
                    { key: "All", text: qsTr("全部"), count: converter.fileCount },
                    { key: "Converting", text: qsTr("转换中"), count: converter.convertingCount },
                    { key: "Done", text: qsTr("已完成"), count: converter.completedCount },
                    { key: "Error", text: qsTr("失败"), count: converter.failedCount },
                    { key: "Cancelled", text: qsTr("已取消"), count: converter.cancelledCount }
                ]
                Button {
                    Layout.preferredHeight: 30
                    text: modelData.text + "  " + modelData.count
                    checkable: true
                    checked: converter.filteredTaskModel.statusFilter === modelData.key
                             || (modelData.key === "All" && converter.filteredTaskModel.statusFilter === "")
                    onClicked: converter.filteredTaskModel.statusFilter = modelData.key
                    background: Rectangle {
                        color: parent.checked ? "#0c63c8" : "#0c1821"
                        border.color: parent.checked ? "#1688ff" : "#203340"
                        radius: 5
                    }
                    contentItem: Text {
                        text: parent.text
                        color: parent.checked ? "#ffffff" : "#c9d2d8"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 13
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: "#162129"
            Row {
                anchors.fill: parent
                Repeater {
                    model: ["", qsTr("文件名"), qsTr("原格式"), qsTr("时长"), qsTr("采样率"), qsTr("码率"), qsTr("输出格式"), qsTr("状态"), qsTr("进度")]
                    Item {
                        objectName: "formatHeaderCell-" + index
                        width: root.columnWidths[index]
                        height: 40
                        ReferenceCheckBox {
                            visible: index === 0
                            anchors.centerIn: parent
                            checked: converter.checkedCount > 0 && converter.checkedCount === converter.fileCount
                            onClicked: converter.setAllVisibleChecked(checked)
                            objectName: index === 0 ? "formatSelectAllCheck" : ""
                        }
                        Text {
                            visible: index !== 0
                            anchors.fill: parent
                            anchors.leftMargin: index === 1 ? 30 : 10
                            verticalAlignment: Text.AlignVCenter
                            text: modelData
                            color: "#aeb9c1"
                            font.pixelSize: 13
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
            rowHeightProvider: function() { return 44 }
            delegate: Rectangle {
                objectName: column === 1 && row === 0 ? "formatTaskFirstFilenameCell" : ""
                implicitWidth: root.columnWidths[column]
                implicitHeight: 44
                color: row % 2 ? "#101a21" : "#0f1820"
                border.color: "#172a36"
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
                    color: model.sourceFormat === "WAV" ? "#e9964a"
                         : model.sourceFormat === "MP3" ? "#35b8e7"
                         : model.sourceFormat === "FLAC" ? "#9c6ade" : "#35bc85"
                    ThemedIcon {
                        objectName: column === 1 && row === 0 ? "formatTaskFirstFileIcon" : ""
                        anchors.centerIn: parent
                        source: Theme.icon("file-music-fill")
                        tint: "#ffffff"
                        sourceSize.width: 18
                        sourceSize.height: 18
                    }
                }
                Text {
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
                        : model.status === "Error" ? qsTr("失败")
                        : model.status === "Cancelled" ? qsTr("已取消") : qsTr("就绪")
                    color: column === 7 && model.status === "Done" ? "#19c37d"
                         : column === 7 && model.status === "Error" ? "#ff4d4f"
                         : column === 7 && model.status === "Converting" ? "#1688ff"
                         : "#c9d2d8"
                    font.pixelSize: 13
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
                    background: Rectangle { implicitHeight: 8; color: "#20303b"; radius: 4 }
                    contentItem: Item {
                        implicitHeight: 8
                        Rectangle {
                            width: rowProgress.visualPosition * parent.width
                            height: parent.height
                            radius: 4
                            color: model.status === "Done" ? "#19c37d" : "#1688ff"
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
                    color: "#c9d2d8"
                    horizontalAlignment: Text.AlignRight
                    font.pixelSize: 12
                }
                TapHandler {
                    enabled: column === 1
                    acceptedButtons: Qt.RightButton
                    onTapped: {
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
            color: "#91a0aa"
            font.pixelSize: 12
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
            onTriggered: converter.retryFailed(settingsPanel.outputFormat,
                                               settingsPanel.bitRate,
                                               settingsPanel.sampleRate,
                                               settingsPanel.channels,
                                               settingsPanel.outputDirectory,
                                               settingsPanel.keepMetadata,
                                               settingsPanel.volumeNormalize,
                                               settingsPanel.extractAudio)
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

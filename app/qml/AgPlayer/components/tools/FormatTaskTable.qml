import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    property var converter
    color: Theme.panel
    border.color: Theme.border
    radius: 6
    clip: true

    readonly property var columnWidths: [54, 230, 120, 100, 115, 130, 120, 120, 210]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            objectName: "formatStatusFilters"
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.leftMargin: 18
            spacing: 12
            Text { text: qsTr("任务列表"); color: Theme.primaryText; font.pixelSize: 15; font.weight: Font.DemiBold }
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
                        color: parent.checked ? Theme.selectedTrackSelection : Theme.elevated
                        border.color: parent.checked ? Theme.accent : Theme.border
                        radius: 5
                    }
                    contentItem: Text {
                        text: parent.text
                        color: Theme.primaryText
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
            color: Theme.elevated
            Row {
                anchors.fill: parent
                Repeater {
                    model: ["", qsTr("文件名"), qsTr("原格式"), qsTr("时长"), qsTr("采样率"), qsTr("码率"), qsTr("输出格式"), qsTr("状态"), qsTr("进度")]
                    Item {
                        width: root.columnWidths[index]
                        height: 40
                        CheckBox {
                            visible: index === 0
                            anchors.centerIn: parent
                            checked: converter.checkedCount > 0 && converter.checkedCount === converter.fileCount
                            onClicked: converter.setAllVisibleChecked(checked)
                            objectName: index === 0 ? "formatSelectAllCheck" : ""
                        }
                        Text {
                            visible: index !== 0
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            verticalAlignment: Text.AlignVCenter
                            text: modelData
                            color: Theme.secondaryText
                            font.pixelSize: 13
                        }
                    }
                }
            }
        }

        TableView {
            id: table
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: converter.filteredTaskModel
            clip: true
            columnWidthProvider: function(column) { return root.columnWidths[column] }
            rowHeightProvider: function() { return 44 }
            delegate: Rectangle {
                implicitWidth: root.columnWidths[column]
                implicitHeight: 44
                color: row % 2 ? Theme.panel : Theme.elevated
                border.color: Theme.border
                border.width: 1

                CheckBox {
                    visible: column === 0
                    anchors.centerIn: parent
                    checked: model.checked
                    onClicked: converter.setTaskChecked(model.taskId, checked)
                }
                Text {
                    visible: column > 0 && column < 8
                    anchors.fill: parent
                    anchors.leftMargin: 10
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
                    color: column === 7 && model.status === "Done" ? Theme.waveformGreen
                         : column === 7 && model.status === "Error" ? Theme.waveformRed
                         : column === 7 && model.status === "Converting" ? Theme.accent
                         : Theme.primaryText
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
                    background: Rectangle { implicitHeight: 8; color: Theme.border; radius: 4 }
                    contentItem: Item {
                        implicitHeight: 8
                        Rectangle {
                            width: rowProgress.visualPosition * parent.width
                            height: parent.height
                            radius: 4
                            color: model.status === "Done" ? Theme.waveformGreen : Theme.accent
                        }
                Text {
                    id: percent
                    visible: column === 8
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    text: Math.round(model.progress * 100) + "%"
                    color: Theme.primaryText
                    horizontalAlignment: Text.AlignRight
                    font.pixelSize: 12
                }
            }
        }

        Text {
            Layout.preferredHeight: 34
            Layout.leftMargin: 18
            verticalAlignment: Text.AlignVCenter
            text: qsTr("共 %1 个任务 / 已选择 %2 个").arg(converter.fileCount).arg(converter.checkedCount)
            color: Theme.secondaryText
            font.pixelSize: 12
        }
    }
}

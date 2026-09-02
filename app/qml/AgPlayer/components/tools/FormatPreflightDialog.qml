import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Dialog {
    id: root
    objectName: "formatPreflightDialog"
    property var converter
    modal: true
    anchors.centerIn: parent
    width: 520
    title: qsTr("转换计划确认")
    standardButtons: Dialog.Ok | Dialog.Cancel
    onAccepted: converter.confirmPendingPlan()
    onRejected: converter.rejectPendingPlan()
    contentItem: ColumnLayout {
        spacing: 12
        Text { text: qsTr("即将处理 %1 个任务").arg(converter.pendingPlan.taskCount || 0); color: Theme.primaryText; font.pixelSize: Theme.fontSizeSection }
        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.secondaryText; text: converter.pendingPlan.requiresConfirmation ? qsTr("检测到参数调整或文件冲突。确认后将使用解析后的安全计划执行。") : qsTr("已完成输入探测与参数校验。确认开始处理。") }
    }
}

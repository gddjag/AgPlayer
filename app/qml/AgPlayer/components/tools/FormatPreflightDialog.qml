import QtQuick
import QtQuick.Controls
import AgPlayer

ThemedDialog {
    id: root
    objectName: "formatPreflightDialog"
    property var converter
    modal: true
    anchors.centerIn: parent
    width: Math.min(520, Math.max(320,
        (parent ? parent.width : 520) - 2 * Theme.spacing2Xl))
    height: Math.min(implicitHeight,
                     Math.max(Theme.controlHeightProminent,
                              (parent ? parent.height : implicitHeight)
                              - 2 * Theme.spacingXl))
    title: qsTr("转换计划确认")
    contentWidth: Math.max(0, width - leftPadding - rightPadding)
    contentHeight: preflightContent.implicitHeight
    onAccepted: converter.confirmPendingPlan()
    onRejected: converter.rejectPendingPlan()

    contentItem: Column {
        id: preflightContent
        width: root.contentWidth
        spacing: Theme.spacingMd

        Text {
            width: parent.width
            text: qsTr("即将处理 %1 个任务").arg(converter.pendingPlan.taskCount || 0)
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeSection
            font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeBody
            text: converter.pendingPlan.requiresConfirmation
                  ? qsTr("检测到参数调整或文件冲突。确认后将使用解析后的安全计划执行。")
                  : qsTr("已完成输入探测与参数校验。确认开始处理。")
        }
    }

    footer: DialogButtonBox {
        id: preflightFooter
        implicitHeight: Theme.controlHeight + topPadding + bottomPadding
        spacing: Theme.spacingSm
        leftPadding: Theme.spacingLg
        rightPadding: Theme.spacingLg
        topPadding: Theme.spacingSm
        bottomPadding: Theme.spacingLg
        alignment: Qt.AlignRight

        background: Item {}

        ThemedButton {
            objectName: "formatPreflightCancelButton"
            text: qsTr("取消")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }

        ThemedButton {
            objectName: "formatPreflightConfirmButton"
            text: qsTr("确认转换")
            primary: true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }

    }
}

import QtQuick
import QtQuick.Controls
import AgPlayer

ThemedDialog {
    id: root
    objectName: "formatErrorDialog"
    property var converter
    property string summary: ""
    property string detail: ""
    modal: true
    anchors.centerIn: parent
    width: Math.min(560, Math.max(320,
        (parent ? parent.width : 560) - 2 * Theme.spacing2Xl))
    height: Math.min(implicitHeight,
                     Math.max(Theme.controlHeightProminent,
                              (parent ? parent.height : implicitHeight)
                              - 2 * Theme.spacingXl))
    title: qsTr("转换失败详情")
    contentWidth: Math.max(0, width - leftPadding - rightPadding)
    contentHeight: errorContent.implicitHeight

    contentItem: Column {
        id: errorContent
        width: root.contentWidth
        spacing: Theme.spacingMd

        Text {
            width: parent.width
            text: root.summary
            color: Theme.error
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeBodyStrong
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }

        ScrollView {
            width: parent.width
            height: 180
            clip: true
            contentWidth: availableWidth

            background: Rectangle {
                color: Theme.contentSurface
                border.color: Theme.opaqueBorder
                radius: Theme.radiusSm
            }

            TextArea {
                id: detailTextArea
                objectName: "formatErrorDetailTextArea"
                width: parent.width
                readOnly: true
                selectByMouse: true
                text: root.detail
                wrapMode: TextEdit.Wrap
                color: Theme.textPrimary
                selectionColor: Theme.accent
                selectedTextColor: Theme.accentText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeBody
                background: Item {}
            }
        }

    }

    footer: DialogButtonBox {
        id: errorFooter
        implicitHeight: Theme.controlHeight + topPadding + bottomPadding
        spacing: Theme.spacingSm
        leftPadding: Theme.spacingLg
        rightPadding: Theme.spacingLg
        topPadding: Theme.spacingSm
        bottomPadding: Theme.spacingLg
        alignment: Qt.AlignRight

        background: Item {}

        ThemedButton {
            objectName: "formatErrorCopyButton"
            text: qsTr("复制详细信息")
            enabled: root.detail.length > 0
            onClicked: root.converter.copyText(root.detail)
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
        }

        ThemedButton {
            objectName: "formatErrorCloseButton"
            text: qsTr("关闭")
            primary: true
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }

    }
}

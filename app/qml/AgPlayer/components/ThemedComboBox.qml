import QtQuick
import QtQuick.Controls
import AgPlayer

ComboBox {
    id: control
    property real textLeftPadding: 10
    property real textRightPadding: 28
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitHeight: Theme.controlHeight
    palette.window: Theme.elevated
    palette.base: Theme.elevated
    palette.text: Theme.primaryText
    palette.button: Theme.elevated
    palette.buttonText: Theme.primaryText
    palette.highlight: Theme.activeSelection
    palette.highlightedText: Theme.activeSelectionText

    contentItem: Text {
        leftPadding: control.textLeftPadding
        rightPadding: control.textRightPadding
        text: control.displayText
        color: control.enabled ? Theme.primaryText : Theme.secondaryText
        opacity: control.enabled ? 1.0 : 0.55
        font.family: Theme.fontPrimary
        font.pixelSize: Theme.fontSizeBody
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: ThemedIcon {
        source: Theme.icon("arrow-down-s-line")
        tint: Theme.iconSecondary
        width: 16
        height: 16
        x: control.width - width - 8
        y: Math.round((control.height - height) / 2)
    }

    background: Rectangle {
        color: !control.enabled ? Theme.disabled
               : control.down ? Theme.surfacePressed
               : control.hovered ? Theme.surfaceHover : Theme.surfaceElevated
        border.color: control.activeFocus ? Theme.focus : Theme.border
        border.width: 1
        radius: Theme.radiusSm
    }

    delegate: ItemDelegate {
        required property int index
        width: ListView.view ? ListView.view.width : control.width
        implicitHeight: Theme.controlHeight
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: control.textAt(index)
            color: parent.highlighted ? Theme.activeSelectionText
                                      : Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeBody
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: parent.highlighted ? Theme.activeSelection : "transparent"
            radius: Theme.radiusSm
        }
    }

    popup: Popup {
        y: control.height + 3
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 260)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm
        }
    }
}

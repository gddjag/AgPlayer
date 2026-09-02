import QtQuick
import QtQuick.Controls
import AgPlayer

MenuItem {
    id: control
    implicitWidth: 230
    implicitHeight: Theme.controlHeight
    leftPadding: 12
    rightPadding: control.subMenu ? 30 : 12

    contentItem: Text {
        text: control.text
        color: control.highlighted || control.hovered
               ? Theme.activeSelectionText
               : control.enabled ? Theme.primaryText : Theme.secondaryText
        opacity: control.enabled ? 1.0 : 0.55
        font.family: Theme.fontPrimary
        font.pixelSize: Theme.fontSizeBody
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    arrow: ThemedIcon {
        visible: control.subMenu !== null
        source: Theme.icon("arrow-right-s-line")
        tint: control.highlighted || control.hovered
              ? Theme.activeSelectionText : Theme.iconSecondary
        width: 16
        height: 16
        x: control.width - width - 9
        y: Math.round((control.height - height) / 2)
    }

    background: Rectangle {
        color: control.highlighted || control.hovered
               ? Theme.activeSelection : "transparent"
        radius: Theme.radiusSm
    }
}

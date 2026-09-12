import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import AgPlayer

T.Button {
    id: control

    property url iconSource
    property int iconSize: Theme.iconSizeMd
    property bool selected: false
    property bool underlineSelection: false
    property int labelPixelSize: Theme.fontSizeBody

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitHeight: Theme.navigationRowHeight
    leftPadding: Theme.spacingMd
    rightPadding: Theme.spacingMd
    Accessible.name: text
    Accessible.role: Accessible.PageTab

    contentItem: RowLayout {
        spacing: Theme.spacingSm

        ThemedIcon {
            source: control.iconSource
            tint: control.selected ? Theme.accent : Theme.iconSecondary
            sourceSize.width: control.iconSize
            sourceSize.height: control.iconSize
            Layout.preferredWidth: control.iconSize
            Layout.preferredHeight: control.iconSize
        }

        Text {
            text: control.text
            color: control.enabled ? Theme.textPrimary : Theme.textDisabled
            font.family: Theme.fontPrimary
            font.pixelSize: control.labelPixelSize
            font.weight: control.selected ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: !control.enabled ? Theme.disabled
               : control.down ? Theme.surfacePressed
               : control.selected && !control.underlineSelection
                   ? Theme.selectedSurface
               : control.hovered ? Theme.surfaceHover : "transparent"
        border.color: control.activeFocus ? Theme.focus
                      : control.selected && !control.underlineSelection
                          ? Theme.accent : "transparent"
        border.width: control.activeFocus ? 2
                      : control.selected && !control.underlineSelection ? 1 : 0

        Rectangle {
            visible: control.selected
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 2
            color: Theme.accent
        }
    }
}

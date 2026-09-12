import QtQuick
import QtQuick.Templates as T
import AgPlayer

T.ScrollBar {
    id: control

    hoverEnabled: true
    implicitWidth: Theme.minimumInteractionExtent
    implicitHeight: Theme.minimumInteractionExtent
    policy: T.ScrollBar.AsNeeded

    contentItem: Rectangle {
        implicitWidth: 4
        implicitHeight: 4
        radius: 2
        color: control.pressed ? Theme.accentPressed
               : control.hovered ? Theme.accentHover : Theme.textTertiary
        opacity: control.active || control.hovered ? 1 : 0.55
    }
}

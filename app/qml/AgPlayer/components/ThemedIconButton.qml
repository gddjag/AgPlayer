import QtQuick
import QtQuick.Templates as T
import AgPlayer

T.Button {
    id: control

    property url iconSource
    property string accessibleName: ""
    property int iconSize: 18
    property bool primary: false
    property bool danger: false
    property bool dangerOnHover: false

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitWidth: Theme.controlHeightCompact
    implicitHeight: Theme.controlHeightCompact
    Accessible.name: accessibleName
    Accessible.role: Accessible.Button

    contentItem: ThemedIcon {
        source: control.iconSource
        tint: !control.enabled ? Theme.textDisabled
              : control.primary || control.danger
                || (control.dangerOnHover && control.hovered)
                ? Theme.accentText : Theme.iconPrimary
        width: control.iconSize
        height: control.iconSize
        anchors.centerIn: parent
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: !control.enabled ? Theme.disabled
               : control.danger || (control.dangerOnHover && control.hovered)
                 ? (control.down ? Qt.darker(Theme.danger, 1.15)
                                 : control.danger ? Theme.danger
                                                  : Qt.lighter(Theme.danger, 1.08))
               : control.primary ? (control.down ? Theme.accentPressed
                                                  : control.hovered ? Theme.accentHover
                                                                    : Theme.accent)
               : control.down ? Theme.surfacePressed
               : control.hovered ? Theme.surfaceHover : "transparent"
        border.color: control.activeFocus ? Theme.focus : "transparent"
        border.width: control.activeFocus ? 2 : 0

        Behavior on color {
            ColorAnimation { duration: 140 }
        }
    }
}

import QtQuick
import QtQuick.Templates as T
import AgPlayer

T.Switch {
    id: control

    implicitWidth: Math.max(44, contentItem.implicitWidth)
    implicitHeight: 32

    indicator: Rectangle {
        implicitWidth: 38
        implicitHeight: 20
        x: 0
        y: (control.height - height) / 2
        radius: height / 2
        color: control.checked ? Theme.cyan : Theme.border

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            x: control.checked ? parent.width - width - 2 : 2
            width: 16
            height: 16
            radius: width / 2
            color: "#FFFFFF"

            Behavior on x {
                NumberAnimation { duration: 120 }
            }
        }
    }

    contentItem: Text {
        text: control.text
        visible: text.length > 0
        color: Theme.primaryText
        font.family: Theme.fontPrimary
        font.pixelSize: 14
        leftPadding: visible ? control.indicator.width + control.spacing : 0
        verticalAlignment: Text.AlignVCenter
    }
}

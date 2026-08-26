import QtQuick
import QtQuick.Templates as T
import AgPlayer

T.Switch {
    id: control

    property color checkedColor: Theme.cyan

    implicitWidth: Math.max(40, contentItem.implicitWidth)
    implicitHeight: 22

    indicator: Rectangle {
        implicitWidth: 40
        implicitHeight: 22
        x: 0
        y: (control.height - height) / 2
        radius: height / 2
        color: control.checked ? control.checkedColor : Theme.border

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            x: control.checked ? parent.width - width - 2 : 2
            width: 18
            height: 18
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
        font.pixelSize: 13
        leftPadding: visible ? control.indicator.width + control.spacing : 0
        verticalAlignment: Text.AlignVCenter
    }
}

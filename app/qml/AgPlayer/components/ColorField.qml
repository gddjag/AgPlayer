import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Control {
    id: root
    property color colorValue: "#000000"
    property string targetProperty: ""
    signal colorEdited(string value)
    onColorEdited: function(value) {
        if (targetProperty.length > 0)
            SettingsController[targetProperty] = value
    }

    implicitWidth: 106
    implicitHeight: 32
    padding: 1

    function normalized(value) {
        var text = String(value || "").trim().toUpperCase()
        return /^#[0-9A-F]{6}$/.test(text) ? text : ""
    }

    contentItem: RowLayout {
        spacing: 6

        Rectangle {
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            radius: 4
            color: root.colorValue
            border.color: Theme.border
        }

        Text {
            id: field
            objectName: "colorFieldHex"
            Layout.fillWidth: true
            text: root.normalized(root.colorValue)
            color: Theme.primaryText
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    background: Rectangle {
        color: Theme.background
        border.color: Theme.border
        radius: Theme.radiusSm
    }

    TapHandler {
        onTapped: picker.openForColor(root.colorValue)
    }

    AgColorPicker {
        id: picker
        objectName: "colorFieldPicker"
        onColorAccepted: color => root.colorEdited(root.normalized(color))
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

AbstractButton {
    id: root
    objectName: "colorFieldButton"
    property color colorValue: "#000000"
    property string editingLabel: ""
    property string targetProperty: ""
    signal colorEdited(string value)
    onColorEdited: function(value) {
        if (targetProperty.length > 0)
            SettingsController[targetProperty] = value
    }

    implicitWidth: 106
    implicitHeight: 32
    padding: 1
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: (root.editingLabel.length > 0
                      ? root.editingLabel + " " : "")
                     + qsTr("Color %1").arg(root.normalized(root.colorValue))

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
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Theme.focus : Theme.border
        radius: Theme.radiusSm
    }

    onClicked: picker.openForColor(root.colorValue, root.editingLabel)
    Keys.onSpacePressed: function(event) {
        root.clicked()
        event.accepted = true
    }
    Keys.onReturnPressed: function(event) {
        root.clicked()
        event.accepted = true
    }
    Keys.onEnterPressed: function(event) {
        root.clicked()
        event.accepted = true
    }

    AgColorPicker {
        id: picker
        objectName: "colorFieldPicker"
        onApplied: color => root.colorEdited(root.normalized(color))
        onClosed: root.forceActiveFocus()
    }
}

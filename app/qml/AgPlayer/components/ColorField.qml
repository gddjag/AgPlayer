import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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
            TapHandler { onTapped: picker.open() }
        }

        TextField {
            id: field
            Layout.fillWidth: true
            text: root.colorValue.toString().toUpperCase()
            selectByMouse: true
            color: acceptableInput ? Theme.primaryText : Theme.favoriteRed
            validator: RegularExpressionValidator {
                regularExpression: /^#[0-9A-Fa-f]{6}$/
            }
            onEditingFinished: {
                var value = root.normalized(text)
                if (value.length > 0) root.colorEdited(value)
                else text = root.colorValue.toString().toUpperCase()
            }
            background: null
        }
    }

    background: Rectangle {
        color: Theme.background
        border.color: field.acceptableInput ? Theme.border : Theme.favoriteRed
        radius: Theme.radiusSm
    }

    ColorDialog {
        id: picker
        title: qsTr("选择颜色")
        selectedColor: root.colorValue
        onAccepted: root.colorEdited(selectedColor.toString().toUpperCase())
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import AgPlayer

Rectangle {
    id: root
    color: Theme.panel
    border.color: Theme.border
    radius: Theme.radiusSm
    implicitHeight: advancedExpanded ? 210 : 112

    property var basicParameters: []
    property var advancedParameters: []
    property var values: ({})
    property bool advancedExpanded: false
    readonly property bool advancedAvailable: advancedParameters.length > 0
    signal valueEdited(string key, var value)

    function currentValue(field) {
        return values && values[field.key] !== undefined
                ? values[field.key] : field["default"]
    }

    function fieldVisible(field) {
        if (!field.visibleWhen) return true
        const dependencyKey = field.visibleWhen.key
        let actual = values ? values[dependencyKey] : undefined
        if (actual === undefined) {
            const controls = basicParameters.concat(advancedParameters)
            for (let index = 0; index < controls.length; ++index) {
                if (controls[index].key === dependencyKey) {
                    actual = controls[index]["default"]
                    break
                }
            }
        }
        return actual === field.visibleWhen.equals
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label {
                text: qsTr("4  声音参数")
                color: Theme.primaryText
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 72
                contentWidth: basicRow.implicitWidth
                contentHeight: basicRow.implicitHeight
                ScrollBar.vertical.policy: ScrollBar.AlwaysOff

                RowLayout {
                    id: basicRow
                    spacing: 12
                    Repeater {
                        model: root.basicParameters
                        delegate: parameterDelegate
                    }
                }
            }
            Button {
                objectName: "voiceCloneAdvancedButton"
                visible: root.advancedAvailable
                text: root.advancedExpanded ? qsTr("收起高级设置") : qsTr("高级设置")
                onClicked: root.advancedExpanded = !root.advancedExpanded
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.advancedAvailable && root.advancedExpanded
            contentWidth: advancedRow.implicitWidth
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff
            RowLayout {
                id: advancedRow
                spacing: 12
                Repeater {
                    model: root.advancedParameters
                    delegate: parameterDelegate
                }
            }
        }
    }

    Component {
        id: parameterDelegate
        ColumnLayout {
            id: fieldRoot
            required property var modelData
            property var field: modelData
            visible: root.fieldVisible(field)
            Layout.preferredWidth: Math.max(150, Math.min(250, field.type === "string" || field.type === "file" ? 220 : 170))
            objectName: (field.group === "advanced"
                         ? "voiceCloneAdvancedParameter_" + field.key
                         : "voiceCloneParameter_" + field.type)
            spacing: 2

            Label {
                Layout.fillWidth: true
                text: fieldRoot.field.label || fieldRoot.field.key
                color: Theme.primaryText
                elide: Text.ElideRight
                font.pixelSize: 12
            }
            Loader {
                id: controlLoader
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                property var fieldData: fieldRoot.field
                property var fieldValue: root.currentValue(fieldRoot.field)
                sourceComponent: fieldData.type === "bool" ? boolControl
                               : fieldData.type === "enum" ? enumControl
                               : fieldData.type === "int" ? integerControl
                               : fieldData.type === "double" ? doubleControl
                               : fieldData.type === "file" ? fileControl
                               : stringControl
                onLoaded: {
                    item.fieldData = fieldData
                    item.fieldValue = fieldValue
                }
            }
            Label {
                Layout.fillWidth: true
                text: fieldRoot.field.description || ""
                visible: text !== ""
                color: Theme.secondaryText
                elide: Text.ElideRight
                font.pixelSize: 10
            }
        }
    }

    Component {
        id: boolControl
        Switch {
            property var fieldData: ({})
            property var fieldValue
            checked: !!fieldValue
            text: checked ? qsTr("开启") : qsTr("关闭")
            onToggled: root.valueEdited(fieldData.key, checked)
        }
    }
    Component {
        id: enumControl
        ComboBox {
            property var fieldData: ({})
            property var fieldValue
            model: fieldData.options || []
            currentIndex: Math.max(0, model.indexOf(fieldValue))
            onActivated: root.valueEdited(fieldData.key, currentText)
        }
    }
    Component {
        id: integerControl
        SpinBox {
            property var fieldData: ({})
            property var fieldValue
            editable: true
            from: fieldData.minimum === undefined ? -2147483647 : fieldData.minimum
            to: fieldData.maximum === undefined ? 2147483647 : fieldData.maximum
            stepSize: fieldData.step || 1
            value: Number(fieldValue || 0)
            onValueModified: root.valueEdited(fieldData.key, value)
        }
    }
    Component {
        id: doubleControl
        SpinBox {
            property var fieldData: ({})
            property var fieldValue
            readonly property int factor: 1000
            editable: true
            from: Math.round((fieldData.minimum === undefined ? -1000000 : fieldData.minimum) * factor)
            to: Math.round((fieldData.maximum === undefined ? 1000000 : fieldData.maximum) * factor)
            stepSize: Math.max(1, Math.round((fieldData.step || 0.01) * factor))
            value: Math.round(Number(fieldValue || 0) * factor)
            textFromValue: function(value) { return (value / factor).toFixed(3).replace(/0+$/, "").replace(/\.$/, "") }
            valueFromText: function(text) { return Math.round(Number(text) * factor) }
            onValueModified: root.valueEdited(fieldData.key, value / factor)
        }
    }
    Component {
        id: stringControl
        TextField {
            property var fieldData: ({})
            property var fieldValue
            text: String(fieldValue || "")
            maximumLength: fieldData.maximumLength || 32767
            onEditingFinished: root.valueEdited(fieldData.key, text)
        }
    }
    Component {
        id: fileControl
        RowLayout {
            property var fieldData: ({})
            property var fieldValue
            TextField {
                id: filePathField
                Layout.fillWidth: true
                text: String(parent.fieldValue || "")
                placeholderText: qsTr("本地文件路径")
                onEditingFinished: root.valueEdited(parent.fieldData.key, text)
            }
            ToolButton {
                icon.source: Theme.icon("folder-open-line")
                icon.color: Theme.iconPrimary
                Accessible.name: qsTr("浏览参数文件")
                onClicked: fieldFileDialog.open()
            }
            FileDialog {
                id: fieldFileDialog
                title: qsTr("选择参数文件")
                onAccepted: {
                    const path = decodeURIComponent(
                        selectedFile.toString().replace(/^file:\/\/\//, ""))
                    filePathField.text = path
                    root.valueEdited(parent.fieldData.key, path)
                }
            }
        }
    }
}

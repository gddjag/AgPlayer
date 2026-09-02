import QtQuick
import QtQuick.Templates as T
import AgPlayer

T.TextField {
    id: control

    readonly property bool showError: error && errorMessage.length > 0

    implicitHeight: Theme.controlHeight
                    + (showError ? Theme.spacingXs
                                   + errorLabel.implicitHeight : 0)
    leftPadding: Theme.spacingMd
    rightPadding: Theme.spacingMd
    topPadding: 0
    bottomPadding: showError ? Theme.spacingXs
                               + errorLabel.implicitHeight : 0
    color: control.enabled ? Theme.textPrimary : Theme.textDisabled
    placeholderTextColor: Theme.textTertiary
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    font.family: Theme.fontPrimary
    font.pixelSize: Theme.fontSizeBody
    verticalAlignment: TextInput.AlignVCenter
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.name: accessibleName.length > 0 ? accessibleName : placeholderText
    Accessible.description: error ? errorMessage : ""
    Accessible.role: Accessible.EditableText

    property string accessibleName: ""
    property bool error: false
    property string errorMessage: ""

    background: Item {
        Rectangle {
            id: inputFrame
            objectName: "themedTextFieldFrame"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: Theme.controlHeight
            radius: Theme.radiusSm
            color: !control.enabled ? Theme.disabled
                   : control.hovered ? Theme.surfaceHover
                                     : Theme.surfaceElevated
            border.color: control.error ? Theme.danger
                          : control.activeFocus ? Theme.focus
                                                : Theme.opaqueBorder
            border.width: control.activeFocus ? 2 : 1

            Behavior on color {
                ColorAnimation { duration: 140 }
            }
        }

        Text {
            id: errorLabel
            objectName: "themedTextFieldErrorLabel"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: inputFrame.bottom
            anchors.topMargin: Theme.spacingXs
            text: control.errorMessage
            color: Theme.danger
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeMeta
            wrapMode: Text.Wrap
            visible: control.showError
        }
    }
}

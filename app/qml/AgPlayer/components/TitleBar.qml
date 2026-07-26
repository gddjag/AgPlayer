import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: titleBar
    color: "transparent"

    property Window window

    signal openSettings()

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingSm
        spacing: Theme.spacingMd

        Item { Layout.fillWidth: true }

        ToolButton {
            objectName: "settingsButton"
            icon.source: Theme.icon("settings-3-fill")
            icon.color: Theme.secondaryText
            icon.width: 18
            icon.height: 18
            Accessible.name: qsTr("Open settings")
            focusPolicy: Qt.StrongFocus
            onClicked: titleBar.openSettings()
            ToolTip.text: qsTr("Settings")
            ToolTip.visible: hovered

            background: Rectangle {
                color: !parent.enabled ? "transparent"
                      : parent.pressed ? Theme.cyan
                      : parent.visualFocus ? Theme.border
                      : parent.hovered ? Theme.border
                      : "transparent"
                border.color: parent.visualFocus ? Theme.cyan : "transparent"
                border.width: parent.visualFocus ? 2 : 0
                radius: Theme.radiusSm
            }
        }

        ToolButton {
            objectName: "minimizeButton"
            icon.source: Theme.icon("subtract-line")
            icon.color: Theme.secondaryText
            icon.width: 18
            icon.height: 18
            Accessible.name: "Minimize"
            focusPolicy: Qt.StrongFocus
            onClicked: window.showMinimized()
            ToolTip.text: "Minimize"
            ToolTip.visible: hovered

            background: Rectangle {
                color: !parent.enabled ? "transparent"
                      : parent.pressed ? Theme.cyan
                      : parent.visualFocus ? Theme.border
                      : parent.hovered ? Theme.border
                      : "transparent"
                border.color: parent.visualFocus ? Theme.cyan : "transparent"
                border.width: parent.visualFocus ? 2 : 0
                radius: Theme.radiusSm
            }
        }

        ToolButton {
            objectName: "maximizeButton"
            icon.source: window.visibility === Window.Maximized
                       ? Theme.icon("fullscreen-exit-fill")
                       : Theme.icon("fullscreen-fill")
            icon.color: Theme.secondaryText
            icon.width: 18
            icon.height: 18
            Accessible.name: window.visibility === Window.Maximized ? "Restore" : "Maximize"
            focusPolicy: Qt.StrongFocus
            onClicked: {
                if (window.visibility === Window.Maximized)
                    window.showNormal()
                else
                    window.showMaximized()
            }
            ToolTip.text: window.visibility === Window.Maximized ? "Restore" : "Maximize"
            ToolTip.visible: hovered

            background: Rectangle {
                color: !parent.enabled ? "transparent"
                      : parent.pressed ? Theme.cyan
                      : parent.visualFocus ? Theme.border
                      : parent.hovered ? Theme.border
                      : "transparent"
                border.color: parent.visualFocus ? Theme.cyan : "transparent"
                border.width: parent.visualFocus ? 2 : 0
                radius: Theme.radiusSm
            }
        }

        ToolButton {
            objectName: "closeButton"
            icon.source: Theme.icon("close-fill")
            icon.color: Theme.secondaryText
            icon.width: 18
            icon.height: 18
            Accessible.name: "Close"
            focusPolicy: Qt.StrongFocus
            onClicked: WindowController.requestClose()
            ToolTip.text: "Close"
            ToolTip.visible: hovered

            background: Rectangle {
                color: !parent.enabled ? "transparent"
                      : parent.pressed ? Theme.favoriteRed
                      : parent.visualFocus ? Theme.border
                      : parent.hovered ? Theme.favoriteRed
                      : "transparent"
                border.color: parent.visualFocus ? Theme.cyan : "transparent"
                border.width: parent.visualFocus ? 2 : 0
                radius: Theme.radiusSm
            }
        }
    }

    // Drag the window from any empty area of the title bar. `z: -1` keeps
    // this MouseArea below the RowLayout so ToolButtons receive presses first;
    // no propagateComposedEvents / mouse.accepted forwarding is needed.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        z: -1
        onPressed: function(mouse) {
            if (window) window.startSystemMove()
        }
        onDoubleClicked: function(mouse) {
            if (window) {
                if (window.visibility === Window.Maximized)
                    window.showNormal()
                else
                    window.showMaximized()
            }
        }
    }
}

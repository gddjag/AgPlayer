import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: titleBar
    color: Theme.panel

    property Window window

    signal openSettings()

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingSm
        spacing: Theme.spacingMd

        Image {
            source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
            sourceSize.width: 28
            sourceSize.height: 28
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            fillMode: Image.PreserveAspectFit
        }

        Text {
            text: "AgPlayer"
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 15
            font.weight: Font.Medium
        }

        Item { Layout.fillWidth: true }

        ToolButton {
            objectName: "audioToolsButton"
            icon.source: Theme.icon("equalizer-fill")
            icon.color: Theme.secondaryText
            icon.width: 18
            icon.height: 18
            Accessible.name: "Open audio tools"
            focusPolicy: Qt.StrongFocus
            onClicked: WindowController.showAudioTools()
            ToolTip.text: qsTr("Audio tools")
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
            objectName: "listWindowButton"
            icon.source: Theme.icon("playlist-2-fill")
            icon.color: WindowController.listWindowVisible ? Theme.cyan : Theme.secondaryText
            icon.width: 18
            icon.height: 18
            Accessible.name: WindowController.listWindowVisible
                             ? qsTr("Hide playlist window")
                             : qsTr("Show playlist window")
            focusPolicy: Qt.StrongFocus
            onClicked: WindowController.toggleListWindow()
            ToolTip.text: Accessible.name
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
            objectName: "miniPlayerButton"
            icon.source: Theme.icon("restore-line")
            icon.color: Theme.secondaryText
            icon.width: 18
            icon.height: 18
            Accessible.name: "Switch to mini player"
            focusPolicy: Qt.StrongFocus
            onClicked: WindowController.showMini()
            ToolTip.text: "Mini player"
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

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import AgPlayer

Window {
    id: settingsWindow
    objectName: "settingsWindow"
    visible: false
    width: 860
    height: Math.min(900, Math.max(640, Screen.desktopAvailableHeight - 40))
    minimumWidth: 840
    minimumHeight: 640
    flags: Qt.FramelessWindowHint
    color: "transparent"
    title: qsTr("AgPlayer · 设置")
    palette.window: Theme.background
    palette.windowText: Theme.primaryText
    palette.base: Theme.surfaceElevated
    palette.alternateBase: Theme.surface
    palette.text: Theme.primaryText
    palette.button: Theme.surfaceElevated
    palette.buttonText: Theme.primaryText
    palette.highlight: Theme.highlight
    palette.highlightedText: Theme.highlightText
    palette.mid: Theme.opaqueBorder

    Component.onCompleted: WindowController.registerSettingsWindow(settingsWindow)

    function openSettings() {
        const wasVisible = visible
        WindowController.presentAuxiliaryWindow(settingsWindow)
        if (wasVisible && !settingsPage.visible)
            settingsPage.open()
    }

    onVisibleChanged: {
        if (visible && !settingsPage.visible)
            settingsPage.open()
    }

    onClosing: function(close) {
        close.accepted = false
        settingsPage.cancelAndClose()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.background
        radius: settingsWindow.visibility === Window.Maximized
                ? 0 : Theme.windowRadius
        border.color: Theme.border
        border.width: 1
    }

    SettingsPage {
        id: settingsPage
        objectName: "settingsPage"
        modal: false
        dim: false
        closePolicy: Popup.CloseOnEscape
        width: parent.width
        height: parent.height
        hostWindow: settingsWindow
    }

    Connections {
        target: settingsPage
        function onClosed() {
            if (!settingsPage.editResolved) {
                settingsPage.editResolved = true
                SettingsController.cancelEdit()
            }
            settingsWindow.hide()
        }
    }

    WindowResizeHandles {
        objectName: "settingsResizeHandles"
        targetWindow: settingsWindow
    }
}

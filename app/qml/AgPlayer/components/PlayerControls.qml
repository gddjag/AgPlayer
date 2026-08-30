import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: "transparent"
    property bool emptyMode: false
    property bool showListWindowButton: true
    readonly property bool compactTransport: width < 760
    signal openEqualizerRequested()

    ToolButton {
        objectName: "listWindowButton"
        anchors.left: parent.left; anchors.leftMargin: 24; anchors.verticalCenter: parent.verticalCenter
        flat: true
        icon.source: Theme.icon("list-unordered")
        icon.color: WindowController.listWindowVisible ? Theme.iconAccent : Theme.iconPrimary
        icon.width: 20; icon.height: 20
        Accessible.name: WindowController.listWindowVisible ? qsTr("Hide playlist window") : qsTr("Show playlist window")
        onClicked: WindowController.toggleListWindow()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered
        background: null; visible: root.showListWindowButton
    }

    TransportControls {
        id: centerControls
        objectName: "centerPlaybackControls"
        anchors.horizontalCenter: parent.horizontalCenter; anchors.verticalCenter: parent.verticalCenter
        compact: root.compactTransport
        spacing: root.emptyMode ? 28 : (compact ? 4 : 16)
        onOpenEqualizerRequested: root.openEqualizerRequested()
    }

    PlayerVolumeControl {
        id: volumeControl
        anchors.left: centerControls.right; anchors.leftMargin: 12; anchors.verticalCenter: centerControls.verticalCenter
        emptyMode: root.emptyMode
    }

    RowLayout {
        objectName: "playerSecondaryActions"
        anchors.right: parent.right; anchors.rightMargin: 24; anchors.verticalCenter: parent.verticalCenter
        spacing: root.compactTransport ? 4 : 14
        ToolButton {
            objectName: "audioToolsButton"
            flat: true
            icon.source: Theme.icon("briefcase-4-line"); icon.color: Theme.iconPrimary
            icon.width: 20; icon.height: 20
            Accessible.name: qsTr("Open audio tools")
            onClicked: WindowController.showAudioTools()
            ToolTip.text: Accessible.name; ToolTip.visible: hovered
            background: null
        }
        ExperienceActions { objectName: "experienceActions"; compact: root.compactTransport; showImmersive: false }
        ToolButton {
            objectName: "themeModeButton"
            visible: !root.compactTransport
            flat: true
            icon.source: Theme.icon("brush-line"); icon.color: Theme.iconPrimary
            icon.width: 20; icon.height: 20
            Accessible.name: qsTr("Switch theme")
            onClicked: SettingsController.themeMode = (SettingsController.themeMode + 1) % 3
            ToolTip.text: Accessible.name; ToolTip.visible: hovered
            background: null
        }
        ExperienceActions { visible: !root.compactTransport; compact: root.compactTransport; showLyrics: false }
        ToolButton {
            objectName: "windowLayoutButton"
            visible: !root.compactTransport
            flat: true
            icon.source: Theme.icon("player-shell-mode"); icon.color: Theme.iconPrimary
            icon.width: 20; icon.height: 20
            Accessible.name: qsTr("Switch to integrated layout")
            onClicked: SettingsController.windowLayoutTheme = "single-window"
            ToolTip.text: Accessible.name; ToolTip.visible: hovered
            background: null
        }
        ToolButton {
            objectName: "miniPlayerButton"
            visible: !root.emptyMode && !root.compactTransport
            flat: true
            icon.source: Theme.icon("picture-in-picture-2-line"); icon.color: Theme.iconPrimary
            icon.width: 20; icon.height: 20
            Accessible.name: qsTr("Switch to mini player")
            onClicked: WindowController.showMini()
            ToolTip.text: Accessible.name; ToolTip.visible: hovered
            background: null
        }
    }
}

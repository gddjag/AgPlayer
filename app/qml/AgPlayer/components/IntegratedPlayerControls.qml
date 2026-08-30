import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

RowLayout {
    id: root
    objectName: "integratedPlayerControls"
    property bool emptyMode: false
    signal openEqualizerRequested()
    anchors.fill: parent
    anchors.leftMargin: 24
    anchors.rightMargin: 24
    spacing: 12

    ToolButton {
        objectName: "listWindowButton"; flat: true
        icon.source: Theme.icon("list-unordered")
        icon.color: WindowController.listWindowVisible ? Theme.iconAccent : Theme.iconPrimary
        icon.width: 20; icon.height: 20
        Accessible.name: WindowController.listWindowVisible ? qsTr("Hide playlist window") : qsTr("Show playlist window")
        onClicked: WindowController.toggleListWindow()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered; background: null
    }
    ToolButton {
        objectName: "audioToolsButton"; flat: true
        icon.source: Theme.icon("briefcase-4-line"); icon.color: Theme.iconPrimary
        icon.width: 20; icon.height: 20; Accessible.name: qsTr("Open audio tools")
        onClicked: WindowController.showAudioTools()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered; background: null
    }
    TransportControls { onOpenEqualizerRequested: root.openEqualizerRequested() }
    ExperienceActions { showImmersive: false; showLyrics: true }
    PlayerVolumeControl { emptyMode: root.emptyMode }
    ToolButton {
        objectName: "themeModeButton"; flat: true
        icon.source: Theme.icon("brush-line"); icon.color: Theme.iconPrimary
        icon.width: 20; icon.height: 20; Accessible.name: qsTr("Switch theme")
        onClicked: SettingsController.themeMode = (SettingsController.themeMode + 1) % 3
        ToolTip.text: Accessible.name; ToolTip.visible: hovered; background: null
    }
    ExperienceActions { showImmersive: true; showLyrics: false }
    ToolButton {
        objectName: "windowLayoutButton"; flat: true
        icon.source: Theme.icon("player-shell-mode"); icon.color: Theme.iconPrimary
        icon.width: 20; icon.height: 20; Accessible.name: qsTr("Switch to classic layout")
        onClicked: SettingsController.windowLayoutTheme = "dual-window"
        ToolTip.text: Accessible.name; ToolTip.visible: hovered; background: null
    }
}

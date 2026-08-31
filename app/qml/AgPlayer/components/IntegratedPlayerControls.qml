import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Item {
    id: root
    objectName: "integratedPlayerControls"
    property bool emptyMode: false
    property real leftReservedWidth: 0
    readonly property bool denseLayout: width < 1440
    readonly property real actionSpacing: denseLayout ? 6 : 10
    signal openEqualizerRequested()
    signal togglePlaylistRequested()
    anchors.fill: parent

    ToolButton {
        id: listWindowButton
        objectName: "listWindowButton"
        anchors.left: parent.left
        anchors.leftMargin: root.leftReservedWidth + 12
        anchors.verticalCenter: parent.verticalCenter
        flat: true
        icon.source: Theme.icon("list-unordered")
        icon.color: WindowController.listWindowVisible
                    ? Theme.iconAccent : Theme.iconPrimary
        icon.width: 20
        icon.height: 20
        Accessible.name: WindowController.listWindowVisible
                         ? qsTr("Hide playlist window")
                         : qsTr("Show playlist window")
        onClicked: root.togglePlaylistRequested()
        ToolTip.text: Accessible.name
        ToolTip.visible: hovered
        background: null
    }

    Item {
        id: centerGroup
        objectName: "integratedCenterControls"
        anchors.verticalCenter: parent.verticalCenter
        width: transportControls.width + 12 + volumeControl.width
        height: Math.max(transportControls.height, volumeControl.height)
        x: {
            var leftLimit = listWindowButton.x + listWindowButton.width + 12
            var centered = (root.width - width) / 2
            var rightLimit = rightActions.x - width - 12
            return Math.max(leftLimit, Math.min(centered, rightLimit))
        }

        TransportControls {
            id: transportControls
            objectName: "integratedTransportControls"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            dense: root.denseLayout
            onOpenEqualizerRequested: root.openEqualizerRequested()
        }

        PlayerVolumeControl {
            id: volumeControl
            anchors.left: transportControls.right
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            emptyMode: root.emptyMode || root.denseLayout
        }
    }

    RowLayout {
        id: rightActions
        objectName: "integratedRightActions"
        anchors.right: parent.right
        anchors.rightMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        spacing: root.actionSpacing

        ToolButton {
            objectName: "audioToolsButton"
            flat: true
            icon.source: Theme.icon("briefcase-4-line")
            icon.color: Theme.iconPrimary
            icon.width: 20
            icon.height: 20
            Accessible.name: qsTr("Open audio tools")
            onClicked: WindowController.showAudioTools()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }
        ExperienceActions {
            objectName: "experienceActions"
            showImmersive: false
            showLyrics: true
        }
        ToolButton {
            objectName: "themeModeButton"
            flat: true
            icon.source: Theme.icon("brush-line")
            icon.color: Theme.iconPrimary
            icon.width: 20
            icon.height: 20
            Accessible.name: qsTr("Switch theme")
            onClicked: SettingsController.themeMode =
                       (SettingsController.themeMode + 1) % 3
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }
        ExperienceActions {
            objectName: "immersiveExperienceActions"
            showImmersive: true
            showLyrics: false
        }
        ToolButton {
            id: windowLayoutButton
            objectName: "windowLayoutButton"
            flat: true
            icon.source: Theme.icon("player-shell-mode")
            icon.color: Theme.iconPrimary
            icon.width: 20
            icon.height: 20
            Accessible.name: qsTr("切换播放器布局")
            onClicked: playerShellMenu.open()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }
    }

    Menu {
        id: playerShellMenu
        objectName: "playerShellMenu"
        x: Math.max(0, windowLayoutButton.mapToItem(root, 0, 0).x
                    + windowLayoutButton.width / 2 - width / 2)
        y: Math.max(0, windowLayoutButton.mapToItem(root, 0, 0).y - height)

        MenuItem {
            objectName: "classicShellMenuItem"
            text: qsTr("经典双窗口")
            checkable: true
            checked: SettingsController.playerShellMode === 0
            onTriggered: SettingsController.playerShellMode = 0
        }
        MenuItem {
            objectName: "integratedShellMenuItem"
            text: qsTr("集成单窗口")
            checkable: true
            checked: SettingsController.playerShellMode === 1
            onTriggered: SettingsController.playerShellMode = 1
        }
        MenuItem {
            objectName: "rollingShellMenuItem"
            text: qsTr("滚动播放模式")
            checkable: true
            checked: SettingsController.playerShellMode === 2
            onTriggered: SettingsController.playerShellMode = 2
        }
    }
}

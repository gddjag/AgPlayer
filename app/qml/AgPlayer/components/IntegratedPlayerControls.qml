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
        width: transportControls.width
        height: Math.max(transportControls.height, volumeControl.height)
        x: root.width / 2 - transportControls.playButtonCenterX

        TransportControls {
            id: transportControls
            objectName: "integratedTransportControls"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            dense: root.denseLayout
            onOpenEqualizerRequested: root.openEqualizerRequested()
        }

        ToolButton {
            id: audioToolsButton
            objectName: "audioToolsButton"
            anchors.right: transportControls.left
            anchors.rightMargin: root.actionSpacing
            anchors.verticalCenter: parent.verticalCenter
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
            id: lyricsActions
            objectName: "experienceActions"
            anchors.left: transportControls.right
            anchors.leftMargin: root.actionSpacing
            anchors.verticalCenter: parent.verticalCenter
            compact: root.denseLayout
            width: implicitWidth
            height: implicitHeight
            showImmersive: false
            showLyrics: true
        }

        PlayerVolumeControl {
            id: volumeControl
            anchors.left: lyricsActions.right
            anchors.leftMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            emptyMode: root.emptyMode || root.denseLayout
        }
    }

    Row {
        id: rightActions
        objectName: "integratedRightActions"
        anchors.right: parent.right
        anchors.rightMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        spacing: root.actionSpacing

        ToolButton {
            id: themeModeButton
            objectName: "themeModeButton"
            flat: true
            icon.source: Theme.icon("brush-line")
            icon.color: Theme.iconPrimary
            icon.width: 20
            icon.height: 20
            Accessible.name: qsTr("Switch theme")
            onClicked: playerShellMenu.open()
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
            objectName: "miniPlayerButton"
            flat: true
            icon.source: Theme.icon("picture-in-picture-2-line")
            icon.color: Theme.iconPrimary
            icon.width: 20
            icon.height: 20
            Accessible.name: qsTr("切换到迷你播放器")
            onClicked: WindowController.showMini()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }
    }

    Menu {
        id: playerShellMenu
        objectName: "playerShellMenu"
        x: Math.max(0, themeModeButton.mapToItem(root, 0, 0).x
                    + themeModeButton.width / 2 - width / 2)
        y: Math.max(0, themeModeButton.mapToItem(root, 0, 0).y - height)

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

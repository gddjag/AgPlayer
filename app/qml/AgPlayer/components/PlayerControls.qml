import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: "transparent"

    property bool emptyMode: false
    property bool showListWindowButton: true
    property int shellMode: SettingsController.playerShellMode
    readonly property bool compactTransport: width < 860
    readonly property bool denseTransport: width < 1200

    signal openEqualizerRequested()
    signal toggleEmbeddedPlaylistRequested()

    ToolButton {
        id: listWindowButton
        objectName: "listWindowButton"
        anchors.left: parent.left
        anchors.leftMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        visible: root.showListWindowButton
        flat: true
        icon.source: Theme.icon("list-unordered")
        icon.color: WindowController.listWindowVisible
                    ? Theme.iconAccent : Theme.iconPrimary
        icon.width: 20
        icon.height: 20
        Accessible.name: qsTr("播放列表")
        onClicked: {
            if (root.shellMode === 1)
                root.toggleEmbeddedPlaylistRequested()
            else
                WindowController.toggleListWindow()
        }
        ToolTip.text: Accessible.name
        ToolTip.visible: hovered
        background: null
    }

    TransportControls {
        id: centerControls
        objectName: "centerPlaybackControls"
        anchors.verticalCenter: parent.verticalCenter
        x: {
            var centered = (root.width - width) / 2
            var leftLimit = listWindowButton.visible
                    ? listWindowButton.x + listWindowButton.width + 12 : 12
            var rightLimit = secondaryActions.x - width
                    - volumeControl.width - 24
            return Math.max(leftLimit, Math.min(centered, rightLimit))
        }
        compact: root.compactTransport
        dense: root.denseTransport
        spacing: root.emptyMode && !root.denseTransport
                 ? 28 : (compact ? 4 : dense ? 8 : 16)
        onOpenEqualizerRequested: root.openEqualizerRequested()
    }

    PlayerVolumeControl {
        id: volumeControl
        anchors.left: centerControls.right
        anchors.leftMargin: 12
        anchors.verticalCenter: centerControls.verticalCenter
        emptyMode: root.emptyMode
    }

    RowLayout {
        id: secondaryActions
        objectName: "playerSecondaryActions"
        anchors.right: parent.right
        anchors.rightMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        spacing: root.denseTransport ? 4 : 14

        ToolButton {
            objectName: "audioToolsButton"
            Layout.preferredWidth: root.denseTransport ? 32 : implicitWidth
            Layout.preferredHeight: root.denseTransport ? 32 : implicitHeight
            flat: true
            icon.source: Theme.icon("briefcase-4-line")
            icon.color: Theme.iconPrimary
            icon.width: 20
            icon.height: 20
            Accessible.name: qsTr("打开音频工具")
            onClicked: WindowController.showAudioTools()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }

        ExperienceActions {
            objectName: "experienceActions"
            compact: root.denseTransport
            showImmersive: false
            showLyrics: true
        }

        ToolButton {
            objectName: "themeModeButton"
            Layout.preferredWidth: root.denseTransport ? 32 : implicitWidth
            Layout.preferredHeight: root.denseTransport ? 32 : implicitHeight
            visible: !root.compactTransport
            flat: true
            icon.source: Theme.icon("brush-line")
            icon.color: Theme.iconPrimary
            icon.width: 20
            icon.height: 20
            Accessible.name: qsTr("切换主题")
            onClicked: SettingsController.themeMode =
                       (SettingsController.themeMode + 1) % 3
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }

        ExperienceActions {
            objectName: "immersiveExperienceActions"
            visible: !root.compactTransport
            compact: root.denseTransport
            showImmersive: true
            showLyrics: false
        }

        ToolButton {
            id: windowLayoutButton
            objectName: "windowLayoutButton"
            Layout.preferredWidth: root.denseTransport ? 32 : implicitWidth
            Layout.preferredHeight: root.denseTransport ? 32 : implicitHeight
            visible: !root.compactTransport
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

        ToolButton {
            objectName: "miniPlayerButton"
            Layout.preferredWidth: root.denseTransport ? 32 : implicitWidth
            Layout.preferredHeight: root.denseTransport ? 32 : implicitHeight
            visible: !root.emptyMode && !root.compactTransport
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
        x: Math.max(0, secondaryActions.x + windowLayoutButton.x
                    + windowLayoutButton.width / 2 - width / 2)
        y: Math.max(0, secondaryActions.y - height)

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

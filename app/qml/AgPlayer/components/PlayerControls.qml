import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer
import "PlayerPresentation.js" as PlayerPresentation

Rectangle {
    id: root
    color: "transparent"

    property bool emptyMode: false
    property bool showListWindowButton: true
    property bool centerTransport: true
    property bool showWaveformMode: true
    property int shellMode: SettingsController.playerShellMode
    readonly property bool rollingLayout: shellMode === 2
    readonly property bool compactTransport: width < 860
    readonly property bool denseTransport: width < 1200
    readonly property var actionProfile: PlayerPresentation.profile(
                                             rollingLayout ? "rolling"
                                                           : "classic")
    property real popupDevicePixelRatioOverrideForTesting: 0
    readonly property real themePopupDevicePixelRatio:
        popupDevicePixelRatioOverrideForTesting > 0
        ? popupDevicePixelRatioOverrideForTesting
        : root.Window.window && root.Window.window.screen
          ? root.Window.window.screen.devicePixelRatio : 1
    signal openEqualizerRequested()
    signal toggleEmbeddedPlaylistRequested()

    function themePopupPositionForDpr(dpr) {
        var window = root.Window.window
        var surface = window ? window.contentItem : root
        return PlayerPresentation.popupPosition(themeModeButton,
                                                playerShellMenu, surface, dpr)
    }

    function openThemePopup() {
        var point = themePopupPositionForDpr(themePopupDevicePixelRatio)
        playerShellMenu.popup(point.x, point.y)
    }

    ToolButton {
        id: listWindowButton
        objectName: "listWindowButton"
        anchors.left: parent.left
        anchors.leftMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        visible: root.showListWindowButton
                 && PlayerPresentation.hasAction(root.actionProfile,
                                                 "listWindowButton")
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
            var centered = root.width / 2 - playButtonCenterX
            var leftLimit = listWindowButton.visible
                    ? listWindowButton.x + listWindowButton.width + 12 : 12
            if (root.rollingLayout)
                return leftLimit
            return root.centerTransport
                    ? Math.max(leftLimit, centered)
                    : leftLimit
        }
        compact: root.compactTransport
        dense: root.denseTransport
        showWaveformMode: root.showWaveformMode
                          && PlayerPresentation.hasAction(
                              root.actionProfile, "waveformModeButton")
        rollingOrder: root.actionProfile.rollingOrder
        spacing: root.emptyMode && !root.denseTransport
                 ? 28 : (compact ? 4 : dense ? 8 : 16)
        onOpenEqualizerRequested: root.openEqualizerRequested()
    }

    ToolButton {
        id: audioToolsButton
        objectName: "audioToolsButton"
        anchors.left: root.rollingLayout ? centerControls.right : undefined
        anchors.leftMargin: root.rollingLayout
                            ? (root.denseTransport ? 4 : 10) : 0
        anchors.right: root.rollingLayout ? undefined : centerControls.left
        anchors.rightMargin: root.rollingLayout
                             ? 0 : (root.denseTransport ? 4 : 10)
        anchors.verticalCenter: centerControls.verticalCenter
        width: root.denseTransport ? 32 : 40
        height: root.denseTransport ? 32 : 40
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

    Loader {
        id: lyricsActions
        active: !root.rollingLayout
        anchors.left: root.rollingLayout ? audioToolsButton.right
                                         : centerControls.right
        anchors.leftMargin: root.denseTransport ? 2 : 8
        anchors.verticalCenter: centerControls.verticalCenter
        sourceComponent: ExperienceActions {
            objectName: "experienceActions"
            compact: root.denseTransport
            presentationProfile: root.actionProfile
            allowImmersive: false
            allowLyrics: true
        }
    }

    PlayerVolumeControl {
        id: volumeControl
        anchors.left: root.rollingLayout ? secondaryActions.right
                                         : lyricsActions.right
        anchors.leftMargin: root.denseTransport ? 0 : 4
        anchors.verticalCenter: centerControls.verticalCenter
        emptyMode: root.emptyMode || root.rollingLayout
        maximumExpandedWidth: Math.min(
            196, Math.max(44, secondaryActions.x - x - 8))
    }

    Row {
        id: secondaryActions
        objectName: "playerSecondaryActions"
        anchors.left: root.rollingLayout ? audioToolsButton.right : undefined
        anchors.leftMargin: root.rollingLayout
                            ? (root.denseTransport ? 2 : 8) : 0
        anchors.right: root.rollingLayout ? undefined : parent.right
        anchors.rightMargin: root.rollingLayout ? 0 : 24
        anchors.verticalCenter: parent.verticalCenter
        spacing: root.denseTransport ? 4 : 14

        ToolButton {
            id: themeModeButton
            objectName: "themeModeButton"
            width: root.denseTransport ? 32 : implicitWidth
            height: root.denseTransport ? 32 : implicitHeight
            visible: PlayerPresentation.hasAction(root.actionProfile,
                                                  "themeModeButton")
                     && !root.compactTransport
            flat: true
            icon.source: Theme.icon("theme-skin")
            icon.color: Theme.iconPrimary
            icon.width: 20
            icon.height: 20
            Accessible.name: qsTr("切换主题")
            onClicked: root.openThemePopup()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }

        ExperienceActions {
            objectName: "immersiveExperienceActions"
            visible: !root.compactTransport
            anchors.verticalCenter: parent.verticalCenter
            presentationProfile: root.actionProfile
            compact: root.denseTransport
            width: implicitWidth
            height: implicitHeight
            allowImmersive: true
            allowLyrics: false
        }

        ToolButton {
            objectName: "miniPlayerButton"
            width: root.denseTransport ? 32 : implicitWidth
            height: root.denseTransport ? 32 : implicitHeight
            visible: PlayerPresentation.hasAction(root.actionProfile,
                                                  "miniPlayerButton")
                     && !root.emptyMode
                     && !root.compactTransport
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
        parent: root.Window.window ? root.Window.window.contentItem : root
        width: 200

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

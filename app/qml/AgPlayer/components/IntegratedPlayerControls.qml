import QtQuick
import QtQuick.Controls
import AgPlayer
import "PlayerPresentation.js" as PlayerPresentation

Item {
    id: root
    objectName: "integratedPlayerControls"
    property bool emptyMode: false
    property real leftReservedWidth: 0
    readonly property bool denseLayout: width < 1440
    readonly property real actionSpacing: denseLayout ? 6 : 10
    readonly property var actionProfile:
        PlayerPresentation.profile("integrated")
    property real popupDevicePixelRatioOverrideForTesting: 0
    readonly property real themePopupDevicePixelRatio:
        popupDevicePixelRatioOverrideForTesting > 0
        ? popupDevicePixelRatioOverrideForTesting
        : root.Window.window && root.Window.window.screen
          ? root.Window.window.screen.devicePixelRatio : 1
    signal openEqualizerRequested()
    signal togglePlaylistRequested()
    anchors.fill: parent

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
        anchors.leftMargin: root.leftReservedWidth + 12
        anchors.verticalCenter: parent.verticalCenter
        visible: false
        flat: true
        icon.source: Theme.icon("list-unordered")
        icon.color: WindowController.listWindowVisible
                    ? Theme.iconAccent : Theme.iconPrimary
        icon.width: 20
        icon.height: 20
        Accessible.name: WindowController.listWindowVisible
                         ? qsTr("收起播放列表")
                         : qsTr("展开播放列表")
        onClicked: root.togglePlaylistRequested()
        ToolTip.text: Accessible.name
        ToolTip.visible: hovered
        background: Rectangle {
            color: parent.down ? Theme.surfacePressed
                : WindowController.listWindowVisible ? Theme.accentSoft
                : parent.hovered ? Theme.surfaceHover : "transparent"
            border.width: parent.visualFocus ? 2 : 0
            border.color: Theme.focus
            radius: Theme.radiusSm
            Behavior on color { ColorAnimation { duration: 100 } }
        }
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
            showWaveformMode: PlayerPresentation.hasAction(
                                  root.actionProfile, "waveformModeButton")
            waveformPlacement: root.actionProfile.waveformPlacement
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
            icon.width: 22
            icon.height: 22
            Accessible.name: qsTr("打开音频工具")
            onClicked: WindowController.showAudioTools()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: Rectangle {
                color: parent.down ? Theme.surfacePressed
                    : parent.hovered ? Theme.surfaceHover : "transparent"
                border.width: parent.visualFocus ? 2 : 0
                border.color: Theme.focus
                radius: Theme.radiusSm
                Behavior on color { ColorAnimation { duration: 100 } }
            }
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
            presentationProfile: root.actionProfile
            allowImmersive: false
            allowLyrics: false
        }

        PlayerVolumeControl {
            id: volumeControl
            anchors.left: lyricsActions.right
            anchors.leftMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            emptyMode: root.emptyMode
            maximumExpandedWidth: Math.min(196, Math.max(buttonExtent,
                rightActions.x - centerGroup.x - x - root.actionSpacing))
        }
    }

    Row {
        id: rightActions
        objectName: "integratedRightActions"
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: root.actionSpacing

        ToolButton {
            id: themeModeButton
            objectName: "themeModeButton"
            visible: PlayerPresentation.hasAction(root.actionProfile,
                                                  "themeModeButton")
            flat: true
            icon.source: Theme.icon("theme-skin")
            icon.color: Theme.iconPrimary
            icon.width: 22
            icon.height: 22
            Accessible.name: qsTr("切换主题皮肤")
            onClicked: root.openThemePopup()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: Rectangle {
                color: parent.down ? Theme.surfacePressed
                    : parent.hovered ? Theme.surfaceHover : "transparent"
                border.width: parent.visualFocus ? 2 : 0
                border.color: Theme.focus
                radius: Theme.radiusSm
                Behavior on color { ColorAnimation { duration: 100 } }
            }
        }
        ExperienceActions {
            objectName: "immersiveExperienceActions"
            anchors.verticalCenter: parent.verticalCenter
            presentationProfile: root.actionProfile
            allowImmersive: true
            allowLyrics: false
        }
        ToolButton {
            objectName: "miniPlayerButton"
            visible: PlayerPresentation.hasAction(root.actionProfile,
                                                  "miniPlayerButton")
            flat: true
            icon.source: Theme.icon("picture-in-picture-2-line")
            icon.color: Theme.iconPrimary
            icon.width: 22
            icon.height: 22
            Accessible.name: qsTr("切换到迷你播放器")
            onClicked: WindowController.showMini()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: Rectangle {
                color: parent.down ? Theme.surfacePressed
                    : parent.hovered ? Theme.surfaceHover : "transparent"
                border.width: parent.visualFocus ? 2 : 0
                border.color: Theme.focus
                radius: Theme.radiusSm
                Behavior on color { ColorAnimation { duration: 100 } }
            }
        }
    }

    Menu {
        id: playerShellMenu
        objectName: "playerShellMenu"
        parent: root.Window.window ? root.Window.window.contentItem : root
        width: Math.max(136, implicitContentWidth + leftPadding + rightPadding)

        background: Rectangle {
            color: Theme.surfaceElevated
            border.color: Theme.opaqueBorder
            radius: Theme.radiusSm
        }

        ThemedMenuItem {
            objectName: "classicShellMenuItem"
            implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
            implicitHeight: 28
            labelPixelSize: 12
            text: qsTr("经典双窗口")
            checkable: true
            checked: SettingsController.playerShellMode === 0
            onTriggered: SettingsController.playerShellMode = 0
        }
        ThemedMenuItem {
            objectName: "integratedShellMenuItem"
            implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
            implicitHeight: 28
            labelPixelSize: 12
            text: qsTr("集成单窗口")
            checkable: true
            checked: SettingsController.playerShellMode === 1
            onTriggered: SettingsController.playerShellMode = 1
        }
        ThemedMenuItem {
            objectName: "rollingShellMenuItem"
            implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
            implicitHeight: 28
            labelPixelSize: 12
            text: qsTr("专业模式")
            checkable: true
            checked: SettingsController.playerShellMode === 2
            onTriggered: SettingsController.playerShellMode = 2
        }
    }
}

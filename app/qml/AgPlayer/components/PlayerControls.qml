import QtQuick
import QtQuick.Controls
import AgPlayer
import "PlayerPresentation.js" as PlayerPresentation

Rectangle {
    id: root
    color: "transparent"

    component ActionBackground: Rectangle {
        color: "transparent"
        border.width: parent.activeFocus
                      && (parent.focusReason === Qt.TabFocusReason
                          || parent.focusReason === Qt.BacktabFocusReason)
                      ? 2 : 0
        border.color: Theme.focus
        radius: Theme.radiusSm
    }

    property bool emptyMode: false
    property var playback: PlaybackController
    property bool showListWindowButton: true
    property bool centerTransport: true
    property bool showWaveformMode: true
    property bool showCueButton: false
    property Item secondaryActionHost: null
    property int shellMode: SettingsController.playerShellMode
    readonly property bool rollingLayout: shellMode === 2
    // Rolling mode reserves a dedicated 520-DIP control region and keeps
    // every playback/theme/EQ entry point available even at the 1000-DIP
    // window minimum. Other shells may still collapse secondary actions.
    readonly property bool compactTransport: !rollingLayout && width < 860
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

    function cancelCueHold() { centerControls.cancelCueHold() }

    ToolButton {
        id: listWindowButton
        objectName: "listWindowButton"
        anchors.left: parent.left
        anchors.leftMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: root.rollingLayout ? 0 : -2
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
        background: ActionBackground {}
    }

    TransportControls {
        id: centerControls
        objectName: "centerPlaybackControls"
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: root.rollingLayout ? 0 : -2
        x: {
            var toolWidth = root.compactTransport ? 32 : 40
            var playWidth = root.compactTransport ? 46 : 52
            var leadingCount = 1 // previous
            if (showCueButton)
                ++leadingCount
            if (showEqualizer && waveformPlacement === "beforePrevious")
                ++leadingCount
            if (showWaveformMode && waveformPlacement === "beforePrevious")
                ++leadingCount
            var playCenterOffset = leadingCount * toolWidth
                    + leadingCount * spacing + playWidth / 2
            var centered = root.width / 2 - playCenterOffset
            var leftLimit = listWindowButton.visible
                    ? listWindowButton.x + listWindowButton.width + 12 : 12
            if (!root.rollingLayout)
                leftLimit += (root.denseTransport ? 32 : 40)
                    + (root.denseTransport ? 4 : 10)
            if (root.rollingLayout)
                return leftLimit
            return root.centerTransport
                    ? Math.max(leftLimit, centered)
                    : leftLimit
        }
        compact: root.compactTransport
        playback: root.playback
        dense: root.denseTransport
        showWaveformMode: root.showWaveformMode
                          && PlayerPresentation.hasAction(
                              root.actionProfile, "waveformModeButton")
        waveformPlacement: root.actionProfile.waveformPlacement
        showCueButton: root.showCueButton
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
        background: ActionBackground {}
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
        anchors.left: root.rollingLayout && root.secondaryActionHost
                     ? audioToolsButton.right
                     : root.rollingLayout ? secondaryActions.right
                                         : lyricsActions.right
        anchors.leftMargin: root.denseTransport ? 0 : 4
        anchors.verticalCenter: centerControls.verticalCenter
        // Rolling mode still needs the hover flyout; it only relocates the
        // shell actions and must not downgrade volume to an icon-only button.
        emptyMode: root.emptyMode
        compact: root.compactTransport
        maximumExpandedWidth: root.rollingLayout && root.secondaryActionHost
                              ? 156
                              : Math.min(196, Math.max(
                                    44, secondaryActions.x - x - 8))
    }

    Row {
        id: secondaryActions
        objectName: "playerSecondaryActions"
        anchors.left: root.rollingLayout ? audioToolsButton.right : undefined
        anchors.leftMargin: root.rollingLayout
                            ? (root.denseTransport ? 2 : 8) : 0
        anchors.right: root.rollingLayout ? undefined : parent.right
        anchors.rightMargin: root.rollingLayout ? 0
                            : 24
        anchors.verticalCenter: centerControls.verticalCenter
        spacing: root.denseTransport ? 2 : 14

        ToolButton {
            id: themeModeButton
            parent: root.secondaryActionHost || secondaryActions
            anchors.left: root.secondaryActionHost ? parent.left : undefined
            anchors.verticalCenter: root.secondaryActionHost
                                    ? parent.verticalCenter : undefined
            objectName: "themeModeButton"
            width: root.secondaryActionHost ? 32
                   : root.denseTransport ? 32 : Math.max(32, implicitWidth)
            height: root.denseTransport ? 32 : Math.max(32, implicitHeight)
            visible: PlayerPresentation.hasAction(root.actionProfile,
                                                  "themeModeButton")
            flat: true
            icon.source: Theme.icon("theme-skin")
            icon.color: Theme.iconPrimary
            icon.width: 22
            icon.height: 22
            Accessible.name: qsTr("切换主题")
            onClicked: root.openThemePopup()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: ActionBackground {}
        }

        ExperienceActions {
            id: immersiveExperienceActions
            objectName: "immersiveExperienceActions"
            parent: root.secondaryActionHost || secondaryActions
            anchors.left: root.secondaryActionHost
                          ? themeModeButton.right : undefined
            anchors.leftMargin: root.secondaryActionHost ? 4 : 0
            visible: true
            anchors.verticalCenter: parent.verticalCenter
            presentationProfile: root.actionProfile
            compact: root.secondaryActionHost !== null || root.denseTransport
            width: implicitWidth
            height: implicitHeight
            allowImmersive: true
            allowLyrics: false
        }

        ToolButton {
            parent: root.secondaryActionHost || secondaryActions
            anchors.left: root.secondaryActionHost
                          ? immersiveExperienceActions.right : undefined
            anchors.leftMargin: root.secondaryActionHost ? 4 : 0
            anchors.verticalCenter: root.secondaryActionHost
                                    ? parent.verticalCenter : undefined
            objectName: "miniPlayerButton"
            width: root.secondaryActionHost ? 32
                   : root.denseTransport ? 32 : Math.max(32, implicitWidth)
            height: root.denseTransport ? 32 : Math.max(32, implicitHeight)
            visible: PlayerPresentation.hasAction(root.actionProfile,
                                                  "miniPlayerButton")
                     && !root.emptyMode
            flat: true
            icon.source: Theme.icon("picture-in-picture-2-line")
            icon.color: Theme.iconPrimary
            icon.width: 22
            icon.height: 22
            Accessible.name: qsTr("切换到迷你播放器")
            onClicked: WindowController.showMini()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: ActionBackground {}
        }
    }

    Menu {
        id: playerShellMenu
        objectName: "playerShellMenu"
        parent: root.Window.window ? root.Window.window.contentItem : root
        width: Math.max(120, implicitContentWidth + leftPadding + rightPadding)

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

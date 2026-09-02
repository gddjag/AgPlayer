import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

RowLayout {
    id: root
    objectName: "experienceActions"
    property string profile: "classic"
    property bool compact: false
    property bool showImmersive: true
    property bool showLyrics: true
    spacing: compact ? 0 : 2

    readonly property var actionProfiles: ({
        "classic": ["listWindowButton", "audioToolsButton", "equalizerButton",
                    "waveformModeButton", "previousButton", "playPauseButton",
                    "nextButton", "modeButton", "lyricsActionButton",
                    "mainVolumeControl", "themeModeButton",
                    "immersiveActionButton", "miniPlayerButton"],
        "integrated": ["listWindowButton", "audioToolsButton",
                       "equalizerButton", "waveformModeButton",
                       "previousButton", "playPauseButton", "nextButton",
                       "modeButton", "lyricsActionButton", "mainVolumeControl",
                       "themeModeButton", "immersiveActionButton",
                       "miniPlayerButton"],
        "rolling": ["previousButton", "playPauseButton", "nextButton",
                    "modeButton", "waveformModeButton", "equalizerButton",
                    "audioToolsButton", "themeModeButton",
                    "immersiveActionButton", "miniPlayerButton",
                    "mainVolumeControl"],
        "mini": ["miniThemeModeButton", "miniWaveformModeButton",
                 "miniPreviousButton", "miniPlayPauseButton",
                 "miniNextButton", "miniModeButton", "miniMuteButton"]
    })
    readonly property var actionOrder: actionProfiles[profile] || []

    function buttonSize() { return compact ? 26 : 32 }

    function popupPosition(button, popup, host, devicePixelRatio,
                           windowSurface) {
        var dpr = Math.max(1, Number(devicePixelRatio || 1))
        var surface = windowSurface || host
        var mapped = button.mapToItem(surface, button.width / 2, 0)
        var x = Math.max(0, Math.min(surface.width - popup.width,
                                    mapped.x - popup.width / 2))
        var y = Math.max(0, Math.min(surface.height - popup.height,
                                    mapped.y - popup.height))
        var snapped = Qt.point(Math.round(x * dpr) / dpr,
                               Math.round(y * dpr) / dpr)
        // Popup coordinates are relative to the window overlay, not to the
        // Item where the Popup declaration happens.
        return snapped
    }

    function popupY(button, popup, host, windowSurface) {
        return popupPosition(button, popup, host, 1, windowSurface).y
    }

    function popupX(button, popup, host, windowSurface) {
        return popupPosition(button, popup, host, 1, windowSurface).x
    }

    ToolButton {
        objectName: root.showLyrics ? "lyricsActionButton" : ""
        visible: root.showLyrics
        Layout.preferredWidth: root.buttonSize()
        Layout.preferredHeight: root.buttonSize()
        flat: true
        checkable: true
        checked: PlayerExperienceController.lyricsVisible
        icon.source: Theme.icon("lyrics")
        icon.color: checked ? Theme.iconAccent : Theme.iconPrimary
        icon.width: 20
        icon.height: 20
        Accessible.name: checked ? qsTr("隐藏歌词") : qsTr("显示歌词")
        onClicked: PlayerExperienceController.toggleLyricsVisible()
        ToolTip.text: Accessible.name
        ToolTip.visible: hovered
        background: null
    }

    ToolButton {
        objectName: root.showImmersive ? "immersiveActionButton" : ""
        visible: root.showImmersive
        Layout.preferredWidth: root.buttonSize()
        Layout.preferredHeight: root.buttonSize()
        flat: true
        checkable: true
        checked: PlayerExperienceController.immersiveMode
                 !== PlayerExperienceController.Off
        icon.source: Theme.icon("immersive-visual-mode")
        icon.color: checked ? Theme.iconAccent : Theme.iconPrimary
        icon.width: 20
        icon.height: 20
        Accessible.name: checked ? qsTr("关闭沉浸视觉") : qsTr("开启沉浸视觉")
        onClicked: PlayerExperienceController.toggleImmersiveMode()
        ToolTip.text: Accessible.name
        ToolTip.visible: hovered
        background: null
    }
}

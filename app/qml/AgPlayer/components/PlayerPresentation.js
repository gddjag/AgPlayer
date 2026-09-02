.pragma library

var profiles = {
    "classic": {
        order: ["listWindowButton", "audioToolsButton", "equalizerButton",
                "waveformModeButton", "previousButton", "playPauseButton",
                "nextButton", "modeButton", "lyricsActionButton",
                "mainVolumeControl", "themeModeButton",
                "immersiveActionButton", "miniPlayerButton"],
        rollingOrder: false
    },
    "integrated": {
        order: ["listWindowButton", "audioToolsButton", "equalizerButton",
                "waveformModeButton", "previousButton", "playPauseButton",
                "nextButton", "modeButton", "lyricsActionButton",
                "mainVolumeControl", "themeModeButton",
                "immersiveActionButton", "miniPlayerButton"],
        rollingOrder: false
    },
    "rolling": {
        order: ["previousButton", "playPauseButton", "nextButton",
                "modeButton", "waveformModeButton", "equalizerButton",
                "audioToolsButton", "themeModeButton",
                "immersiveActionButton", "miniPlayerButton",
                "mainVolumeControl"],
        rollingOrder: true
    },
    "mini": {
        order: ["miniThemeModeButton", "miniWaveformModeButton",
                "miniPreviousButton", "miniPlayPauseButton",
                "miniNextButton", "miniModeButton", "miniMuteButton"],
        rollingOrder: false
    }
}

function profile(name) {
    return profiles[name] || profiles.classic
}

function hasAction(profileValue, objectName) {
    return profileValue && profileValue.order
            && profileValue.order.indexOf(objectName) !== -1
}

function popupPosition(button, popup, windowSurface, devicePixelRatio) {
    var dpr = Math.max(1, Number(devicePixelRatio || 1))
    var mapped = button.mapToItem(windowSurface, button.width / 2, 0)
    var popupWidth = Math.max(0, Number(popup.width || popup.implicitWidth || 0))
    var popupHeight = Math.max(0, Number(popup.height || popup.implicitHeight || 0))
    var x = Math.max(0, Math.min(windowSurface.width - popupWidth,
                                mapped.x - popupWidth / 2))
    var y = Math.max(0, Math.min(windowSurface.height - popupHeight,
                                mapped.y - popupHeight))
    return Qt.point(Math.round(x * dpr) / dpr,
                    Math.round(y * dpr) / dpr)
}

.pragma library

var profiles = {
    "classic": {
        actions: ({
            "listWindowButton": true, "audioToolsButton": true,
            "equalizerButton": true, "waveformModeButton": true,
            "previousButton": true, "playPauseButton": true,
            "nextButton": true, "modeButton": true,
            "lyricsActionButton": true, "mainVolumeControl": true,
            "themeModeButton": true, "immersiveActionButton": true,
            "miniPlayerButton": true
        }),
        waveformPlacement: "beforePrevious"
    },
    "integrated": {
        actions: ({
            "listWindowButton": true, "audioToolsButton": true,
            "equalizerButton": true, "waveformModeButton": true,
            "previousButton": true, "playPauseButton": true,
            "nextButton": true, "modeButton": true,
            "lyricsActionButton": true, "mainVolumeControl": true,
            "themeModeButton": true, "immersiveActionButton": true,
            "miniPlayerButton": true
        }),
        waveformPlacement: "beforePrevious"
    },
    "rolling": {
        actions: ({
            "previousButton": true, "playPauseButton": true,
            "nextButton": true, "modeButton": true,
            "waveformModeButton": false, "equalizerButton": true,
            "audioToolsButton": true, "themeModeButton": true,
            "immersiveActionButton": true, "miniPlayerButton": true,
            "mainVolumeControl": true
        }),
        waveformPlacement: "afterMode"
    },
    "mini": {
        actions: ({
            "miniWaveformModeButton": true,
            "miniPreviousButton": true, "miniPlayPauseButton": true,
            "miniNextButton": true, "miniModeButton": true,
            "miniMuteButton": true
        })
    }
}

function profile(name) {
    return profiles[name] || profiles.classic
}

function hasAction(profileValue, objectName) {
    return profileValue && profileValue.actions
            && profileValue.actions[objectName] === true
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

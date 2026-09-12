import QtQuick
import QtQuick.Controls
import AgPlayer

Item {
    id: root
    objectName: "immersiveSurface"
    property int hostMode: PlayerExperienceController.Windowed
    property bool attached: false
    property bool hostExposed: false
    property bool renderingEnabled: true
    property var waveformSession: null
    property bool panelIdle: false
    property bool panelAutoHidden: true
    property bool manualCameraActive: false
    property bool qaSyntheticFeatures: false
    readonly property bool referenceThemeActive: PlayerExperienceController.themeId.length > 0
    readonly property bool inkMode: PlayerExperienceController.materialMode === 2
    readonly property bool lightEnvironment: {
        if (!referenceThemeActive) return inkMode
        var c = PlayerExperienceController.themeBackground
        return c.r * 0.2126 + c.g * 0.7152 + c.b * 0.0722 > 0.65
    }
    readonly property color materialBaseColor: PlayerExperienceController.baseColor
    readonly property color environmentCoolColor: PlayerExperienceController.coolColor
    readonly property color environmentWarmColor: PlayerExperienceController.warmColor
    readonly property color inkPaper: {
        var c = materialBaseColor
        return c.r * 0.2126 + c.g * 0.7152 + c.b * 0.0722 < 0.5
                ? Qt.rgba(0.95 + c.r * 0.03, 0.95 + c.g * 0.03,
                          0.95 + c.b * 0.03, 1) : c
    }
    readonly property var terrainItem: terrainLoader.item
    readonly property bool localGlowLifecycleEligible:
        renderingEnabled && active && hostExposed && !inkMode && !referenceThemeActive
        && PlayerExperienceController.glowIntensity > 0
        && PlayerExperienceController.columnLightSpill > 0
    readonly property bool localGlowRequested:
        localGlowLifecycleEligible && terrainItem
        && terrainItem.renderStatus === TerrainReactorItem.Ready
    property string platformPlugin: String(Qt.platform.pluginName || "")
    readonly property bool waylandFallback:
        hostMode === PlayerExperienceController.Desktop
        && platformPlugin.toLowerCase().indexOf("wayland") >= 0
    readonly property bool active:
        attached && PlayerExperienceController.immersiveMode
        !== PlayerExperienceController.Off
    signal returnToWindowRequested()
    signal minimizeRequested()

    function notePointerActivity() {
        if (!active || !hostExposed)
            return
        if (controlPanel.colorPickerOpen)
            return
        if (PlayerExperienceController.panelVisible && !panelAutoHidden)
            panelAutoHideTimer.restart()
        if (hostMode !== PlayerExperienceController.Fullscreen) {
            panelIdle = false
            panelIdleTimer.stop()
            return
        }
        panelIdle = false
        panelIdleTimer.restart()
    }

    function revealPanelFromHotCorner() {
        if (!active || !hostExposed)
            return
        PlayerExperienceController.panelVisible = true
        panelAutoHidden = false
        panelIdle = false
        panelIdleTimer.stop()
        panelAutoHideTimer.restart()
    }

    function noteManualCameraActivity() {
        if (!active || !hostExposed)
            return
        manualCameraActive = true
        cameraResumeTimer.restart()
        notePointerActivity()
    }

    function qualityForHost() {
        if (hostMode === PlayerExperienceController.Desktop)
            return TerrainReactorItem.Eco
        switch (PlayerExperienceController.qualityPreset) {
        case PlayerExperienceController.Eco: return TerrainReactorItem.Eco
        case PlayerExperienceController.Auto:
            return referenceThemeActive ? TerrainReactorItem.High : TerrainReactorItem.Balanced
        case PlayerExperienceController.High:
        case PlayerExperienceController.Ultra: return TerrainReactorItem.High
        default: return TerrainReactorItem.Balanced
        }
    }

    function currentTitle() {
        var track = LibraryModel.trackForId(PlaybackController.currentTrackId)
        return track && track.title ? track.title : ""
    }

    Rectangle {
        anchors.fill: parent
        color: "#03040a" // theme-color-allow: immersive media visual contract
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: root.referenceThemeActive ? PlayerExperienceController.themeBackground
                     : root.inkMode ? root.inkPaper : Qt.tint(PlayerExperienceController.baseColor,
                               Qt.rgba( // theme-color-allow: immersive media visual contract
                                   root.environmentCoolColor.r,
                                   root.environmentCoolColor.g,
                                   root.environmentCoolColor.b, 0.035))
            }
            GradientStop {
                position: 0.58
                color: root.referenceThemeActive ? PlayerExperienceController.themeBackground
                     : root.inkMode ? root.inkPaper : Qt.tint(Qt.darker(PlayerExperienceController.baseColor, 1.7),
                               Qt.rgba( // theme-color-allow: immersive media visual contract
                                   root.environmentWarmColor.r,
                                   root.environmentWarmColor.g,
                                   root.environmentWarmColor.b, 0.025))
            }
            GradientStop { position: 1.0; color: root.referenceThemeActive ? PlayerExperienceController.themeBackground : root.inkMode ? root.inkPaper : "#020305" } // theme-color-allow: immersive media visual contract
        }
    }

    Item {
        id: ambientColorField
        visible: !root.inkMode && !root.referenceThemeActive
        anchors.fill: parent
        opacity: 0.72

        Repeater {
            model: 7
            Rectangle {
                required property int index
                anchors.centerIn: parent
                anchors.verticalCenterOffset: parent.height * 0.16
                width: parent.width * (0.42 + index * 0.105)
                height: parent.height * (0.30 + index * 0.075)
                radius: Math.min(width, height) / 2
                color: index % 3 === 0 ? PlayerExperienceController.warmColor
                     : index % 3 === 1 ? PlayerExperienceController.coolColor
                                       : PlayerExperienceController.accentColor
                opacity: 0.010 - index * 0.0008
            }
        }
    }

    Loader {
        id: terrainLoader
        anchors.fill: parent
        active: root.active
        sourceComponent: root.renderingEnabled
                         ? nativeTerrainComponent : inertTerrainComponent
        onItemChanged: root.synchronizeAudioFeatures()
    }

    Loader {
        id: localGlowLoader
        objectName: "immersiveColumnGlowLoader"
        anchors.fill: parent
        active: root.localGlowRequested
        sourceComponent: localGlowComponent
    }

    Component {
        id: localGlowComponent
        ImmersiveColumnGlow {
            objectName: "immersiveColumnGlow"
            sourceItem: root.terrainItem
            intensity: PlayerExperienceController.glowIntensity / 100.0
            spill: PlayerExperienceController.columnLightSpill / 100.0
            radius: PlayerExperienceController.columnLightRadius / 100.0
        }
    }

    Component {
        id: nativeTerrainComponent
        TerrainReactorItem {
            objectName: "terrainReactor"
            spatialLyrics: PlayerExperienceController.lyricsVisible && LyricsService.enabled
                           && LyricsService.currentLine.length > 0
                           ? [LyricsService.previousLine, LyricsService.currentLine, LyricsService.nextLine] : []
            active: root.active && root.hostExposed
            hostExposed: root.hostExposed
            featureSource: AudioVisualFeatureController
            styleSource: PlayerExperienceController
            trackIdentity: root.qaSyntheticFeatures ? "qa-reference-track"
                         : PlayerExperienceController.songAdaptiveColorEnabled
                           && root.waveformSession
                           ? root.waveformSession.trackId : ""
            quality: root.qualityForHost()
            deterministicSeed: 0x5eed
            useSyntheticFeatures: root.qaSyntheticFeatures
            Component.onCompleted: {
                if (root.qaSyntheticFeatures) {
                    setSyntheticFeatures([0.92, 0.88, 0.45, 0.42,
                                          0.62, 0.70, 0.56, 0.38],
                                         0.72, 0.64, true, true)
                }
            }
        }
    }

    Component {
        id: inertTerrainComponent
        Item {
            objectName: "terrainReactor"
            property bool renderingRequested: false
            property int liveRendererCount: 0
            property int renderStatus: TerrainReactorItem.Inactive
            property string diagnostic: ""
            property var featureBands: []
            property real featureEnergy: 0
            property real featureSpectralFlux: 0
            property bool featureKick: false
            property bool featureSnare: false
            function orbitBy(yawDelta, pitchDelta, nowSeconds) {}
            function zoomBy(wheelDelta, nowSeconds) {}
        }
    }

    function synchronizeAudioFeatures() {
        AudioVisualFeatureController.setActive(
                    SettingsController.playerShellMode === 2
                    || (root.terrainItem ? root.terrainItem.renderingRequested : false))
    }

    Connections {
        target: root.terrainItem
        function onRenderingRequestedChanged() {
            root.synchronizeAudioFeatures()
        }
    }

    Rectangle {
        id: renderFallbackMessage
        objectName: "immersiveRenderFallbackMessage"
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 48)
        height: renderBackendFallbackText.implicitHeight + 30
        radius: 12
        color: Qt.rgba(0.035, 0.04, 0.06, 0.92) // theme-color-allow: immersive media visual contract
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.16) // theme-color-allow: immersive media visual contract
        visible: root.terrainItem
                 && (root.terrainItem.renderStatus
                     === TerrainReactorItem.SoftwareBackend
                     || root.terrainItem.renderStatus
                     === TerrainReactorItem.ResourceError)

        Text {
            id: renderBackendFallbackText
            anchors.fill: parent
            anchors.margins: 15
            text: root.terrainItem && root.terrainItem.diagnostic
                  ? root.terrainItem.diagnostic
                  : qsTr("当前图形后端无法运行沉浸视觉，播放不受影响。")
            color: Qt.rgba(1, 1, 1, 0.84) // theme-color-allow: immersive media visual contract
            font.pixelSize: Theme.fontSizeCaption
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
    }

    MouseArea {
        id: orbitArea
        objectName: "immersiveOrbitArea"
        anchors.fill: parent
        enabled: root.active && root.terrainItem !== null
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        property real lastX: 0
        property real lastY: 0
        property real pressX: 0
        property real pressY: 0
        property bool orbitDragged: false
        onEntered: root.notePointerActivity()
        onPositionChanged: function(mouse) {
            root.notePointerActivity()
            if (!pressed || !root.terrainItem)
                return
            orbitDragged = orbitDragged || Math.hypot(mouse.x - pressX, mouse.y - pressY) > 6
            if (!orbitDragged) return
            root.terrainItem.orbitBy(-(mouse.x - lastX) * 2 * Math.PI / Math.max(1, height),
                                     (mouse.y - lastY) * 2 * Math.PI / Math.max(1, height),
                                     Date.now() / 1000.0)
            lastX = mouse.x
            lastY = mouse.y
            root.noteManualCameraActivity()
        }
        onPressed: function(mouse) {
            lastX = mouse.x
            lastY = mouse.y
            pressX = mouse.x
            pressY = mouse.y
            orbitDragged = false
            root.noteManualCameraActivity()
        }
        onReleased: root.notePointerActivity()
        onClicked: function(mouse) {
            if (!orbitDragged && root.terrainItem && root.terrainItem.triggerRipple)
                root.terrainItem.triggerRipple(mouse.x, mouse.y)
        }
        onWheel: function(wheel) {
            if (!root.terrainItem)
                return
            root.terrainItem.zoomBy(wheel.angleDelta.y, Date.now() / 1000.0)
            root.noteManualCameraActivity()
            wheel.accepted = true
        }
    }

    Item {
        objectName: "immersivePanelRevealZone"
        anchors.left: parent.left
        anchors.top: parent.top
        width: 64
        height: 56
        z: 30
        HoverHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onHoveredChanged: if (hovered) root.revealPanelFromHotCorner()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        visible: !root.inkMode
        border.width: 0
        gradient: Gradient {
            GradientStop { position: 0.00; color: Qt.rgba(0, 0, 0, 0.10) } // theme-color-allow: immersive media visual contract
            GradientStop { position: 0.62; color: Qt.rgba(0, 0, 0, 0.00) } // theme-color-allow: immersive media visual contract
            GradientStop { position: 1.00; color: Qt.rgba(0, 0, 0, 0.48) } // theme-color-allow: immersive media visual contract
        }
    }

    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 16
        anchors.topMargin: 14
        spacing: 8
        z: 20

        ToolButton {
            objectName: "immersiveReturnToWindowButton"
            display: AbstractButton.IconOnly
            implicitWidth: 32
            implicitHeight: 32
            text: qsTr("返回窗口主题")
            icon.source: Theme.icon("arrow-go-back-line")
            icon.color: "#ece8ef" // theme-color-allow: immersive media visual contract
            icon.width: 16
            icon.height: 16
            Accessible.name: text
            onClicked: root.returnToWindowRequested()
            background: Rectangle {
                radius: 9
                color: parent.hovered ? Qt.rgba(1, 1, 1, 0.105) // theme-color-allow: immersive media visual contract
                                      : Qt.rgba(0.04, 0.035, 0.05, 0.72) // theme-color-allow: immersive media visual contract
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.10) // theme-color-allow: immersive media visual contract
            }
        }

        ToolButton {
            objectName: "immersiveMinimizeButton"
            display: AbstractButton.IconOnly
            implicitWidth: 32
            implicitHeight: 32
            text: qsTr("最小化")
            icon.source: Theme.icon("subtract-line")
            icon.color: "#ece8ef" // theme-color-allow: immersive media visual contract
            icon.width: 16
            icon.height: 16
            Accessible.name: text
            onClicked: root.minimizeRequested()
            background: Rectangle {
                radius: 9
                color: parent.hovered ? Qt.rgba(1, 1, 1, 0.105) // theme-color-allow: immersive media visual contract
                                      : Qt.rgba(0.04, 0.035, 0.05, 0.72) // theme-color-allow: immersive media visual contract
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.10) // theme-color-allow: immersive media visual contract
            }
        }

        ToolButton {
            objectName: "immersiveFullscreenButton"
            display: AbstractButton.IconOnly
            implicitWidth: 32
            implicitHeight: 32
            text: root.hostMode === PlayerExperienceController.Fullscreen
                   ? qsTr("退出全屏") : qsTr("全屏")
            icon.source: Theme.icon(root.hostMode
                                    === PlayerExperienceController.Fullscreen
                                    ? "fullscreen-exit-fill"
                                    : "fullscreen-fill")
            icon.color: "#ece8ef" // theme-color-allow: immersive media visual contract
            icon.width: 16
            icon.height: 16
            Accessible.name: text
            onClicked: PlayerExperienceController.hostMode =
                       root.hostMode === PlayerExperienceController.Fullscreen
                       ? PlayerExperienceController.Windowed
                       : PlayerExperienceController.Fullscreen
            background: Rectangle {
                radius: 9
                color: parent.hovered ? Qt.rgba(1, 1, 1, 0.105) // theme-color-allow: immersive media visual contract
                                      : Qt.rgba(0.04, 0.035, 0.05, 0.72) // theme-color-allow: immersive media visual contract
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.10) // theme-color-allow: immersive media visual contract
            }
        }
    }

    ImmersiveControlPanel {
        id: controlPanel
        objectName: "immersiveControlPanelHost"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 14
        anchors.topMargin: 58
        visible: opacity > 0
        enabled: opacity > 0.05
        opacity: PlayerExperienceController.panelVisible
                 && !root.panelAutoHidden ? 1 : 0
        z: 10
        featureBands: root.terrainItem ? root.terrainItem.featureBands : []
        featureEnergy: root.terrainItem ? root.terrainItem.featureEnergy : 0
        featureSpectralFlux: root.terrainItem
                             ? root.terrainItem.featureSpectralFlux : 0
        featureKick: root.terrainItem ? root.terrainItem.featureKick : false
        onPointerActivity: root.notePointerActivity()
        onColorPickerOpenChanged: {
            if (colorPickerOpen) {
                panelAutoHideTimer.stop()
                panelIdleTimer.stop()
                root.panelAutoHidden = false
                root.panelIdle = false
            } else {
                root.notePointerActivity()
            }
        }
        Behavior on opacity { NumberAnimation { duration: 220 } }
    }

    ImmersiveQueueDrawer {
        id: queueDrawer
        anchors.fill: parent
        dragSuppressed: orbitArea.pressed
        z: 12
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: waveform.top
        anchors.bottomMargin: 2
        text: root.currentTitle()
        color: root.lightEnvironment ? "#293D40" : Qt.rgba(0.88, 0.92, 0.97, root.panelIdle ? 0.42 : 0.72) // theme-color-allow: immersive media visual contract
        font.pixelSize: Theme.fontSizeCaption
        elide: Text.ElideRight
        width: Math.min(parent.width * 0.68, implicitWidth)
        horizontalAlignment: Text.AlignHCenter
        z: 8
    }

    SharedWaveformView {
        id: waveform
        objectName: "immersiveWaveformHost"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Math.max(18, parent.width * 0.045)
        anchors.rightMargin: Math.max(18, parent.width * 0.045)
        anchors.bottomMargin: 10
        height: 52
        waveformSession: root.waveformSession
        lightBackground: root.lightEnvironment
        opacityScale: root.panelIdle ? 0.55 : 1.0
        z: 9
    }

    LyricsPanel {
        id: lyricsPanel
        objectName: "immersiveLyricsPanel"
        spatialMode: true
        // The native scene owns synchronized 3D text. Retain the existing
        // import/status/untimed presentation when no native lyric is available.
        visible: LyricsService.enabled && (!root.renderingEnabled
                 || !root.terrainItem || root.terrainItem.renderStatus !== TerrainReactorItem.Ready
                 || !LyricsService.currentLine.length)
        lightBackground: root.lightEnvironment
        fullscreen: root.hostMode === PlayerExperienceController.Fullscreen
        placement: PlayerExperienceController.lyricPosition
        width: Math.min(placement === PlayerExperienceController.Center ? 700 : 560,
                        parent.width * (placement === PlayerExperienceController.Center
                                        ? 0.62 : 0.42))
        height: Math.max(112, implicitHeight + 24,
                        128 * (PlayerExperienceController.lyricSize / 100.0))
        x: {
            var travel = Math.max(0, parent.width - width)
            var fine = (PlayerExperienceController.lyricPositionX - 50)
                       / 100.0 * Math.min(parent.width * 0.24, 320)
            if (placement === PlayerExperienceController.Left)
                return Math.max(28, parent.width * 0.055 + fine)
            if (placement === PlayerExperienceController.Right)
                return Math.min(travel - 28,
                                parent.width - width - parent.width * 0.055 + fine)
            return travel / 2 + fine
        }
        y: Math.max(72 + perspectiveTopInset,
                    Math.min(waveform.y - height - 20,
                    (parent.height - height - waveform.height - 42)
                    * PlayerExperienceController.lyricPositionY / 100.0))
        z: 10
    }

    Rectangle {
        visible: root.waylandFallback
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 18
        width: fallbackText.implicitWidth + 28
        height: 36
        radius: 12
        color: Qt.rgba(0.08, 0.09, 0.12, 0.88) // theme-color-allow: immersive media visual contract
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.12) // theme-color-allow: immersive media visual contract
        z: 20
        Text {
            id: fallbackText
            anchors.centerIn: parent
            text: qsTr("Wayland 不支持桌面层级，已使用普通无边框透明窗口")
            color: Theme.primaryText
            font.pixelSize: Theme.fontSizeCaption
        }
    }

    Timer {
        id: panelIdleTimer
        objectName: "immersivePanelIdleTimer"
        interval: 3000
        onTriggered: if (root.hostMode === PlayerExperienceController.Fullscreen)
                         root.panelIdle = true
    }

    Timer {
        id: panelAutoHideTimer
        objectName: "immersivePanelAutoHideTimer"
        interval: 5000
        onTriggered: if (!controlPanel.colorPickerOpen) root.panelAutoHidden = true
    }

    Timer {
        id: cameraResumeTimer
        objectName: "immersiveCameraResumeTimer"
        interval: 4000
        onTriggered: root.manualCameraActive = false
    }

    onHostModeChanged: synchronizePresentationTimers()
    onAttachedChanged: synchronizePresentationTimers()
    function synchronizePresentationTimers() {
        if (active && hostExposed) {
            panelAutoHidden = true
            panelIdle = false
            panelIdleTimer.stop()
            panelAutoHideTimer.stop()
            return
        }
        panelIdleTimer.stop()
        panelAutoHideTimer.stop()
        cameraResumeTimer.stop()
        manualCameraActive = false
    }
    onActiveChanged: synchronizePresentationTimers()
    onHostExposedChanged: synchronizePresentationTimers()
    Component.onCompleted: synchronizeAudioFeatures()
    // A single immersive host may detach while the rolling shell still owns meters.
    Component.onDestruction: AudioVisualFeatureController.setActive(
                                 SettingsController.playerShellMode === 2)
}

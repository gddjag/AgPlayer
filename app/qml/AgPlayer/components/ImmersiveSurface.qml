import QtQuick
import QtQuick.Controls
import QtQuick.Effects
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
    property bool panelAutoHidden: false
    property bool manualCameraActive: false
    property bool qaSyntheticFeatures: false
    readonly property var terrainItem: terrainLoader.item
    property string platformPlugin: String(Qt.platform.pluginName || "")
    readonly property bool waylandFallback:
        hostMode === PlayerExperienceController.Desktop
        && platformPlugin.toLowerCase().indexOf("wayland") >= 0
    readonly property bool active:
        attached && PlayerExperienceController.immersiveMode
        !== PlayerExperienceController.Off

    function notePointerActivity() {
        panelAutoHidden = false
        if (PlayerExperienceController.panelVisible)
            panelAutoHideTimer.restart()
        if (hostMode !== PlayerExperienceController.Fullscreen) {
            panelIdle = false
            panelIdleTimer.stop()
            return
        }
        panelIdle = false
        panelIdleTimer.restart()
    }

    function noteManualCameraActivity() {
        manualCameraActive = true
        cameraResumeTimer.restart()
        notePointerActivity()
    }

    function qualityForHost() {
        if (hostMode === PlayerExperienceController.Desktop)
            return TerrainReactorItem.Eco
        switch (PlayerExperienceController.qualityPreset) {
        case PlayerExperienceController.Eco: return TerrainReactorItem.Eco
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
            GradientStop { position: 0.0; color: "#101218" } // theme-color-allow: immersive media visual contract
            GradientStop { position: 0.58; color: "#080a0f" } // theme-color-allow: immersive media visual contract
            GradientStop { position: 1.0; color: "#020305" } // theme-color-allow: immersive media visual contract
        }
    }

    Item {
        id: ambientColorField
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
        sourceComponent: root.renderingEnabled
                         ? nativeTerrainComponent : inertTerrainComponent
        onItemChanged: root.synchronizeAudioFeatures()
    }

    MultiEffect {
        objectName: "immersiveReactorBloom"
        anchors.fill: terrainLoader
        source: terrainLoader
        visible: root.active && root.hostExposed
                 && root.hostMode !== PlayerExperienceController.Desktop
                 && PlayerExperienceController.glowIntensity > 4
        blurEnabled: true
        blur: 0.52 + PlayerExperienceController.glowIntensity / 100 * 0.26
        blurMax: 28
        blurMultiplier: 0.72
        brightness: 0.48 + (root.terrainItem
                            ? Math.min(1, root.terrainItem.featureEnergy) * 0.32 : 0)
        saturation: 0.16
        opacity: 0.18 + PlayerExperienceController.glowIntensity / 100 * 0.16
    }

    Item {
        id: reactorSoftBloom
        anchors.fill: parent
        visible: root.active && root.hostExposed
        opacity: 0.42 + (root.terrainItem
                         ? Math.min(1, root.terrainItem.featureEnergy) * 0.58 : 0)

        Repeater {
            model: 6
            Rectangle {
                required property int index
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: parent.height * 0.08
                width: parent.width * (0.58 - index * 0.065)
                height: parent.height * (0.43 - index * 0.045)
                radius: Math.min(width, height) / 2
                color: index % 2 === 0 ? PlayerExperienceController.peakColor
                                       : PlayerExperienceController.warmColor
                opacity: 0.014
            }
        }
    }

    Component {
        id: nativeTerrainComponent
        TerrainReactorItem {
            objectName: "terrainReactor"
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
            property real featureEnergy: 0
            function orbitBy(yawDelta, pitchDelta, nowSeconds) {}
            function zoomBy(wheelDelta, nowSeconds) {}
        }
    }

    function synchronizeAudioFeatures() {
        AudioVisualFeatureController.setActive(
                    root.terrainItem
                    ? root.terrainItem.renderingRequested : false)
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
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
    }

    MouseArea {
        id: orbitArea
        objectName: "immersiveOrbitArea"
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        property real lastX: 0
        property real lastY: 0
        onEntered: root.notePointerActivity()
        onPositionChanged: function(mouse) {
            root.notePointerActivity()
            if (!pressed)
                return
            root.terrainItem.orbitBy(-(mouse.x - lastX) * 0.004,
                                     (mouse.y - lastY) * 0.003,
                                     Date.now() / 1000.0)
            lastX = mouse.x
            lastY = mouse.y
            root.noteManualCameraActivity()
        }
        onPressed: function(mouse) {
            lastX = mouse.x
            lastY = mouse.y
            root.noteManualCameraActivity()
        }
        onReleased: root.notePointerActivity()
        onDoubleClicked: PlayerExperienceController.togglePanelVisible()
        onWheel: function(wheel) {
            root.terrainItem.zoomBy(wheel.angleDelta.y, Date.now() / 1000.0)
            root.noteManualCameraActivity()
            wheel.accepted = true
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
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
        opacity: PlayerExperienceController.panelVisible && !root.panelIdle
                 && !root.panelAutoHidden ? 1 : 0
        z: 10
        onPointerActivity: root.notePointerActivity()
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
        color: Qt.rgba(0.88, 0.92, 0.97, root.panelIdle ? 0.42 : 0.72) // theme-color-allow: immersive media visual contract
        font.pixelSize: 11
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
        opacityScale: root.panelIdle ? 0.55 : 1.0
        z: 9
    }

    LyricsPanel {
        id: lyricsPanel
        objectName: "immersiveLyricsPanel"
        spatialMode: true
        fullscreen: root.hostMode === PlayerExperienceController.Fullscreen
        placement: PlayerExperienceController.lyricPosition
        width: Math.min(placement === PlayerExperienceController.Center ? 700 : 560,
                        parent.width * (placement === PlayerExperienceController.Center
                                        ? 0.62 : 0.42))
        height: 128 * (PlayerExperienceController.lyricSize / 100.0)
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
        y: Math.max(72, Math.min(waveform.y - height - 20,
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
            font.pixelSize: 11
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
        onTriggered: root.panelAutoHidden = true
    }

    Timer {
        id: cameraResumeTimer
        objectName: "immersiveCameraResumeTimer"
        interval: 4000
        onTriggered: root.manualCameraActive = false
    }

    onHostModeChanged: notePointerActivity()
    onAttachedChanged: if (attached) notePointerActivity()
    Component.onCompleted: synchronizeAudioFeatures()
    Component.onDestruction: AudioVisualFeatureController.setActive(false)
}

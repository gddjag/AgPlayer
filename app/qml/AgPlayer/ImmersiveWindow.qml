import QtQuick
import AgPlayer

Window {
    id: root
    objectName: "immersiveVisualWindow"

    property var waveformSession: null
    property bool renderingEnabled: true
    property bool qaSyntheticFeatures: false
    property int qaViewportWidth: 0
    property int qaViewportHeight: 0
    property alias surfaceItem: surface

    function returnToWindowTheme() {
        if (PlayerExperienceController.hostMode
                === PlayerExperienceController.Fullscreen)
            PlayerExperienceController.hostMode =
                    PlayerExperienceController.Windowed
        PlayerExperienceController.immersiveMode =
                PlayerExperienceController.Off
    }

    visible: false
    width: qaViewportWidth > 0 ? qaViewportWidth
                               : Math.min(1440, Screen.width * 0.92)
    height: qaViewportHeight > 0 ? qaViewportHeight
                                 : Math.min(900, Screen.height * 0.88)
    minimumWidth: 960
    minimumHeight: 640
    color: PlayerExperienceController.hostMode === PlayerExperienceController.Desktop
           ? "transparent" : "#050206" // theme-color-allow: immersive media visual contract
    title: qsTr("AgPlayer 沉浸视觉")
    flags: Qt.Window | Qt.FramelessWindowHint
           | (PlayerExperienceController.hostMode === PlayerExperienceController.Desktop
              ? Qt.WindowStaysOnBottomHint : 0)
           | (PlayerExperienceController.hostMode === PlayerExperienceController.Desktop
              && PlayerExperienceController.desktopMousePassthrough
              ? Qt.WindowTransparentForInput : 0)

    function synchronizeHost() {
        if (PlayerExperienceController.immersiveMode
                === PlayerExperienceController.Off) {
            visible = false
            WindowController.leaveImmersivePresentation()
            return
        }
        var wasVisible = visible
        if (PlayerExperienceController.hostMode
                === PlayerExperienceController.Fullscreen) {
            showFullScreen()
            WindowController.enterImmersivePresentation()
            return
        }
        if (PlayerExperienceController.hostMode
                === PlayerExperienceController.Desktop) {
            x = Screen.virtualX
            y = Screen.virtualY
            width = Screen.width
            height = Screen.height
        } else if (!wasVisible) {
            x = Screen.virtualX + Math.max(0, (Screen.width - width) / 2)
            y = Screen.virtualY + Math.max(0, (Screen.height - height) / 2)
        }
        showNormal()
        WindowController.enterImmersivePresentation()
        requestActivate()
    }

    onClosing: function(close) {
        close.accepted = false
        root.returnToWindowTheme()
    }

    Connections {
        target: PlayerExperienceController
        function onImmersiveModeChanged() { Qt.callLater(root.synchronizeHost) }
        function onHostModeChanged() { Qt.callLater(root.synchronizeHost) }
        function onDesktopMousePassthroughChanged() {
            if (PlayerExperienceController.hostMode
                    === PlayerExperienceController.Desktop)
                Qt.callLater(root.synchronizeHost)
        }
    }

    Shortcut {
        objectName: "immersiveEscapeShortcut"
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: PlayerExperienceController.hostMode
                 === PlayerExperienceController.Fullscreen
        onActivated: PlayerExperienceController.hostMode =
                     PlayerExperienceController.Windowed
    }

    ImmersiveSurface {
        id: surface
        anchors.fill: parent
        hostMode: PlayerExperienceController.hostMode
        attached: PlayerExperienceController.immersiveMode
                  !== PlayerExperienceController.Off
        hostExposed: root.visible
                     && root.visibility !== Window.Minimized
                     && root.visibility !== Window.Hidden
        renderingEnabled: root.renderingEnabled
        waveformSession: root.waveformSession
        qaSyntheticFeatures: root.qaSyntheticFeatures
        onReturnToWindowRequested: root.returnToWindowTheme()
        onMinimizeRequested: root.showMinimized()
    }

    Component.onCompleted: synchronizeHost()
}

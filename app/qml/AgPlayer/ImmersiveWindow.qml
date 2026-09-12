import QtQuick
import QtQuick.Templates as T
import AgPlayer

Window {
    id: root
    objectName: "immersiveVisualWindow"
    // Main is hidden during presentation. Do not let its native ownership
    // remove this window's independent taskbar/minimize restore entry.
    transientParent: null

    property var waveformSession: null
    property var dropUrlsSubmitter: null
    property bool renderingEnabled: true
    property bool qaSyntheticFeatures: false
    property int qaViewportWidth: 0
    property int qaViewportHeight: 0
    property alias surfaceItem: surface
    property rect windowedGeometry: Qt.rect(0, 0, 0, 0)
    property int synchronizedHostMode: PlayerExperienceController.Windowed

    readonly property bool hasKeyboardFocus:
        visible && active && visibility !== Window.Minimized
        // Transient windows may remain "active" with focus in their owner.
        && activeFocusItem !== null && activeFocusItem.activeFocus
    readonly property bool transportShortcutsEnabled:
        hasKeyboardFocus && !editingControl()
    readonly property bool arrowShortcutsEnabled:
        hasKeyboardFocus && !editingArrowControl()

    function editingArrowControl() {
        var item = activeFocusItem
        // A hidden/disabled former editor must not suppress the window keys.
        if (item && (!item.visible || !item.enabled))
            return false
        while (item) {
            // Ordinary buttons and passive Control containers do not edit
            // with arrows. Preserve actual value/text/navigation editors.
            if (item instanceof TextInput || item instanceof TextEdit
                    || item instanceof T.Slider || item instanceof T.RangeSlider
                    || item instanceof T.Dial || item instanceof T.SpinBox
                    || item instanceof T.ComboBox || item instanceof T.Tumbler
                    || item instanceof T.ScrollBar || item instanceof ListView)
                return true
            item = item.parent
        }
        return false
    }

    function editingControl() {
        var item = activeFocusItem
        while (item) {
            // Text editing owns Space. Other panel controls must not disable
            // the immersive window's explicit play/pause command.
            if (item instanceof TextInput || item instanceof TextEdit)
                return true
            item = item.parent
        }
        return false
    }

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
            releaseResources()
            WindowController.leaveImmersivePresentation()
            return
        }
        var wasVisible = visible
        var requestedHostMode = PlayerExperienceController.hostMode
        var previousHostMode = synchronizedHostMode
        if (previousHostMode === PlayerExperienceController.Windowed
                && requestedHostMode !== PlayerExperienceController.Windowed
                && visibility === Window.Windowed) {
            windowedGeometry = Qt.rect(x, y, width, height)
        }
        synchronizedHostMode = requestedHostMode
        if (requestedHostMode === PlayerExperienceController.Fullscreen) {
            showFullScreen()
            WindowController.enterImmersivePresentation()
            requestActivate()
            return
        }
        if (requestedHostMode === PlayerExperienceController.Desktop) {
            x = Screen.virtualX
            y = Screen.virtualY
            width = Screen.width
            height = Screen.height
        } else if (!wasVisible) {
            x = Screen.virtualX + Math.max(0, (Screen.width - width) / 2)
            y = Screen.virtualY + Math.max(0, (Screen.height - height) / 2)
        }
        var restoreGeometry = requestedHostMode
                === PlayerExperienceController.Windowed
                && windowedGeometry.width > 0
                && (visibility === Window.FullScreen
                    || previousHostMode === PlayerExperienceController.Desktop)
        showNormal()
        if (restoreGeometry) {
            x = windowedGeometry.x
            y = windowedGeometry.y
            width = windowedGeometry.width
            height = windowedGeometry.height
        }
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

    Shortcut {
        objectName: "immersivePreviousShortcut"
        sequence: "Left"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: root.arrowShortcutsEnabled
        onActivated: PlaybackController.previous()
    }

    Shortcut {
        objectName: "immersiveNextShortcut"
        sequence: "Right"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: root.arrowShortcutsEnabled
        onActivated: PlaybackController.next()
    }

    Shortcut {
        objectName: "immersivePlaybackShortcut"
        sequence: "Space"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: root.transportShortcutsEnabled
        onActivated: PlaybackController.togglePlayback()
    }

    Shortcut {
        objectName: "immersiveShuffleShortcut"
        sequence: "Down"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: root.arrowShortcutsEnabled
        onActivated: PlaybackController.setMode(PlaybackController.Shuffle)
    }

    Shortcut {
        objectName: "immersiveLyricsShortcut"
        sequence: "Up"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: root.arrowShortcutsEnabled
        onActivated: PlayerExperienceController.lyricsVisible =
                     !PlayerExperienceController.lyricsVisible
    }

    ImmersiveSurface {
        id: surface
        // Give the first presentation an explicit keyboard focus target.
        // Declarative initial focus still yields to editors and sliders.
        focus: true
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

    Item {
        id: windowMoveRegion
        objectName: "immersiveWindowMoveRegion"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.rightMargin: 136
        height: 48
        z: 30
        enabled: root.visible
                 && PlayerExperienceController.hostMode
                    === PlayerExperienceController.Windowed
                 && root.visibility !== Window.FullScreen
        visible: enabled

        DragHandler {
            objectName: "immersiveWindowMoveHandler"
            target: null
            acceptedButtons: Qt.LeftButton
            enabled: windowMoveRegion.enabled
            onActiveChanged: {
                if (active)
                    root.startSystemMove()
            }
        }
    }

    FileDropArea {
        objectName: "immersiveFileDropArea"
        anchors.fill: parent
        z: 40
        enabled: root.visible
        urlsSubmitter: root.dropUrlsSubmitter
    }

    Component.onCompleted: synchronizeHost()
}

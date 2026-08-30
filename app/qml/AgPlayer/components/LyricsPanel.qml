import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import AgPlayer

Item {
    id: root
    objectName: "lyricsPanel"

    property var service: LyricsService
    property bool fullscreen: false
    property bool spatialMode: false
    property int placement: PlayerExperienceController.lyricPosition
    property int clarity: PlayerExperienceController.lyricClarity
    property int depth: PlayerExperienceController.lyricDepth
    property int lyricSize: PlayerExperienceController.lyricSize
    property int lyricOpacity: PlayerExperienceController.lyricOpacity
    readonly property bool sidePlacement:
        placement !== PlayerExperienceController.Center
    readonly property int lineAlignment:
        placement === PlayerExperienceController.Left ? Text.AlignLeft
        : placement === PlayerExperienceController.Right ? Text.AlignRight
        : Text.AlignHCenter
    readonly property real sizeScale: lyricSize / 100.0
    readonly property real clarityScale: clarity / 100.0
    readonly property real depthScale: depth / 100.0

    visible: service && service.enabled
    opacity: lyricOpacity / 100.0
    clip: false

    function statusText() {
        if (!service)
            return ""
        switch (service.status) {
        case LyricsService.Loading: return qsTr("正在查找歌词…")
        case LyricsService.NotFound: return qsTr("未找到歌词，可手动导入 LRC")
        case LyricsService.Offline: return qsTr("歌词服务暂时离线，可重试或导入 LRC")
        case LyricsService.Error: return qsTr("歌词读取失败，可重试")
        default: return ""
        }
    }

    function noteManualScroll() {
        if (service)
            service.pauseFollow(5000)
    }

    function stopCinematicSettle() {
        currentLineSettle.stop()
        currentLine.settleOpacity = 1
        currentLine.settleScale = 1
    }

    function startCinematicSettle() {
        stopCinematicSettle()
        if (!spatialMode || !enabled || !visible || !service
                || !service.enabled || service.status !== LyricsService.Ready
                || service.currentLine.length === 0)
            return
        currentLine.settleOpacity = 0.62
        currentLine.settleScale = 0.94
        currentLineSettle.start()
    }

    Rectangle {
        id: glass
        anchors.fill: parent
        anchors.margins: root.spatialMode ? -18 : 0
        radius: 16
        color: root.spatialMode ? "transparent"
                                : Qt.rgba(0.025, 0.035, 0.065, 0.46) // theme-color-allow: immersive media visual contract
        border.width: root.spatialMode ? 0 : 1
        border.color: Qt.rgba(1, 1, 1, 0.08) // theme-color-allow: immersive media visual contract
    }

    Column {
        id: lyricStack
        objectName: "cinematicLyricsStage"
        anchors.fill: parent
        anchors.leftMargin: root.spatialMode ? 0 : 18
        anchors.rightMargin: root.spatialMode ? 0 : 18
        anchors.topMargin: root.spatialMode ? 0 : 8
        anchors.bottomMargin: root.spatialMode ? 0 : 8
        spacing: Math.max(3, 6 * root.sizeScale)
        transform: Rotation {
            objectName: "cinematicLyricsPerspective"
            origin.x: root.placement === PlayerExperienceController.Right
                      ? lyricStack.width : root.placement === PlayerExperienceController.Left
                                           ? 0 : lyricStack.width / 2
            origin.y: lyricStack.height / 2
            axis { x: 0; y: 1; z: 0 }
            angle: !root.spatialMode || !root.sidePlacement ? 0
                   : (root.placement === PlayerExperienceController.Left ? -1 : 1)
                     * (6 + root.depthScale * 10)
        }

        Text {
            objectName: "previousLyricLine"
            width: parent.width
            text: root.service ? root.service.previousLine : ""
            color: Qt.rgba(0.86, 0.89, 0.94, // theme-color-allow: immersive media visual contract
                           0.22 + root.clarityScale * 0.30)
            font.pixelSize: Math.round(13 * root.sizeScale)
            horizontalAlignment: root.lineAlignment
            wrapMode: root.spatialMode ? Text.Wrap : Text.NoWrap
            maximumLineCount: root.spatialMode ? 2 : 1
            elide: Text.ElideRight
            opacity: root.spatialMode
                     ? 0.32 + root.clarityScale * 0.18 : 1
            visible: !root.spatialMode || text.length > 0
            scale: root.spatialMode
                   ? 0.88 - root.depthScale * 0.05
                   : 1.0 - root.depthScale * 0.08
            transformOrigin: root.placement === PlayerExperienceController.Right
                             ? Item.Right : root.placement === PlayerExperienceController.Left
                                            ? Item.Left : Item.Center
        }

        Text {
            id: currentLine
            objectName: "currentLyricLine"
            property real settleOpacity: 1
            property real settleScale: 1
            width: parent.width
            text: root.service && root.service.currentLine.length > 0
                  ? root.service.currentLine : root.statusText()
            color: root.sidePlacement && root.spatialMode
                   ? PlayerExperienceController.warmColor : "#FFF8F0" // theme-color-allow: immersive media visual contract
            font.pixelSize: Math.round(21 * root.sizeScale)
            font.weight: root.clarity >= 64 ? Font.DemiBold : Font.Medium
            horizontalAlignment: root.lineAlignment
            wrapMode: root.spatialMode ? Text.Wrap : Text.NoWrap
            maximumLineCount: root.spatialMode ? 2 : 1
            elide: Text.ElideRight
            opacity: root.spatialMode ? settleOpacity : 1
            scale: root.spatialMode
                   ? (1.04 + root.depthScale * 0.05) * settleScale : 1
            transformOrigin: root.placement === PlayerExperienceController.Right
                             ? Item.Right : root.placement === PlayerExperienceController.Left
                                            ? Item.Left : Item.Center
        }

        Text {
            objectName: "nextLyricLine"
            width: parent.width
            text: root.service ? root.service.nextLine : ""
            color: Qt.rgba(0.86, 0.89, 0.94, // theme-color-allow: immersive media visual contract
                           0.19 + root.clarityScale * 0.26)
            font.pixelSize: Math.round(13 * root.sizeScale)
            horizontalAlignment: root.lineAlignment
            wrapMode: root.spatialMode ? Text.Wrap : Text.NoWrap
            maximumLineCount: root.spatialMode ? 2 : 1
            elide: Text.ElideRight
            opacity: root.spatialMode
                     ? 0.26 + root.clarityScale * 0.15 : 1
            visible: !root.spatialMode || text.length > 0
            scale: root.spatialMode
                   ? 0.82 - root.depthScale * 0.05
                   : 1.0 - root.depthScale * 0.14
            transformOrigin: root.placement === PlayerExperienceController.Right
                             ? Item.Right : root.placement === PlayerExperienceController.Left
                                            ? Item.Left : Item.Center
        }
    }

    ParallelAnimation {
        id: currentLineSettle
        objectName: "cinematicLyricsSettleAnimation"
        NumberAnimation {
            target: currentLine
            property: "settleOpacity"
            to: 1
            duration: 180
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: currentLine
            property: "settleScale"
            to: 1
            duration: 220
            easing.type: Easing.OutCubic
        }
    }

    Connections {
        target: root.service
        function onCurrentLineChanged() { root.startCinematicSettle() }
        function onStatusChanged() {
            if (!root.service || root.service.status !== LyricsService.Ready)
                root.stopCinematicSettle()
        }
        function onEnabledChanged() {
            if (!root.service || !root.service.enabled)
                root.stopCinematicSettle()
        }
    }

    onSpatialModeChanged: if (!spatialMode) stopCinematicSettle()
    onServiceChanged: if (currentLine && currentLineSettle) startCinematicSettle()
    onEnabledChanged: if (!enabled) stopCinematicSettle()
    onVisibleChanged: if (!visible) stopCinematicSettle()

    WheelHandler { onWheel: root.noteManualScroll() }

    Row {
        visible: !root.spatialMode
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 8
        anchors.bottomMargin: 5
        spacing: 2
        ToolButton {
            width: 24; height: 24; flat: true
            icon.source: Theme.icon("subtract-line")
            icon.color: Theme.iconSecondary
            Accessible.name: qsTr("歌词提前 100 毫秒")
            onClicked: if (root.service) root.service.offsetMs -= 100
            background: null
        }
        ToolButton {
            width: 24; height: 24; flat: true
            icon.source: Theme.icon("add-line")
            icon.color: Theme.iconSecondary
            Accessible.name: qsTr("歌词延后 100 毫秒")
            onClicked: if (root.service) root.service.offsetMs += 100
            background: null
        }
        ToolButton {
            width: 24; height: 24; flat: true
            icon.source: Theme.icon("arrow-go-forward-line")
            icon.color: Theme.iconSecondary
            Accessible.name: qsTr("重试歌词")
            onClicked: if (root.service) root.service.retry()
            background: null
        }
        ToolButton {
            width: 24; height: 24; flat: true
            icon.source: Theme.icon("folder-open-line")
            icon.color: Theme.iconSecondary
            Accessible.name: qsTr("导入 LRC")
            onClicked: lrcDialog.open()
            background: null
        }
    }

    FileDialog {
        id: lrcDialog
        title: qsTr("导入歌词")
        nameFilters: [qsTr("LRC 歌词 (*.lrc)"), qsTr("文本文件 (*.txt)")]
        onAccepted: if (root.service) root.service.importLrc(selectedFile)
    }
}

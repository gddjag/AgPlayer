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

    Rectangle {
        id: glass
        anchors.fill: parent
        anchors.margins: root.spatialMode ? -18 : 0
        radius: 16
        color: root.spatialMode ? "transparent"
                                : Qt.rgba(0.025, 0.035, 0.065, 0.46)
        border.width: root.spatialMode ? 0 : 1
        border.color: Qt.rgba(1, 1, 1, 0.08)
    }

    Column {
        id: lyricStack
        anchors.fill: parent
        anchors.leftMargin: root.spatialMode ? 0 : 18
        anchors.rightMargin: root.spatialMode ? 0 : 18
        anchors.topMargin: root.spatialMode ? 0 : 8
        anchors.bottomMargin: root.spatialMode ? 0 : 8
        spacing: Math.max(3, 6 * root.sizeScale)
        transform: Rotation {
            origin.x: root.placement === PlayerExperienceController.Right
                      ? lyricStack.width : root.placement === PlayerExperienceController.Left
                                           ? 0 : lyricStack.width / 2
            origin.y: lyricStack.height / 2
            axis { x: 0; y: 1; z: 0 }
            angle: !root.spatialMode || !root.sidePlacement ? 0
                   : (root.placement === PlayerExperienceController.Left ? -1 : 1)
                     * (7 + root.depthScale * 15)
        }

        Text {
            objectName: "previousLyricLine"
            width: parent.width
            text: root.service ? root.service.previousLine : ""
            color: Qt.rgba(0.86, 0.89, 0.94,
                           0.22 + root.clarityScale * 0.30)
            font.pixelSize: Math.round(13 * root.sizeScale)
            horizontalAlignment: root.lineAlignment
            elide: Text.ElideRight
            scale: 1.0 - root.depthScale * 0.08
            transformOrigin: root.placement === PlayerExperienceController.Right
                             ? Item.Right : root.placement === PlayerExperienceController.Left
                                            ? Item.Left : Item.Center
        }

        Text {
            objectName: "currentLyricLine"
            width: parent.width
            text: root.service && root.service.currentLine.length > 0
                  ? root.service.currentLine : root.statusText()
            color: root.sidePlacement && root.spatialMode
                   ? PlayerExperienceController.warmColor : "#FFF8F0"
            font.pixelSize: Math.round(21 * root.sizeScale)
            font.weight: root.clarity >= 64 ? Font.DemiBold : Font.Medium
            horizontalAlignment: root.lineAlignment
            elide: Text.ElideRight
        }

        Text {
            objectName: "nextLyricLine"
            width: parent.width
            text: root.service ? root.service.nextLine : ""
            color: Qt.rgba(0.86, 0.89, 0.94,
                           0.19 + root.clarityScale * 0.26)
            font.pixelSize: Math.round(13 * root.sizeScale)
            horizontalAlignment: root.lineAlignment
            elide: Text.ElideRight
            scale: 1.0 - root.depthScale * 0.14
            transformOrigin: root.placement === PlayerExperienceController.Right
                             ? Item.Right : root.placement === PlayerExperienceController.Left
                                            ? Item.Left : Item.Center
        }
    }

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

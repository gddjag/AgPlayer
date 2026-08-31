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

    function importSelectedFile(url) {
        return service ? service.importLrc(url) : false
    }

    function routeReason(diagnostic) {
        switch (diagnostic) {
        case "not-found": return qsTr("未找到匹配歌词")
        case "empty-search": return qsTr("没有匹配结果")
        case "no-acceptable-match": return qsTr("匹配结果不够准确")
        case "rate-limited": return qsTr("请求过于频繁")
        case "network-unavailable": return qsTr("网络不可用")
        default: return qsTr("服务暂时不可用")
        }
    }

    Rectangle {
        id: glass
        objectName: "lyricsPanelSurface"
        anchors.fill: parent
        anchors.margins: root.spatialMode ? -18 : 0
        radius: 16
        color: root.spatialMode ? "transparent" : Theme.glassSurface
        border.width: root.spatialMode ? 0 : 1
        border.color: Theme.glassBorder
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
            color: root.spatialMode
                   ? "#DBE3F0" // theme-color-allow: immersive lyric overlay
                   : Theme.textSecondary
            opacity: root.spatialMode ? 0.22 + root.clarityScale * 0.30 : 1.0
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
                   ? PlayerExperienceController.warmColor : Theme.primaryText
            font.pixelSize: Math.round(21 * root.sizeScale)
            font.weight: root.clarity >= 64 ? Font.DemiBold : Font.Medium
            horizontalAlignment: root.lineAlignment
            elide: Text.ElideRight
        }

        Text {
            objectName: "nextLyricLine"
            width: parent.width
            text: root.service ? root.service.nextLine : ""
            color: root.spatialMode
                   ? "#DBE3F0" // theme-color-allow: immersive lyric overlay
                   : Theme.textSecondary
            opacity: root.spatialMode ? 0.19 + root.clarityScale * 0.26 : 1.0
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

    Text {
        id: sourceText
        objectName: "lyricsSourceText"
        visible: root.service && root.service.sourceProvider.length > 0
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 10
        z: 2
        color: Theme.textSecondary
        font.pixelSize: Math.round(11 * root.sizeScale)
        text: root.service && root.service.sourceAttribution.length > 0
              ? root.service.sourceAttribution
              : qsTr("来源：%1").arg(root.service ? root.service.sourceProvider : "")
    }

    Flickable {
        id: untimedFlickable
        objectName: "untimedLyricsFlickable"
        visible: root.service && root.service.status === LyricsService.Ready
                 && !root.service.synchronizedLyrics
                 && !root.service.instrumental
                 && root.service.untimedLyrics.length > 0
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        anchors.topMargin: sourceText.visible ? 28 : 10
        anchors.bottomMargin: 24
        clip: true
        contentWidth: width
        contentHeight: untimedText.height
        z: 1
        Text {
            id: untimedText
            objectName: "untimedLyricsText"
            width: untimedFlickable.width
            text: root.service ? root.service.untimedLyrics : ""
            wrapMode: Text.Wrap
            color: Theme.primaryText
            font.pixelSize: Math.round(16 * root.sizeScale)
            horizontalAlignment: root.lineAlignment
        }
    }

    Text {
        objectName: "lyricsTimingNotice"
        visible: untimedFlickable.visible
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 10
        z: 2
        color: Theme.textSecondary
        font.pixelSize: Math.round(11 * root.sizeScale)
        text: qsTr("纯文本歌词，无时间轴")
    }

    Rectangle {
        objectName: "lyricsRouteNotice"
        visible: !!(root.service && root.service.routeNotice
                    && root.service.routeNotice.providerName)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 6
        radius: 8
        color: Theme.glassSurface
        border.color: Theme.glassBorder
        z: 3
        width: Math.min(parent.width - 24, routeNoticeText.implicitWidth + 20)
        height: routeNoticeText.implicitHeight + 10
        Text {
            id: routeNoticeText
            anchors.centerIn: parent
            color: Theme.textSecondary
            font.pixelSize: Math.round(11 * root.sizeScale)
            text: root.service && root.service.routeNotice
                  ? root.service.routeNotice.providerName + ": "
                    + root.routeReason(root.service.routeNotice.diagnostic) : ""
        }
    }

    Column {
        objectName: "lyricsRouteAttempts"
        visible: root.service && (root.service.status === LyricsService.NotFound
                                  || root.service.status === LyricsService.Offline
                                  || root.service.status === LyricsService.Error)
                 && root.service.routeAttempts.length > 0
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        z: 2
        Repeater {
            objectName: "lyricsRouteAttemptRepeater"
            model: root.service ? root.service.routeAttempts : []
            delegate: Text {
                objectName: "lyricsRouteAttemptText"
                color: Theme.textSecondary
                font.pixelSize: Math.round(10 * root.sizeScale)
                text: modelData.providerName + ": " + root.routeReason(modelData.diagnostic)
            }
        }
    }

    Row {
        visible: !root.spatialMode
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 8
        anchors.bottomMargin: 5
        spacing: 2
        ToolButton {
            objectName: "lyricsOffsetEarlierButton"
            width: 24; height: 24; flat: true
            icon.source: Theme.icon("subtract-line")
            icon.color: Theme.iconSecondary
            Accessible.name: qsTr("歌词提前 100 毫秒")
            onClicked: if (root.service) root.service.offsetMs -= 100
            background: null
        }
        ToolButton {
            objectName: "lyricsOffsetLaterButton"
            width: 24; height: 24; flat: true
            icon.source: Theme.icon("add-line")
            icon.color: Theme.iconSecondary
            Accessible.name: qsTr("歌词延后 100 毫秒")
            onClicked: if (root.service) root.service.offsetMs += 100
            background: null
        }
        ToolButton {
            objectName: "lyricsRetryButton"
            width: 24; height: 24; flat: true
            icon.source: Theme.icon("arrow-go-forward-line")
            icon.color: Theme.iconSecondary
            Accessible.name: qsTr("重试歌词")
            onClicked: if (root.service) root.service.retry()
            background: null
        }
        ToolButton {
            objectName: "lyricsImportButton"
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
        onAccepted: root.importSelectedFile(selectedFile)
    }
}

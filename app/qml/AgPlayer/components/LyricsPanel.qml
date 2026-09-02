import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import AgPlayer

Item {
    id: root
    objectName: "lyricsPanel"

    signal closeRequested()

    property var service: LyricsService
    property bool fullscreen: false
    property bool spatialMode: false
    property int placement: PlayerExperienceController.lyricPosition
    property int clarity: PlayerExperienceController.lyricClarity
    property int depth: PlayerExperienceController.lyricDepth
    property int lyricSize: PlayerExperienceController.lyricSize
    property int lyricOpacity: PlayerExperienceController.lyricOpacity
    property int chromeAutoHideDelay: 3000
    property bool chromeVisible: true
    readonly property bool sidePlacement:
        placement !== PlayerExperienceController.Center
    readonly property int lineAlignment:
        placement === PlayerExperienceController.Left ? Text.AlignLeft
        : placement === PlayerExperienceController.Right ? Text.AlignRight
        : Text.AlignHCenter
    readonly property real sizeScale: lyricSize / 100.0
    readonly property real clarityScale: clarity / 100.0
    readonly property real depthScale: depth / 100.0
    readonly property bool spatialUntimedFallback:
        spatialMode && service && service.status === LyricsService.Ready
        && !service.synchronizedLyrics && !service.instrumental
        && service.untimedLyrics.length > 0

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
        revealChrome()
        if (service)
            service.pauseFollow(5000)
    }

    function revealChrome() {
        if (spatialMode)
            return
        chromeHideTimer.stop()
        chromeVisible = true
    }

    function scheduleChromeHide() {
        if (spatialMode || !visible)
            return
        chromeVisible = true
        chromeHideTimer.restart()
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
        case "timeout": return qsTr("请求超时")
        case "network-error": return qsTr("网络请求失败")
        case "invalid-response": return qsTr("返回内容无效")
        case "dns": return qsTr("DNS 解析失败")
        case "tls":
        case "tls-error": return qsTr("TLS 安全连接失败")
        case "server-error":
        case "provider-error": return qsTr("服务端异常")
        case "all-routes-failed": return qsTr("所有线路均不可用")
        case "circuit-open": return qsTr("线路暂时熔断")
        case "provider-unavailable": return qsTr("线路不可用")
        default: return qsTr("服务暂时不可用")
        }
    }

    function routeAttemptsSummary(attempts) {
        var rows = attempts === undefined
                ? (root.service ? root.service.routeAttempts : [])
                : attempts
        var parts = []
        for (var index = 0; index < rows.length; ++index) {
            var row = rows[index]
            parts.push(String(row.providerName || row.providerId || qsTr("未知线路"))
                       + "：" + root.routeReason(String(row.diagnostic || "")))
        }
        return parts.join("；")
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
        visible: root.spatialMode || root.chromeVisible
    }

    HoverHandler {
        id: chromeHover
        enabled: !root.spatialMode
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onHoveredChanged: {
            if (hovered)
                root.revealChrome()
            else
                root.scheduleChromeHide()
        }
    }

    Timer {
        id: chromeHideTimer
        interval: root.chromeAutoHideDelay
        repeat: false
        onTriggered: root.chromeVisible = false
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
        visible: !root.spatialUntimedFallback
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
            color: root.spatialMode
                   ? Theme.onBrandGradientText
                   : Theme.textSecondary
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
            color: root.spatialMode
                   ? (root.sidePlacement
                      ? PlayerExperienceController.warmColor
                      : Theme.onBrandGradientText)
                   : Theme.primaryText
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
            color: root.spatialMode
                   ? Theme.onBrandGradientText
                   : Theme.textSecondary
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

    onSpatialModeChanged: {
        if (!spatialMode) {
            stopCinematicSettle()
            scheduleChromeHide()
        } else {
            chromeHideTimer.stop()
            chromeVisible = true
        }
    }
    onServiceChanged: if (currentLine && currentLineSettle) startCinematicSettle()
    onEnabledChanged: if (!enabled) stopCinematicSettle()
    onVisibleChanged: {
        if (!visible) {
            chromeHideTimer.stop()
            stopCinematicSettle()
        } else if (!spatialMode) {
            scheduleChromeHide()
        }
    }

    WheelHandler { onWheel: root.noteManualScroll() }

    Text {
        id: sourceText
        objectName: "lyricsSourceText"
        readonly property string providerName:
            String(root.service && root.service.sourceProvider || "")
        readonly property string attribution:
            String(root.service && root.service.sourceAttribution || "")
        visible: (root.spatialMode || root.chromeVisible)
                 && providerName.length > 0
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 10
        z: 2
        color: Theme.textSecondary
        font.pixelSize: Math.round(11 * root.sizeScale)
        opacity: root.spatialMode ? 0.72 : 1
        text: attribution.length > 0
              ? attribution : qsTr("来源：%1").arg(providerName)
    }

    ToolButton {
        id: closeButton
        objectName: "lyricsCloseButton"
        visible: !root.spatialMode && root.chromeVisible
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 6
        width: 28
        height: 28
        flat: true
        z: 5
        icon.source: Theme.icon("close-line")
        icon.color: Theme.iconSecondary
        icon.width: 18
        icon.height: 18
        Accessible.name: qsTr("关闭歌词窗口")
        ToolTip.text: Accessible.name
        ToolTip.visible: hovered
        onClicked: root.closeRequested()
        background: Rectangle {
            color: closeButton.hovered ? Theme.hoverSurface : "transparent"
            radius: Theme.radiusSm
        }
    }

    Flickable {
        id: untimedFlickable
        objectName: "untimedLyricsFlickable"
        readonly property string untimedTextValue:
            String(root.service && root.service.untimedLyrics || "")
        visible: root.service && root.service.status === LyricsService.Ready
                 && !root.service.synchronizedLyrics
                 && !root.service.instrumental
                 && untimedTextValue.length > 0
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
            text: untimedFlickable.untimedTextValue
            wrapMode: Text.Wrap
            color: root.spatialMode ? Theme.onBrandGradientText
                                    : Theme.primaryText
            font.pixelSize: Math.round(16 * root.sizeScale)
            horizontalAlignment: root.lineAlignment
        }
    }

    Text {
        objectName: "lyricsTimingNotice"
        visible: (root.spatialMode || root.chromeVisible)
                 && untimedFlickable.visible
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
        visible: (root.spatialMode || root.chromeVisible)
                 && !!(root.service && root.service.routeNotice
                     && root.service.routeNotice.providerName)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: sourceText.visible ? 30 : 6
        radius: 8
        color: Theme.glassSurface
        border.color: Theme.glassBorder
        z: 3
        width: Math.min(parent.width - 24, routeNoticeText.implicitWidth + 20)
        height: routeNoticeText.implicitHeight + 10
        Text {
            id: routeNoticeText
            objectName: "lyricsRouteNoticeText"
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
        Accessible.name: root.routeAttemptsSummary()
        visible: (root.spatialMode || root.chromeVisible)
                 && root.service
                 && (root.service.status === LyricsService.NotFound
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
        id: chromeControls
        objectName: "lyricsChromeControls"
        visible: !root.spatialMode && root.chromeVisible
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 8
        anchors.bottomMargin: 5
        spacing: 2
        ToolButton {
            objectName: "lyricsOffsetEarlierButton"
            width: 28; height: 28; flat: true
            icon.source: Theme.icon("subtract-line")
            icon.color: Theme.iconSecondary
            icon.width: 18; icon.height: 18
            Accessible.name: qsTr("歌词提前 100 毫秒")
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            onClicked: if (root.service) root.service.offsetMs -= 100
            background: null
        }
        ToolButton {
            objectName: "lyricsOffsetLaterButton"
            width: 28; height: 28; flat: true
            icon.source: Theme.icon("add-line")
            icon.color: Theme.iconSecondary
            icon.width: 18; icon.height: 18
            Accessible.name: qsTr("歌词延后 100 毫秒")
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            onClicked: if (root.service) root.service.offsetMs += 100
            background: null
        }
        ToolButton {
            objectName: "lyricsRetryButton"
            width: 28; height: 28; flat: true
            icon.source: Theme.icon("restore-line")
            icon.color: Theme.iconSecondary
            icon.width: 18; icon.height: 18
            Accessible.name: qsTr("刷新歌词")
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            onClicked: if (root.service) root.service.retry()
            background: null
        }
        ToolButton {
            objectName: "lyricsImportButton"
            width: 28; height: 28; flat: true
            icon.source: Theme.icon("folder-open-line")
            icon.color: Theme.iconSecondary
            icon.width: 18; icon.height: 18
            Accessible.name: qsTr("导入 LRC")
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
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

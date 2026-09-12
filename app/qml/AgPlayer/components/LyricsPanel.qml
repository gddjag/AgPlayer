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
    property bool lightBackground: false
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
        spatialMode && sidePlacement
        ? (placement === PlayerExperienceController.Left
           ? Text.AlignLeft : Text.AlignRight)
        : Text.AlignHCenter
    readonly property real sizeScale: lyricSize / 100.0
    readonly property real clarityScale: clarity / 100.0
    readonly property real depthScale: depth / 100.0
    readonly property bool spatialUntimedFallback:
        spatialMode && service && service.status === LyricsService.Ready
        && !service.synchronizedLyrics && !service.instrumental
        && service.untimedLyrics.length > 0
    readonly property bool hasTimelineModel:
        !spatialMode && service && service.lines !== undefined
        && service.currentLineIndex !== undefined
        && service.status === LyricsService.Ready
        && service.synchronizedLyrics

    component LyricsActionButton: ToolButton {
        implicitWidth: Theme.controlHeightCompact
        implicitHeight: Theme.controlHeightCompact
        flat: true
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        icon.color: Theme.iconSecondary
        icon.width: Theme.iconSizeMd
        icon.height: Theme.iconSizeMd
        ToolTip.text: Accessible.name
        ToolTip.visible: hovered
        background: Rectangle {
            color: parent.down ? Theme.surfacePressed
                               : parent.hovered ? Theme.hoverSurface
                                                : "transparent"
            border.width: parent.visualFocus ? 2 : 0
            border.color: Theme.focus
            radius: Theme.radiusSm
        }
    }
    readonly property real perspectiveTopInset:
        spatialMode ? Math.ceil(16 * sizeScale * depthScale) : 0
    readonly property color spatialThemeColor: PlayerExperienceController.warmColor
    readonly property color spatialForegroundColor: Qt.rgba(
        0.58 + spatialThemeColor.r * 0.42,
        0.58 + spatialThemeColor.g * 0.42,
        0.58 + spatialThemeColor.b * 0.42,
        1)

    implicitHeight: lyricStack.implicitHeight + (spatialMode ? 0 : 16)

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
        lyricStack.exchangeOffsetY = 0
    }

    function startCinematicSettle() {
        stopCinematicSettle()
        if (!enabled || !visible || !service
                || !service.enabled || service.status !== LyricsService.Ready
                || service.currentLine.length === 0)
            return
        lyricStack.exchangeOffsetY = currentLine.height + lyricStack.spacing
        currentLineSettle.start()
    }

    function lyricPlaneProjection(line, recession) {
        if (!spatialMode || depthScale <= 0)
            return Qt.matrix4x4(1, 0, 0, 0, 0, 1, 0, 0,
                               0, 0, 1, 0, 0, 0, 0, 1)
        var amount = Math.min(1, depthScale)
        var yaw = sidePlacement
                ? (placement === PlayerExperienceController.Left ? 1 : -1)
                  * 28 * amount * Math.PI / 180 : 0
        var pitch = (sidePlacement ? -4 : -10) * amount * Math.PI / 180
        var cy = Math.cos(yaw), sy = Math.sin(yaw)
        var cp = Math.cos(pitch), sp = Math.sin(pitch)
        var cameraDistance = Math.max(320, line.width * 0.9)
        var ox = placement === PlayerExperienceController.Left ? 0
               : placement === PlayerExperienceController.Right ? line.width
                                                               : line.width / 2
        var oy = currentLine.y + currentLine.height / 2 - line.y
        // Project each plane from the same reading-edge camera. The inward
        // edge recedes; previous/next lines sit behind the sharp current line.
        // A homogeneous divide gives real convergence, not an affine skew.
        var a = cp * sy / cameraDistance
        var b = -sp / cameraDistance
        var c = 1 + recession * amount - a * ox - b * oy
        return Qt.matrix4x4(
                    cy + ox * a, ox * b, 0, ox * c - cy * ox,
                    sp * sy + oy * a, cp + oy * b, 0,
                    oy * c - sp * sy * ox - cp * oy,
                    0, 0, 1, 0,
                    a, b, 0, c)
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
        var rows = attempts === undefined || attempts === null
                ? (root.service && root.service.routeAttempts
                   ? root.service.routeAttempts : [])
                : attempts
        if (!rows || rows.length === undefined)
            rows = []
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
        property real exchangeOffsetY: 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - (root.spatialMode ? 0 : 36)
        spacing: Math.max(3, 6 * root.sizeScale)
        visible: !root.spatialUntimedFallback && !root.hasTimelineModel

        Text {
            id: previousLine
            objectName: "previousLyricLine"
            width: parent.width
            text: root.service ? root.service.previousLine : ""
            color: root.lightBackground ? "#293D40" : root.spatialMode // theme-color-allow: immersive ink material foreground
                   ? Theme.onBrandGradientText
                   : Theme.textSecondary
            font.family: Theme.fontPrimary
            font.pixelSize: Math.max(Theme.fontSizeCaption,
                                     Math.round(Theme.fontSizeBody * root.sizeScale))
            fontSizeMode: root.spatialMode ? Text.Fit : Text.FixedSize
            minimumPixelSize: Theme.fontSizeCaption
            lineHeight: 1.12
            lineHeightMode: Text.ProportionalHeight
            height: root.spatialMode ? Math.ceil(font.pixelSize * 2.35)
                                     : implicitHeight
            horizontalAlignment: root.lineAlignment
            wrapMode: Text.Wrap
            maximumLineCount: root.spatialMode ? 2 : 3
            elide: root.spatialMode ? Text.ElideRight : Text.ElideNone
            opacity: root.spatialMode
                     ? 0.32 + root.clarityScale * 0.18 : 1
            visible: true
            scale: root.spatialMode
                   ? 0.94 - root.depthScale * 0.02
                   : 1.0 - root.depthScale * 0.08
            transformOrigin: root.placement === PlayerExperienceController.Right
                             ? Item.Right : root.placement === PlayerExperienceController.Left
                                            ? Item.Left : Item.Center
            transform: [
                Translate {
                    objectName: "previousLyricDepthTransform"
                    y: (root.spatialMode ? -root.depthScale * 6 : 0)
                       + lyricStack.exchangeOffsetY
                },
                Matrix4x4 {
                    matrix: root.lyricPlaneProjection(previousLine, 0.12)
                }
            ]
        }

        Text {
            id: currentLine
            objectName: "currentLyricLine"
            width: parent.width
            text: root.service && root.service.currentLine.length > 0
                  ? root.service.currentLine : root.statusText()
            color: Theme.accent
            font.family: Theme.fontPrimary
            font.pixelSize: Math.max(Theme.fontSizeSection,
                                     Math.round(Theme.fontSizePageTitle * root.sizeScale))
            fontSizeMode: root.spatialMode ? Text.Fit : Text.FixedSize
            minimumPixelSize: Theme.fontSizeSection
            lineHeight: 1.12
            lineHeightMode: Text.ProportionalHeight
            height: root.spatialMode ? Math.ceil(font.pixelSize * 2.35)
                                     : implicitHeight
            font.weight: Font.Bold
            horizontalAlignment: root.lineAlignment
            wrapMode: Text.Wrap
            maximumLineCount: root.spatialMode ? 2 : 3
            elide: root.spatialMode ? Text.ElideRight : Text.ElideNone
            opacity: 1
            scale: root.spatialMode
                   ? 1.04 + root.depthScale * 0.05 : 1
            transformOrigin: root.placement === PlayerExperienceController.Right
                             ? Item.Right : root.placement === PlayerExperienceController.Left
                                            ? Item.Left : Item.Center
            transform: [
                Translate {
                    objectName: "currentLyricEntranceTransform"
                    y: lyricStack.exchangeOffsetY
                },
                Matrix4x4 {
                    objectName: "cinematicLyricsPerspective"
                    matrix: root.lyricPlaneProjection(currentLine, 0)
                }
            ]
        }

        Text {
            id: nextLine
            objectName: "nextLyricLine"
            width: parent.width
            text: root.service ? root.service.nextLine : ""
            color: root.lightBackground ? "#293D40" : root.spatialMode // theme-color-allow: immersive ink material foreground
                   ? Theme.onBrandGradientText
                   : Theme.textSecondary
            font.family: Theme.fontPrimary
            font.pixelSize: Math.max(Theme.fontSizeCaption,
                                     Math.round(Theme.fontSizeBody * root.sizeScale))
            fontSizeMode: root.spatialMode ? Text.Fit : Text.FixedSize
            minimumPixelSize: Theme.fontSizeCaption
            lineHeight: 1.12
            lineHeightMode: Text.ProportionalHeight
            height: root.spatialMode ? Math.ceil(font.pixelSize * 2.35)
                                     : implicitHeight
            horizontalAlignment: root.lineAlignment
            wrapMode: Text.Wrap
            maximumLineCount: root.spatialMode ? 2 : 3
            elide: root.spatialMode ? Text.ElideRight : Text.ElideNone
            opacity: root.spatialMode
                     ? 0.26 + root.clarityScale * 0.15 : 1
            visible: true
            scale: root.spatialMode
                   ? 0.90 - root.depthScale * 0.02
                   : 1.0 - root.depthScale * 0.14
            transformOrigin: root.placement === PlayerExperienceController.Right
                             ? Item.Right : root.placement === PlayerExperienceController.Left
                                            ? Item.Left : Item.Center
            transform: [
                Translate {
                    objectName: "nextLyricDepthTransform"
                    y: (root.spatialMode ? root.depthScale * 6 : 0)
                       + lyricStack.exchangeOffsetY
                },
                Matrix4x4 {
                    matrix: root.lyricPlaneProjection(nextLine, 0.22)
                }
            ]
        }
    }

    ListView {
        id: timelineList
        objectName: "lyricsTimelineList"
        visible: root.hasTimelineModel
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        anchors.topMargin: sourceText.visible ? 30 : 12
        anchors.bottomMargin: 42
        clip: true
        model: root.service ? root.service.lines : null
        currentIndex: root.service
                      && typeof root.service.currentLineIndex === "number"
                      ? root.service.currentLineIndex : -1
        preferredHighlightBegin: height / 2 - 22
        preferredHighlightEnd: height / 2 + 22
        highlightRangeMode: ListView.StrictlyEnforceRange
        highlight: Item { }
        highlightMoveDuration: 320
        highlightResizeDuration: 220
        boundsBehavior: Flickable.StopAtBounds
        header: Item { width: 1; height: Math.max(0, timelineList.height / 2 - 22) }
        footer: Item { width: 1; height: Math.max(0, timelineList.height / 2 - 22) }
        onMovementStarted: root.noteManualScroll()

        delegate: Item {
            required property int index
            required property string text
            width: ListView.view.width
            height: Math.max(36, Math.round(44 * root.sizeScale))

            Text {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                text: parent.text
                color: parent.index === timelineList.currentIndex
                       ? Theme.accent : Theme.textSecondary
                opacity: parent.index === timelineList.currentIndex ? 1 : 0.64
                font.family: Theme.fontPrimary
                font.pixelSize: Math.max(
                    Theme.fontSizeBody,
                    Math.round((parent.index === timelineList.currentIndex
                                ? Theme.fontSizeSection : Theme.fontSizeBody)
                               * root.sizeScale))
                font.weight: parent.index === timelineList.currentIndex
                             ? Font.Bold : Font.Normal
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }
    }

    NumberAnimation {
        id: currentLineSettle
        objectName: "cinematicLyricsSettleAnimation"
        target: lyricStack
        property: "exchangeOffsetY"
        to: 0
        duration: 320
        easing.type: Easing.OutCubic
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
    onServiceChanged: if (currentLine && currentLineSettle) stopCinematicSettle()
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
        visible: !root.spatialMode && root.chromeVisible
                 && providerName.length > 0
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 10
        z: 2
        color: Theme.textSecondary
        font.family: Theme.fontPrimary
        font.pixelSize: Theme.fontSizeCaption
        width: Math.max(0, parent.width - closeButton.width - 36)
        elide: Text.ElideRight
        maximumLineCount: 1
        opacity: root.spatialMode ? 0.72 : 1
        text: attribution.length > 0
              ? attribution : qsTr("来源：%1").arg(providerName)
    }

    LyricsActionButton {
        id: closeButton
        objectName: "lyricsCloseButton"
        visible: !root.spatialMode && root.chromeVisible
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 6
        width: 28
        height: 28
        z: 5
        icon.source: Theme.icon("close-line")
        icon.width: 18
        icon.height: 18
        Accessible.name: qsTr("关闭歌词窗口")
        onClicked: root.closeRequested()
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
        contentHeight: untimedText.height > height
                       ? untimedText.height + height : height
        z: 1
        Text {
            id: untimedText
            objectName: "untimedLyricsText"
            width: untimedFlickable.width
            y: height > untimedFlickable.height
               ? untimedFlickable.height / 2
               : (untimedFlickable.height - height) / 2
            text: untimedFlickable.untimedTextValue
            wrapMode: Text.Wrap
            color: root.spatialMode ? Theme.onBrandGradientText
                                    : Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Math.max(Theme.fontSizeBody,
                                     Math.round(Theme.fontSizeSection * root.sizeScale))
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Rectangle {
        objectName: "lyricsRouteNotice"
        visible: (root.spatialMode || root.chromeVisible)
                 && !!(root.service && root.service.routeNotice
                     && root.service.routeNotice.providerName)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Math.max(
            sourceText.visible ? sourceText.y + sourceText.height + 6 : 6,
            closeButton.visible ? closeButton.y + closeButton.height + 6 : 6)
        radius: 8
        color: Theme.glassSurface
        border.color: Theme.glassBorder
        z: 3
        width: Math.max(0, Math.min(parent.width - 24,
                                   routeNoticeText.implicitWidth + 20))
        height: Math.max(Theme.controlHeight,
                         routeNoticeText.implicitHeight + 10)
        Text {
            id: routeNoticeText
            objectName: "lyricsRouteNoticeText"
            anchors.fill: parent
            anchors.margins: 5
            color: Theme.textSecondary
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeCaption
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            text: root.service && root.service.routeNotice
                  ? root.service.routeNotice.providerName + ": "
                    + root.routeReason(root.service.routeNotice.diagnostic) : ""
        }
    }

    Column {
        id: routeAttemptsColumn
        objectName: "lyricsRouteAttempts"
        Accessible.name: root.routeAttemptsSummary()
        visible: (root.spatialMode || root.chromeVisible)
                 && root.service
                 && (root.service.status === LyricsService.NotFound
                                  || root.service.status === LyricsService.Offline
                                  || root.service.status === LyricsService.Error)
                 && root.service.routeAttempts.length > 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.spatialMode
                              ? 8 : Theme.controlHeightCompact + 12
        z: 2
        Repeater {
            objectName: "lyricsRouteAttemptRepeater"
            model: root.service ? root.service.routeAttempts : []
            delegate: Text {
                objectName: "lyricsRouteAttemptText"
                width: routeAttemptsColumn.width
                color: Theme.textSecondary
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeCaption
                elide: Text.ElideRight
                maximumLineCount: 1
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
        LyricsActionButton {
            objectName: "lyricsOffsetEarlierButton"
            icon.source: Theme.icon("subtract-line")
            Accessible.name: qsTr("歌词提前 100 毫秒")
            onClicked: if (root.service) root.service.offsetMs -= 100
        }
        LyricsActionButton {
            objectName: "lyricsOffsetLaterButton"
            icon.source: Theme.icon("add-line")
            Accessible.name: qsTr("歌词延后 100 毫秒")
            onClicked: if (root.service) root.service.offsetMs += 100
        }
        LyricsActionButton {
            objectName: "lyricsRetryButton"
            icon.source: Theme.icon("restore-line")
            Accessible.name: qsTr("刷新歌词")
            onClicked: if (root.service) root.service.retry()
        }
        LyricsActionButton {
            objectName: "lyricsImportButton"
            icon.source: Theme.icon("folder-open-line")
            Accessible.name: qsTr("导入 LRC")
            onClicked: lrcDialog.open()
        }
    }

    Row {
        objectName: "lyricsFontSizeControl"
        visible: !root.spatialMode && root.chromeVisible
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 10
        anchors.bottomMargin: 6
        spacing: 6
        z: 5

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("字号")
            color: Theme.textSecondary
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeCaption
        }
        ThemedSlider {
            id: lyricsFontSizeSlider
            objectName: "lyricsFontSizeSlider"
            width: 104
            height: 28
            from: 60
            to: 140
            stepSize: 1
            value: PlayerExperienceController.lyricSize
            onMoved: PlayerExperienceController.lyricSize = Math.round(value)
            Accessible.name: qsTr("歌词字号")
        }
    }

    FileDialog {
        id: lrcDialog
        title: qsTr("导入歌词")
        nameFilters: [qsTr("LRC 歌词 (*.lrc)"), qsTr("文本文件 (*.txt)")]
        onAccepted: root.importSelectedFile(selectedFile)
    }
}

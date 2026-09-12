import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import AgPlayer

Window {
    id: window
    objectName: "equalizerWindow"
    visible: false
    width: 1080
    height: 620
    minimumWidth: 960
    minimumHeight: 460
    flags: Qt.FramelessWindowHint
    color: "transparent"
    title: qsTr("18 段图形均衡器")
    property int gainRevision: 0
    property int statusRefreshRevision: 0
    property real testDisplayOutputPeakDb: NaN
    readonly property real displayedOutputPeakDb:
        isNaN(testDisplayOutputPeakDb) ? EqualizerController.outputPeakDb
                                       : testDisplayOutputPeakDb
    readonly property bool spacious: width >= 1400 && height >= 800
    readonly property bool compactToolbar: width < 1250
    readonly property var bandFrequencies: [20, 31.5, 50, 80, 125, 200, 315,
                                            500, 800, 1250, 2000, 3150, 5000,
                                            8000, 10000, 12500, 16000, 20000]
    readonly property var bandLabels: ["20", "31.5", "50", "80", "125",
                                       "200", "315", "500", "800", "1.25k",
                                       "2k", "3.15k", "5k", "8k", "10k",
                                       "12.5k", "16k", "20k"]
    palette.window: Theme.background
    palette.windowText: Theme.primaryText
    palette.base: Theme.surfaceElevated
    palette.text: Theme.primaryText
    palette.button: Theme.surfaceElevated
    palette.buttonText: Theme.primaryText
    palette.highlight: Theme.highlight
    palette.highlightedText: Theme.highlightText

    component ToolbarButton: Button {
        property url iconSource: ""
        implicitHeight: Theme.controlHeightProminent
        implicitWidth: window.compactToolbar ? 96 : 122
        font.family: Theme.fontPrimary
        font.pixelSize: Theme.fontSizeBody
        font.weight: Font.Medium
        icon.source: iconSource
        icon.color: Theme.textPrimary
        icon.width: Theme.iconSizeMd
        icon.height: Theme.iconSizeMd
        spacing: 8
        display: AbstractButton.TextBesideIcon
        palette.buttonText: Theme.textPrimary
        background: Rectangle {
            radius: Theme.radiusSm
            color: parent.down ? Theme.surfacePressed
                               : parent.hovered ? Theme.surfaceHover
                                                : Theme.surfaceElevated
            border.color: parent.activeFocus ? Theme.focus : Theme.borderStrong
            border.width: parent.activeFocus ? 2 : 1
        }
    }

    component SegmentButton: Button {
        property bool selected: false
        implicitHeight: window.compactToolbar ? 32 : 38
        padding: 0
        font.family: Theme.fontPrimary
        font.pixelSize: Theme.fontSizeBody
        font.weight: Font.Normal
        palette.buttonText: selected ? Theme.highlightText : Theme.textSecondary
        background: Rectangle {
            radius: Theme.radiusSm
            color: parent.selected ? Theme.highlight
                                   : parent.down ? Theme.surfacePressed
                                                 : "transparent"
            border.color: parent.selected ? Theme.highlightBorder : "transparent"
        }
    }

    function openEqualizer() {
        EqualizerController.refreshStatus()
        WindowController.presentAuxiliaryWindow(window)
    }

    function meterColor(index) {
        if (index < 5)
            return "#3AAA65" // theme-color-allow: fixed equalizer meter scale
        if (index < 9)
            return "#88C32E" // theme-color-allow: fixed equalizer meter scale
        if (index < 13)
            return "#C3AB33" // theme-color-allow: fixed equalizer meter scale
        if (index < 16)
            return "#D4A03C" // theme-color-allow: fixed equalizer meter scale
        return "#7F3431" // theme-color-allow: fixed equalizer meter scale
    }

    function meterNormalizedPosition(db) {
        var boundaries = [-24, -12, -6, -3, 0]
        var clamped = Math.max(boundaries[0],
                               Math.min(boundaries[boundaries.length - 1], db))
        for (var segment = 0; segment < boundaries.length - 1; ++segment) {
            if (clamped <= boundaries[segment + 1]) {
                var withinSegment = (clamped - boundaries[segment])
                                    / (boundaries[segment + 1]
                                       - boundaries[segment])
                return (segment + withinSegment)
                       / (boundaries.length - 1)
            }
        }
        return 1
    }

    function meterPositionForDb(db, span) {
        return meterNormalizedPosition(db) * span
    }

    function meterActiveBlockCount(db) {
        return Math.ceil(meterNormalizedPosition(db) * 18)
    }

    function meterBlockActive(index) {
        return index < meterActiveBlockCount(displayedOutputPeakDb)
    }

    onClosing: function(close) {
        close.accepted = false
        hide()
    }

    Connections {
        target: EqualizerController
        function onResponseCurveChanged() { ++window.gainRevision }
        function onBandGainChanged() { ++window.gainRevision }
    }

    Timer {
        objectName: "equalizerStatusRefreshTimer"
        interval: 33
        repeat: true
        running: window.visible
        onTriggered: {
            EqualizerController.refreshStatus()
            ++window.statusRefreshRevision
        }
    }

    Rectangle {
        objectName: "equalizerBackdrop"
        anchors.fill: parent
        color: Theme.background
    }

    Rectangle {
        id: frame
        objectName: "equalizerFrame"
        x: 5
        y: 6
        width: Math.max(0, window.width - 10)
        height: Math.max(0, window.height - 11)
        radius: window.visibility === Window.Maximized ? 0 : 8
        clip: true
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0; color: Theme.background }
            GradientStop { position: 1; color: Theme.surface }
        }
        border.color: Theme.opaqueBorder
        border.width: 1

        Item {
            id: equalizerContent
            objectName: "equalizerContent"
            anchors.fill: parent

            Item {
                id: titleBar
                objectName: "equalizerTitleBar"
                x: 0
                y: 0
                width: parent.width
                height: Theme.navigationActionExtent + Theme.spacingLg

                DragHandler {
                    target: null
                    onActiveChanged: if (active) window.startSystemMove()
                }

                Label {
                    id: equalizerTitle
                    objectName: "equalizerTitle"
                    anchors.centerIn: parent
                    text: qsTr("18 段图形均衡器")
                    color: Theme.textPrimary
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizePageTitle
                    font.weight: Font.Normal
                    renderType: Text.NativeRendering
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 15
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    ToolButton {
                        objectName: "equalizerMinimizeButton"
                        width: Theme.navigationActionExtent
                        height: Theme.navigationActionExtent
                        flat: true
                        icon.source: Theme.icon("subtract-line")
                        icon.color: Theme.textPrimary
                        icon.width: 18
                        icon.height: 18
                        Accessible.name: qsTr("最小化")
                        onClicked: window.showMinimized()
                        background: null
                    }
                    ToolButton {
                        objectName: "equalizerMaximizeButton"
                        width: Theme.navigationActionExtent
                        height: Theme.navigationActionExtent
                        flat: true
                        icon.source: Theme.icon("checkbox-blank-line")
                        icon.color: Theme.textPrimary
                        icon.width: 18
                        icon.height: 18
                        Accessible.name: qsTr("最大化")
                        onClicked: window.visibility === Window.Maximized
                                   ? window.showNormal() : window.showMaximized()
                        background: null
                    }
                    ToolButton {
                        objectName: "equalizerCloseButton"
                        width: Theme.navigationActionExtent
                        height: Theme.navigationActionExtent
                        flat: true
                        icon.source: Theme.icon("close-fill")
                        icon.color: Theme.textPrimary
                        icon.width: 18
                        icon.height: 18
                        Accessible.name: qsTr("关闭")
                        onClicked: window.hide()
                        background: Rectangle {
                            radius: 6
                            color: parent.hovered ? Theme.surfaceHover : "transparent"
                        }
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.opaqueDivider
                }
            }

            Flickable {
                id: contentScroller
                objectName: "equalizerContentScroller"
                x: 0
                y: titleBar.height
                width: parent.width
                height: parent.height - titleBar.height
                contentWidth: width
                contentHeight: body.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height
                ScrollBar.vertical: ScrollBar {
                    objectName: "equalizerContentScrollBar"
                    policy: contentScroller.contentHeight > contentScroller.height
                            ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                }

                Item {
                    id: body
                    width: contentScroller.width
                    height: window.spacious ? 857 : contentScroller.height

                    Rectangle {
                        id: headerPanel
                        objectName: "equalizerHeaderPanel"
                        x: 0
                        y: 0
                        width: parent.width
                        height: window.spacious ? 72 : 48
                        gradient: Gradient {
                            orientation: Gradient.Vertical
                            GradientStop { position: 0; color: Theme.surface }
                            GradientStop { position: 1; color: Theme.surfaceElevated }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: window.compactToolbar ? 12 : 30
                            anchors.rightMargin: window.compactToolbar ? 12 : 29
                            spacing: window.compactToolbar ? 6 : 14

                            ThemedSwitch {
                                objectName: "equalizerEnabledSwitch"
                                checked: EqualizerController.enabled
                                text: qsTr("启用")
                                indicatorWidth: window.compactToolbar ? 44 : 56
                                indicatorHeight: window.compactToolbar ? 26 : 34
                                labelPixelSize: Theme.fontSizeBody
                                Accessible.name: qsTr("启用均衡器")
                                onToggled: EqualizerController.enabled = checked
                            }
                            Rectangle {
                                objectName: "equalizerHeaderDivider"
                                Layout.leftMargin: window.compactToolbar ? 2 : 28
                                Layout.rightMargin: window.compactToolbar ? 2 : 18
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: window.compactToolbar ? 32 : 42
                                color: Theme.opaqueDivider
                                opacity: 0.65
                            }
                            Label {
                                text: qsTr("预设：")
                                color: Theme.textPrimary
                                font.family: Theme.fontPrimary
                                font.pixelSize: Theme.fontSizeBody
                            }
                            ThemedComboBox {
                                id: presetBox
                                objectName: "equalizerPresetBox"
                                Layout.preferredWidth: window.compactToolbar ? 150 : 239
                                Layout.preferredHeight: window.compactToolbar ? 36 : 44
                                model: EqualizerController.presetNames
                                currentIndex: EqualizerController.presetIds.indexOf(
                                                  EqualizerController.currentPresetId)
                                displayText: EqualizerController.currentPresetId
                                             === "custom" ? qsTr("自定义")
                                                          : currentText
                                Accessible.name: qsTr("均衡器预设")
                                onActivated: function(index) {
                                    EqualizerController.applyPreset(
                                                EqualizerController.presetIds[index])
                                }
                            }
                            Item { Layout.fillWidth: true }
                            ToolbarButton {
                                objectName: "equalizerSaveButton"
                                implicitWidth: window.compactToolbar ? 105 : 157
                                text: qsTr("保存预设")
                                iconSource: Theme.icon("save-3-line")
                                Accessible.name: qsTr("保存自定义预设")
                                onClicked: saveDialog.open()
                            }
                            ToolbarButton {
                                objectName: "equalizerManageButton"
                                implicitWidth: window.compactToolbar ? 110 : 162
                                text: qsTr("管理预设")
                                iconSource: Theme.icon("list-unordered")
                                Accessible.name: qsTr("管理自定义预设")
                                onClicked: managePopup.open()
                            }
                            ToolbarButton {
                                objectName: "equalizerResetButton"
                                implicitWidth: window.compactToolbar ? 84 : 122
                                text: qsTr("重置")
                                iconSource: Theme.icon("restore-line")
                                Accessible.name: qsTr("全部归零")
                                onClicked: EqualizerController.resetAll()
                            }
                        }
                    }

                    Rectangle {
                        id: responsePanel
                        objectName: "equalizerResponsePanel"
                        x: window.spacious ? 22 : 8
                        y: headerPanel.height
                        width: parent.width - x * 2
                        height: window.spacious ? 291
                                                 : Math.max(104, Math.min(120,
                                                     body.height * 0.27))
                        radius: window.spacious ? 10 : 8
                        gradient: Gradient {
                            orientation: Gradient.Vertical
                            GradientStop { position: 0; color: Theme.surfaceElevated }
                            GradientStop { position: 1; color: Theme.surface }
                        }
                        border.color: Theme.opaqueBorder

                        EqualizerResponseCurve {
                            anchors.fill: parent
                            gainRevision: window.gainRevision
                        }

                        Label {
                            objectName: "equalizerResponsePreviewLabel"
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 12
                            text: EqualizerController.sampleRate > 0
                                  && !EqualizerController.sampleRateSupported
                                  ? qsTr("当前 %1 Hz 不支持均衡器")
                                    .arg(EqualizerController.sampleRate)
                                  : EqualizerController.sampleRate > 0
                                    ? qsTr("实际输出 · %1 Hz")
                                      .arg(EqualizerController.sampleRate)
                                    : qsTr("48 kHz 设计预览")
                            color: EqualizerController.sampleRate > 0
                                   && !EqualizerController.sampleRateSupported
                                   ? Theme.warning : Theme.textSecondary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeCaption
                        }
                    }

                    Rectangle {
                        id: bandsPanel
                        objectName: "equalizerBandsPanel"
                        x: window.spacious ? 22 : 8
                        y: responsePanel.y + responsePanel.height
                           + (window.spacious ? 14 : 0)
                        width: parent.width - x * 2
                        height: window.spacious ? 375 : footerPanel.y - y
                        radius: window.spacious ? 10 : 8
                        clip: true
                        gradient: Gradient {
                            orientation: Gradient.Vertical
                            GradientStop { position: 0; color: Theme.surfaceElevated }
                            GradientStop { position: 1; color: Theme.surface }
                        }
                        border.color: Theme.opaqueBorder

                        Flickable {
                            id: bandFlickable
                            objectName: "equalizerBandScroller"
                            anchors.fill: parent
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            contentWidth: window.spacious ? Math.max(width, 1510)
                                                          : Math.max(width, 1028)
                            contentHeight: height
                            ScrollBar.horizontal: ScrollBar {
                                objectName: "equalizerBandScrollBar"
                                policy: bandFlickable.contentWidth > bandFlickable.width
                                        ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                            }

                            Item {
                                width: bandFlickable.contentWidth
                                height: bandFlickable.height

                                Row {
                                    id: bandRow
                                    x: window.spacious ? 30 : 26
                                    y: 0
                                    height: bandFlickable.height
                                    spacing: 0

                                    Repeater {
                                        id: bandRepeater
                                        objectName: "equalizerBandRepeater"
                                        model: 18

                                        EqualizerBandSlider {
                                            required property int index
                                            width: window.spacious ? 72 : 52
                                            height: bandFlickable.height
                                            bandIndex: index
                                            frequencyLabel: window.bandLabels[index]
                                            gainDb: {
                                                window.gainRevision
                                                return EqualizerController.bandGain(index)
                                            }
                                            spacious: window.spacious
                                            accessibleLabel: window.bandFrequencies[index]
                                                             + " Hz"
                                        }
                                    }

                                    Item { width: window.spacious ? 16 : 6; height: 1 }
                                    Rectangle {
                                        width: 1
                                        height: window.spacious ? 341
                                                                : Math.max(0,
                                                                    bandFlickable.height - 24)
                                        y: window.spacious ? 16 : 12
                                        color: Theme.opaqueDivider
                                    }
                                    Item { width: window.spacious ? 16 : 7; height: 1 }

                                    EqualizerBandSlider {
                                        width: window.spacious ? 72 : 52
                                        height: bandFlickable.height
                                        bandIndex: -1
                                        frequencyLabel: qsTr("前级")
                                        accessibleLabel: qsTr("前级增益")
                                        gainDb: EqualizerController.preampDb
                                        preamp: true
                                        spacious: window.spacious
                                    }
                                }
                            }
                        }

                        Label {
                            objectName: "equalizerBandsMaxLabel"
                            x: 4
                            y: window.spacious ? 38 : 21
                            width: 34
                            height: 22
                            z: 2
                            text: "+" + EqualizerController.gainRangeDb.toFixed(0)
                            color: Theme.textSecondary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeBody
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        Label {
                            objectName: "equalizerBandsZeroLabel"
                            x: 4
                            y: window.spacious ? 157
                                               : 32 + Math.max(86,
                                                   bandsPanel.height - 92) / 2 - 11
                            width: 34
                            height: 22
                            z: 2
                            text: "0"
                            color: Theme.textSecondary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeBody
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        Label {
                            objectName: "equalizerBandsMinLabel"
                            x: 4
                            y: window.spacious ? 276
                                               : 32 + Math.max(86,
                                                   bandsPanel.height - 92) - 11
                            width: 34
                            height: 22
                            z: 2
                            text: "−" + EqualizerController.gainRangeDb.toFixed(0)
                            color: Theme.textSecondary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeBody
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Rectangle {
                        id: footerPanel
                        objectName: "equalizerFooterPanel"
                        x: 0
                        y: body.height - height
                        width: parent.width
                        height: window.spacious ? 77 : 64
                        color: Theme.surface
                        border.color: Theme.opaqueBorder
                        border.width: 1

                        Flickable {
                            id: footerScroller
                            objectName: "equalizerFooterScroller"
                            anchors.fill: parent
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            contentWidth: window.spacious ? Math.max(width, 1662)
                                                          : Math.max(width, 816)
                            contentHeight: height
                            ScrollBar.horizontal: ScrollBar {
                                objectName: "equalizerFooterScrollBar"
                                policy: footerScroller.contentWidth > footerScroller.width
                                        ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                            }

                            Item {
                                width: footerScroller.contentWidth
                                height: footerScroller.height

                                Label {
                                    x: window.spacious ? 40 : 16
                                    y: window.spacious ? 24 : 11
                                    width: window.spacious ? contentWidth : 42
                                    height: 42
                                    text: qsTr("范围：")
                                    color: Theme.textPrimary
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: Theme.fontSizeBody
                                    verticalAlignment: Text.AlignVCenter
                                }

                                Rectangle {
                                    id: rangeControl
                                    objectName: "equalizerRangeControl"
                                    x: window.spacious ? 105 : 62
                                    y: window.spacious ? 15 : 11
                                    width: window.spacious ? 332 : 216
                                    height: 42
                                    radius: 12
                                    color: Theme.surfaceElevated
                                    border.color: Theme.borderStrong

                                    Row {
                                        anchors.fill: parent
                                        SegmentButton {
                                            objectName: "equalizerRange6Button"
                                            width: window.spacious ? 109 : 72
                                            height: parent.height
                                            text: qsTr("±6 dB")
                                            selected: EqualizerController.gainRangeDb === 6
                                            onClicked: EqualizerController.setGainRangeDb(6)
                                        }
                                        SegmentButton {
                                            objectName: "equalizerRange12Button"
                                            width: window.spacious ? 110 : 72
                                            height: parent.height
                                            text: qsTr("±12 dB")
                                            selected: EqualizerController.gainRangeDb === 12
                                            onClicked: EqualizerController.setGainRangeDb(12)
                                        }
                                        SegmentButton {
                                            objectName: "equalizerRange18Button"
                                            width: window.spacious ? 112 : 72
                                            height: parent.height
                                            text: qsTr("±18 dB")
                                            selected: EqualizerController.gainRangeDb === 18
                                            onClicked: EqualizerController.setGainRangeDb(18)
                                        }
                                    }
                                }

                                Label {
                                    x: window.spacious ? 557 : 294
                                    y: window.spacious ? 24 : 11
                                    width: window.spacious ? contentWidth : 42
                                    height: 42
                                    text: qsTr("精度：")
                                    color: Theme.textPrimary
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: Theme.fontSizeBody
                                    verticalAlignment: Text.AlignVCenter
                                }

                                Rectangle {
                                    id: precisionControl
                                    objectName: "equalizerPrecisionControl"
                                    x: window.spacious ? 626 : 345
                                    y: window.spacious ? 15 : 11
                                    width: window.spacious ? 243 : 150
                                    height: 42
                                    radius: 12
                                    color: Theme.surfaceElevated
                                    border.color: Theme.borderStrong

                                    Row {
                                        anchors.fill: parent
                                        SegmentButton {
                                            objectName: "equalizerPrecisionHighButton"
                                            width: window.spacious ? 81 : 50
                                            height: parent.height
                                            text: qsTr("高")
                                            selected: EqualizerController.precisionMode
                                                      === "high"
                                            onClicked: EqualizerController.setPrecisionMode(
                                                           "high")
                                        }
                                        SegmentButton {
                                            objectName: "equalizerPrecisionMediumButton"
                                            width: window.spacious ? 81 : 50
                                            height: parent.height
                                            text: qsTr("中")
                                            selected: EqualizerController.precisionMode
                                                      === "medium"
                                            onClicked: EqualizerController.setPrecisionMode(
                                                           "medium")
                                        }
                                        SegmentButton {
                                            objectName: "equalizerPrecisionLowButton"
                                            width: window.spacious ? 81 : 50
                                            height: parent.height
                                            text: qsTr("低")
                                            selected: EqualizerController.precisionMode
                                                      === "low"
                                            onClicked: EqualizerController.setPrecisionMode(
                                                           "low")
                                        }
                                    }
                                }

                                Label {
                                    x: window.spacious ? 1011 : 510
                                    y: window.spacious ? 24 : 11
                                    width: window.spacious ? contentWidth : 70
                                    height: 42
                                    text: qsTr("输出电平：")
                                    color: Theme.textPrimary
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: Theme.fontSizeBody
                                    verticalAlignment: Text.AlignVCenter
                                }

                                Item {
                                    id: outputMeter
                                    objectName: "equalizerOutputMeter"
                                    x: window.spacious ? 1137 : 586
                                    y: window.spacious ? 25 : 15
                                    width: window.spacious ? 407 : 150
                                    height: 32

                                    Row {
                                        x: 0
                                        y: 0
                                        width: parent.width
                                        height: 10
                                        spacing: 4

                                        Repeater {
                                            model: 18
                                            Rectangle {
                                                required property int index
                                                width: (outputMeter.width - 17 * 4) / 18
                                                height: 10
                                                radius: 1
                                                color: window.meterColor(index)
                                                opacity: window.meterBlockActive(index)
                                                         ? 1.0 : 0.28
                                            }
                                        }
                                    }

                                    Repeater {
                                        model: [{"db": -24, "text": "-24"},
                                                {"db": -12, "text": "-12"},
                                                {"db": -6, "text": "-6"},
                                                {"db": -3, "text": "-3"},
                                                {"db": 0, "text": "0"}]
                                        Item {
                                            required property int index
                                            required property var modelData
                                            objectName: "equalizerMeterTick-" + index
                                            x: window.meterPositionForDb(modelData.db,
                                                                         outputMeter.width)
                                            y: 13
                                            width: 0
                                            height: 19

                                            Label {
                                                x: index === 0 ? 0
                                                   : index === 4 ? -width
                                                   : -width / 2
                                                text: modelData.text
                                                color: Theme.textSecondary
                                                font.family: Theme.fontPrimary
                                                font.pixelSize: Theme.fontSizeBody
                                            }
                                        }
                                    }
                                }

                                Label {
                                    objectName: "equalizerOutputLevelText"
                                    x: window.spacious ? 1565 : 744
                                    y: window.spacious ? 24 : 11
                                    width: window.spacious ? 78 : 70
                                    height: 42
                                    text: window.displayedOutputPeakDb.toFixed(1) + " dB"
                                    color: Theme.textPrimary
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: Theme.fontSizeBody
                                    horizontalAlignment: Text.AlignRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    ThemedDialog {
        id: saveDialog
        objectName: "equalizerSaveDialog"
        modal: true
        width: 380
        height: 205
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: Theme.surfaceElevated
            radius: 16
            border.color: Theme.borderStrong
        }
        contentItem: ColumnLayout {
            spacing: 14
            Label {
                text: qsTr("保存自定义预设")
                color: Theme.textPrimary
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizePageTitle
                font.weight: Font.Medium
            }
            ThemedTextField {
                id: saveName
                objectName: "equalizerSaveNameField"
                Layout.fillWidth: true
                placeholderText: qsTr("预设名称")
                selectByMouse: true
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ToolbarButton {
                    objectName: "equalizerSaveCancelButton"
                    implicitWidth: 90
                    implicitHeight: 40
                    text: qsTr("取消")
                    onClicked: saveDialog.close()
                }
                ToolbarButton {
                    objectName: "equalizerSaveConfirmButton"
                    implicitWidth: 90
                    implicitHeight: 40
                    text: qsTr("保存")
                    enabled: saveName.text.trim().length > 0
                    onClicked: {
                        EqualizerController.saveCustomPreset(saveName.text)
                        saveName.clear()
                        saveDialog.close()
                    }
                }
            }
        }
        onOpened: saveName.forceActiveFocus()
    }

    Popup {
        id: managePopup
        objectName: "equalizerManagePopup"
        x: Math.round((window.width - width) / 2)
        y: 94
        width: Math.min(640, window.width - 48)
        height: 390
        padding: 18
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.surfaceElevated
            radius: 16
            border.color: Theme.borderStrong
        }

        contentItem: ColumnLayout {
            id: manageColumn
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("管理自定义预设")
                    color: Theme.textPrimary
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizePageTitle
                    font.weight: Font.Medium
                }
                Item { Layout.fillWidth: true }
                ToolButton {
                    objectName: "equalizerManageCloseButton"
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    icon.source: Theme.icon("close-fill")
                    icon.color: Theme.textPrimary
                    onClicked: managePopup.close()
                }
            }

            ThemedComboBox {
                id: customPresetBox
                objectName: "equalizerManagePresetBox"
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: EqualizerController.presetIds
                    .filter(function(id) { return id.indexOf("custom-") === 0 })
                    .map(function(id) {
                        var ids = EqualizerController.presetIds
                        var names = EqualizerController.presetNames
                        return {"value": id, "text": names[ids.indexOf(id)]}
                    })
            }

            TextField {
                id: renameField
                objectName: "equalizerRenameField"
                Layout.fillWidth: true
                placeholderText: qsTr("新名称")
                selectByMouse: true
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ToolbarButton {
                    objectName: "equalizerRenameButton"
                    implicitWidth: 104
                    implicitHeight: 40
                    text: qsTr("重命名")
                    enabled: customPresetBox.currentValue !== undefined
                             && renameField.text.trim().length > 0
                    onClicked: EqualizerController.renameCustomPreset(
                                   customPresetBox.currentValue,
                                   renameField.text)
                }
                ToolbarButton {
                    objectName: "equalizerDeleteButton"
                    implicitWidth: 90
                    implicitHeight: 40
                    text: qsTr("删除")
                    enabled: customPresetBox.currentValue !== undefined
                    onClicked: EqualizerController.deleteCustomPreset(
                                   customPresetBox.currentValue)
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.borderStrong
            }

            Label {
                text: qsTr("高级")
                color: Theme.textSecondary
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeBody
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                ToolbarButton {
                    objectName: "equalizerBypassButton"
                    implicitWidth: 125
                    implicitHeight: 42
                    text: EqualizerController.bypassed ? qsTr("取消旁路")
                                                       : qsTr("旁路")
                    checkable: true
                    checked: EqualizerController.bypassed
                    Accessible.name: qsTr("旁路均衡器")
                    onToggled: EqualizerController.bypassed = checked
                }
                ThemedSwitch {
                    objectName: "equalizerAutoProtection"
                    checked: EqualizerController.autoClipProtection
                    text: qsTr("自动防削波")
                    indicatorWidth: 46
                    indicatorHeight: 28
                    labelPixelSize: Theme.fontSizeBody
                    Accessible.name: qsTr("自动防削波")
                    onToggled: EqualizerController.autoClipProtection = checked
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: EqualizerController.protectionDb < -0.05
                          ? qsTr("保护中") : qsTr("无需衰减")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                }
                Label {
                    text: EqualizerController.protectionDb.toFixed(1) + " dB"
                    color: EqualizerController.protectionDb < -0.05
                           ? "#B15DED" : Theme.textSecondary // theme-color-allow: equalizer band identity
                    font.pixelSize: Theme.fontSizeBody
                }
            }
        }
    }

    WindowResizeHandles { targetWindow: window }
}

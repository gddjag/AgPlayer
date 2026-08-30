import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import AgPlayer

Window {
    id: window
    objectName: "equalizerWindow"
    visible: false
    width: 1000
    height: 600
    minimumWidth: 880
    minimumHeight: 520
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
        implicitHeight: 48
        implicitWidth: 122
        font.family: "Microsoft YaHei UI"
        font.pixelSize: window.compactToolbar ? 15 : 19
        font.weight: Font.Medium
        icon.source: iconSource
        icon.color: "#EFF3F7"
        icon.width: 22
        icon.height: 22
        spacing: 8
        display: AbstractButton.TextBesideIcon
        palette.buttonText: "#EFF3F7"
        background: Rectangle {
            radius: 15
            color: parent.down ? "#273139"
                               : parent.hovered ? "#202930" : "#171E24"
            border.color: parent.activeFocus ? Theme.focus : "#66717A"
            border.width: parent.activeFocus ? 2 : 1
        }
    }

    component SegmentButton: Button {
        property bool selected: false
        implicitHeight: 42
        padding: 0
        font.family: "Microsoft YaHei UI"
        font.pixelSize: 18
        font.weight: Font.Normal
        palette.buttonText: selected ? "#FFFFFF" : "#D9DEE2"
        background: Rectangle {
            radius: 11
            color: parent.selected ? "#0B60C8"
                                   : parent.down ? "#273139" : "transparent"
            border.color: parent.selected ? "#167AE0" : "transparent"
        }
    }

    function openEqualizer() {
        EqualizerController.refreshStatus()
        WindowController.presentAuxiliaryWindow(window)
    }

    function meterColor(index) {
        if (index < 5)
            return "#3AAA65"
        if (index < 9)
            return "#88C32E"
        if (index < 13)
            return "#C3AB33"
        if (index < 16)
            return "#D4A03C"
        return "#7F3431"
    }

    function meterBlockActive(index) {
        var clamped = Math.max(-24, Math.min(0, displayedOutputPeakDb))
        return index < Math.ceil((clamped + 24) / 24 * 18)
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
        anchors.fill: parent
        color: "#000000"
    }

    Rectangle {
        id: frame
        x: 5
        y: 6
        width: Math.max(0, window.width - 10)
        height: Math.max(0, window.height - 11)
        radius: window.visibility === Window.Maximized ? 0 : 29
        clip: true
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0; color: "#090D10" }
            GradientStop { position: 1; color: "#10171B" }
        }
        border.color: "#11191E"
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
                height: 72

                DragHandler {
                    target: null
                    onActiveChanged: if (active) window.startSystemMove()
                }

                Label {
                    id: equalizerTitle
                    objectName: "equalizerTitle"
                    anchors.centerIn: parent
                    text: qsTr("18 段图形均衡器")
                    color: "#EFF3F7"
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 28
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
                        width: 30
                        height: 30
                        flat: true
                        icon.source: Theme.icon("subtract-line")
                        icon.color: "#EFF3F7"
                        Accessible.name: qsTr("最小化")
                        onClicked: window.showMinimized()
                        background: null
                    }
                    ToolButton {
                        objectName: "equalizerMaximizeButton"
                        width: 30
                        height: 30
                        flat: true
                        icon.source: Theme.icon("checkbox-blank-line")
                        icon.color: "#EFF3F7"
                        Accessible.name: qsTr("最大化")
                        onClicked: window.visibility === Window.Maximized
                                   ? window.showNormal() : window.showMaximized()
                        background: null
                    }
                    ToolButton {
                        objectName: "equalizerCloseButton"
                        width: 30
                        height: 30
                        flat: true
                        icon.source: Theme.icon("close-fill")
                        icon.color: "#EFF3F7"
                        Accessible.name: qsTr("关闭")
                        onClicked: window.hide()
                        background: Rectangle {
                            radius: 6
                            color: parent.hovered ? "#273139" : "transparent"
                        }
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: "#181D23"
                }
            }

            Flickable {
                id: contentScroller
                objectName: "equalizerContentScroller"
                x: 0
                y: 72
                width: parent.width
                height: parent.height - 72
                contentWidth: width
                contentHeight: body.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height
                ScrollBar.vertical: ScrollBar {
                    policy: contentScroller.contentHeight > contentScroller.height
                            ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                }

                Item {
                    id: body
                    width: contentScroller.width
                    height: 857

                    Rectangle {
                        id: headerPanel
                        objectName: "equalizerHeaderPanel"
                        x: 0
                        y: 0
                        width: parent.width
                        height: 86
                        gradient: Gradient {
                            orientation: Gradient.Vertical
                            GradientStop { position: 0; color: "#11171C" }
                            GradientStop { position: 1; color: "#151D22" }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: window.compactToolbar ? 18 : 30
                            anchors.rightMargin: window.compactToolbar ? 18 : 24
                            spacing: window.compactToolbar ? 8 : 14

                            ThemedSwitch {
                                objectName: "equalizerEnabledSwitch"
                                checked: EqualizerController.enabled
                                text: qsTr("启用")
                                indicatorWidth: window.compactToolbar ? 54 : 67
                                indicatorHeight: window.compactToolbar ? 32 : 39
                                labelPixelSize: window.compactToolbar ? 15 : 19
                                Accessible.name: qsTr("启用均衡器")
                                onToggled: EqualizerController.enabled = checked
                            }
                            Rectangle {
                                Layout.leftMargin: window.compactToolbar ? 4 : 18
                                Layout.rightMargin: window.compactToolbar ? 4 : 18
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: 48
                                color: "#6F777D"
                                opacity: 0.65
                            }
                            Label {
                                text: qsTr("预设：")
                                color: "#EFF3F7"
                                font.family: "Microsoft YaHei UI"
                                font.pixelSize: window.compactToolbar ? 15 : 19
                            }
                            ThemedComboBox {
                                id: presetBox
                                objectName: "equalizerPresetBox"
                                Layout.preferredWidth: window.compactToolbar ? 170 : 239
                                Layout.preferredHeight: 48
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
                                implicitWidth: window.compactToolbar ? 116 : 157
                                text: qsTr("保存预设")
                                iconSource: Theme.icon("save-3-line")
                                Accessible.name: qsTr("保存自定义预设")
                                onClicked: saveDialog.open()
                            }
                            ToolbarButton {
                                objectName: "equalizerManageButton"
                                implicitWidth: window.compactToolbar ? 122 : 162
                                text: qsTr("管理预设")
                                iconSource: Theme.icon("list-unordered")
                                Accessible.name: qsTr("管理自定义预设")
                                onClicked: managePopup.open()
                            }
                            ToolbarButton {
                                objectName: "equalizerResetButton"
                                implicitWidth: window.compactToolbar ? 94 : 122
                                text: qsTr("重置")
                                iconSource: Theme.icon("arrow-go-back-line")
                                Accessible.name: qsTr("全部归零")
                                onClicked: EqualizerController.resetAll()
                            }
                        }
                    }

                    Rectangle {
                        id: responsePanel
                        objectName: "equalizerResponsePanel"
                        x: 22
                        y: 86
                        width: parent.width - 44
                        height: 291
                        radius: 20
                        gradient: Gradient {
                            orientation: Gradient.Vertical
                            GradientStop { position: 0; color: "#1A2228" }
                            GradientStop { position: 1; color: "#141B20" }
                        }
                        border.color: "#303940"

                        EqualizerResponseCurve {
                            anchors.fill: parent
                            gainRevision: window.gainRevision
                        }
                    }

                    Rectangle {
                        id: bandsPanel
                        objectName: "equalizerBandsPanel"
                        x: 22
                        y: 391
                        width: parent.width - 44
                        height: 375
                        radius: 20
                        clip: true
                        gradient: Gradient {
                            orientation: Gradient.Vertical
                            GradientStop { position: 0; color: "#182127" }
                            GradientStop { position: 1; color: "#11181C" }
                        }
                        border.color: "#303940"

                        Flickable {
                            id: bandFlickable
                            objectName: "equalizerBandScroller"
                            anchors.fill: parent
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            contentWidth: Math.max(width, 1616)
                            contentHeight: height
                            ScrollBar.horizontal: ScrollBar {
                                policy: bandFlickable.contentWidth > bandFlickable.width
                                        ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                            }

                            Item {
                                width: bandFlickable.contentWidth
                                height: bandFlickable.height

                                Row {
                                    id: bandRow
                                    x: 30
                                    y: 0
                                    height: 375
                                    spacing: 0

                                    Repeater {
                                        id: bandRepeater
                                        objectName: "equalizerBandRepeater"
                                        model: 18

                                        EqualizerBandSlider {
                                            required property int index
                                            width: 79
                                            height: 375
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

                                    Item { width: 22; height: 1 }
                                    Rectangle {
                                        width: 1
                                        height: 341
                                        y: 16
                                        color: "#6F777D"
                                    }
                                    Item { width: 23; height: 1 }

                                    EqualizerBandSlider {
                                        width: 79
                                        height: 375
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
                    }

                    Rectangle {
                        id: footerPanel
                        objectName: "equalizerFooterPanel"
                        x: 0
                        y: 780
                        width: parent.width
                        height: 77
                        color: "#11181C"
                        border.color: "#263039"
                        border.width: 1

                        Flickable {
                            id: footerScroller
                            objectName: "equalizerFooterScroller"
                            anchors.fill: parent
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            contentWidth: Math.max(width, 1615)
                            contentHeight: height
                            ScrollBar.horizontal: ScrollBar {
                                policy: footerScroller.contentWidth > footerScroller.width
                                        ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                            }

                            Item {
                                width: footerScroller.contentWidth
                                height: footerScroller.height

                                Label {
                                    x: 40
                                    y: 24
                                    height: 42
                                    text: qsTr("范围：")
                                    color: "#EFF3F7"
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: 18
                                    verticalAlignment: Text.AlignVCenter
                                }

                                Rectangle {
                                    id: rangeControl
                                    objectName: "equalizerRangeControl"
                                    x: 105
                                    y: 15
                                    width: 332
                                    height: 42
                                    radius: 12
                                    color: "#151D23"
                                    border.color: "#465159"

                                    Row {
                                        anchors.fill: parent
                                        SegmentButton {
                                            objectName: "equalizerRange6Button"
                                            width: 109
                                            height: parent.height
                                            text: qsTr("±6 dB")
                                            selected: EqualizerController.gainRangeDb === 6
                                            onClicked: EqualizerController.setGainRangeDb(6)
                                        }
                                        SegmentButton {
                                            objectName: "equalizerRange12Button"
                                            width: 110
                                            height: parent.height
                                            text: qsTr("±12 dB")
                                            selected: EqualizerController.gainRangeDb === 12
                                            onClicked: EqualizerController.setGainRangeDb(12)
                                        }
                                        SegmentButton {
                                            objectName: "equalizerRange18Button"
                                            width: 112
                                            height: parent.height
                                            text: qsTr("±18 dB")
                                            selected: EqualizerController.gainRangeDb === 18
                                            onClicked: EqualizerController.setGainRangeDb(18)
                                        }
                                    }
                                }

                                Label {
                                    x: 557
                                    y: 24
                                    height: 42
                                    text: qsTr("精度：")
                                    color: "#EFF3F7"
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: 18
                                    verticalAlignment: Text.AlignVCenter
                                }

                                Rectangle {
                                    id: precisionControl
                                    objectName: "equalizerPrecisionControl"
                                    x: 626
                                    y: 15
                                    width: 243
                                    height: 42
                                    radius: 12
                                    color: "#151D23"
                                    border.color: "#465159"

                                    Row {
                                        anchors.fill: parent
                                        SegmentButton {
                                            objectName: "equalizerPrecisionHighButton"
                                            width: 81
                                            height: parent.height
                                            text: qsTr("高")
                                            selected: EqualizerController.precisionMode
                                                      === "high"
                                            onClicked: EqualizerController.setPrecisionMode(
                                                           "high")
                                        }
                                        SegmentButton {
                                            objectName: "equalizerPrecisionMediumButton"
                                            width: 81
                                            height: parent.height
                                            text: qsTr("中")
                                            selected: EqualizerController.precisionMode
                                                      === "medium"
                                            onClicked: EqualizerController.setPrecisionMode(
                                                           "medium")
                                        }
                                        SegmentButton {
                                            objectName: "equalizerPrecisionLowButton"
                                            width: 81
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
                                    x: 1011
                                    y: 24
                                    height: 42
                                    text: qsTr("输出电平：")
                                    color: "#EFF3F7"
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: 18
                                    verticalAlignment: Text.AlignVCenter
                                }

                                Item {
                                    id: outputMeter
                                    objectName: "equalizerOutputMeter"
                                    x: 1137
                                    y: 25
                                    width: 407
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
                                                width: (407 - 17 * 4) / 18
                                                height: 10
                                                radius: 1
                                                color: window.meterColor(index)
                                                opacity: window.meterBlockActive(index)
                                                         ? 1.0 : 0.28
                                            }
                                        }
                                    }

                                    Repeater {
                                        model: [{"x": 0, "text": "-24"},
                                                {"x": 94, "text": "-12"},
                                                {"x": 196, "text": "-6"},
                                                {"x": 294, "text": "-3"},
                                                {"x": 382, "text": "0"}]
                                        Label {
                                            required property var modelData
                                            x: modelData.x
                                            y: 13
                                            text: modelData.text
                                            color: "#D6DCE1"
                                            font.family: "Microsoft YaHei UI"
                                            font.pixelSize: 14
                                        }
                                    }
                                }

                                Label {
                                    objectName: "equalizerOutputLevelText"
                                    x: 1565
                                    y: 24
                                    width: 78
                                    height: 42
                                    text: window.displayedOutputPeakDb.toFixed(1) + " dB"
                                    color: "#EFF3F7"
                                    font.family: "Microsoft YaHei UI"
                                    font.pixelSize: 18
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

    Dialog {
        id: saveDialog
        objectName: "equalizerSaveDialog"
        modal: true
        width: 380
        height: 205
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: "#182127"
            radius: 16
            border.color: "#465159"
        }
        contentItem: ColumnLayout {
            spacing: 14
            Label {
                text: qsTr("保存自定义预设")
                color: "#EFF3F7"
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 20
                font.weight: Font.Medium
            }
            TextField {
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
        width: 480
        height: 390
        padding: 18
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: "#182127"
            radius: 16
            border.color: "#465159"
        }

        contentItem: ColumnLayout {
            id: manageColumn
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("管理自定义预设")
                    color: "#EFF3F7"
                    font.family: "Microsoft YaHei UI"
                    font.pixelSize: 20
                    font.weight: Font.Medium
                }
                Item { Layout.fillWidth: true }
                ToolButton {
                    objectName: "equalizerManageCloseButton"
                    width: 32
                    height: 32
                    icon.source: Theme.icon("close-fill")
                    icon.color: "#EFF3F7"
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
                color: "#465159"
            }

            Label {
                text: qsTr("高级")
                color: "#AEB7BE"
                font.family: "Microsoft YaHei UI"
                font.pixelSize: 15
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
                    labelPixelSize: 15
                    Accessible.name: qsTr("自动防削波")
                    onToggled: EqualizerController.autoClipProtection = checked
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: EqualizerController.protectionDb < -0.05
                          ? qsTr("保护中") : qsTr("无需衰减")
                    color: "#AEB7BE"
                    font.pixelSize: 14
                }
                Label {
                    text: EqualizerController.protectionDb.toFixed(1) + " dB"
                    color: EqualizerController.protectionDb < -0.05
                           ? "#B15DED" : "#AEB7BE"
                    font.pixelSize: 14
                }
            }
        }
    }

    WindowResizeHandles { targetWindow: window }
}

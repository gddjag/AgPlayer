import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import AgPlayer

Window {
    id: window
    objectName: "equalizerWindow"
    visible: false
    width: 860
    height: 520
    minimumWidth: 760
    minimumHeight: 480
    flags: Qt.FramelessWindowHint
    color: "transparent"
    title: qsTr("十八段图形均衡器")
    property int gainRevision: 0
    readonly property bool spacious: width >= 1000 && height >= 600
    palette.window: Theme.background
    palette.windowText: Theme.primaryText
    palette.base: Theme.surfaceElevated
    palette.text: Theme.primaryText
    palette.button: Theme.surfaceElevated
    palette.buttonText: Theme.primaryText
    palette.highlight: Theme.highlight
    palette.highlightedText: Theme.highlightText

    component ToolbarButton: Button {
        implicitHeight: window.spacious ? 46 : 40
        implicitWidth: window.spacious ? 112 : 76
        font.family: Theme.fontPrimary
        font.pixelSize: window.spacious ? 17 : 14
        palette.buttonText: Theme.primaryText
        background: Rectangle {
            radius: 10
            color: parent.down ? Theme.surfacePressed
                               : parent.hovered ? Theme.surfaceHover
                                                : Theme.surfaceElevated
            border.color: parent.activeFocus ? Theme.focus : Theme.borderStrong
            border.width: parent.activeFocus ? 2 : 1
        }
    }

    function openEqualizer() {
        EqualizerController.refreshStatus()
        WindowController.presentAuxiliaryWindow(window)
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

    Rectangle {
        anchors.fill: parent
        radius: window.visibility === Window.Maximized ? 0 : Theme.windowRadius
        color: Theme.background
        border.color: Theme.border
        border.width: 1
    }

    ColumnLayout {
        id: equalizerContent
        objectName: "equalizerContent"
        x: 1
        y: 1
        width: window.width - 2
        height: window.height - 2
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: window.spacious ? 64 : 48

            DragHandler {
                target: null
                onActiveChanged: if (active) window.startSystemMove()
            }

            Label {
                id: equalizerTitle
                objectName: "equalizerTitle"
                anchors.centerIn: parent
                text: qsTr("十八段图形均衡器")
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: window.spacious ? 26 : 20
                font.weight: Font.Medium
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                ToolButton {
                    objectName: "equalizerMinimizeButton"
                    width: 30; height: 30; flat: true
                    icon.source: Theme.icon("subtract-line")
                    icon.color: Theme.iconPrimary
                    Accessible.name: qsTr("最小化")
                    onClicked: window.showMinimized()
                    background: null
                }
                ToolButton {
                    objectName: "equalizerMaximizeButton"
                    width: 30; height: 30; flat: true
                    icon.source: Theme.icon("checkbox-blank-line")
                    icon.color: Theme.iconPrimary
                    Accessible.name: qsTr("最大化")
                    onClicked: window.visibility === Window.Maximized
                               ? window.showNormal() : window.showMaximized()
                    background: null
                }
                ToolButton {
                    objectName: "equalizerCloseButton"
                    width: 30; height: 30; flat: true
                    icon.source: Theme.icon("close-fill")
                    icon.color: Theme.iconPrimary
                    Accessible.name: qsTr("关闭")
                    onClicked: window.hide()
                    background: Rectangle {
                        radius: 6
                        color: parent.hovered ? Theme.surfaceHover : "transparent"
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        Rectangle {
            objectName: "equalizerHeaderPanel"
            Layout.fillWidth: true
            Layout.preferredHeight: window.spacious ? 88 : 72
            color: Theme.surface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                spacing: 12

                ThemedSwitch {
                    objectName: "equalizerEnabledSwitch"
                    checked: EqualizerController.enabled
                    text: qsTr("启用")
                    indicatorWidth: window.spacious ? 56 : 38
                    indicatorHeight: window.spacious ? 32 : 20
                    labelPixelSize: window.spacious ? 18 : 14
                    Accessible.name: qsTr("启用均衡器")
                    onToggled: EqualizerController.enabled = checked
                }
                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 32
                    color: Theme.divider
                }
                Label {
                    text: qsTr("预设")
                    color: Theme.primaryText
                    font.pixelSize: window.spacious ? 18 : 14
                }
                ThemedComboBox {
                    id: presetBox
                    objectName: "equalizerPresetBox"
                    Layout.preferredWidth: window.spacious ? 250 : 190
                    Layout.preferredHeight: window.spacious ? 46 : 40
                    model: EqualizerController.presetNames
                    currentIndex: EqualizerController.presetIds.indexOf(
                                      EqualizerController.currentPresetId)
                    displayText: EqualizerController.currentPresetId === "custom"
                                 ? qsTr("Custom") : currentText
                    Accessible.name: qsTr("均衡器预设")
                    onActivated: EqualizerController.applyPreset(
                                     EqualizerController.presetIds[index])
                }
                Item { Layout.fillWidth: true }
                ToolbarButton {
                    objectName: "equalizerSaveButton"
                    text: qsTr("保存")
                    Accessible.name: qsTr("保存自定义预设")
                    onClicked: saveDialog.open()
                }
                ToolbarButton {
                    objectName: "equalizerManageButton"
                    text: qsTr("管理")
                    Accessible.name: qsTr("管理自定义预设")
                    onClicked: managePopup.open()
                }
                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 32
                    color: Theme.divider
                }
                ToolbarButton {
                    objectName: "equalizerBypassButton"
                    text: EqualizerController.bypassed ? qsTr("取消旁路") : qsTr("旁路")
                    checkable: true
                    checked: EqualizerController.bypassed
                    Accessible.name: qsTr("旁路均衡器")
                    onToggled: EqualizerController.bypassed = checked
                }
                ToolbarButton {
                    objectName: "equalizerResetButton"
                    implicitWidth: 94
                    text: qsTr("全部归零")
                    Accessible.name: qsTr("全部归零")
                    onClicked: EqualizerController.resetAll()
                }
            }
        }

        Rectangle {
            objectName: "equalizerResponsePanel"
            Layout.fillWidth: true
            Layout.preferredHeight: window.spacious ? 255
                                                    : Math.max(128, Math.min(182, window.height * 0.25))
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 12
            color: Theme.surface
            radius: 14
            border.color: Theme.border

            EqualizerResponseCurve {
                anchors.fill: parent
                anchors.margins: 8
            }
        }

        RowLayout {
            objectName: "equalizerBandsPanel"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 8
            Layout.bottomMargin: 8
            spacing: 4

            Item {
                Layout.preferredWidth: 36
                Layout.fillHeight: true
                Label { anchors.top: parent.top; text: "+12"; color: Theme.secondaryText; font.pixelSize: 11 }
                Label { anchors.verticalCenter: parent.verticalCenter; text: "0"; color: Theme.secondaryText; font.pixelSize: 11 }
                Label { anchors.bottom: parent.bottom; anchors.bottomMargin: 34; text: "−12"; color: Theme.secondaryText; font.pixelSize: 11 }
            }

            Flickable {
                id: bandFlickable
                objectName: "equalizerBandScroller"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: bandRow.width
                contentHeight: height
                interactive: contentWidth > width
                ScrollBar.horizontal: ScrollBar { policy: bandFlickable.contentWidth > bandFlickable.width ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

                Row {
                    id: bandRow
                    property real slotWidth: (width - 1) / 18
                    height: bandFlickable.height - 8
                    width: Math.max(bandFlickable.width,
                                    18 * (window.spacious ? 72 : 56) + 1)
                    spacing: 0

                    Repeater {
                        id: bandRepeater
                        objectName: "equalizerBandRepeater"
                        model: 17
                        EqualizerBandSlider {
                            required property int index
                            objectName: "equalizerBand-" + index
                            width: bandRow.slotWidth
                            height: bandRow.height
                            bandIndex: index
                            frequencyLabel: ["20", "31.5", "50", "80", "125", "200",
                                             "315", "500", "800", "1.25k", "2k", "3.15k",
                                             "5k", "8k", "12.5k", "16k", "20k"][index]
                            gainDb: {
                                window.gainRevision
                                return EqualizerController.bandGain(index)
                            }
                            spacious: window.spacious
                            accessibleLabel: ["20 Hz", "31.5 Hz", "50 Hz", "80 Hz",
                                              "125 Hz", "200 Hz", "315 Hz", "500 Hz",
                                              "800 Hz", "1.25 kHz", "2 kHz", "3.15 kHz",
                                              "5 kHz", "8 kHz", "12.5 kHz", "16 kHz",
                                              "20 kHz"][index]
                        }
                    }

                    Rectangle {
                        width: 1
                        height: parent.height - 40
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.divider
                    }

                    EqualizerBandSlider {
                        objectName: "equalizerBand-17"
                        width: bandRow.slotWidth
                        height: bandRow.height
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

        Rectangle {
            objectName: "equalizerFooterPanel"
            Layout.fillWidth: true
            Layout.preferredHeight: window.spacious ? 84 : 58
            color: Theme.surface
            border.color: Theme.divider
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                spacing: 16
                ThemedSwitch {
                    objectName: "equalizerAutoProtection"
                    checked: EqualizerController.autoClipProtection
                    text: qsTr("自动防削波")
                    indicatorWidth: window.spacious ? 56 : 38
                    indicatorHeight: window.spacious ? 32 : 20
                    labelPixelSize: window.spacious ? 18 : 14
                    Accessible.name: qsTr("自动防削波")
                    onToggled: EqualizerController.autoClipProtection = checked
                }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 28; color: Theme.divider }
                Label { text: qsTr("余量"); color: Theme.secondaryText; font.pixelSize: window.spacious ? 17 : 14 }
                Label { text: "0.5 dB"; color: Theme.waveformCyan; font.pixelSize: window.spacious ? 17 : 14 }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 28; color: Theme.divider }
                Label {
                    text: EqualizerController.protectionDb < -0.05 ? qsTr("保护中") : qsTr("无需衰减")
                    color: Theme.secondaryText
                    font.pixelSize: window.spacious ? 17 : 14
                }
                Label {
                    text: EqualizerController.protectionDb.toFixed(1) + " dB"
                    color: EqualizerController.protectionDb < -0.05
                           ? Theme.waveformViolet : Theme.secondaryText
                    font.pixelSize: window.spacious ? 17 : 14
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: qsTr("双击滑杆归零 · 滚轮或方向键微调")
                    color: Theme.secondaryText
                    font.pixelSize: window.spacious ? 16 : 13
                }
            }
        }
    }

    Dialog {
        id: saveDialog
        title: qsTr("保存自定义预设")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Save | Dialog.Cancel
        TextField {
            id: saveName
            width: 260
            placeholderText: qsTr("预设名称")
            selectByMouse: true
        }
        onAccepted: {
            EqualizerController.saveCustomPreset(saveName.text)
            saveName.clear()
        }
    }

    Popup {
        id: managePopup
        x: Math.round((window.width - width) / 2)
        y: 96
        width: 360
        padding: 16
        modal: true
        background: Rectangle {
            color: Theme.surfaceElevated
            radius: 14
            border.color: Theme.border
        }
        ColumnLayout {
            width: parent.width
            spacing: 10
            Label { text: qsTr("管理自定义预设"); color: Theme.primaryText; font.weight: Font.DemiBold }
            ThemedComboBox {
                id: customPresetBox
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: EqualizerController.presetIds
                    .filter(function(id) { return id.indexOf("custom-") === 0 })
                    .map(function(id) {
                        var ids = EqualizerController.presetIds
                        var names = EqualizerController.presetNames
                        return { "value": id, "text": names[ids.indexOf(id)] }
                    })
            }
            TextField {
                id: renameField
                Layout.fillWidth: true
                placeholderText: qsTr("新名称")
                selectByMouse: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                ToolbarButton {
                    text: qsTr("重命名")
                    enabled: customPresetBox.currentValue !== undefined
                    onClicked: EqualizerController.renameCustomPreset(
                                   customPresetBox.currentValue, renameField.text)
                }
                ToolbarButton {
                    text: qsTr("删除")
                    enabled: customPresetBox.currentValue !== undefined
                    onClicked: EqualizerController.deleteCustomPreset(
                                   customPresetBox.currentValue)
                }
            }
        }
    }

    WindowResizeHandles { targetWindow: window }
}

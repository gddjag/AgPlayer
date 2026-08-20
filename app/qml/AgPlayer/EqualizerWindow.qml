import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import AgPlayer

Window {
    id: window
    objectName: "equalizerWindow"
    visible: false
    width: 520
    height: 307
    minimumWidth: 520
    minimumHeight: 307
    flags: Qt.FramelessWindowHint
    color: "transparent"
    title: qsTr("十段图形均衡器")
    property int gainRevision: 0
    palette.window: Theme.background
    palette.windowText: Theme.primaryText
    palette.base: Theme.elevated
    palette.text: Theme.primaryText
    palette.button: Theme.elevated
    palette.buttonText: Theme.primaryText
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText

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
            objectName: "equalizerHeaderPanel"
            Layout.fillWidth: true
            Layout.preferredHeight: 34

            DragHandler {
                target: null
                onActiveChanged: if (active) window.startSystemMove()
            }

            RowLayout {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                ThemedIcon {
                    source: Theme.icon("equalizer-line")
                    tint: Theme.iconAccent
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                }
                Label {
                    text: qsTr("十段图形均衡器")
                    color: Theme.primaryText
                    objectName: "equalizerTitle"
                    font.pixelSize: Math.max(15, Qt.application.font.pixelSize)
                    font.weight: Font.DemiBold
                }
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                ToolButton {
                    width: 34; height: 32; flat: true
                    icon.source: Theme.icon("subtract-line")
                    icon.color: Theme.iconPrimary
                    onClicked: window.showMinimized()
                    background: null
                }
                ToolButton {
                    width: 34; height: 32; flat: true
                    icon.source: Theme.icon("checkbox-blank-line")
                    icon.color: Theme.iconPrimary
                    onClicked: window.visibility === Window.Maximized
                               ? window.showNormal() : window.showMaximized()
                    background: null
                }
                ToolButton {
                    width: 34; height: 32; flat: true
                    icon.source: Theme.icon("close-fill")
                    icon.color: Theme.iconPrimary
                    onClicked: window.hide()
                    background: Rectangle {
                        color: parent.hovered ? Theme.hoverSurface : "transparent"
                    }
                }
            }
        }

        Rectangle {
            objectName: "equalizerResponsePanel"
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: Theme.panel
            border.color: Theme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 6

                ThemedSwitch {
                    objectName: "equalizerEnabledSwitch"
                    checked: EqualizerController.enabled
                    text: qsTr("启用")
                    onToggled: EqualizerController.enabled = checked
                }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: Theme.border }
                Label { text: qsTr("预设"); color: Theme.secondaryText }
                ComboBox {
                    id: presetBox
                    Layout.preferredWidth: 88
                    model: EqualizerController.presetNames
                    currentIndex: Math.max(0, EqualizerController.presetIds.indexOf(
                                              EqualizerController.currentPresetId))
                    onActivated: EqualizerController.applyPreset(
                                     EqualizerController.presetIds[index])
                }
                Button {
                    Layout.preferredWidth: 44
                    text: qsTr("保存")
                    onClicked: saveDialog.open()
                }
                Button {
                    Layout.preferredWidth: 44
                    text: qsTr("管理")
                    onClicked: managePopup.open()
                }
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "equalizerBypassButton"
                    Layout.preferredWidth: 52
                    text: EqualizerController.bypassed ? qsTr("取消旁路") : qsTr("旁路")
                    checkable: true
                    checked: EqualizerController.bypassed
                    onToggled: EqualizerController.bypassed = checked
                }
                Button {
                    objectName: "equalizerResetButton"
                    Layout.preferredWidth: 48
                    text: qsTr("归零")
                    onClicked: EqualizerController.resetAll()
                }
                Label {
                    visible: false
                    text: "44.1–192 kHz"
                    color: Theme.accent
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 6
            color: Theme.panel
            radius: Theme.radiusSm
            border.color: Theme.border
            border.width: 1

            EqualizerResponseCurve {
                anchors.fill: parent
                anchors.margins: 8
            }
            Label {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.top: parent.top
                anchors.topMargin: 4
                text: "+12"
                color: Theme.secondaryText
                font.pixelSize: Math.max(12, Qt.application.font.pixelSize)
            }
            Label {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 4
                text: "−12"
                color: Theme.secondaryText
                font.pixelSize: Math.max(12, Qt.application.font.pixelSize)
            }
        }

        RowLayout {
            objectName: "equalizerBandsPanel"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 4
            Layout.bottomMargin: 3
            spacing: 2

            Repeater {
                id: bandRepeater
                objectName: "equalizerBandRepeater"
                model: 10
                // qmllint disable required
                EqualizerBandSlider {
                    required property int index
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    bandIndex: index
                    frequencyLabel: ["31", "62", "125", "250", "500",
                                     "1k", "2k", "4k", "8k", "16k"][index]
                    gainDb: {
                        window.gainRevision
                        return EqualizerController.bandGain(index)
                    }
                    accessibleLabel: ["31.25 Hz", "62.5 Hz", "125 Hz",
                                      "250 Hz", "500 Hz", "1 kHz", "2 kHz",
                                      "4 kHz", "8 kHz", "16 kHz"][index]
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                Layout.topMargin: 18
                Layout.bottomMargin: 18
                color: Theme.border
            }

            EqualizerBandSlider {
                Layout.preferredWidth: 44
                Layout.fillHeight: true
                bandIndex: -1
                frequencyLabel: qsTr("前级")
                accessibleLabel: qsTr("前级增益")
                gainDb: EqualizerController.preampDb
                preamp: true
            }
        }

        Rectangle {
            objectName: "equalizerFooterPanel"
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            color: Theme.panel
            border.color: Theme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8
                ThemedSwitch {
                    objectName: "equalizerAutoProtection"
                    checked: EqualizerController.autoClipProtection
                    text: qsTr("自动防削波")
                    onToggled: EqualizerController.autoClipProtection = checked
                }
                Label { text: qsTr("余量 0.5 dB"); color: Theme.secondaryText }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 20; color: Theme.border }
                Label {
                    text: EqualizerController.protectionDb < -0.05
                          ? qsTr("保护中 %1 dB").arg(
                                EqualizerController.protectionDb.toFixed(1))
                          : qsTr("无需衰减")
                    color: EqualizerController.protectionDb < -0.05
                           ? Theme.accent : Theme.secondaryText
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: qsTr("双击归零")
                    color: Theme.secondaryText
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
            width: 240
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
        y: 80
        width: 340
        padding: 12
        modal: true
        background: Rectangle {
            color: Theme.elevated
            radius: Theme.radiusSm
            border.color: Theme.border
        }
        ColumnLayout {
            width: parent.width
            Label { text: qsTr("管理自定义预设"); color: Theme.primaryText; font.weight: Font.DemiBold }
            ComboBox {
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
                Button {
                    text: qsTr("重命名")
                    enabled: customPresetBox.currentValue !== undefined
                    onClicked: EqualizerController.renameCustomPreset(
                                   customPresetBox.currentValue, renameField.text)
                }
                Button {
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

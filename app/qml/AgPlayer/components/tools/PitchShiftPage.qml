import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

// Pitch Shift tool page: adjust pitch (cents) with optional tempo preservation.
// Features:
// - Pitch adjustment in cents (-1200..1200, 1 cent precision)
// - "Keep tempo" mode: pitch shift only, duration preserved (OLA time-stretch)
// - "Pitch + tempo" mode: pitch and tempo change together
// - Tempo ratio control (0.5..2.0)
// - Drag-drop file import, progress visualization, export
Rectangle {
    id: page
    color: Theme.background

    property var shifter: PitchShifter

    Component {
        id: fileDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFile
            nameFilters: [qsTr("Audio files (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)")]
            onAccepted: shifter.loadFile(files[0])
        }
    }

    Component {
        id: outputDirDialogComponent
        FolderDialog {
            onAccepted: outputDirField.text = folder
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        // === File Import Section ===
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            color: Theme.panel
            radius: Theme.radiusSm
            border.color: Theme.border
            border.width: 1

            DropArea {
                anchors.fill: parent
                keys: ["text/uri-list"]
                onDropped: function(drop) {
                    if (drop.hasUrls && drop.urls.length > 0) {
                        shifter.loadFile(drop.urls[0])
                        drop.acceptProposedAction()
                    }
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingMd

                Text {
                    text: shifter.hasInput ? shifter.inputFileName
                                           : qsTr("Drop an audio file here or click Browse")
                    color: shifter.hasInput ? Theme.primaryText : Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Button {
                    text: qsTr("Browse")
                    enabled: !shifter.busy
                    onClicked: {
                        var dlg = fileDialogComponent.createObject(page)
                        dlg.open()
                    }

                    background: Rectangle {
                        color: !parent.enabled ? Theme.panel
                              : parent.pressed ? Theme.violet
                              : parent.hovered ? Theme.cyan
                              : Theme.panel
                        border.color: !parent.enabled ? Theme.border : Theme.cyan
                        border.width: 1
                        radius: Theme.radiusSm
                    }

                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled ? Theme.primaryText : Theme.secondaryText
                        font.pixelSize: 12
                        font.family: Theme.fontPrimary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Button {
                    text: qsTr("Clear")
                    enabled: shifter.hasInput && !shifter.busy
                    onClicked: shifter.clear()

                    background: Rectangle {
                        color: parent.pressed ? Theme.favoriteRed
                              : parent.hovered ? Theme.border
                              : Theme.panel
                        border.color: Theme.favoriteRed
                        border.width: 1
                        radius: Theme.radiusSm
                    }

                    contentItem: Text {
                        text: parent.text
                        color: Theme.primaryText
                        font.pixelSize: 12
                        font.family: Theme.fontPrimary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        // === Pitch Control Section ===
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 220
            color: Theme.panel
            radius: Theme.radiusSm
            border.color: Theme.border
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingMd

                // Pitch cents display
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Text {
                        text: qsTr("Pitch")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        Layout.preferredWidth: 60
                    }

                    Slider {
                        id: pitchSlider
                        Layout.fillWidth: true
                        from: -1200
                        to: 1200
                        value: 0
                        stepSize: 1
                        enabled: !shifter.busy

                        background: Rectangle {
                            x: parent.leftPadding
                            y: parent.topPadding + parent.availableHeight / 2 - 2
                            width: parent.availableWidth
                            height: 4
                            radius: 2
                            color: Theme.background

                            Rectangle {
                                width: parent.parent.visualPosition * parent.width
                                height: parent.height
                                radius: parent.radius
                                color: Theme.cyan
                            }
                        }

                        handle: Rectangle {
                            x: parent.leftPadding + parent.visualPosition
                               * (parent.availableWidth - width)
                            y: parent.topPadding + parent.availableHeight / 2 - height / 2
                            width: 16
                            height: 16
                            radius: 8
                            color: parent.pressed ? Theme.violet : Theme.cyan
                            border.color: Theme.background
                            border.width: 2
                        }
                    }

                    Text {
                        text: {
                            var cents = Math.round(pitchSlider.value)
                            var sign = cents >= 0 ? "+" : ""
                            return sign + cents + " cents"
                        }
                        color: Theme.cyan
                        font.family: Theme.fontPrimary
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        Layout.preferredWidth: 90
                        horizontalAlignment: Text.AlignRight
                    }
                }

                // Quick preset buttons
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs

                    Item { Layout.fillWidth: true }

                    Repeater {
                        model: [
                            { label: "-12", value: -1200 },
                            { label: "-7", value: -700 },
                            { label: "-5", value: -500 },
                            { label: "-3", value: -300 },
                            { label: "-2", value: -200 },
                            { label: "-1", value: -100 },
                            { label: "0", value: 0 },
                            { label: "+1", value: 100 },
                            { label: "+2", value: 200 },
                            { label: "+3", value: 300 },
                            { label: "+5", value: 500 },
                            { label: "+7", value: 700 },
                            { label: "+12", value: 1200 }
                        ]

                        Button {
                            text: modelData.label
                            Layout.preferredWidth: 42
                            enabled: !shifter.busy
                            onClicked: pitchSlider.value = modelData.value

                            background: Rectangle {
                                color: pitchSlider.value === modelData.value
                                       ? Theme.cyan
                                       : (parent.pressed ? Theme.violet
                                          : parent.hovered ? Theme.border
                                          : Theme.background)
                                radius: Theme.radiusSm
                                border.color: Theme.border
                                border.width: 1
                            }

                            contentItem: Text {
                                text: parent.text
                                color: pitchSlider.value === modelData.value
                                       ? Theme.background : Theme.primaryText
                                font.pixelSize: 11
                                font.family: Theme.fontPrimary
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                // Mode selection: Keep Tempo vs Pitch+Tempo
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingLg

                    RadioButton {
                        id: keepTempoRadio
                        text: qsTr("Keep Tempo (pitch only)")
                        checked: true
                        enabled: !shifter.busy

                        contentItem: Text {
                            text: parent.text
                            color: parent.enabled ? Theme.primaryText : Theme.secondaryText
                            font.pixelSize: 12
                            font.family: Theme.fontPrimary
                            leftPadding: parent.indicator.width + parent.spacing
                            verticalAlignment: Text.AlignVCenter
                        }

                        indicator: Rectangle {
                            width: 16
                            height: 16
                            x: 0
                            y: parent.height / 2 - 8
                            radius: 8
                            color: Theme.background
                            border.color: parent.checked ? Theme.cyan : Theme.border
                            border.width: 2

                            Rectangle {
                                width: 8
                                height: 8
                                x: 4
                                y: 4
                                radius: 4
                                color: Theme.cyan
                                visible: parent.parent.checked
                            }
                        }
                    }

                    RadioButton {
                        id: pitchTempoRadio
                        text: qsTr("Pitch + Tempo (change duration)")
                        enabled: !shifter.busy

                        contentItem: Text {
                            text: parent.text
                            color: parent.enabled ? Theme.primaryText : Theme.secondaryText
                            font.pixelSize: 12
                            font.family: Theme.fontPrimary
                            leftPadding: parent.indicator.width + parent.spacing
                            verticalAlignment: Text.AlignVCenter
                        }

                        indicator: Rectangle {
                            width: 16
                            height: 16
                            x: 0
                            y: parent.height / 2 - 8
                            radius: 8
                            color: Theme.background
                            border.color: parent.checked ? Theme.cyan : Theme.border
                            border.width: 2

                            Rectangle {
                                width: 8
                                height: 8
                                x: 4
                                y: 4
                                radius: 4
                                color: Theme.cyan
                                visible: parent.parent.checked
                            }
                        }
                    }
                }

                // Tempo ratio control (visible in both modes)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Text {
                        text: qsTr("Tempo")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        Layout.preferredWidth: 60
                    }

                    Slider {
                        id: tempoSlider
                        Layout.fillWidth: true
                        from: 0.5
                        to: 2.0
                        value: 1.0
                        stepSize: 0.01
                        enabled: !shifter.busy

                        background: Rectangle {
                            x: parent.leftPadding
                            y: parent.topPadding + parent.availableHeight / 2 - 2
                            width: parent.availableWidth
                            height: 4
                            radius: 2
                            color: Theme.background

                            Rectangle {
                                width: parent.parent.visualPosition * parent.width
                                height: parent.height
                                radius: parent.radius
                                color: Theme.violet
                            }
                        }

                        handle: Rectangle {
                            x: parent.leftPadding + parent.visualPosition
                               * (parent.availableWidth - width)
                            y: parent.topPadding + parent.availableHeight / 2 - height / 2
                            width: 16
                            height: 16
                            radius: 8
                            color: parent.pressed ? Theme.cyan : Theme.violet
                            border.color: Theme.background
                            border.width: 2
                        }
                    }

                    Text {
                        text: (tempoSlider.value).toFixed(2) + "x"
                        color: Theme.violet
                        font.family: Theme.fontPrimary
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        Layout.preferredWidth: 60
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }
        }

        // === Output + Action Section ===
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            TextField {
                id: outputDirField
                Layout.fillWidth: true
                color: Theme.primaryText
                font.pixelSize: 12
                font.family: Theme.fontPrimary
                placeholderText: qsTr("Output dir (same as source if empty)")
                enabled: !shifter.busy
                background: Rectangle {
                    color: Theme.panel
                    radius: Theme.radiusSm
                    border.color: Theme.border
                    border.width: 1
                }
            }

            Button {
                text: qsTr("Browse")
                enabled: !shifter.busy
                onClicked: {
                    var dlg = outputDirDialogComponent.createObject(page)
                    dlg.open()
                }

                background: Rectangle {
                    color: parent.pressed ? Theme.violet
                          : parent.hovered ? Theme.cyan
                          : Theme.panel
                    border.color: Theme.cyan
                    border.width: 1
                    radius: Theme.radiusSm
                }

                contentItem: Text {
                    text: parent.text
                    color: Theme.primaryText
                    font.pixelSize: 12
                    font.family: Theme.fontPrimary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                text: qsTr("Cancel")
                visible: shifter.busy
                onClicked: shifter.cancel()

                background: Rectangle {
                    color: parent.pressed ? Theme.favoriteRed
                          : parent.hovered ? Theme.border
                          : Theme.panel
                    border.color: Theme.favoriteRed
                    border.width: 1
                    radius: Theme.radiusSm
                }

                contentItem: Text {
                    text: parent.text
                    color: Theme.primaryText
                    font.pixelSize: 12
                    font.family: Theme.fontPrimary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                text: qsTr("Export")
                enabled: !shifter.busy && shifter.hasInput
                onClicked: {
                    shifter.start(
                        Math.round(pitchSlider.value),
                        keepTempoRadio.checked,
                        tempoSlider.value,
                        outputDirField.text.trim())
                }

                background: Rectangle {
                    color: !parent.enabled ? Theme.panel
                          : parent.pressed ? Theme.violet
                          : parent.hovered ? Theme.cyan
                          : Theme.panel
                    border.color: !parent.enabled ? Theme.border : Theme.cyan
                    border.width: 1
                    radius: Theme.radiusSm
                }

                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? Theme.primaryText : Theme.secondaryText
                    font.pixelSize: 12
                    font.family: Theme.fontPrimary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // Progress bar
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs
            visible: shifter.busy || shifter.progress > 0

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: shifter.busy ? qsTr("Processing...")
                                       : qsTr("Export complete")
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }

                Text {
                    text: Math.round(shifter.progress * 100) + "%"
                    color: Theme.cyan
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
            }

            ProgressBar {
                Layout.fillWidth: true
                value: shifter.progress
                background: Rectangle {
                    color: Theme.panel
                    radius: Theme.radiusSm
                    border.color: Theme.border
                    border.width: 1
                    implicitHeight: 4
                }
                contentItem: Rectangle {
                    color: Theme.cyan
                    radius: Theme.radiusSm
                    implicitHeight: 4
                    width: parent.width * parent.value
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    // Status message
    Text {
        id: statusText
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: Theme.spacingSm
        color: Theme.secondaryText
        font.family: Theme.fontPrimary
        font.pixelSize: 12
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        Timer {
            id: statusTimer
            interval: 4000
            onTriggered: statusText.opacity = 0
        }
        onTextChanged: opacity = 1
    }

    Connections {
        target: shifter
        function onPitchShiftCompleted(outputPath) {
            statusText.text = qsTr("Exported: %1").arg(outputPath)
            statusText.color = Theme.cyan
            statusTimer.restart()
        }
        function onErrorOccurred(message) {
            statusText.text = message
            statusText.color = Theme.favoriteRed
            statusTimer.restart()
        }
    }
}

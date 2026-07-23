import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

// Speed Adjust tool page: change playback speed while preserving pitch.
// Internally delegates to PitchShifter's OLA time-stretch (pitch_cents=0,
// keep_tempo=true, tempo_ratio=1/speed_ratio).
//   speed_ratio > 1.0: faster playback (shorter duration)
//   speed_ratio < 1.0: slower playback (longer duration)
Rectangle {
    id: page
    color: Theme.background

    property var adjuster: SpeedAdjuster

    Component {
        id: fileDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFile
            nameFilters: [qsTr("Audio files (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)")]
            onAccepted: adjuster.loadFile(files[0])
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
                        adjuster.loadFile(drop.urls[0])
                        drop.acceptProposedAction()
                    }
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingMd

                Text {
                    text: adjuster.hasInput ? adjuster.inputFileName
                                           : qsTr("Drop an audio file here or click Browse")
                    color: adjuster.hasInput ? Theme.primaryText : Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Button {
                    text: qsTr("Browse")
                    enabled: !adjuster.busy
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
                    enabled: adjuster.hasInput && !adjuster.busy
                    onClicked: adjuster.clear()

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

        // === Speed Control Section ===
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 180
            color: Theme.panel
            radius: Theme.radiusSm
            border.color: Theme.border
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingMd

                // Speed ratio display
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Text {
                        text: qsTr("Speed")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        Layout.preferredWidth: 60
                    }

                    Slider {
                        id: speedSlider
                        Layout.fillWidth: true
                        from: 0.5
                        to: 2.0
                        value: 1.0
                        stepSize: 0.01
                        enabled: !adjuster.busy

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
                        text: (speedSlider.value).toFixed(2) + "x"
                        color: Theme.cyan
                        font.family: Theme.fontPrimary
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        Layout.preferredWidth: 70
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
                            { label: "0.5x", value: 0.5 },
                            { label: "0.75x", value: 0.75 },
                            { label: "1.0x", value: 1.0 },
                            { label: "1.25x", value: 1.25 },
                            { label: "1.5x", value: 1.5 },
                            { label: "1.75x", value: 1.75 },
                            { label: "2.0x", value: 2.0 }
                        ]

                        Button {
                            text: modelData.label
                            Layout.preferredWidth: 52
                            enabled: !adjuster.busy
                            onClicked: speedSlider.value = modelData.value

                            background: Rectangle {
                                color: Math.abs(speedSlider.value - modelData.value) < 0.005
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
                                color: Math.abs(speedSlider.value - modelData.value) < 0.005
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

                // Info text
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Pitch is preserved. Speed > 1.0 = faster, < 1.0 = slower.")
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
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
                enabled: !adjuster.busy
                background: Rectangle {
                    color: Theme.panel
                    radius: Theme.radiusSm
                    border.color: Theme.border
                    border.width: 1
                }
            }

            Button {
                text: qsTr("Browse")
                enabled: !adjuster.busy
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
                visible: adjuster.busy
                onClicked: adjuster.cancel()

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
                enabled: !adjuster.busy && adjuster.hasInput
                onClicked: {
                    adjuster.start(speedSlider.value, outputDirField.text.trim())
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
            visible: adjuster.busy || adjuster.progress > 0

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: adjuster.busy ? qsTr("Processing...")
                                       : qsTr("Export complete")
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }

                Text {
                    text: Math.round(adjuster.progress * 100) + "%"
                    color: Theme.cyan
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
            }

            ProgressBar {
                Layout.fillWidth: true
                value: adjuster.progress
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
        target: adjuster
        function onSpeedAdjustCompleted(outputPath) {
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

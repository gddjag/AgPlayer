import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

// Format Convert tool page: batch transcode audio files to a target format.
// Supports drag-drop import, format selection (MP3/WAV/FLAC/AAC/OGG/Opus),
// bit rate / sample rate / channels / CPU core count settings, and parallel
// processing with progress visualization.
Rectangle {
    id: page
    color: Theme.background

    property var converter: FormatConverter

    Component {
        id: fileDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFiles
            nameFilters: [qsTr("Audio files (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)")]
            onAccepted: converter.loadFiles(files)
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

        // === Top Section: File List ===
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingSm

            Text {
                text: qsTr("BATCH TRANSCODE")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 11
                font.capitalization: Font.AllUppercase
                font.weight: Font.Medium
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.panel
                radius: Theme.radiusSm
                border.color: Theme.border
                border.width: 1

                DropArea {
                    anchors.fill: parent
                    keys: ["text/uri-list"]
                    onDropped: function(drop) {
                        if (drop.hasUrls) {
                            converter.loadFiles(drop.urls)
                            drop.acceptProposedAction()
                        }
                    }
                }

                ListView {
                    id: fileList
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    model: converter.fileCount
                    delegate: Rectangle {
                        width: fileList.width
                        height: 32
                        color: index % 2 === 0 ? "transparent"
                                               : Qt.rgba(1, 1, 1, 0.02)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingSm
                            anchors.rightMargin: Theme.spacingSm
                            spacing: Theme.spacingSm

                            Text {
                                text: (index + 1) + "."
                                color: Theme.secondaryText
                                font.family: Theme.fontPrimary
                                font.pixelSize: 12
                                Layout.preferredWidth: 30
                            }

                            Text {
                                text: converter.entryAt(index)
                                color: Theme.primaryText
                                font.family: Theme.fontPrimary
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }
                }

                // Empty state
                ColumnLayout {
                    anchors.centerIn: parent
                    visible: converter.fileCount === 0
                    spacing: Theme.spacingSm

                    Text {
                        text: qsTr("Drop audio files here or click Add")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 13
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Button {
                        text: qsTr("Add Files")
                        Layout.alignment: Qt.AlignHCenter
                        onClicked: {
                            var dlg = fileDialogComponent.createObject(page)
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
                }
            }
        }

        // === Bottom Section: Settings + Action ===
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 200
            color: Theme.panel
            radius: Theme.radiusSm
            border.color: Theme.border
            border.width: 1

            GridLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                columns: 4
                rowSpacing: Theme.spacingSm
                columnSpacing: Theme.spacingMd

                // Format selection
                Label {
                    text: qsTr("Format")
                    color: Theme.secondaryText
                    font.pixelSize: 12
                    font.family: Theme.fontPrimary
                }
                ComboBox {
                    id: formatCombo
                    Layout.fillWidth: true
                    model: [
                        { key: "mp3", label: qsTr("MP3") },
                        { key: "wav", label: qsTr("WAV") },
                        { key: "flac", label: qsTr("FLAC") },
                        { key: "aac", label: qsTr("AAC (M4A)") },
                        { key: "ogg", label: qsTr("OGG Vorbis") },
                        { key: "opus", label: qsTr("Opus") }
                    ]
                    textRole: "label"
                    valueRole: "key"
                    currentIndex: 0

                    contentItem: Text {
                        text: formatCombo.currentText
                        color: Theme.primaryText
                        font.pixelSize: 12
                        font.family: Theme.fontPrimary
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: Theme.spacingSm
                    }

                    background: Rectangle {
                        color: Theme.background
                        radius: Theme.radiusSm
                        border.color: Theme.border
                        border.width: 1
                    }
                }

                // Bit rate
                Label {
                    text: qsTr("Bit Rate")
                    color: Theme.secondaryText
                    font.pixelSize: 12
                    font.family: Theme.fontPrimary
                }
                ComboBox {
                    id: bitRateCombo
                    Layout.fillWidth: true
                    model: ListModel {
                        ListElement { label: qsTr("Auto"); value: 0 }
                        ListElement { label: "320 kbps"; value: 320000 }
                        ListElement { label: "256 kbps"; value: 256000 }
                        ListElement { label: "192 kbps"; value: 192000 }
                        ListElement { label: "128 kbps"; value: 128000 }
                        ListElement { label: "96 kbps"; value: 96000 }
                    }
                    textRole: "label"
                    valueRole: "value"
                    currentIndex: 0

                    contentItem: Text {
                        text: bitRateCombo.currentText
                        color: Theme.primaryText
                        font.pixelSize: 12
                        font.family: Theme.fontPrimary
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: Theme.spacingSm
                    }

                    background: Rectangle {
                        color: Theme.background
                        radius: Theme.radiusSm
                        border.color: Theme.border
                        border.width: 1
                    }
                }

                // Sample rate
                Label {
                    text: qsTr("Sample Rate")
                    color: Theme.secondaryText
                    font.pixelSize: 12
                    font.family: Theme.fontPrimary
                }
                ComboBox {
                    id: sampleRateCombo
                    Layout.fillWidth: true
                    model: ListModel {
                        ListElement { label: qsTr("Source"); value: 0 }
                        ListElement { label: "48000 Hz"; value: 48000 }
                        ListElement { label: "44100 Hz"; value: 44100 }
                        ListElement { label: "32000 Hz"; value: 32000 }
                        ListElement { label: "22050 Hz"; value: 22050 }
                    }
                    textRole: "label"
                    valueRole: "value"
                    currentIndex: 0

                    contentItem: Text {
                        text: sampleRateCombo.currentText
                        color: Theme.primaryText
                        font.pixelSize: 12
                        font.family: Theme.fontPrimary
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: Theme.spacingSm
                    }

                    background: Rectangle {
                        color: Theme.background
                        radius: Theme.radiusSm
                        border.color: Theme.border
                        border.width: 1
                    }
                }

                // Channels
                Label {
                    text: qsTr("Channels")
                    color: Theme.secondaryText
                    font.pixelSize: 12
                    font.family: Theme.fontPrimary
                }
                ComboBox {
                    id: channelsCombo
                    Layout.fillWidth: true
                    model: ListModel {
                        ListElement { label: qsTr("Source"); value: 0 }
                        ListElement { label: qsTr("Stereo"); value: 2 }
                        ListElement { label: qsTr("Mono"); value: 1 }
                    }
                    textRole: "label"
                    valueRole: "value"
                    currentIndex: 0

                    contentItem: Text {
                        text: channelsCombo.currentText
                        color: Theme.primaryText
                        font.pixelSize: 12
                        font.family: Theme.fontPrimary
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: Theme.spacingSm
                    }

                    background: Rectangle {
                        color: Theme.background
                        radius: Theme.radiusSm
                        border.color: Theme.border
                        border.width: 1
                    }
                }

                // CPU cores
                Label {
                    text: qsTr("CPU Cores")
                    color: Theme.secondaryText
                    font.pixelSize: 12
                    font.family: Theme.fontPrimary
                }
                SpinBox {
                    id: cpuCoresSpin
                    Layout.fillWidth: true
                    from: 1
                    to: 32
                    value: 4

                    contentItem: Text {
                        text: parent.value
                        color: Theme.primaryText
                        font.pixelSize: 12
                        font.family: Theme.fontPrimary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: Theme.background
                        radius: Theme.radiusSm
                        border.color: Theme.border
                        border.width: 1
                    }
                }

                // Output directory
                Label {
                    text: qsTr("Output Dir")
                    color: Theme.secondaryText
                    font.pixelSize: 12
                    font.family: Theme.fontPrimary
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    TextField {
                        id: outputDirField
                        Layout.fillWidth: true
                        color: Theme.primaryText
                        font.pixelSize: 12
                        font.family: Theme.fontPrimary
                        placeholderText: qsTr("Same as source (default)")
                        background: Rectangle {
                            color: Theme.background
                            radius: Theme.radiusSm
                            border.color: Theme.border
                            border.width: 1
                        }
                    }

                    Button {
                        text: qsTr("Browse")
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
                }

                // Item to fill the remaining grid space
                Item { Layout.fillWidth: true; Layout.columnSpan: 2 }

                // Start + Cancel buttons
                RowLayout {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Item { Layout.fillWidth: true }

                    Button {
                        text: qsTr("Cancel")
                        visible: converter.busy
                        onClicked: converter.cancel()

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
                        text: qsTr("Start Transcode")
                        enabled: !converter.busy && converter.fileCount > 0
                        onClicked: {
                            converter.start(formatCombo.currentValue,
                                            bitRateCombo.currentValue,
                                            sampleRateCombo.currentValue,
                                            channelsCombo.currentValue,
                                            cpuCoresSpin.value,
                                            outputDirField.text.trim())
                        }

                        background: Rectangle {
                            color: !parent.enabled ? Theme.panel
                                  : parent.pressed ? Theme.violet
                                  : parent.hovered ? Theme.cyan
                                  : Theme.panel
                            border.color: !parent.enabled ? Theme.border
                                         : Theme.cyan
                            border.width: 1
                            radius: Theme.radiusSm
                        }

                        contentItem: Text {
                            text: parent.text
                            color: parent.enabled ? Theme.primaryText
                                                  : Theme.secondaryText
                            font.pixelSize: 12
                            font.family: Theme.fontPrimary
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }

        // Progress bar + status
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs
            visible: converter.busy || converter.progress > 0

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Text {
                    text: converter.busy
                          ? qsTr("Transcoding... %1/%2 completed")
                            .arg(converter.completedCount).arg(converter.fileCount)
                          : qsTr("Done: %1 success, %2 failed")
                            .arg(converter.completedCount).arg(converter.failedCount)
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }

                Text {
                    text: Math.round(converter.progress * 100) + "%"
                    color: Theme.cyan
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
            }

            ProgressBar {
                Layout.fillWidth: true
                value: converter.progress
                background: Rectangle {
                    color: Theme.background
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
    }

    // Show a message when transcode completes
    Connections {
        target: converter
        function onTranscodeCompleted(successCount, failureCount) {
            if (failureCount === 0) {
                statusText.text = qsTr("All %1 files transcoded successfully.").arg(successCount)
                statusText.color = Theme.cyan
            } else {
                statusText.text = qsTr("%1 succeeded, %2 failed.").arg(successCount).arg(failureCount)
                statusText.color = Theme.favoriteRed
            }
            statusTimer.restart()
        }
        function onErrorOccurred(message) {
            statusText.text = message
            statusText.color = Theme.favoriteRed
            statusTimer.restart()
        }
    }

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
}

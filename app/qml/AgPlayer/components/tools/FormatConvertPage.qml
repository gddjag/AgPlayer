import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

// Format Convert tool page: batch transcode audio files to a target format.
// Supports drag-drop import, output format/bit-rate/sample-rate/channel
// selection, keep-metadata/volume-normalize/extract-audio options, output
// directory choice and sequential background processing with status tracking.
Rectangle {
    id: page
    color: Theme.background

    property var converter: FormatConverter

    Component {
        id: fileDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFiles
            nameFilters: [qsTr("Audio/Video files (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma *.mp4 *.mkv *.avi *.mov *.webm)")]
            onAccepted: {
                converter.loadFiles(files)
                destroy()
            }
            onRejected: destroy()
        }
    }

    Component {
        id: outputDirDialogComponent
        FolderDialog {
            onAccepted: {
                outputDirField.text = folder.toString().replace(/^file:\/+/, "")
                destroy()
            }
            onRejected: destroy()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        // === Header ===
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Text {
                text: qsTr("Format Conversion")
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 18
                font.weight: Font.Medium
            }

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Add Files")
                enabled: !converter.busy
                onClicked: {
                    const dlg = fileDialogComponent.createObject(page)
                    dlg.open()
                }

                background: Rectangle {
                    color: parent.pressed ? Theme.violet
                          : parent.hovered ? Qt.lighter(Theme.cyan, 1.1)
                          : Theme.cyan
                    radius: Theme.radiusSm
                }

                contentItem: Text {
                    text: "+  " + parent.text
                    color: Theme.background
                    font.pixelSize: 13
                    font.family: Theme.fontPrimary
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // === Drag-and-drop import area ===
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            color: "transparent"
            radius: Theme.radiusSm

            Canvas {
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d")
                    const w = width
                    const h = height
                    const r = Theme.radiusSm
                    ctx.clearRect(0, 0, w, h)
                    ctx.strokeStyle = Theme.border
                    ctx.lineWidth = 1
                    ctx.setLineDash([6, 4])
                    ctx.beginPath()
                    ctx.moveTo(r, 0)
                    ctx.lineTo(w - r, 0)
                    ctx.quadraticCurveTo(w, 0, w, r)
                    ctx.lineTo(w, h - r)
                    ctx.quadraticCurveTo(w, h, w - r, h)
                    ctx.lineTo(r, h)
                    ctx.quadraticCurveTo(0, h, 0, h - r)
                    ctx.lineTo(0, r)
                    ctx.quadraticCurveTo(0, 0, r, 0)
                    ctx.closePath()
                    ctx.stroke()
                }
            }

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

            MouseArea {
                anchors.fill: parent
                enabled: !converter.busy
                onClicked: {
                    const dlg = fileDialogComponent.createObject(page)
                    dlg.open()
                }
            }

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.spacingSm

                Image {
                    Layout.alignment: Qt.AlignHCenter
                    source: Theme.icon("folder-open-fill")
                    sourceSize.width: 32
                    sourceSize.height: 32
                    fillMode: Image.PreserveAspectFit
                }

                Text {
                    text: extractAudioCheck.checked
                          ? qsTr("Drop audio/video files here or click Add Files")
                          : qsTr("Drop audio files here or click Add Files")
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 13
                    Layout.alignment: Qt.AlignHCenter
                }

                Text {
                    text: extractAudioCheck.checked
                          ? qsTr("Supports MP3 / WAV / FLAC / AAC / M4A / OGG / Opus / MP4 / MKV / AVI / MOV / WebM")
                          : qsTr("Supports MP3 / WAV / FLAC / AAC / M4A / OGG / Opus")
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 11
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        // === File list table ===
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMd

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Text {
                    text: qsTr("Conversion List") + " (" + converter.fileCount + ")"
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 14
                    font.weight: Font.Medium
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("Clear List")
                    enabled: converter.fileCount > 0 && !converter.busy
                    onClicked: converter.clear()

                    background: Rectangle {
                        color: "transparent"
                    }

                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled ? Theme.secondaryText : Theme.border
                        font.pixelSize: 12
                        font.family: Theme.fontPrimary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.panel
                radius: Theme.radiusSm
                border.color: Theme.border
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    spacing: Theme.spacingSm

                    // Table header
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Item { Layout.preferredWidth: 36 }

                        Text {
                            text: qsTr("File Name")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                            font.weight: Font.Medium
                            Layout.fillWidth: true
                        }

                        Text {
                            text: qsTr("Format")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                            font.weight: Font.Medium
                            Layout.preferredWidth: 56
                        }

                        Text {
                            text: qsTr("Size")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                            font.weight: Font.Medium
                            Layout.preferredWidth: 72
                        }

                        Text {
                            text: qsTr("Duration")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                            font.weight: Font.Medium
                            Layout.preferredWidth: 60
                        }

                        Text {
                            text: qsTr("Status")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                            font.weight: Font.Medium
                            Layout.preferredWidth: 72
                        }

                        Item { Layout.preferredWidth: 36 }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.border
                    }

                    // File rows
                    ListView {
                        id: fileList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: converter.files
                        spacing: 0

                        delegate: Rectangle {
                            width: fileList.width
                            height: 40
                            color: index % 2 === 0 ? "transparent"
                                                   : Qt.rgba(1, 1, 1, 0.02)

                            RowLayout {
                                anchors.fill: parent
                                spacing: Theme.spacingSm

                                Image {
                                    source: Theme.icon("music-2-fill")
                                    sourceSize.width: 16
                                    sourceSize.height: 16
                                    fillMode: Image.PreserveAspectFit
                                    Layout.preferredWidth: 36
                                    Layout.alignment: Qt.AlignHCenter
                                }

                                Text {
                                    text: modelData.fileName
                                    color: Theme.primaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: modelData.format
                                    color: Theme.secondaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 56
                                }

                                Text {
                                    text: converter.formatFileSize(modelData.fileSize)
                                    color: Theme.secondaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 72
                                }

                                Text {
                                    text: converter.formatDuration(modelData.durationMs)
                                    color: Theme.secondaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 60
                                }

                                Text {
                                    text: modelData.status === "Error" && modelData.errorMessage !== ""
                                          ? qsTr("Error")
                                          : modelData.status
                                    color: modelData.status === "Done" ? Theme.cyan
                                          : modelData.status === "Error" ? Theme.favoriteRed
                                          : modelData.status === "Converting" ? Theme.cyan
                                          : Theme.secondaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 12
                                    font.weight: modelData.status === "Converting" ? Font.Medium : Font.Normal
                                    Layout.preferredWidth: 72
                                }

                                Button {
                                    Layout.preferredWidth: 36
                                    enabled: !converter.busy
                                    onClicked: converter.removeFile(index)

                                    background: Rectangle {
                                        color: "transparent"
                                    }

                                    contentItem: Text {
                                        text: "\u00D7"
                                        color: parent.enabled ? Theme.secondaryText : Theme.border
                                        font.pixelSize: 18
                                        font.family: Theme.fontPrimary
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
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
                                text: qsTr("No files in the conversion list")
                                color: Theme.secondaryText
                                font.family: Theme.fontPrimary
                                font.pixelSize: 13
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }
                    }
                }
            }
        }

        // === Output options ===
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: optionsGrid.implicitHeight + Theme.spacingMd * 2
            color: Theme.panel
            radius: Theme.radiusSm
            border.color: Theme.border
            border.width: 1

            GridLayout {
                id: optionsGrid
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                columns: 8
                rowSpacing: Theme.spacingSm
                columnSpacing: Theme.spacingMd

                Label {
                    text: qsTr("Output Format")
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
                        { key: "aac", label: qsTr("AAC") },
                        { key: "m4a", label: qsTr("M4A") },
                        { key: "ogg", label: qsTr("OGG") },
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

                Label {
                    text: qsTr("Bit Rate")
                    color: Theme.secondaryText
                    font.pixelSize: 12
                    font.family: Theme.fontPrimary
                }
                ComboBox {
                    id: bitRateCombo
                    Layout.fillWidth: true
                    model: [
                        { label: qsTr("%1 kbps").arg(64), value: 64000 },
                        { label: qsTr("%1 kbps").arg(96), value: 96000 },
                        { label: qsTr("%1 kbps").arg(128), value: 128000 },
                        { label: qsTr("%1 kbps").arg(192), value: 192000 },
                        { label: qsTr("%1 kbps").arg(256), value: 256000 },
                        { label: qsTr("%1 kbps").arg(320), value: 320000 },
                        { label: qsTr("Lossless"), value: 0 }
                    ]
                    textRole: "label"
                    valueRole: "value"
                    currentIndex: 5

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

                Label {
                    text: qsTr("Sample Rate")
                    color: Theme.secondaryText
                    font.pixelSize: 12
                    font.family: Theme.fontPrimary
                }
                ComboBox {
                    id: sampleRateCombo
                    Layout.fillWidth: true
                    model: [
                        { label: qsTr("Auto"), value: 0 },
                        { label: qsTr("%1 Hz").arg(44100), value: 44100 },
                        { label: qsTr("%1 Hz").arg(48000), value: 48000 },
                        { label: qsTr("%1 Hz").arg(22050), value: 22050 },
                        { label: qsTr("%1 Hz").arg(16000), value: 16000 },
                        { label: qsTr("%1 Hz").arg(8000), value: 8000 }
                    ]
                    textRole: "label"
                    valueRole: "value"
                    currentIndex: 1

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

                Label {
                    text: qsTr("Channels")
                    color: Theme.secondaryText
                    font.pixelSize: 12
                    font.family: Theme.fontPrimary
                }
                ComboBox {
                    id: channelsCombo
                    Layout.fillWidth: true
                    model: [
                        { label: qsTr("Auto"), value: 0 },
                        { label: qsTr("Mono"), value: 1 },
                        { label: qsTr("Stereo"), value: 2 }
                    ]
                    textRole: "label"
                    valueRole: "value"
                    currentIndex: 2

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
            }
        }

        // === Checkboxes ===
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXl

            CheckBox {
                id: keepMetadataCheck
                checked: true
                text: qsTr("Keep metadata (title / artist / album / cover)")

                indicator: Rectangle {
                    implicitWidth: 16
                    implicitHeight: 16
                    x: parent.leftPadding
                    y: parent.height / 2 - height / 2
                    radius: Theme.radiusSm
                    color: parent.checked ? Theme.cyan : "transparent"
                    border.color: parent.checked ? Theme.cyan : Theme.border
                    border.width: 1

                    Text {
                        text: "\u2713"
                        color: Theme.background
                        font.pixelSize: 10
                        anchors.centerIn: parent
                        visible: parent.parent.checked
                    }
                }

                contentItem: Text {
                    text: parent.text
                    color: Theme.primaryText
                    font.pixelSize: 12
                    font.family: Theme.fontPrimary
                    leftPadding: parent.indicator.width + parent.spacing
                    verticalAlignment: Text.AlignVCenter
                }
            }

            CheckBox {
                id: volumeNormalizeCheck
                text: qsTr("Volume normalize")

                indicator: Rectangle {
                    implicitWidth: 16
                    implicitHeight: 16
                    x: parent.leftPadding
                    y: parent.height / 2 - height / 2
                    radius: Theme.radiusSm
                    color: parent.checked ? Theme.cyan : "transparent"
                    border.color: parent.checked ? Theme.cyan : Theme.border
                    border.width: 1

                    Text {
                        text: "\u2713"
                        color: Theme.background
                        font.pixelSize: 10
                        anchors.centerIn: parent
                        visible: parent.parent.checked
                    }
                }

                contentItem: Text {
                    text: parent.text
                    color: Theme.primaryText
                    font.pixelSize: 12
                    font.family: Theme.fontPrimary
                    leftPadding: parent.indicator.width + parent.spacing
                    verticalAlignment: Text.AlignVCenter
                }
            }

            CheckBox {
                id: extractAudioCheck
                text: qsTr("Extract audio from video")

                indicator: Rectangle {
                    implicitWidth: 16
                    implicitHeight: 16
                    x: parent.leftPadding
                    y: parent.height / 2 - height / 2
                    radius: Theme.radiusSm
                    color: parent.checked ? Theme.cyan : "transparent"
                    border.color: parent.checked ? Theme.cyan : Theme.border
                    border.width: 1

                    Text {
                        text: "\u2713"
                        color: Theme.background
                        font.pixelSize: 10
                        anchors.centerIn: parent
                        visible: parent.parent.checked
                    }
                }

                contentItem: Text {
                    text: parent.text
                    color: Theme.primaryText
                    font.pixelSize: 12
                    font.family: Theme.fontPrimary
                    leftPadding: parent.indicator.width + parent.spacing
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Item { Layout.fillWidth: true }
        }

        // === Output directory + action ===
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd

            Text {
                text: qsTr("Output Directory")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
            }

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
                enabled: !converter.busy
                onClicked: {
                    const dlg = outputDirDialogComponent.createObject(page)
                    dlg.open()
                }

                background: Rectangle {
                    color: parent.pressed ? Theme.violet
                          : parent.hovered ? Theme.cyan
                          : Theme.panel
                    border.color: Theme.border
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
                text: converter.busy ? qsTr("Cancel") : qsTr("Start Conversion")
                enabled: converter.busy || converter.fileCount > 0
                onClicked: {
                    if (converter.busy) {
                        converter.cancel()
                    } else {
                        converter.start(formatCombo.currentValue,
                                        bitRateCombo.currentValue,
                                        sampleRateCombo.currentValue,
                                        channelsCombo.currentValue,
                                        outputDirField.text.trim(),
                                        keepMetadataCheck.checked,
                                        volumeNormalizeCheck.checked,
                                        extractAudioCheck.checked)
                    }
                }

                background: Rectangle {
                    color: !parent.enabled ? Theme.panel
                          : parent.pressed ? Theme.violet
                          : parent.hovered ? Qt.lighter(Theme.cyan, 1.1)
                          : Theme.cyan
                    radius: Theme.radiusSm
                }

                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? Theme.background : Theme.secondaryText
                    font.pixelSize: 13
                    font.family: Theme.fontPrimary
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // === Progress bar + status ===
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
                            .arg(converter.completedCount - converter.failedCount)
                            .arg(converter.failedCount)
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

        // === Tip ===
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Text {
                text: "\u24D8"
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
            }

            Text {
                text: qsTr("Lossy-to-lossy conversion may cause secondary quality loss.")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }
        }
    }

    // Show messages when transcode completes or warnings/errors occur.
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
        function onWarningOccurred(message) {
            statusText.text = message
            statusText.color = Theme.secondaryText
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
            interval: 5000
            onTriggered: statusText.opacity = 0
        }
        onTextChanged: opacity = 1
    }
}

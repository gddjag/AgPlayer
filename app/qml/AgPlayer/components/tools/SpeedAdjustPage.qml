import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

// Speed Adjust tool page: BPM-based speed adjustment with pitch preservation.
// Internally delegates to PitchShifter's OLA time-stretch (pitch_cents=0,
// keep_tempo=true, tempo_ratio=detected_bpm/target_bpm).
Rectangle {
    id: page
    color: Theme.background

    property var adjuster: SpeedAdjuster

    // Default segment labels shown in the reference design.
    property var defaultSegments: [
        { label: qsTr("Intro"), color: "#1688FF" },
        { label: qsTr("Build Up"), color: "#00D4FF" },
        { label: qsTr("Drop"), color: "#7B2FF7" },
        { label: qsTr("Breakdown"), color: "#E62E9B" },
        { label: qsTr("Drop 2"), color: "#FF4057" },
        { label: qsTr("Outro"), color: "#00C853" }
    ]

    function formatTime(ms): string {
        if (ms <= 0) return "0:00"
        const totalSec = Math.floor(ms / 1000)
        const min = Math.floor(totalSec / 60)
        const sec = totalSec % 60
        return min + ":" + (sec < 10 ? "0" : "") + sec
    }

    function formatTimeMs(ms): string {
        const totalSec = Math.floor(ms / 1000)
        const min = Math.floor(totalSec / 60)
        const sec = totalSec % 60
        const milli = Math.floor(ms % 1000)
        const secStr = (sec < 10 ? "0" : "") + sec
        const milliStr = (milli < 100 ? "0" : "") + (milli < 10 ? "0" : "") + milli
        return min + ":" + secStr + "." + milliStr
    }

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

                Rectangle {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    Layout.alignment: Qt.AlignVCenter
                    color: Qt.rgba(0, 0.83, 1, 0.12)
                    radius: Theme.radiusSm

                    Text {
                        anchors.centerIn: parent
                        text: "\u266A"
                        color: Theme.cyan
                        font.pixelSize: 24
                        font.family: Theme.fontFallback
                    }
                }

                ColumnLayout {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 240
                    spacing: Theme.spacingXs

                    Text {
                        text: adjuster.hasInput ? adjuster.inputFileName
                                                : qsTr("Drop an audio file here or click Browse")
                        color: adjuster.hasInput ? Theme.primaryText : Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        spacing: Theme.spacingXs
                        visible: adjuster.hasInput

                        Repeater {
                            model: {
                                const badges = []
                                if (adjuster.inputDurationMs > 0)
                                    badges.push(page.formatTime(adjuster.inputDurationMs))
                                return badges
                            }

                            Rectangle {
                                color: Theme.background
                                radius: Theme.radiusSm
                                border.color: Theme.border
                                border.width: 1
                                implicitWidth: badgeText.implicitWidth + Theme.spacingMd * 2
                                implicitHeight: badgeText.implicitHeight + Theme.spacingXs * 2

                                Text {
                                    id: badgeText
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: Theme.secondaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.spacingXs
                    visible: adjuster.hasInput

                    WaveformItem {
                        id: waveform
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        peaks: adjuster.waveformPeaks
                        position: 0
                        duration: adjuster.inputDurationMs
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Text {
                            text: "0:00"
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: page.formatTime(adjuster.inputDurationMs)
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    visible: !adjuster.hasInput
                }

                Button {
                    text: qsTr("Browse")
                    enabled: !adjuster.busy
                    onClicked: {
                        const dlg = fileDialogComponent.createObject(page)
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

        // === BPM Header + Segment Labels ===
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: Theme.panel
            radius: Theme.radiusSm
            border.color: Theme.border
            border.width: 1
            visible: adjuster.hasInput

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingSm

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Text {
                        text: qsTr("Adjust BPM")
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 14
                        font.weight: Font.Medium
                    }

                    Text {
                        text: "\u24D8"
                        color: Theme.secondaryText
                        font.pixelSize: 12
                        font.family: Theme.fontFallback

                        ToolTip.text: qsTr("Markers are manually added; segment processing is future work.")
                        ToolTip.visible: infoHover.hovered
                        ToolTip.delay: 500

                        HoverHandler {
                            id: infoHover
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: qsTr("+ Add Marker")
                        enabled: adjuster.hasInput && !adjuster.busy
                        onClicked: adjuster.addMarker(0, qsTr("Marker"))

                        background: Rectangle {
                            color: parent.pressed ? Theme.violet
                                  : parent.hovered ? Qt.rgba(0, 0.83, 1, 0.2)
                                  : "transparent"
                            border.color: Theme.cyan
                            border.width: 1
                            radius: Theme.radiusSm
                        }

                        contentItem: Text {
                            text: parent.text
                            color: Theme.cyan
                            font.pixelSize: 12
                            font.family: Theme.fontPrimary
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs

                    Repeater {
                        model: page.defaultSegments

                        Button {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 28
                            text: modelData.label
                            enabled: adjuster.hasInput && !adjuster.busy
                            onClicked: adjuster.addMarker(0, modelData.label)

                            background: Rectangle {
                                color: parent.pressed ? Theme.violet
                                      : parent.hovered ? Qt.lighter(modelData.color, 1.3)
                                      : modelData.color
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
        }

        // === Main Control Grid ===
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 3
            columnSpacing: Theme.spacingLg
            rowSpacing: Theme.spacingLg

            // --- BPM Analysis Panel ---
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
                    spacing: Theme.spacingLg

                    Text {
                        text: qsTr("BPM Analysis")
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 14
                        font.weight: Font.Medium
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Text {
                            text: qsTr("Detected BPM")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                        }

                        Text {
                            text: adjuster.detectedBpm > 0
                                  ? adjuster.detectedBpm.toFixed(2)
                                  : "--"
                            color: Theme.cyan
                            font.family: Theme.fontPrimary
                            font.pixelSize: 32
                            font.weight: Font.Medium
                        }

                        Text {
                            text: qsTr("Confidence %1%").arg(
                                adjuster.detectedBpm > 0
                                    ? Math.round(adjuster.bpmConfidence)
                                    : 0)
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs

                        Button {
                            Layout.fillWidth: true
                            text: qsTr("Re-analyze")
                            enabled: adjuster.hasInput && !adjuster.busy
                            onClicked: adjuster.analyzeBpm()

                            background: Rectangle {
                                color: parent.pressed ? Theme.violet
                                      : parent.hovered ? Theme.cyan
                                      : Theme.background
                                border.color: Theme.cyan
                                border.width: 1
                                radius: Theme.radiusSm
                            }

                            contentItem: Text {
                                text: parent.text
                                color: parent.hovered && parent.enabled
                                       ? Theme.background : Theme.primaryText
                                font.pixelSize: 12
                                font.family: Theme.fontPrimary
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Button {
                            Layout.fillWidth: true
                            text: qsTr("Half Beat")
                            enabled: adjuster.hasInput && adjuster.detectedBpm > 0 && !adjuster.busy
                            onClicked: adjuster.halfBeat()

                            background: Rectangle {
                                color: parent.pressed ? Theme.violet
                                      : parent.hovered ? Theme.border
                                      : Theme.background
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
                            Layout.fillWidth: true
                            text: qsTr("Double Beat")
                            enabled: adjuster.hasInput && adjuster.detectedBpm > 0 && !adjuster.busy
                            onClicked: adjuster.doubleBeat()

                            background: Rectangle {
                                color: parent.pressed ? Theme.violet
                                      : parent.hovered ? Theme.border
                                      : Theme.background
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
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // --- Speed Settings Panel ---
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
                    spacing: Theme.spacingLg

                    Text {
                        text: qsTr("Speed Settings")
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 14
                        font.weight: Font.Medium
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Text {
                            text: qsTr("Target BPM")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSm

                            TextField {
                                id: targetBpmField
                                Layout.preferredWidth: 100
                                text: adjuster.targetBpm.toFixed(2)
                                color: Theme.primaryText
                                font.pixelSize: 14
                                font.family: Theme.fontPrimary
                                horizontalAlignment: Text.AlignHCenter
                                enabled: adjuster.hasInput && !adjuster.busy
                                validator: DoubleValidator {
                                    bottom: 20
                                    top: 300
                                    decimals: 2
                                }
                                onEditingFinished: {
                                    const value = parseFloat(text)
                                    if (!isNaN(value))
                                        adjuster.targetBpm = value
                                }
                                background: Rectangle {
                                    color: Theme.background
                                    radius: Theme.radiusSm
                                    border.color: Theme.border
                                    border.width: 1
                                }
                            }

                            Button {
                                text: "+"
                                Layout.preferredWidth: 32
                                enabled: adjuster.hasInput && !adjuster.busy
                                onClicked: adjuster.targetBpm = adjuster.targetBpm + 1

                                background: Rectangle {
                                    color: parent.pressed ? Theme.violet
                                          : parent.hovered ? Theme.border
                                          : Theme.background
                                    border.color: Theme.border
                                    border.width: 1
                                    radius: Theme.radiusSm
                                }

                                contentItem: Text {
                                    text: parent.text
                                    color: Theme.primaryText
                                    font.pixelSize: 16
                                    font.family: Theme.fontFallback
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Text {
                            text: qsTr("Speed Percentage")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                        }

                        Text {
                            text: adjuster.detectedBpm > 0
                                  ? adjuster.speedPercentage.toFixed(2) + " %"
                                  : "--"
                            color: Theme.cyan
                            font.family: Theme.fontPrimary
                            font.pixelSize: 18
                            font.weight: Font.Medium
                        }

                        Slider {
                            id: speedSlider
                            Layout.fillWidth: true
                            from: 50
                            to: 200
                            value: adjuster.speedPercentage
                            stepSize: 0.1
                            enabled: adjuster.hasInput && adjuster.detectedBpm > 0 && !adjuster.busy
                            onMoved: {
                                const ratio = value / 100.0
                                adjuster.targetBpm = adjuster.detectedBpm * ratio
                            }

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
                    }

                    // Keep pitch toggle
                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: qsTr("Keep Pitch")
                            color: Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                            Layout.fillWidth: true
                        }

                        Switch {
                            id: keepPitchSwitch
                            checked: adjuster.keepPitch
                            enabled: adjuster.hasInput && !adjuster.busy
                            onClicked: adjuster.keepPitch = checked

                            indicator: Rectangle {
                                implicitWidth: 44
                                implicitHeight: 24
                                x: parent.leftPadding
                                y: parent.height / 2 - height / 2
                                radius: 12
                                color: parent.checked ? Theme.cyan : Theme.background
                                border.color: parent.checked ? Theme.cyan : Theme.border
                                border.width: 1

                                Rectangle {
                                    x: parent.parent.checked
                                       ? parent.width - width - 3
                                       : 3
                                    y: 3
                                    width: 18
                                    height: 18
                                    radius: 9
                                    color: Theme.primaryText
                                }
                            }
                        }
                    }

                    // Beat align toggle
                    RowLayout {
                        Layout.fillWidth: true

                        RowLayout {
                            spacing: Theme.spacingXs
                            Layout.fillWidth: true

                            Text {
                                text: qsTr("Beat Align")
                                color: Theme.primaryText
                                font.family: Theme.fontPrimary
                                font.pixelSize: 12
                            }

                            Text {
                                text: "\u24D8"
                                color: Theme.secondaryText
                                font.pixelSize: 12
                                font.family: Theme.fontFallback

                                ToolTip.text: qsTr("Experimental feature; currently not supported")
                                ToolTip.visible: beatInfoHover.hovered
                                ToolTip.delay: 500

                                HoverHandler {
                                    id: beatInfoHover
                                }
                            }
                        }

                        Switch {
                            id: beatAlignSwitch
                            checked: adjuster.beatAlign
                            enabled: adjuster.hasInput && !adjuster.busy
                            onClicked: adjuster.beatAlign = checked

                            indicator: Rectangle {
                                implicitWidth: 44
                                implicitHeight: 24
                                x: parent.leftPadding
                                y: parent.height / 2 - height / 2
                                radius: 12
                                color: parent.checked ? Theme.cyan : Theme.background
                                border.color: parent.checked ? Theme.cyan : Theme.border
                                border.width: 1

                                Rectangle {
                                    x: parent.parent.checked
                                       ? parent.width - width - 3
                                       : 3
                                    y: 3
                                    width: 18
                                    height: 18
                                    radius: 9
                                    color: Theme.primaryText
                                }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // --- Markers Panel ---
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
                    spacing: Theme.spacingMd

                    Text {
                        text: qsTr("Segments / Markers (Manual)")
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 14
                        font.weight: Font.Medium
                    }

                    // Header
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Text {
                            Layout.preferredWidth: 90
                            text: qsTr("Name")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                        }

                        Text {
                            Layout.preferredWidth: 80
                            text: qsTr("Start Time")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("BPM")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                        }
                    }

                    ListView {
                        id: markerList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: adjuster.markers

                        delegate: RowLayout {
                            width: markerList.width
                            spacing: Theme.spacingSm

                            Text {
                                Layout.preferredWidth: 90
                                text: modelData.label
                                color: Theme.primaryText
                                font.family: Theme.fontPrimary
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.preferredWidth: 80
                                text: page.formatTimeMs(modelData.timeMs)
                                color: Theme.secondaryText
                                font.family: Theme.fontPrimary
                                font.pixelSize: 12
                            }

                            Text {
                                Layout.fillWidth: true
                                text: Number(modelData.bpm).toFixed(2)
                                color: Theme.secondaryText
                                font.family: Theme.fontPrimary
                                font.pixelSize: 12
                            }

                            Button {
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 28
                                text: "\u2715"
                                enabled: !adjuster.busy
                                onClicked: adjuster.removeMarker(index)

                                background: Rectangle {
                                    color: parent.pressed ? Theme.favoriteRed
                                          : parent.hovered ? Theme.border
                                          : "transparent"
                                    radius: Theme.radiusSm
                                }

                                contentItem: Text {
                                    text: parent.text
                                    color: Theme.favoriteRed
                                    font.pixelSize: 12
                                    font.family: Theme.fontFallback
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }
                }
            }
        }

        // === Preview / Output Panel ===
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

                Text {
                    text: qsTr("Preview / Output")
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 14
                    font.weight: Font.Medium
                }

                // Preview row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMd

                    Button {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        enabled: adjuster.hasInput && !adjuster.busy
                        onClicked: statusText.text = qsTr("Playback preview not supported")

                        background: Rectangle {
                            color: parent.pressed ? Theme.violet
                                  : parent.hovered ? Theme.border
                                  : Theme.background
                            radius: 20
                            border.color: Theme.border
                            border.width: 1
                        }

                        contentItem: Text {
                            text: "\u25B6"
                            color: Theme.primaryText
                            font.pixelSize: 18
                            font.family: Theme.fontFallback
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Text {
                        text: "0:00.000"
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        Layout.preferredWidth: 70
                    }

                    Slider {
                        id: previewSlider
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, adjuster.inputDurationMs)
                        value: 0
                        enabled: adjuster.hasInput && !adjuster.busy

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
                            width: 12
                            height: 12
                            radius: 6
                            color: Theme.cyan
                        }
                    }

                    Text {
                        text: page.formatTimeMs(adjuster.inputDurationMs)
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        Layout.preferredWidth: 70
                    }

                    Text {
                        text: "\uD83D\uDD0A"
                        color: Theme.secondaryText
                        font.pixelSize: 14
                    }

                    Slider {
                        id: volumeSlider
                        Layout.preferredWidth: 100
                        from: 0
                        to: 100
                        value: 80
                        enabled: false

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
                            width: 12
                            height: 12
                            radius: 6
                            color: Theme.cyan
                        }
                    }
                }

                // Output format row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingLg

                    RowLayout {
                        spacing: Theme.spacingSm

                        Text {
                            text: qsTr("Output Format")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                        }

                        Button {
                            id: formatMp3
                            text: qsTr("MP3")
                            checkable: true
                            checked: true
                            enabled: !adjuster.busy
                            onClicked: {
                                formatWav.checked = false
                                formatFlac.checked = false
                            }

                            background: Rectangle {
                                color: parent.checked ? Theme.background : Theme.panel
                                border.color: parent.checked ? Theme.cyan : Theme.border
                                border.width: 1
                                radius: Theme.radiusSm
                            }

                            contentItem: Text {
                                text: parent.text
                                color: parent.checked ? Theme.cyan : Theme.primaryText
                                font.pixelSize: 12
                                font.family: Theme.fontPrimary
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Button {
                            id: formatWav
                            text: qsTr("WAV")
                            checkable: true
                            checked: false
                            enabled: !adjuster.busy
                            onClicked: {
                                formatMp3.checked = false
                                formatFlac.checked = false
                            }

                            background: Rectangle {
                                color: parent.checked ? Theme.background : Theme.panel
                                border.color: parent.checked ? Theme.cyan : Theme.border
                                border.width: 1
                                radius: Theme.radiusSm
                            }

                            contentItem: Text {
                                text: parent.text
                                color: parent.checked ? Theme.cyan : Theme.primaryText
                                font.pixelSize: 12
                                font.family: Theme.fontPrimary
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Button {
                            id: formatFlac
                            text: qsTr("FLAC")
                            checkable: true
                            checked: false
                            enabled: !adjuster.busy
                            onClicked: {
                                formatMp3.checked = false
                                formatWav.checked = false
                            }

                            background: Rectangle {
                                color: parent.checked ? Theme.background : Theme.panel
                                border.color: parent.checked ? Theme.cyan : Theme.border
                                border.width: 1
                                radius: Theme.radiusSm
                            }

                            contentItem: Text {
                                text: parent.text
                                color: parent.checked ? Theme.cyan : Theme.primaryText
                                font.pixelSize: 12
                                font.family: Theme.fontPrimary
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: qsTr("Start Processing")
                        enabled: !adjuster.busy && adjuster.hasInput && adjuster.detectedBpm > 0
                        onClicked: {
                            const format = formatWav.checked ? "wav"
                                            : formatFlac.checked ? "flac"
                                            : "mp3"
                            adjuster.startBpmAdjust(
                                adjuster.targetBpm,
                                keepPitchSwitch.checked,
                                format,
                                outputDirField.text.trim())
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

                // Save location row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Text {
                        text: qsTr("Save Location")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        Layout.preferredWidth: 90
                    }

                    TextField {
                        id: outputDirField
                        Layout.fillWidth: true
                        color: Theme.primaryText
                        font.pixelSize: 12
                        font.family: Theme.fontPrimary
                        placeholderText: qsTr("Same as source (default)")
                        enabled: !adjuster.busy
                        background: Rectangle {
                            color: Theme.background
                            radius: Theme.radiusSm
                            border.color: Theme.border
                            border.width: 1
                        }
                    }

                    Button {
                        text: qsTr("Browse")
                        enabled: !adjuster.busy
                        onClicked: {
                            const dlg = outputDirDialogComponent.createObject(page)
                            dlg.open()
                        }

                        background: Rectangle {
                            color: parent.pressed ? Theme.violet
                                  : parent.hovered ? Theme.cyan
                                  : Theme.background
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
                                  : Theme.background
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

                // Info text
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs

                    Text {
                        text: "\u24D8"
                        color: Theme.secondaryText
                        font.pixelSize: 12
                        font.family: Theme.fontFallback
                    }

                    Text {
                        text: qsTr("BPM detection results may vary slightly depending on audio quality.")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 11
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
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
        }

        Item { Layout.fillHeight: true }
    }

    // Status message overlay
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

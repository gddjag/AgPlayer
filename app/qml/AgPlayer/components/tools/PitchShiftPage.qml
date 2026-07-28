import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

// Pitch Shift tool page: adjust pitch with semitone/cents controls, presets,
// advanced toggles, output format/sample-rate selection, and export.
// Layout matches the design reference "音频工具 升降调.png".
Rectangle {
    id: page
    readonly property bool previewIsCurrent:
        AudioPreviewController.sourcePath.length > 0
        && AudioPreviewController.isCurrentSource(shifter.inputUrl)
    color: Theme.background

    property var shifter: PitchShifter

    // Internal pitch state: semitone + cents are independent controls.
    // Effective pitch cents is clamped to the Core supported range.
    property int selectedPreset: 0 // 0 = Original, 7 = Custom
    property int semitoneValue: 0
    property int centsValue: 0

    function formatTime(ms): string {
        if (ms <= 0) return "0:00"
        const totalSec = Math.floor(ms / 1000)
        const min = Math.floor(totalSec / 60)
        const sec = totalSec % 60
        return min + ":" + (sec < 10 ? "0" : "") + sec
    }

    function effectivePitchCents(): int {
        const total = semitoneValue * 100 + centsValue
        return Math.max(-1200, Math.min(1200, total))
    }

    function applyPreset(index, semitone, cents): void {
        selectedPreset = index
        semitoneValue = semitone
        centsValue = cents
    }

    function updatePresetFromManual(): void {
        // If the current values match a preset, highlight it; otherwise Custom (7).
        const presets = [
            { s: 0, c: 0 },
            { s: 2, c: 0 },
            { s: 4, c: 0 },
            { s: 2, c: 0 },
            { s: 4, c: 0 },
            { s: -2, c: 0 },
            { s: -4, c: 0 }
        ]
        let match = 7
        for (let i = 0; i < presets.length; ++i) {
            if (presets[i].s === semitoneValue && presets[i].c === centsValue) {
                match = i
                break
            }
        }
        selectedPreset = match
    }

    Component {
        id: fileDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFile
            nameFilters: [qsTr("Audio files (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)")]
            onAccepted: shifter.loadFile(selectedFile)
        }
    }

    Component {
        id: outputDirDialogComponent
        FolderDialog {
            onAccepted: outputDirField.text = selectedFolder.toString().replace(/^file:\/+/, "")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        // === Top File Info / Import Bar ===
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

                // Music icon
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
                        text: shifter.hasInput ? shifter.inputFileName
                                               : qsTr("Drop an audio file here or click Browse")
                        color: shifter.hasInput ? Theme.primaryText : Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        spacing: Theme.spacingXs
                        visible: shifter.hasInput

                        Repeater {
                            model: {
                                const badges = []
                                if (shifter.inputFormat.length > 0)
                                    badges.push(shifter.inputFormat.toUpperCase())
                                if (shifter.inputSampleRate > 0)
                                    badges.push((shifter.inputSampleRate / 1000).toFixed(1) + " kHz")
                                if (shifter.inputDurationMs > 0)
                                    badges.push(page.formatTime(shifter.inputDurationMs))
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

                // Playback preview uses its own player and never interrupts
                // the main playback queue.
                Rectangle {
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    Layout.alignment: Qt.AlignVCenter
                    color: shifter.hasInput ? Theme.background : "transparent"
                    radius: 20
                    border.color: shifter.hasInput ? Theme.border : "transparent"
                    border.width: 1
                    visible: shifter.hasInput
                    opacity: 1.0

                    Text {
                        anchors.centerIn: parent
                        text: page.previewIsCurrent
                              && AudioPreviewController.playing
                              ? "\u23F8" : "\u25B6"
                        color: Theme.primaryText
                        font.pixelSize: 18
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: AudioPreviewController.toggle(shifter.inputUrl)
                    }

                    ToolTip.text: qsTr("播放或暂停原始音频")
                    ToolTip.visible: previewHover.hovered
                    ToolTip.delay: 500

                    HoverHandler {
                        id: previewHover
                    }
                }

                // Waveform
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.spacingXs
                    visible: shifter.hasInput

                    WaveformItem {
                        id: waveform
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        peaks: shifter.waveformPeaks
                        position: 0
                        duration: shifter.inputDurationMs
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Text {
                            text: "-00:00"
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: page.formatTime(shifter.inputDurationMs)
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    visible: !shifter.hasInput
                }

                Button {
                    text: qsTr("Browse")
                    enabled: !shifter.busy
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

        // === Main Control Grid ===
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 3
            columnSpacing: Theme.spacingLg
            rowSpacing: Theme.spacingLg

            // --- Presets Panel ---
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
                        text: qsTr("Presets")
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 14
                        font.weight: Font.Medium
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Repeater {
                            model: [
                                { label: qsTr("Original (0)"), s: 0, c: 0 },
                                { label: qsTr("Male +2"), s: 2, c: 0 },
                                { label: qsTr("Male +4"), s: 4, c: 0 },
                                { label: qsTr("Female +2"), s: 2, c: 0 },
                                { label: qsTr("Female +4"), s: 4, c: 0 },
                                { label: qsTr("Deep -2"), s: -2, c: 0 },
                                { label: qsTr("Deep -4"), s: -4, c: 0 },
                                { label: qsTr("Custom"), s: 0, c: 0 }
                            ]

                            Button {
                                Layout.fillWidth: true
                                text: modelData.label
                                enabled: !shifter.busy
                                onClicked: page.applyPreset(index, modelData.s, modelData.c)

                                background: Rectangle {
                                    color: page.selectedPreset === index
                                           ? Theme.cyan
                                           : (parent.pressed ? Theme.violet
                                              : parent.hovered ? Theme.border
                                              : Theme.background)
                                    radius: Theme.radiusSm
                                    border.color: page.selectedPreset === index
                                                  ? Theme.cyan : Theme.border
                                    border.width: 1
                                }

                                contentItem: Text {
                                    text: parent.text
                                    color: page.selectedPreset === index
                                           ? Theme.background : Theme.primaryText
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

            // --- Pitch Adjustment Panel ---
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
                        text: qsTr("Pitch Adjustment")
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 14
                        font.weight: Font.Medium
                    }

                    // Semitone control
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Text {
                            text: qsTr("Semitone")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSm

                            Button {
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                text: "-"
                                enabled: !shifter.busy && page.semitoneValue > -12
                                onClicked: {
                                    page.semitoneValue = Math.max(-12, page.semitoneValue - 1)
                                    page.updatePresetFromManual()
                                }

                                background: Rectangle {
                                    color: parent.pressed ? Theme.violet
                                          : parent.hovered ? Theme.border
                                          : Theme.background
                                    radius: Theme.radiusSm
                                    border.color: Theme.border
                                    border.width: 1
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

                            Text {
                                text: (page.semitoneValue >= 0 ? "+" : "") + page.semitoneValue
                                color: Theme.cyan
                                font.family: Theme.fontPrimary
                                font.pixelSize: 18
                                font.weight: Font.Medium
                                Layout.preferredWidth: 48
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Button {
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                text: "+"
                                enabled: !shifter.busy && page.semitoneValue < 12
                                onClicked: {
                                    page.semitoneValue = Math.min(12, page.semitoneValue + 1)
                                    page.updatePresetFromManual()
                                }

                                background: Rectangle {
                                    color: parent.pressed ? Theme.violet
                                          : parent.hovered ? Theme.border
                                          : Theme.background
                                    radius: Theme.radiusSm
                                    border.color: Theme.border
                                    border.width: 1
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

                        Slider {
                            id: semitoneSlider
                            Layout.fillWidth: true
                            from: -12
                            to: 12
                            value: page.semitoneValue
                            stepSize: 1
                            enabled: !shifter.busy
                            onMoved: {
                                const rounded = Math.round(value)
                                if (page.semitoneValue !== rounded) {
                                    page.semitoneValue = rounded
                                    page.updatePresetFromManual()
                                }
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

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: "-12"
                                color: Theme.secondaryText
                                font.family: Theme.fontPrimary
                                font.pixelSize: 11
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: "+12"
                                color: Theme.secondaryText
                                font.family: Theme.fontPrimary
                                font.pixelSize: 11
                            }
                        }
                    }

                    // Cents control
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Text {
                            text: qsTr("Cents")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSm

                            Button {
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                text: "-"
                                enabled: !shifter.busy && page.centsValue > -100
                                onClicked: {
                                    page.centsValue = Math.max(-100, page.centsValue - 1)
                                    page.updatePresetFromManual()
                                }

                                background: Rectangle {
                                    color: parent.pressed ? Theme.violet
                                          : parent.hovered ? Theme.border
                                          : Theme.background
                                    radius: Theme.radiusSm
                                    border.color: Theme.border
                                    border.width: 1
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

                            Text {
                                text: (page.centsValue >= 0 ? "+" : "") + page.centsValue
                                color: Theme.cyan
                                font.family: Theme.fontPrimary
                                font.pixelSize: 18
                                font.weight: Font.Medium
                                Layout.preferredWidth: 48
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Button {
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                text: "+"
                                enabled: !shifter.busy && page.centsValue < 100
                                onClicked: {
                                    page.centsValue = Math.min(100, page.centsValue + 1)
                                    page.updatePresetFromManual()
                                }

                                background: Rectangle {
                                    color: parent.pressed ? Theme.violet
                                          : parent.hovered ? Theme.border
                                          : Theme.background
                                    radius: Theme.radiusSm
                                    border.color: Theme.border
                                    border.width: 1
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

                        Slider {
                            id: centsSlider
                            Layout.fillWidth: true
                            from: -100
                            to: 100
                            value: page.centsValue
                            stepSize: 1
                            enabled: !shifter.busy
                            onMoved: {
                                const rounded = Math.round(value)
                                if (page.centsValue !== rounded) {
                                    page.centsValue = rounded
                                    page.updatePresetFromManual()
                                }
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

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: "-100"
                                color: Theme.secondaryText
                                font.family: Theme.fontPrimary
                                font.pixelSize: 11
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: "+100"
                                color: Theme.secondaryText
                                font.family: Theme.fontPrimary
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }

            // --- Advanced Options Panel ---
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
                        text: qsTr("Advanced Options")
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 14
                        font.weight: Font.Medium
                    }

                    // Keep tempo toggle
                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: qsTr("Keep Tempo")
                            color: Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                            Layout.fillWidth: true
                        }

                        Switch {
                            id: keepTempoSwitch
                            checked: true
                            enabled: !shifter.busy

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

                    // Vocal protection toggle
                    RowLayout {
                        Layout.fillWidth: true

                        RowLayout {
                            spacing: Theme.spacingXs
                            Layout.fillWidth: true

                            Text {
                                text: qsTr("Vocal Protection (Experimental)")
                                color: Theme.primaryText
                                font.family: Theme.fontPrimary
                                font.pixelSize: 12
                            }

                            Text {
                                text: "\u24D8"
                                color: Theme.secondaryText
                                font.pixelSize: 12
                                font.family: Theme.fontFallback

                                ToolTip.text: qsTr("为人声提供柔和的共振峰补偿")
                                ToolTip.visible: infoHover.hovered
                                ToolTip.delay: 500

                                HoverHandler {
                                    id: infoHover
                                }
                            }
                        }

                        Switch {
                            id: vocalProtectionSwitch
                            checked: false
                            enabled: !shifter.busy

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

                    // Smooth transition toggle
                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: qsTr("Smooth Transition")
                            color: Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                            Layout.fillWidth: true
                        }

                        Switch {
                            id: smoothTransitionSwitch
                            checked: false
                            enabled: !shifter.busy

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
        }

        // === Output + Action Panel ===
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

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingLg

                    // Output format
                    RowLayout {
                        spacing: Theme.spacingSm

                        Text {
                            text: qsTr("Output Format")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                        }

                        ComboBox {
                            id: formatCombo
                            Layout.preferredWidth: 120
                            model: [
                                { key: "source", label: qsTr("Source") },
                                { key: "mp3", label: qsTr("MP3") },
                                { key: "wav", label: qsTr("WAV") },
                                { key: "flac", label: qsTr("FLAC") },
                                { key: "aac", label: qsTr("AAC") },
                                { key: "ogg", label: qsTr("OGG") },
                                { key: "opus", label: qsTr("Opus") }
                            ]
                            textRole: "label"
                            valueRole: "key"
                            currentIndex: 0
                            enabled: !shifter.busy

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
                    }

                    // Sample rate
                    RowLayout {
                        spacing: Theme.spacingSm

                        Text {
                            text: qsTr("Sample Rate")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                        }

                        ComboBox {
                            id: sampleRateCombo
                            Layout.preferredWidth: 140
                            model: [
                                { label: qsTr("Auto"), value: 0 },
                                { label: "44100 Hz", value: 44100 },
                                { label: "48000 Hz", value: 48000 },
                                { label: "22050 Hz", value: 22050 },
                                { label: "16000 Hz", value: 16000 }
                            ]
                            textRole: "label"
                            valueRole: "value"
                            currentIndex: 0
                            enabled: !shifter.busy

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
                    }

                    Item { Layout.fillWidth: true }

                    // Start processing button
                    Button {
                        text: qsTr("Start Processing")
                        enabled: !shifter.busy && shifter.hasInput
                        onClicked: {
                            shifter.start(
                                page.effectivePitchCents(),
                                keepTempoSwitch.checked,
                                1.0,
                                formatCombo.currentValue,
                                sampleRateCombo.currentValue,
                                vocalProtectionSwitch.checked,
                                smoothTransitionSwitch.checked,
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

                // Save location
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
                        enabled: !shifter.busy
                        background: Rectangle {
                            color: Theme.background
                            radius: Theme.radiusSm
                            border.color: Theme.border
                            border.width: 1
                        }
                    }

                    Button {
                        text: qsTr("Browse")
                        enabled: !shifter.busy
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
                        visible: shifter.busy
                        onClicked: shifter.cancel()

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
                        text: qsTr("Vocal protection is intended for daily use; avoid processing for professional projects.")
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
        target: shifter
        function onPitchShiftCompleted(outputPath) {
            statusText.text = qsTr("Exported: %1").arg(outputPath)
            statusText.color = Theme.cyan
            statusTimer.restart()
        }
        function onPitchShiftCompletedWithWarnings(outputPath, warning) {
            statusText.text = qsTr("Exported: %1 (%2)").arg(outputPath).arg(warning)
            statusText.color = Theme.cyan
            statusTimer.restart()
        }
        function onErrorOccurred(message) {
            statusText.text = message
            statusText.color = Theme.favoriteRed
            statusTimer.restart()
        }
    }

    Connections {
        target: AudioPreviewController
        function onErrorOccurred(message) {
            statusText.text = message
            statusText.color = Theme.favoriteRed
            statusTimer.restart()
        }
    }
}

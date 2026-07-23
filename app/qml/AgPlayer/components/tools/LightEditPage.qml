import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

// Light Edit tool page: multi-track waveform editor for non-destructive trim,
// linear fade in/out, and gain. The active track is edited through the existing
// ag_light_edit backend; unsupported multi-track tools are disabled with a tooltip.
Rectangle {
    id: page
    color: Theme.background

    property var editor: LightEditor
    property bool hasDuration: editor.durationMs > 0

    property int selectedTrack: 0
    property real zoomScale: 1.0
    property int viewportOffsetMs: 0
    property int playheadMs: 0
    property int trimStartMs: 0
    property int trimEndMs: 0

    readonly property var trackColors: [
        Theme.waveformRed,
        Theme.waveformBlue,
        Theme.waveformGreen,
        Theme.waveformCyan,
        Theme.waveformViolet,
        Theme.waveformMagenta
    ]

    function formatTime(ms) {
        if (ms <= 0) return "00:00"
        const totalSec = Math.floor(ms / 1000)
        const m = Math.floor(totalSec / 60)
        const s = totalSec % 60
        return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s
    }

    function gainToDb(g) {
        if (g <= 0.0001) return "-inf dB"
        const db = 20.0 * Math.log(g) / Math.log(10.0)
        return db.toFixed(1) + " dB"
    }

    function applyFadeIn() {
        fadeInField.text = "1000"
    }

    function applyFadeOut() {
        fadeOutField.text = "1000"
    }

    function applyCrop() {
        if (!page.hasDuration) return
        const mid = (page.trimStartMs + page.trimEndMs) / 2
        if (page.playheadMs < mid) {
            page.trimEndMs = Math.max(page.trimStartMs + 200, page.playheadMs)
        } else {
            page.trimStartMs = Math.min(page.trimEndMs - 200, page.playheadMs)
        }
    }

    function zoomIn() {
        page.zoomScale = Math.min(page.zoomScale * 1.25, 50.0)
    }

    function zoomOut() {
        page.zoomScale = Math.max(page.zoomScale / 1.25, 0.05)
    }

    function zoomFit() {
        if (!page.hasDuration || tracksArea.width <= 0) return
        page.zoomScale = tracksArea.width / (editor.durationMs * 0.03)
        page.viewportOffsetMs = 0
    }

    function zoomAll() {
        page.zoomScale = 1.0
        page.viewportOffsetMs = 0
    }

    Component {
        id: fileDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFile
            nameFilters: [qsTr("Audio files (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)")]
            onAccepted: editor.loadFile(files[0])
        }
    }

    Component {
        id: outputDirDialogComponent
        FolderDialog {
            onAccepted: outputDirField.text = folder.toString().replace(/^file:\/+/, "")
        }
    }

    ListModel {
        id: trackModel
        ListElement { name: ""; durationMs: 0; peaks: []; hasFile: false }
        ListElement { name: ""; durationMs: 0; peaks: []; hasFile: false }
        ListElement { name: ""; durationMs: 0; peaks: []; hasFile: false }
        ListElement { name: ""; durationMs: 0; peaks: []; hasFile: false }
        ListElement { name: ""; durationMs: 0; peaks: []; hasFile: false }
        ListElement { name: ""; durationMs: 0; peaks: []; hasFile: false }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        // === Top header: import, file info, playback controls, time ===
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 70
            color: Theme.panel
            radius: Theme.radiusSm
            border.color: Theme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingMd

                Button {
                    text: "+  " + qsTr("Add File")
                    enabled: !editor.busy
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
                        text: parent.text
                        color: Theme.background
                        font.pixelSize: 13
                        font.family: Theme.fontPrimary
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Button {
                    text: qsTr("Clear")
                    enabled: editor.hasInput && !editor.busy
                    onClicked: editor.clear()

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
                        font.pixelSize: 13
                        font.family: Theme.fontPrimary
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                ColumnLayout {
                    Layout.alignment: Qt.AlignVCenter
                    spacing: Theme.spacingXs

                    Text {
                        text: editor.hasInput
                              ? editor.inputFileName
                              : qsTr("No audio file loaded")
                        color: editor.hasInput ? Theme.primaryText : Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                        Layout.preferredWidth: 220
                    }

                    RowLayout {
                        spacing: Theme.spacingXs
                        visible: editor.hasInput

                        Repeater {
                            model: {
                                const badges = []
                                if (editor.inputFormat.length > 0)
                                    badges.push(editor.inputFormat.toUpperCase())
                                if (editor.inputSampleRate > 0)
                                    badges.push((editor.inputSampleRate / 1000).toFixed(1) + " kHz")
                                if (editor.inputChannels > 0)
                                    badges.push(editor.inputChannels + " ch")
                                if (editor.durationMs > 0)
                                    badges.push(page.formatTime(editor.durationMs))
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
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // Playback preview controls (not supported in this phase).
                RowLayout {
                    spacing: Theme.spacingSm

                    Button {
                        text: "\u23EE"
                        enabled: false
                        Layout.preferredWidth: 36
                        Layout.preferredHeight: 36

                        background: Rectangle {
                            color: Theme.background
                            radius: Theme.radiusSm
                            border.color: Theme.border
                            border.width: 1
                        }
                        contentItem: Text {
                            text: parent.text
                            color: Theme.secondaryText
                            font.pixelSize: 16
                            font.family: Theme.fontFallback
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        ToolTip.text: qsTr("Playback preview not supported")
                        ToolTip.visible: prevHover.hovered
                        ToolTip.delay: 400
                        HoverHandler { id: prevHover }
                    }

                    Button {
                        text: "\u25B6"
                        enabled: false
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40

                        background: Rectangle {
                            color: Theme.background
                            radius: 20
                            border.color: Theme.border
                            border.width: 1
                        }
                        contentItem: Text {
                            text: parent.text
                            color: Theme.secondaryText
                            font.pixelSize: 18
                            font.family: Theme.fontFallback
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        ToolTip.text: qsTr("Playback preview not supported")
                        ToolTip.visible: playHover.hovered
                        ToolTip.delay: 400
                        HoverHandler { id: playHover }
                    }

                    Button {
                        text: "\u23ED"
                        enabled: false
                        Layout.preferredWidth: 36
                        Layout.preferredHeight: 36

                        background: Rectangle {
                            color: Theme.background
                            radius: Theme.radiusSm
                            border.color: Theme.border
                            border.width: 1
                        }
                        contentItem: Text {
                            text: parent.text
                            color: Theme.secondaryText
                            font.pixelSize: 16
                            font.family: Theme.fontFallback
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        ToolTip.text: qsTr("Playback preview not supported")
                        ToolTip.visible: nextHover.hovered
                        ToolTip.delay: 400
                        HoverHandler { id: nextHover }
                    }
                }

                Slider {
                    id: volumeSlider
                    Layout.preferredWidth: 100
                    from: 0.0
                    to: 1.0
                    value: 1.0
                    enabled: false

                    background: Rectangle {
                        x: parent.leftPadding
                        y: parent.topPadding + parent.availableHeight / 2 - 2
                        width: parent.availableWidth
                        height: 4
                        radius: 2
                        color: Theme.background
                    }
                    handle: Rectangle {
                        x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
                        y: parent.topPadding + parent.availableHeight / 2 - height / 2
                        width: 14
                        height: 14
                        radius: 7
                        color: Theme.secondaryText
                    }

                    ToolTip.text: qsTr("Volume control not supported")
                    ToolTip.visible: volumeHover.hovered
                    ToolTip.delay: 400
                    HoverHandler { id: volumeHover }
                }

                Text {
                    text: page.formatTime(page.playheadMs) + " / " + page.formatTime(page.hasDuration ? editor.durationMs : 0)
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    Layout.preferredWidth: 110
                    horizontalAlignment: Text.AlignRight
                }
            }
        }

        // === Multi-track waveform area ===
        Item {
            id: tracksArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 240

            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                Repeater {
                    model: trackModel

                    MultiTrackWaveform {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        trackIndex: index
                        trackName: model.name
                        trackDurationMs: model.durationMs
                        trackPeaks: model.peaks
                        trackColor: page.trackColors[index]
                        isActive: index === 0
                        isSelected: page.selectedTrack === index
                        showTimeRuler: index === 0
                        zoomScale: page.zoomScale
                        viewportOffsetMs: page.viewportOffsetMs
                        playheadMs: page.playheadMs
                        hasFile: model.hasFile

                        onTrackClicked: page.selectedTrack = index
                        onSeekRequested: function(positionMs) {
                            page.playheadMs = Math.max(0, Math.min(positionMs, editor.durationMs))
                        }
                    }
                }
            }

            DropArea {
                id: trackDropArea
                anchors.fill: parent
                enabled: !editor.busy
                keys: ["text/uri-list"]

                onEntered: function(drag) {
                    if (drag.hasUrls) drag.accept(Qt.CopyAction)
                    else drag.accepted = false
                }
                onDropped: function(drop) {
                    if (drop.hasUrls && drop.urls.length > 0)
                        editor.loadFile(drop.urls[0])
                }

                Rectangle {
                    id: dropOverlay
                    anchors.fill: parent
                    color: Qt.rgba(0, 0, 0, 0.5)
                    border.color: Theme.cyan
                    border.width: 2
                    opacity: trackDropArea.containsDrag ? 1 : 0
                    visible: opacity > 0

                    Behavior on opacity { NumberAnimation { duration: 120 } }

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Drop audio file here")
                        color: Theme.cyan
                        font.family: Theme.fontPrimary
                        font.pixelSize: 14
                        font.weight: Font.Medium
                    }
                }
            }
        }

        // === Zoom controls ===
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Item { Layout.fillWidth: true }

            Repeater {
                model: [
                    { icon: "\u2795", label: qsTr("Zoom In"), action: function() { page.zoomIn() } },
                    { icon: "\u2796", label: qsTr("Zoom Out"), action: function() { page.zoomOut() } },
                    { icon: "\u26F6", label: qsTr("Fit"), action: function() { page.zoomFit() } },
                    { icon: "\u25A1", label: qsTr("All"), action: function() { page.zoomAll() } }
                ]

                Button {
                    text: modelData.icon
                    enabled: !editor.busy
                    Layout.preferredWidth: 34
                    Layout.preferredHeight: 30

                    onClicked: modelData.action()

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
                        color: parent.enabled ? Theme.primaryText : Theme.secondaryText
                        font.pixelSize: 14
                        font.family: Theme.fontFallback
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    ToolTip.text: modelData.label
                    ToolTip.visible: zoomHover.hovered
                    ToolTip.delay: 400
                    HoverHandler { id: zoomHover }
                }
            }
        }

        // === Trim range slider ===
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Text {
                text: page.formatTime(page.trimStartMs)
                color: Theme.cyan
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                Layout.preferredWidth: 50
                horizontalAlignment: Text.AlignHCenter
            }

            RangeSlider {
                id: trimSlider
                Layout.fillWidth: true
                from: 0
                to: page.hasDuration ? editor.durationMs : 1
                first.value: page.trimStartMs
                second.value: page.trimEndMs
                enabled: !editor.busy && page.hasDuration

                first.onMoved: {
                    let v = Math.round(first.value)
                    if (v > page.trimEndMs - 200) v = Math.max(0, page.trimEndMs - 200)
                    page.trimStartMs = v
                }
                second.onMoved: {
                    let v = Math.round(second.value)
                    if (v < page.trimStartMs + 200) v = Math.min(page.hasDuration ? editor.durationMs : 1, page.trimStartMs + 200)
                    page.trimEndMs = v
                }

                background: Rectangle {
                    x: parent.leftPadding
                    y: parent.topPadding + parent.availableHeight / 2 - 2
                    width: parent.availableWidth
                    height: 4
                    radius: 2
                    color: Theme.background

                    Rectangle {
                        x: parent.parent.first.visualPosition * parent.width
                        width: (parent.parent.second.visualPosition - parent.parent.first.visualPosition) * parent.width
                        height: parent.height
                        radius: parent.radius
                        color: Theme.cyan
                    }
                }

                first.handle: Rectangle {
                    x: parent.leftPadding + parent.first.visualPosition * (parent.availableWidth - width)
                    y: parent.topPadding + parent.availableHeight / 2 - height / 2
                    width: 14
                    height: 14
                    radius: 7
                    color: parent.pressed ? Theme.violet : Theme.cyan
                    border.color: Theme.background
                    border.width: 2
                }

                second.handle: Rectangle {
                    x: parent.leftPadding + parent.second.visualPosition * (parent.availableWidth - width)
                    y: parent.topPadding + parent.availableHeight / 2 - height / 2
                    width: 14
                    height: 14
                    radius: 7
                    color: parent.pressed ? Theme.violet : Theme.cyan
                    border.color: Theme.background
                    border.width: 2
                }
            }

            Text {
                text: page.formatTime(page.trimEndMs)
                color: Theme.cyan
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                Layout.preferredWidth: 50
                horizontalAlignment: Text.AlignHCenter
            }

            Connections {
                target: page
                function onTrimStartMsChanged() { trimSlider.first.value = page.trimStartMs }
                function onTrimEndMsChanged() { trimSlider.second.value = page.trimEndMs }
            }
        }

        // === Editing toolbar ===
        RowLayout {
            id: toolbarRow
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Repeater {
                model: [
                    { icon: "\u21A9", label: qsTr("Undo"), enabled: false, needsFile: false, tooltip: qsTr("Not supported") },
                    { icon: "\u21AA", label: qsTr("Redo"), enabled: false, needsFile: false, tooltip: qsTr("Not supported") },
                    { icon: "\u2702", label: qsTr("Cut"), enabled: false, needsFile: false, tooltip: qsTr("Not supported") },
                    { icon: "\u29C9", label: qsTr("Copy"), enabled: false, needsFile: false, tooltip: qsTr("Not supported") },
                    { icon: "\u25A4", label: qsTr("Paste"), enabled: false, needsFile: false, tooltip: qsTr("Not supported") },
                    { icon: "\u2715", label: qsTr("Delete"), enabled: false, needsFile: false, tooltip: qsTr("Not supported") },
                    { icon: "\u275A", label: qsTr("Split"), enabled: false, needsFile: false, tooltip: qsTr("Not supported") },
                    { icon: "\u224B", label: qsTr("Merge"), enabled: false, needsFile: false, tooltip: qsTr("Not supported") },
                    { icon: "\u25E2", label: qsTr("Fade In"), enabled: true, needsFile: true, tooltip: qsTr("Apply 1000 ms fade in") },
                    { icon: "\u25E3", label: qsTr("Fade Out"), enabled: true, needsFile: true, tooltip: qsTr("Apply 1000 ms fade out") },
                    { icon: "\u2298", label: qsTr("Mute"), enabled: true, needsFile: true, tooltip: qsTr("Set gain to silence") },
                    { icon: "\u25A3", label: qsTr("Crop"), enabled: true, needsFile: true, tooltip: qsTr("Crop selection to playhead") }
                ]

                Item {
                    Layout.preferredWidth: 52
                    Layout.preferredHeight: 52

                    Button {
                        id: toolBtn
                        anchors.fill: parent
                        enabled: modelData.enabled && (!modelData.needsFile || page.hasDuration)

                        onClicked: {
                            if (modelData.label === qsTr("Fade In")) page.applyFadeIn()
                            else if (modelData.label === qsTr("Fade Out")) page.applyFadeOut()
                            else if (modelData.label === qsTr("Mute")) gainSlider.value = 0.0
                            else if (modelData.label === qsTr("Crop")) page.applyCrop()
                        }

                        background: Rectangle {
                            color: !parent.enabled ? Theme.panel
                                  : parent.pressed ? Theme.violet
                                  : parent.hovered ? Theme.border
                                  : Theme.panel
                            border.color: !parent.enabled ? Theme.border : Theme.cyan
                            border.width: 1
                            radius: Theme.radiusSm
                        }

                        contentItem: Column {
                            anchors.centerIn: parent
                            spacing: 2

                            Text {
                                text: modelData.icon
                                color: parent.parent.enabled ? Theme.primaryText : Theme.secondaryText
                                font.pixelSize: 16
                                font.family: Theme.fontFallback
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Text {
                                text: modelData.label
                                color: parent.parent.enabled ? Theme.primaryText : Theme.secondaryText
                                font.pixelSize: 10
                                font.family: Theme.fontPrimary
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }

                    HoverHandler { id: toolHover }
                    ToolTip.text: modelData.tooltip
                    ToolTip.visible: toolHover.hovered && modelData.tooltip.length > 0
                    ToolTip.delay: 400
                }
            }
        }

        // === Fade / Gain parameters ===
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: Theme.panel
            radius: Theme.radiusSm
            border.color: Theme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingLg

                RowLayout {
                    spacing: Theme.spacingSm

                    Text {
                        text: qsTr("Fade In")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                    }

                    TextField {
                        id: fadeInField
                        Layout.preferredWidth: 70
                        text: "0"
                        color: Theme.primaryText
                        font.pixelSize: 12
                        font.family: Theme.fontPrimary
                        enabled: !editor.busy && page.hasDuration
                        validator: IntValidator { bottom: 0; top: 600000 }
                        horizontalAlignment: Text.AlignHCenter
                        background: Rectangle {
                            color: Theme.background
                            radius: Theme.radiusSm
                            border.color: Theme.border
                            border.width: 1
                        }
                    }

                    Text {
                        text: qsTr("ms")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 11
                    }
                }

                RowLayout {
                    spacing: Theme.spacingSm

                    Text {
                        text: qsTr("Fade Out")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                    }

                    TextField {
                        id: fadeOutField
                        Layout.preferredWidth: 70
                        text: "0"
                        color: Theme.primaryText
                        font.pixelSize: 12
                        font.family: Theme.fontPrimary
                        enabled: !editor.busy && page.hasDuration
                        validator: IntValidator { bottom: 0; top: 600000 }
                        horizontalAlignment: Text.AlignHCenter
                        background: Rectangle {
                            color: Theme.background
                            radius: Theme.radiusSm
                            border.color: Theme.border
                            border.width: 1
                        }
                    }

                    Text {
                        text: qsTr("ms")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 11
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Text {
                        text: qsTr("Gain")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                    }

                    Slider {
                        id: gainSlider
                        Layout.fillWidth: true
                        from: 0.0
                        to: 4.0
                        value: 1.0
                        stepSize: 0.05
                        enabled: !editor.busy && page.hasDuration

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
                            x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
                            y: parent.topPadding + parent.availableHeight / 2 - height / 2
                            width: 14
                            height: 14
                            radius: 7
                            color: parent.pressed ? Theme.violet : Theme.cyan
                            border.color: Theme.background
                            border.width: 2
                        }
                    }

                    Text {
                        text: page.gainToDb(gainSlider.value)
                        color: Theme.cyan
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        Layout.preferredWidth: 70
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }
        }

        // === Export settings panel ===
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 170
            color: Theme.panel
            radius: Theme.radiusSm
            border.color: Theme.border
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingMd

                Text {
                    text: qsTr("Export Settings")
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    Layout.fillWidth: true
                }

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
                            enabled: !editor.busy

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
                            enabled: !editor.busy

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

                    RowLayout {
                        spacing: Theme.spacingSm

                        Text {
                            text: qsTr("Channels")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                        }

                        ComboBox {
                            id: channelsCombo
                            Layout.preferredWidth: 120
                            model: [
                                { label: qsTr("Auto"), value: 0 },
                                { label: qsTr("Mono"), value: 1 },
                                { label: qsTr("Stereo"), value: 2 }
                            ]
                            textRole: "label"
                            valueRole: "value"
                            currentIndex: 0
                            enabled: !editor.busy

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

                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Text {
                        text: qsTr("Output Directory")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        Layout.preferredWidth: 110
                    }

                    TextField {
                        id: outputDirField
                        Layout.fillWidth: true
                        color: Theme.primaryText
                        font.pixelSize: 12
                        font.family: Theme.fontPrimary
                        placeholderText: qsTr("Same as source (default)")
                        enabled: !editor.busy
                        background: Rectangle {
                            color: Theme.background
                            radius: Theme.radiusSm
                            border.color: Theme.border
                            border.width: 1
                        }
                    }

                    Button {
                        text: qsTr("Browse")
                        enabled: !editor.busy
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
                        visible: editor.busy
                        onClicked: editor.cancel()

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

                    Button {
                        text: qsTr("Export")
                        enabled: !editor.busy && editor.hasInput
                        onClicked: {
                            const fadeIn = parseInt(fadeInField.text) || 0
                            const fadeOut = parseInt(fadeOutField.text) || 0
                            editor.start(page.trimStartMs,
                                         page.trimEndMs,
                                         fadeIn,
                                         fadeOut,
                                         gainSlider.value,
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

                // Info note.
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
                        text: qsTr("Output format, sample rate and channel settings are preview-only in this phase; export keeps the source codec.")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 11
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // === Progress bar ===
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs
            visible: editor.busy || editor.progress > 0

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: editor.busy ? qsTr("Processing...") : qsTr("Export complete")
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }

                Text {
                    text: Math.round(editor.progress * 100) + "%"
                    color: Theme.cyan
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
            }

            ProgressBar {
                Layout.fillWidth: true
                value: editor.progress
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
    }

    // Status message overlay.
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
        target: editor
        function onInputFileChanged() {
            page.trimStartMs = 0
            page.trimEndMs = page.hasDuration ? editor.durationMs : 0
            page.playheadMs = 0
            page.viewportOffsetMs = 0
            page.zoomScale = 1.0
            page.selectedTrack = 0

            trackModel.set(0, {
                name: editor.inputFileName,
                durationMs: editor.durationMs,
                peaks: editor.waveformPeaks,
                hasFile: editor.hasInput
            })
            for (let i = 1; i < 6; ++i) {
                trackModel.set(i, { name: "", durationMs: 0, peaks: [], hasFile: false })
            }
        }
        function onLightEditCompleted(outputPath) {
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

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

// Light Edit tool page: non-destructive trim, linear fade in/out, and gain.
// All edits are applied in a single decode -> edit -> re-encode pass via
// ag_light_edit. Trim sliders are bounded by the loaded file's duration
// (read from ag_metadata_open when a file is loaded).
Rectangle {
    id: page
    color: Theme.background

    property var editor: LightEditor
    property bool hasDuration: editor.durationMs > 0

    // Helper: format milliseconds as mm:ss
    function formatTime(ms) {
        if (ms <= 0) return "00:00"
        var totalSec = Math.floor(ms / 1000)
        var m = Math.floor(totalSec / 60)
        var s = totalSec % 60
        return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s
    }

    // Helper: convert linear gain to dB display string
    function gainToDb(g) {
        if (g <= 0.0001) return "-inf dB"
        var db = 20.0 * Math.log(g) / Math.log(10.0)
        return db.toFixed(1) + " dB"
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
                        editor.loadFile(drop.urls[0])
                        drop.acceptProposedAction()
                    }
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingMd

                Text {
                    text: editor.hasInput
                          ? editor.inputFileName
                            + (page.hasDuration ? "  (" + formatTime(editor.durationMs) + ")" : "")
                          : qsTr("Drop an audio file here or click Browse")
                    color: editor.hasInput ? Theme.primaryText : Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Button {
                    text: qsTr("Browse")
                    enabled: !editor.busy
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
                    enabled: editor.hasInput && !editor.busy
                    onClicked: editor.clear()

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

        // === Trim Section ===
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
                    text: qsTr("Trim")
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }

                // Trim start
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Text {
                        text: qsTr("Start")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 11
                        Layout.preferredWidth: 50
                    }

                    Slider {
                        id: trimStartSlider
                        Layout.fillWidth: true
                        from: 0
                        to: page.hasDuration ? editor.durationMs : 1
                        value: 0
                        stepSize: 100
                        enabled: !editor.busy && page.hasDuration

                        onMoved: {
                            if (value > trimEndSlider.value - 200) {
                                value = Math.max(0, trimEndSlider.value - 200)
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
                            width: 14
                            height: 14
                            radius: 7
                            color: parent.pressed ? Theme.violet : Theme.cyan
                            border.color: Theme.background
                            border.width: 2
                        }
                    }

                    Text {
                        text: formatTime(trimStartSlider.value)
                        color: Theme.cyan
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        Layout.preferredWidth: 56
                        horizontalAlignment: Text.AlignRight
                    }
                }

                // Trim end
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Text {
                        text: qsTr("End")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 11
                        Layout.preferredWidth: 50
                    }

                    Slider {
                        id: trimEndSlider
                        Layout.fillWidth: true
                        from: 0
                        to: page.hasDuration ? editor.durationMs : 1
                        value: page.hasDuration ? editor.durationMs : 0
                        stepSize: 100
                        enabled: !editor.busy && page.hasDuration

                        onMoved: {
                            if (value < trimStartSlider.value + 200) {
                                value = Math.min(page.hasDuration ? editor.durationMs : 1,
                                                 trimStartSlider.value + 200)
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
                            width: 14
                            height: 14
                            radius: 7
                            color: parent.pressed ? Theme.violet : Theme.cyan
                            border.color: Theme.background
                            border.width: 2
                        }
                    }

                    Text {
                        text: formatTime(trimEndSlider.value)
                        color: Theme.cyan
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        Layout.preferredWidth: 56
                        horizontalAlignment: Text.AlignRight
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Selected length: %1  (of %2)")
                          .arg(formatTime(trimEndSlider.value - trimStartSlider.value))
                          .arg(formatTime(page.hasDuration ? editor.durationMs : 0))
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        // === Fade + Gain Section ===
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 160
            color: Theme.panel
            radius: Theme.radiusSm
            border.color: Theme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingLg

                // Fade in
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.spacingSm

                    Text {
                        text: qsTr("Fade In (ms)")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 11
                    }

                    TextField {
                        id: fadeInField
                        Layout.fillWidth: true
                        text: "0"
                        color: Theme.primaryText
                        font.pixelSize: 12
                        font.family: Theme.fontPrimary
                        enabled: !editor.busy
                        validator: IntValidator { bottom: 0; top: 600000 }
                        background: Rectangle {
                            color: Theme.background
                            radius: Theme.radiusSm
                            border.color: Theme.border
                            border.width: 1
                        }
                    }

                    Text {
                        text: qsTr("Fade Out (ms)")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 11
                    }

                    TextField {
                        id: fadeOutField
                        Layout.fillWidth: true
                        text: "0"
                        color: Theme.primaryText
                        font.pixelSize: 12
                        font.family: Theme.fontPrimary
                        enabled: !editor.busy
                        validator: IntValidator { bottom: 0; top: 600000 }
                        background: Rectangle {
                            color: Theme.background
                            radius: Theme.radiusSm
                            border.color: Theme.border
                            border.width: 1
                        }
                    }
                }

                // Gain
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.spacingSm

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: qsTr("Gain")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                            Layout.preferredWidth: 50
                        }

                        Slider {
                            id: gainSlider
                            Layout.fillWidth: true
                            from: 0.0
                            to: 4.0
                            value: 1.0
                            stepSize: 0.05
                            enabled: !editor.busy

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
                                width: 14
                                height: 14
                                radius: 7
                                color: parent.pressed ? Theme.violet : Theme.cyan
                                border.color: Theme.background
                                border.width: 2
                            }
                        }

                        Text {
                            text: gainToDb(gainSlider.value)
                            color: Theme.cyan
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                            Layout.preferredWidth: 70
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    // Gain preset buttons
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs

                        Item { Layout.fillWidth: true }

                        Repeater {
                            model: [
                                { label: "0.5x", value: 0.5 },
                                { label: "1.0x", value: 1.0 },
                                { label: "1.5x", value: 1.5 },
                                { label: "2.0x", value: 2.0 }
                            ]

                            Button {
                                text: modelData.label
                                Layout.preferredWidth: 48
                                enabled: !editor.busy
                                onClicked: gainSlider.value = modelData.value

                                background: Rectangle {
                                    color: Math.abs(gainSlider.value - modelData.value) < 0.025
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
                                    color: Math.abs(gainSlider.value - modelData.value) < 0.025
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

                    Item { Layout.fillHeight: true }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Linear gain factor. 1.0 = no change.")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 10
                        horizontalAlignment: Text.AlignHCenter
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
                enabled: !editor.busy
                background: Rectangle {
                    color: Theme.panel
                    radius: Theme.radiusSm
                    border.color: Theme.border
                    border.width: 1
                }
            }

            Button {
                text: qsTr("Browse")
                enabled: !editor.busy
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
                visible: editor.busy
                onClicked: editor.cancel()

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
                enabled: !editor.busy && editor.hasInput
                onClicked: {
                    var fadeIn = parseInt(fadeInField.text) || 0
                    var fadeOut = parseInt(fadeOutField.text) || 0
                    editor.start(Math.round(trimStartSlider.value),
                                 Math.round(trimEndSlider.value),
                                 fadeIn, fadeOut,
                                 gainSlider.value,
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
            visible: editor.busy || editor.progress > 0

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: editor.busy ? qsTr("Processing...")
                                       : qsTr("Export complete")
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

    // Reset trim end slider when a new file loads.
    Connections {
        target: editor
        function onInputFileChanged() {
            trimStartSlider.value = 0
            trimEndSlider.value = page.hasDuration ? editor.durationMs : 0
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

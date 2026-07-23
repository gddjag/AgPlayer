import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

// Info Edit tool page: batch metadata editing (top) and batch filename
// renaming (bottom). Files are imported via drag-drop or file dialog.
// Metadata is written via FFmpeg stream copy; renaming is a filesystem op.
Rectangle {
    id: page
    color: Theme.background

    property var editor: MetadataEditor
    property var selectedIndices: [] // tracked locally for "Selected Files" apply

    function formatDuration(ms) {
        if (ms <= 0) {
            return "00:00"
        }
        const totalSec = Math.floor(ms / 1000)
        const min = Math.floor(totalSec / 60)
        const sec = totalSec % 60
        return (min < 10 ? "0" + min : min) + ":" + (sec < 10 ? "0" + sec : sec)
    }

    function isSelected(index) {
        return selectedIndices.indexOf(index) !== -1
    }

    function toggleSelection(index) {
        const idx = selectedIndices.indexOf(index)
        if (idx === -1) {
            selectedIndices.push(index)
        } else {
            selectedIndices.splice(idx, 1)
        }
        selectedIndicesChanged()
    }

    Component {
        id: audioFileDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFiles
            nameFilters: [qsTr("Audio files (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)")]
            onAccepted: editor.loadFiles(files)
        }
    }

    Component {
        id: imageFileDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFile
            nameFilters: [qsTr("Image files (*.png *.jpg *.jpeg *.gif *.bmp *.webp)")]
            onAccepted: editor.setCoverImage(file)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        // === Top Section: Metadata Batch Edit ===
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMd

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Text {
                    text: qsTr("Batch Metadata Edit")
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 16
                    font.weight: Font.Medium
                }

                Text {
                    text: "\u24D8"
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 14
                }

                Item { Layout.fillWidth: true }
            }

            // Drag-and-drop import area
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 72
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
                            editor.loadFiles(drop.urls)
                            drop.acceptProposedAction()
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        const dlg = audioFileDialogComponent.createObject(page)
                        dlg.open()
                    }
                }

                RowLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacingMd

                    Text {
                        text: "\uFF0B"
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 24
                    }

                    ColumnLayout {
                        spacing: Theme.spacingXs

                        Text {
                            text: qsTr("Drop audio files here or click to add")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 13
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: qsTr("Supports MP3 / WAV / FLAC / M4A, etc.")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.spacingMd

                // File list (left)
                Rectangle {
                    id: fileListContainer
                    Layout.fillHeight: true
                    Layout.preferredWidth: parent.width * 0.42
                    color: Theme.panel
                    radius: Theme.radiusSm
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingMd
                        spacing: Theme.spacingSm

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSm

                            Text {
                                text: qsTr("File List") + " (" + editor.fileCount + ")"
                                color: Theme.primaryText
                                font.family: Theme.fontPrimary
                                font.pixelSize: 13
                                font.weight: Font.Medium
                            }

                            Item { Layout.fillWidth: true }

                            Button {
                                text: qsTr("Clear List")
                                enabled: editor.fileCount > 0
                                onClicked: editor.clear()

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

                        ListView {
                            id: fileList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: editor.fileCount
                            delegate: Rectangle {
                                width: fileList.width
                                height: 36
                                color: {
                                    if (page.isSelected(index)) {
                                        return Qt.rgba(0, 0.83, 1, 0.12)
                                    }
                                    return index % 2 === 0 ? "transparent"
                                                           : Qt.rgba(1, 1, 1, 0.02)
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: page.toggleSelection(index)
                                }

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
                                        Layout.preferredWidth: 28
                                    }

                                    Text {
                                        text: "\u266A"
                                        color: Theme.secondaryText
                                        font.family: Theme.fontPrimary
                                        font.pixelSize: 12
                                        Layout.preferredWidth: 18
                                    }

                                    Text {
                                        text: editor.entryAt(index).fileName
                                        color: Theme.primaryText
                                        font.family: Theme.fontPrimary
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: page.formatDuration(editor.entryAt(index).durationMs)
                                        color: Theme.secondaryText
                                        font.family: Theme.fontPrimary
                                        font.pixelSize: 11
                                        Layout.alignment: Qt.AlignRight
                                    }
                                }
                            }

                            // Empty state inside the list panel
                            ColumnLayout {
                                anchors.centerIn: parent
                                visible: editor.fileCount === 0
                                spacing: Theme.spacingSm

                                Text {
                                    text: qsTr("No audio files")
                                    color: Theme.secondaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 13
                                    Layout.alignment: Qt.AlignHCenter
                                }
                            }
                        }
                    }
                }

                // Metadata form (right)
                Rectangle {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    color: Theme.panel
                    radius: Theme.radiusSm
                    border.color: Theme.border
                    border.width: 1

                    GridLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingMd
                        columns: 2
                        rowSpacing: Theme.spacingSm
                        columnSpacing: Theme.spacingMd

                        Label {
                            text: qsTr("Title")
                            color: Theme.secondaryText
                            font.pixelSize: 12
                            font.family: Theme.fontPrimary
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        }
                        TextField {
                            id: titleField
                            Layout.fillWidth: true
                            color: Theme.primaryText
                            font.pixelSize: 12
                            font.family: Theme.fontPrimary
                            placeholderText: qsTr("e.g., Song Title")
                            background: Rectangle {
                                color: Theme.background
                                radius: Theme.radiusSm
                                border.color: Theme.border
                                border.width: 1
                            }
                        }

                        Label {
                            text: qsTr("Artist")
                            color: Theme.secondaryText
                            font.pixelSize: 12
                            font.family: Theme.fontPrimary
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        }
                        TextField {
                            id: artistField
                            Layout.fillWidth: true
                            color: Theme.primaryText
                            font.pixelSize: 12
                            font.family: Theme.fontPrimary
                            placeholderText: qsTr("e.g., Artist Name")
                            background: Rectangle {
                                color: Theme.background
                                radius: Theme.radiusSm
                                border.color: Theme.border
                                border.width: 1
                            }
                        }

                        Label {
                            text: qsTr("Album")
                            color: Theme.secondaryText
                            font.pixelSize: 12
                            font.family: Theme.fontPrimary
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        }
                        TextField {
                            id: albumField
                            Layout.fillWidth: true
                            color: Theme.primaryText
                            font.pixelSize: 12
                            font.family: Theme.fontPrimary
                            placeholderText: qsTr("e.g., Album Name")
                            background: Rectangle {
                                color: Theme.background
                                radius: Theme.radiusSm
                                border.color: Theme.border
                                border.width: 1
                            }
                        }

                        Label {
                            text: qsTr("Cover")
                            color: Theme.secondaryText
                            font.pixelSize: 12
                            font.family: Theme.fontPrimary
                            Layout.alignment: Qt.AlignRight | Qt.AlignTop
                            Layout.topMargin: Theme.spacingSm
                        }
                        RowLayout {
                            spacing: Theme.spacingMd

                            Rectangle {
                                Layout.preferredWidth: 96
                                Layout.preferredHeight: 96
                                color: Theme.background
                                radius: Theme.radiusSm
                                border.color: Theme.border
                                border.width: 1

                                Image {
                                    id: coverImage
                                    anchors.fill: parent
                                    anchors.margins: Theme.spacingSm
                                    source: editor.coverImage
                                    fillMode: Image.PreserveAspectFit
                                    visible: editor.coverImage !== ""
                                }

                                ColumnLayout {
                                    anchors.centerIn: parent
                                    visible: editor.coverImage === ""
                                    spacing: Theme.spacingXs

                                    Text {
                                        text: "\uD83D\uDBC4"
                                        color: Theme.secondaryText
                                        font.family: Theme.fontPrimary
                                        font.pixelSize: 28
                                        Layout.alignment: Qt.AlignHCenter
                                    }

                                    Text {
                                        text: "\uFF0B"
                                        color: Theme.secondaryText
                                        font.family: Theme.fontPrimary
                                        font.pixelSize: 14
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                }
                            }

                            ColumnLayout {
                                spacing: Theme.spacingSm

                                Button {
                                    text: qsTr("Select Image")
                                    onClicked: {
                                        const dlg = imageFileDialogComponent.createObject(page)
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
                                    text: qsTr("Clear Image")
                                    enabled: editor.coverImage !== ""
                                    onClicked: editor.clearCoverImage()

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
                                        font.pixelSize: 12
                                        font.family: Theme.fontPrimary
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }

                        Item { Layout.fillHeight: true; Layout.columnSpan: 2 }

                        // Apply range + action button
                        RowLayout {
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            spacing: Theme.spacingMd

                            ColumnLayout {
                                spacing: Theme.spacingXs

                                Text {
                                    text: qsTr("Apply Range:")
                                    color: Theme.secondaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 12
                                }

                                RowLayout {
                                    spacing: Theme.spacingMd

                                    ButtonGroup { id: rangeGroup }

                                    RadioButton {
                                        id: selectedFilesRadio
                                        text: qsTr("Apply to Selected Files")
                                        ButtonGroup.group: rangeGroup

                                        contentItem: Text {
                                            text: parent.text
                                            color: Theme.primaryText
                                            font.pixelSize: 12
                                            font.family: Theme.fontPrimary
                                            leftPadding: parent.indicator.width + parent.spacing
                                            verticalAlignment: Text.AlignVCenter
                                        }

                                        indicator: Rectangle {
                                            implicitWidth: 16
                                            implicitHeight: 16
                                            x: parent.leftPadding
                                            y: parent.height / 2 - height / 2
                                            radius: width / 2
                                            color: "transparent"
                                            border.color: parent.checked ? Theme.cyan : Theme.border
                                            border.width: 1

                                            Rectangle {
                                                width: 8
                                                height: 8
                                                anchors.centerIn: parent
                                                radius: width / 2
                                                color: Theme.cyan
                                                visible: parent.parent.checked
                                            }
                                        }
                                    }

                                    RadioButton {
                                        id: allFilesRadio
                                        text: qsTr("Apply to All Files")
                                        checked: true
                                        ButtonGroup.group: rangeGroup

                                        contentItem: Text {
                                            text: parent.text
                                            color: Theme.primaryText
                                            font.pixelSize: 12
                                            font.family: Theme.fontPrimary
                                            leftPadding: parent.indicator.width + parent.spacing
                                            verticalAlignment: Text.AlignVCenter
                                        }

                                        indicator: Rectangle {
                                            implicitWidth: 16
                                            implicitHeight: 16
                                            x: parent.leftPadding
                                            y: parent.height / 2 - height / 2
                                            radius: width / 2
                                            color: "transparent"
                                            border.color: parent.checked ? Theme.cyan : Theme.border
                                            border.width: 1

                                            Rectangle {
                                                width: 8
                                                height: 8
                                                anchors.centerIn: parent
                                                radius: width / 2
                                                color: Theme.cyan
                                                visible: parent.parent.checked
                                            }
                                        }
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true }

                            Button {
                                text: qsTr("Process")
                                enabled: !editor.busy && editor.fileCount > 0
                                onClicked: {
                                    const fields = {
                                        "title": titleField.text,
                                        "artist": artistField.text,
                                        "album": albumField.text
                                    }
                                    let indices = []
                                    if (selectedFilesRadio.checked) {
                                        indices = page.selectedIndices
                                    }
                                    editor.applyMetadata(fields, indices)
                                }

                                background: Rectangle {
                                    color: !parent.enabled ? Theme.panel
                                          : parent.pressed ? Theme.violet
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
                    }
                }
            }
        }

        // === Bottom Section: Filename Batch Rename ===
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 300
            spacing: Theme.spacingMd

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Text {
                    text: qsTr("Batch Filename Rename")
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 16
                    font.weight: Font.Medium
                }

                Text {
                    text: "\u24D8"
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 14
                }

                Item { Layout.fillWidth: true }
            }

            // Drag-and-drop import area for rename
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 64
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
                            editor.loadFiles(drop.urls)
                            drop.acceptProposedAction()
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        const dlg = audioFileDialogComponent.createObject(page)
                        dlg.open()
                    }
                }

                RowLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacingMd

                    Text {
                        text: "\uFF0B"
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 24
                    }

                    ColumnLayout {
                        spacing: Theme.spacingXs

                        Text {
                            text: qsTr("Drop audio files here or click to add")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 13
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: qsTr("Supports MP3 / WAV / FLAC / M4A, etc.")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                            Layout.alignment: Qt.AlignHCenter
                        }
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

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    spacing: Theme.spacingLg

                    // Rename controls
                    ColumnLayout {
                        Layout.fillHeight: true
                        Layout.preferredWidth: parent.width * 0.45
                        spacing: Theme.spacingMd

                        Text {
                            text: qsTr("Naming Rule Settings")
                            color: Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 13
                            font.weight: Font.Medium
                        }

                        RowLayout {
                            spacing: Theme.spacingSm

                            Label {
                                text: qsTr("Prefix")
                                color: Theme.secondaryText
                                font.pixelSize: 12
                                font.family: Theme.fontPrimary
                                Layout.preferredWidth: 56
                            }

                            TextField {
                                id: prefixField
                                Layout.fillWidth: true
                                color: Theme.primaryText
                                font.pixelSize: 12
                                font.family: Theme.fontPrimary
                                placeholderText: qsTr("Optional prefix")
                                background: Rectangle {
                                    color: Theme.background
                                    radius: Theme.radiusSm
                                    border.color: Theme.border
                                    border.width: 1
                                }
                            }

                            CheckBox {
                                id: autoNumberCheck
                                checked: true

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
                                    text: qsTr("Auto Number")
                                    color: Theme.primaryText
                                    font.pixelSize: 12
                                    font.family: Theme.fontPrimary
                                    leftPadding: parent.indicator.width + parent.spacing
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }

                            Label {
                                text: qsTr("Start")
                                color: Theme.secondaryText
                                font.pixelSize: 12
                                font.family: Theme.fontPrimary
                                enabled: autoNumberCheck.checked
                            }

                            SpinBox {
                                id: numberStartSpin
                                from: 0
                                to: 99999
                                value: 1
                                enabled: autoNumberCheck.checked

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

                            Label {
                                text: qsTr("Digits")
                                color: Theme.secondaryText
                                font.pixelSize: 12
                                font.family: Theme.fontPrimary
                                enabled: autoNumberCheck.checked
                            }

                            SpinBox {
                                id: numberDigitsSpin
                                from: 1
                                to: 5
                                value: 2
                                enabled: autoNumberCheck.checked

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
                        }

                        RowLayout {
                            spacing: Theme.spacingSm

                            Label {
                                text: qsTr("Suffix")
                                color: Theme.secondaryText
                                font.pixelSize: 12
                                font.family: Theme.fontPrimary
                                Layout.preferredWidth: 56
                            }

                            TextField {
                                id: suffixField
                                Layout.fillWidth: true
                                color: Theme.primaryText
                                font.pixelSize: 12
                                font.family: Theme.fontPrimary
                                placeholderText: qsTr("Optional suffix")
                                background: Rectangle {
                                    color: Theme.background
                                    radius: Theme.radiusSm
                                    border.color: Theme.border
                                    border.width: 1
                                }
                            }

                            Text {
                                text: qsTr("Example: %1").arg(editor.renameExample(
                                    prefixField.text,
                                    suffixField.text,
                                    autoNumberCheck.checked,
                                    numberStartSpin.value,
                                    numberDigitsSpin.value))
                                color: Theme.secondaryText
                                font.family: Theme.fontPrimary
                                font.pixelSize: 12
                            }

                            Item { Layout.fillWidth: true }
                        }

                        Item { Layout.fillHeight: true }

                        Button {
                            text: qsTr("Process")
                            enabled: !editor.busy && editor.fileCount > 0
                            onClicked: {
                                editor.applyRename(prefixField.text,
                                                   suffixField.text,
                                                   autoNumberCheck.checked,
                                                   numberStartSpin.value,
                                                   numberDigitsSpin.value)
                            }

                            background: Rectangle {
                                color: !parent.enabled ? Theme.panel
                                      : parent.pressed ? Theme.violet
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

                    // Preview table
                    Rectangle {
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        color: Theme.background
                        radius: Theme.radiusSm
                        border.color: Theme.border
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.spacingMd
                            spacing: Theme.spacingSm

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingSm

                                Text {
                                    text: qsTr("File Preview") + " (" + editor.fileCount + ")"
                                    color: Theme.primaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                }

                                Item { Layout.fillWidth: true }

                                Button {
                                    text: qsTr("Clear List")
                                    enabled: editor.fileCount > 0
                                    onClicked: editor.clear()

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

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingSm

                                Text {
                                    text: qsTr("#")
                                    color: Theme.secondaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 11
                                    Layout.preferredWidth: 28
                                }

                                Text {
                                    text: qsTr("Original File Name")
                                    color: Theme.secondaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 11
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: qsTr("New File Name (Preview)")
                                    color: Theme.secondaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 11
                                    Layout.fillWidth: true
                                }
                            }

                            ScrollView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                ListView {
                                    id: previewList
                                    clip: true
                                    model: editor.renamePreviewEntries(
                                        prefixField.text,
                                        suffixField.text,
                                        autoNumberCheck.checked,
                                        numberStartSpin.value,
                                        numberDigitsSpin.value)
                                    delegate: RowLayout {
                                        width: previewList.width
                                        height: 28
                                        spacing: Theme.spacingSm

                                        Text {
                                            text: (index + 1) + "."
                                            color: Theme.secondaryText
                                            font.family: Theme.fontPrimary
                                            font.pixelSize: 11
                                            Layout.preferredWidth: 28
                                        }

                                        Text {
                                            text: modelData.original
                                            color: Theme.primaryText
                                            font.family: Theme.fontPrimary
                                            font.pixelSize: 11
                                            elide: Text.ElideMiddle
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            text: "\u2192"
                                            color: Theme.secondaryText
                                            font.family: Theme.fontPrimary
                                            font.pixelSize: 11
                                        }

                                        Text {
                                            text: modelData.preview
                                            color: Theme.primaryText
                                            font.family: Theme.fontPrimary
                                            font.pixelSize: 11
                                            elide: Text.ElideMiddle
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                            }
                        }
                    }
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
                spacing: Theme.spacingSm

                Text {
                    text: editor.busy ? qsTr("Processing...") : qsTr("Done")
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

        // Privacy note
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
                text: qsTr("All operations are performed locally; no files are uploaded.")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }
        }
    }

    // Pre-fill form with the first file's metadata when entries are loaded.
    Connections {
        target: editor
        function onEntriesLoaded() {
            titleField.text = ""
            artistField.text = ""
            albumField.text = ""
            page.selectedIndices = []
            if (editor.fileCount > 0) {
                const first = editor.entryAt(0)
                titleField.text = first.title
                artistField.text = first.artist
                albumField.text = first.album
                for (let i = 0; i < editor.fileCount; ++i) {
                    page.selectedIndices.push(i)
                }
            }
            page.selectedIndicesChanged()
        }
    }
}

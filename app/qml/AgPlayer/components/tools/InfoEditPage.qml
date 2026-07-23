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
    property var selectedIndices: []  // tracked locally for "Selected Files" apply

    Component {
        id: fileDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFiles
            nameFilters: [qsTr("Audio files (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)")]
            onAccepted: editor.loadFiles(files)
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
            spacing: Theme.spacingSm

            Text {
                text: qsTr("METADATA BATCH EDIT")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 11
                font.capitalization: Font.AllUppercase
                font.weight: Font.Medium
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.spacingMd

                // File list (left, 40%)
                Rectangle {
                    id: fileListContainer
                    Layout.fillHeight: true
                    Layout.preferredWidth: parent.width * 0.4
                    color: Theme.panel
                    radius: Theme.radiusSm
                    border.color: Theme.border
                    border.width: 1

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

                    ListView {
                        id: fileList
                        anchors.fill: parent
                        anchors.margins: 1
                        clip: true
                        model: editor.fileCount
                        delegate: Rectangle {
                            width: fileList.width
                            height: 36
                            color: index % 2 === 0 ? "transparent"
                                                   : Qt.rgba(1, 1, 1, 0.02)

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spacingSm
                                anchors.rightMargin: Theme.spacingSm
                                spacing: Theme.spacingSm

                                CheckBox {
                                    id: fileCheck
                                    checked: true
                                    onCheckedChanged: {
                                        if (checked) {
                                            if (page.selectedIndices.indexOf(index) === -1)
                                                page.selectedIndices.push(index)
                                        } else {
                                            var idx = page.selectedIndices.indexOf(index)
                                            if (idx !== -1)
                                                page.selectedIndices.splice(idx, 1)
                                        }
                                        page.selectedIndicesChanged()
                                    }
                                    Component.onCompleted: {
                                        if (page.selectedIndices.indexOf(index) === -1)
                                            page.selectedIndices.push(index)
                                    }
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
                                    text: editor.entryAt(index).format
                                    color: Theme.secondaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }

                    // Empty state
                    ColumnLayout {
                        anchors.centerIn: parent
                        visible: editor.fileCount === 0
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
                        }
                    }
                }

                // Metadata form (right, 60%)
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
                            Layout.alignment: Qt.AlignRight
                        }
                        TextField {
                            id: titleField
                            Layout.fillWidth: true
                            color: Theme.primaryText
                            font.pixelSize: 12
                            font.family: Theme.fontPrimary
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
                            Layout.alignment: Qt.AlignRight
                        }
                        TextField {
                            id: artistField
                            Layout.fillWidth: true
                            color: Theme.primaryText
                            font.pixelSize: 12
                            font.family: Theme.fontPrimary
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
                            Layout.alignment: Qt.AlignRight
                        }
                        TextField {
                            id: albumField
                            Layout.fillWidth: true
                            color: Theme.primaryText
                            font.pixelSize: 12
                            font.family: Theme.fontPrimary
                            background: Rectangle {
                                color: Theme.background
                                radius: Theme.radiusSm
                                border.color: Theme.border
                                border.width: 1
                            }
                        }

                        Label {
                            text: qsTr("Year")
                            color: Theme.secondaryText
                            font.pixelSize: 12
                            font.family: Theme.fontPrimary
                            Layout.alignment: Qt.AlignRight
                        }
                        TextField {
                            id: yearField
                            Layout.fillWidth: true
                            color: Theme.primaryText
                            font.pixelSize: 12
                            font.family: Theme.fontPrimary
                            background: Rectangle {
                                color: Theme.background
                                radius: Theme.radiusSm
                                border.color: Theme.border
                                border.width: 1
                            }
                        }

                        Label {
                            text: qsTr("Genre")
                            color: Theme.secondaryText
                            font.pixelSize: 12
                            font.family: Theme.fontPrimary
                            Layout.alignment: Qt.AlignRight
                        }
                        TextField {
                            id: genreField
                            Layout.fillWidth: true
                            color: Theme.primaryText
                            font.pixelSize: 12
                            font.family: Theme.fontPrimary
                            background: Rectangle {
                                color: Theme.background
                                radius: Theme.radiusSm
                                border.color: Theme.border
                                border.width: 1
                            }
                        }

                        Item { Layout.fillHeight: true; Layout.columnSpan: 2 }

                        // Apply range + button
                        RowLayout {
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            spacing: Theme.spacingMd

                            ButtonGroup { id: rangeGroup }

                            RadioButton {
                                text: qsTr("All Files")
                                checked: true
                                ButtonGroup.group: rangeGroup
                                color: Theme.primaryText
                            }
                            RadioButton {
                                text: qsTr("Selected Files")
                                ButtonGroup.group: rangeGroup
                                color: Theme.primaryText
                            }

                            Item { Layout.fillWidth: true }

                            Button {
                                text: qsTr("Batch Apply")
                                enabled: !editor.busy && editor.fileCount > 0
                                onClicked: {
                                    var fields = {
                                        "title": titleField.text,
                                        "artist": artistField.text,
                                        "album": albumField.text,
                                        "year": yearField.text,
                                        "genre": genreField.text
                                    }
                                    var indices = []
                                    if (rangeGroup.checkedButton.text === qsTr("Selected Files")) {
                                        indices = page.selectedIndices
                                    }
                                    editor.applyMetadata(fields, indices)
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
            }

            // Progress bar
            ProgressBar {
                Layout.fillWidth: true
                visible: editor.busy
                value: editor.progress
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

        // === Bottom Section: Filename Batch Rename ===
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 260
            spacing: Theme.spacingSm

            Text {
                text: qsTr("BATCH FILENAME RENAME")
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

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    spacing: Theme.spacingLg

                    // Rename controls (left)
                    ColumnLayout {
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        RowLayout {
                            spacing: Theme.spacingSm
                            Label {
                                text: qsTr("Prefix")
                                color: Theme.secondaryText
                                font.pixelSize: 12
                                font.family: Theme.fontPrimary
                            }
                            TextField {
                                id: prefixField
                                Layout.fillWidth: true
                                color: Theme.primaryText
                                font.pixelSize: 12
                                font.family: Theme.fontPrimary
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
                            }
                            TextField {
                                id: suffixField
                                Layout.fillWidth: true
                                color: Theme.primaryText
                                font.pixelSize: 12
                                font.family: Theme.fontPrimary
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
                            CheckBox {
                                id: autoNumberCheck
                                text: qsTr("Auto Number")
                                checked: false
                                color: Theme.primaryText
                                font.family: Theme.fontPrimary
                                font.pixelSize: 12
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
                            }
                        }

                        Item { Layout.fillHeight: true }

                        Button {
                            text: qsTr("Batch Rename")
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

                    // Preview list (right)
                    ColumnLayout {
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs

                        Text {
                            text: qsTr("PREVIEW")
                            color: Theme.secondaryText
                            font.pixelSize: 10
                            font.family: Theme.fontPrimary
                            font.capitalization: Font.AllUppercase
                        }

                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ListView {
                                id: previewList
                                clip: true
                                model: editor.previewRename(
                                    prefixField.text,
                                    suffixField.text,
                                    autoNumberCheck.checked,
                                    numberStartSpin.value,
                                    numberDigitsSpin.value)
                                delegate: Text {
                                    text: modelData
                                    color: Theme.primaryText
                                    font.pixelSize: 11
                                    font.family: Theme.fontPrimary
                                    elide: Text.ElideMiddle
                                    width: previewList.width
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Pre-fill form with first file's metadata when entries are loaded.
    Connections {
        target: editor
        function onEntriesLoaded() {
            if (editor.fileCount > 0) {
                var first = editor.entryAt(0)
                titleField.text = first.title
                artistField.text = first.artist
                albumField.text = first.album
                yearField.text = first.year
                genreField.text = first.genre
                page.selectedIndices = []
                for (var i = 0; i < editor.fileCount; ++i) {
                    page.selectedIndices.push(i)
                }
                page.selectedIndicesChanged()
            }
        }
    }
}

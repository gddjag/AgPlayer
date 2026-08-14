import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    property var converter
    property string outputDirectory: ""
    property string outputFormat: converter.selectedFormat
    property int bitRate: bitRateBox.currentValue || 320000
    property int sampleRate: sampleRateBox.currentValue || 0
    property int channels: channelLayout === "mono" ? 1
                           : channelLayout === "stereo" ? 2 : 0
    property string bitrateMode: cbrButton.checked ? "cbr" : "vbr"
    property string conflictPolicy: conflictBox.currentValue || "auto-number"
    property string sampleFormat: sampleFormatBox.currentValue || ""
    property string channelLayout: channelBox.currentValue || ""
    property bool keepMetadata: keepMetadataCheck.checked
    property bool keepCover: keepCoverCheck.checked
    property bool preserveDirectories: preserveDirectoriesCheck.checked
    property bool extractAudio: extractAudioCheck.checked
    property bool volumeNormalize: false
    signal chooseOutputDirectory()

    color: Theme.panel
    border.color: Theme.border
    radius: 6
    clip: true

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: parent.width
            spacing: 6

            Text {
                Layout.leftMargin: 16
                Layout.topMargin: 10
                text: qsTr("转换设置")
                color: Theme.primaryText
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }

            ColumnLayout {
                id: formatGroup
                objectName: "formatOutputFormatGroup"
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                spacing: 6
                Text { text: qsTr("A. 输出格式"); color: Theme.secondaryText; font.pixelSize: 13 }
                GridLayout {
                    columns: 4
                    rowSpacing: 6
                    columnSpacing: 8
                    Repeater {
                        model: converter.outputCapabilities
                        Button {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            enabled: modelData.available
                            text: modelData.label
                            checkable: true
                            checked: root.outputFormat === modelData.key
                            onClicked: {
                                root.outputFormat = modelData.key
                                converter.selectedFormat = modelData.key
                            }
                            ToolTip.visible: hovered && !modelData.available
                            ToolTip.text: modelData.reason
                            background: Rectangle {
                                color: parent.checked ? Theme.accent : Theme.elevated
                                border.color: parent.checked ? Theme.accent : Theme.border
                                radius: 5
                            }
                            contentItem: Text {
                                text: parent.text
                                color: parent.enabled ? Theme.primaryText : Theme.secondaryText
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 13
                            }
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.preferredHeight: 1; color: Theme.border }

            GridLayout {
                id: encodingGroup
                objectName: "formatEncodingSettingsGroup"
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                columns: 2
                columnSpacing: 10
                rowSpacing: 6
                Text { text: qsTr("B. 编码参数"); color: Theme.secondaryText; font.pixelSize: 13; Layout.columnSpan: 2 }
                Text { text: qsTr("编码器"); color: Theme.secondaryText }
                ComboBox { objectName: "formatEncoderBox"; Layout.fillWidth: true; model: [converter.currentCapability.encoderLabel || "--"] }
                Text { text: qsTr("码率模式"); color: Theme.secondaryText }
                RowLayout {
                    Button {
                        id: cbrButton; text: "CBR"; checkable: true; checked: true
                        Layout.fillWidth: true; onClicked: vbrButton.checked = false
                        background: Rectangle { color: parent.checked ? Theme.accent : Theme.elevated; border.color: parent.checked ? Theme.accent : Theme.border; radius: 5 }
                        contentItem: Text { text: parent.text; color: Theme.primaryText; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        id: vbrButton; text: "VBR"; checkable: true
                        Layout.fillWidth: true; onClicked: cbrButton.checked = false
                        background: Rectangle { color: parent.checked ? Theme.accent : Theme.elevated; border.color: parent.checked ? Theme.accent : Theme.border; radius: 5 }
                        contentItem: Text { text: parent.text; color: Theme.primaryText; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }
                Text { text: qsTr("目标码率"); color: Theme.secondaryText }
                ComboBox { id: bitRateBox; Layout.fillWidth: true; enabled: converter.currentCapability.lossy === true; model: [{text:"128 kbps",value:128000},{text:"192 kbps",value:192000},{text:"256 kbps",value:256000},{text:"320 kbps",value:320000}]; textRole:"text"; valueRole:"value"; currentIndex:3 }
                Text { text: qsTr("采样率"); color: Theme.secondaryText }
                ComboBox {
                    id: sampleRateBox
                    Layout.fillWidth: true
                    model: [{text:qsTr("原始采样率（自动）"), value:0}].concat(
                        (converter.currentCapability.sampleRates || []).map(function(value) {
                            return { text: (value / 1000) + " kHz", value: value }
                        }))
                    textRole: "text"; valueRole: "value"
                }
                Text { text: qsTr("声道"); color: Theme.secondaryText }
                ComboBox { id: channelBox; Layout.fillWidth: true; model: [{text:qsTr("自动"),value:""},{text:qsTr("单声道"),value:"mono"},{text:qsTr("立体声"),value:"stereo"}]; textRole:"text"; valueRole:"value"; currentIndex:2 }
                Text { text: qsTr("位深 / 采样格式"); color: Theme.secondaryText }
                ComboBox {
                    id: sampleFormatBox
                    Layout.fillWidth: true
                    model: [{text:qsTr("自动"), value:""}].concat(
                        (converter.currentCapability.sampleFormats || []).map(function(value) {
                            return { text: value, value: value }
                        }))
                    textRole: "text"; valueRole: "value"
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.preferredHeight: 1; color: Theme.border }

            GridLayout {
                id: outputOptions
                objectName: "formatOutputOptionsGroup"
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                columns: 2
                columnSpacing: 10
                rowSpacing: 6
                Text { text: qsTr("C. 输出选项"); color: Theme.secondaryText; font.pixelSize: 13; Layout.columnSpan: 2 }
                Text { text: qsTr("输出目录"); color: Theme.secondaryText }
                RowLayout {
                    TextField {
                        objectName: "formatOutputDirectoryRow"
                        Layout.fillWidth: true
                        text: root.outputDirectory
                        placeholderText: qsTr("选择输出目录")
                        onTextEdited: root.outputDirectory = text
                    }
                    ToolButton { icon.source: Theme.icon("folder-open-line"); onClicked: root.chooseOutputDirectory() }
                }
                Text { text: qsTr("文件冲突策略"); color: Theme.secondaryText }
                ComboBox { id: conflictBox; Layout.fillWidth: true; model: [{text:qsTr("自动序号"),value:"auto-number"},{text:qsTr("跳过"),value:"skip"},{text:qsTr("覆盖"),value:"overwrite"},{text:qsTr("询问"),value:"ask"}]; textRole:"text"; valueRole:"value" }
                CheckBox { id: keepMetadataCheck; objectName: "keepMetadataCheck"; text: qsTr("保留元数据"); checked: SettingsController.preserveMetadata; onToggled: SettingsController.preserveMetadata = checked }
                CheckBox { id: keepCoverCheck; text: qsTr("保留封面"); checked: true; enabled: converter.currentCapability.supportsCover === true }
                CheckBox { id: preserveDirectoriesCheck; text: qsTr("保留目录结构"); checked: true }
                CheckBox { id: extractAudioCheck; objectName: "extractAudioCheck"; text: qsTr("从视频中提取音频"); checked: false }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 6
                Layout.bottomMargin: 12
                Layout.preferredHeight: 54
                color: Theme.background
                border.color: Theme.border
                radius: 6
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    Text { text: "ⓘ"; color: Theme.accent; font.pixelSize: 19 }
                    Text { Layout.fillWidth: true; text: qsTr("提示：转换任务采用本地处理模式。\n受系统性能影响，实际编码参数可能存在差异。"); color: Theme.secondaryText; wrapMode: Text.WordWrap; font.pixelSize: 12 }
                }
            }
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    property var converter
    property string outputDirectory: ""
    property bool expanded: true
    property bool forceCollapsed: false
    readonly property bool isExpanded: expanded && !forceCollapsed
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
    signal outputDirectoryEdited(string directory)

    color: "#101a21"
    border.color: "#203340"
    radius: 6
    clip: true

    RowLayout {
        id: settingsHeader
        height: 44
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 16
        anchors.rightMargin: 8
        spacing: 8

        Text {
            visible: root.isExpanded
            Layout.fillWidth: true
            text: qsTr("转换设置")
            color: "#eef3f6"
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }
        ToolButton {
            objectName: "formatSettingsAdvancedToggle"
            Layout.alignment: Qt.AlignRight
            icon.source: Theme.icon(root.isExpanded ? "arrow-up-s-line" : "arrow-go-forward-line")
            onClicked: {
                if (!root.forceCollapsed)
                    root.expanded = !root.expanded
            }
        }
    }

    ScrollView {
        visible: root.isExpanded
        anchors.top: settingsHeader.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: parent.width
            spacing: 4

            ColumnLayout {
                id: formatGroup
                objectName: "formatOutputFormatGroup"
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                spacing: 4
                Text { text: qsTr("A. 输出格式"); color: "#c9d2d8"; font.pixelSize: 13 }
                GridLayout {
                    columns: 4
                    rowSpacing: 4
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
                                color: parent.checked ? "#0c63c8" : "#0c1821"
                                border.color: parent.checked ? "#1688ff" : "#263b49"
                                radius: 5
                            }
                            contentItem: Text {
                                text: parent.text
                                color: parent.enabled ? "#eef3f6" : "#667782"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 13
                            }
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.preferredHeight: 1; color: "#263b49" }

            GridLayout {
                id: encodingGroup
                objectName: "formatEncodingSettingsGroup"
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                columns: 2
                columnSpacing: 10
                rowSpacing: 4
                Text { text: qsTr("B. 编码参数"); color: "#c9d2d8"; font.pixelSize: 13; Layout.columnSpan: 2 }
                Text { text: qsTr("编码器"); color: "#aeb9c1" }
                ComboBox { objectName: "formatEncoderBox"; Layout.fillWidth: true; Layout.preferredHeight: 32; model: [converter.currentCapability.encoderLabel || "--"] }
                Text { text: qsTr("码率模式"); color: "#aeb9c1" }
                RowLayout {
                    Button {
                        id: cbrButton; text: "CBR"; checkable: true; checked: true
                        Layout.fillWidth: true; Layout.preferredHeight: 32; onClicked: vbrButton.checked = false
                        background: Rectangle { color: parent.checked ? "#0c63c8" : "#0c1821"; border.color: parent.checked ? "#1688ff" : "#263b49"; radius: 5 }
                        contentItem: Text { text: parent.text; color: "#eef3f6"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        id: vbrButton; text: "VBR"; checkable: true
                        Layout.fillWidth: true; Layout.preferredHeight: 32; onClicked: cbrButton.checked = false
                        background: Rectangle { color: parent.checked ? "#0c63c8" : "#0c1821"; border.color: parent.checked ? "#1688ff" : "#263b49"; radius: 5 }
                        contentItem: Text { text: parent.text; color: "#eef3f6"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }
                Text { text: qsTr("目标码率"); color: "#aeb9c1" }
                ComboBox { id: bitRateBox; Layout.fillWidth: true; Layout.preferredHeight: 32; enabled: converter.currentCapability.lossy === true; model: [{text:"128 kbps",value:128000},{text:"192 kbps",value:192000},{text:"256 kbps",value:256000},{text:"320 kbps",value:320000}]; textRole:"text"; valueRole:"value"; currentIndex:3 }
                Text { text: qsTr("采样率"); color: "#aeb9c1" }
                ComboBox {
                    id: sampleRateBox
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    model: [{text:qsTr("原始采样率（自动）"), value:0}].concat(
                        (converter.currentCapability.sampleRates || []).map(function(value) {
                            return { text: (value / 1000) + " kHz", value: value }
                        }))
                    textRole: "text"; valueRole: "value"
                }
                Text { text: qsTr("声道"); color: "#aeb9c1" }
                ComboBox { id: channelBox; Layout.fillWidth: true; Layout.preferredHeight: 32; model: [{text:qsTr("自动"),value:""},{text:qsTr("单声道"),value:"mono"},{text:qsTr("立体声"),value:"stereo"}]; textRole:"text"; valueRole:"value"; currentIndex:2 }
                Text { text: qsTr("位深 / 采样格式"); color: "#aeb9c1" }
                ComboBox {
                    id: sampleFormatBox
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    model: [{text:qsTr("自动"), value:""}].concat(
                        (converter.currentCapability.sampleFormats || []).map(function(value) {
                            return { text: value, value: value }
                        }))
                    textRole: "text"; valueRole: "value"
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.preferredHeight: 1; color: "#263b49" }

            GridLayout {
                id: outputOptions
                objectName: "formatOutputOptionsGroup"
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                columns: 2
                columnSpacing: 10
                rowSpacing: 4
                Text { text: qsTr("C. 输出选项"); color: "#c9d2d8"; font.pixelSize: 13; Layout.columnSpan: 2 }
                Text { text: qsTr("输出目录"); color: "#aeb9c1" }
                RowLayout {
                    TextField {
                        objectName: "formatOutputDirectoryRow"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        text: root.outputDirectory
                        placeholderText: qsTr("选择输出目录")
                        onTextEdited: root.outputDirectoryEdited(text)
                    }
                    ToolButton { icon.source: Theme.icon("folder-open-line"); onClicked: root.chooseOutputDirectory() }
                }
                Text { text: qsTr("文件冲突策略"); color: "#aeb9c1" }
                ComboBox { id: conflictBox; Layout.fillWidth: true; Layout.preferredHeight: 32; model: [{text:qsTr("自动序号"),value:"auto-number"},{text:qsTr("跳过"),value:"skip"},{text:qsTr("覆盖"),value:"overwrite"},{text:qsTr("询问"),value:"ask"}]; textRole:"text"; valueRole:"value" }
                CheckBox { id: keepMetadataCheck; objectName: "keepMetadataCheck"; Layout.preferredHeight: 28; text: qsTr("保留元数据"); checked: SettingsController.preserveMetadata; onToggled: SettingsController.preserveMetadata = checked }
                CheckBox { id: keepCoverCheck; Layout.preferredHeight: 28; text: qsTr("保留封面"); checked: true; enabled: converter.currentCapability.supportsCover === true }
                CheckBox { id: preserveDirectoriesCheck; Layout.preferredHeight: 28; text: qsTr("保留目录结构"); checked: true }
                CheckBox { id: extractAudioCheck; objectName: "extractAudioCheck"; Layout.preferredHeight: 28; text: qsTr("从视频中提取音频"); checked: false }
            }

            Rectangle {
                objectName: "formatLocalProcessingHint"
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 32
                Layout.bottomMargin: 6
                Layout.preferredHeight: 54
                color: "#0a151d"
                border.color: "#263b49"
                radius: 6
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    ThemedIcon { source: Theme.icon("information-line"); tint: "#49b7ff"; sourceSize.width: 20; sourceSize.height: 20 }
                    Text { Layout.fillWidth: true; text: qsTr("提示：转换任务采用本地处理模式。\n受系统性能影响，实际编码参数可能存在差异。"); color: "#9aa8b2"; wrapMode: Text.WordWrap; font.pixelSize: 12 }
                }
            }

            GridLayout {
                objectName: "formatAdvancedSettings"
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 40
                Layout.bottomMargin: 14
                columns: 2
                columnSpacing: 10
                rowSpacing: 6
                Text { text: qsTr("高级设置"); color: "#c9d2d8"; font.pixelSize: 13; Layout.columnSpan: 2 }
                Text { text: qsTr("并发任务"); color: "#aeb9c1" }
                ComboBox {
                    objectName: "converterParallelJobsBox"
                    Layout.fillWidth: true
                    model: [1, 2, 3, 4]
                    currentIndex: Math.max(0, model.indexOf(SettingsController.parallelJobs))
                    onActivated: SettingsController.parallelJobs = currentValue
                }
            }
        }
    }
}

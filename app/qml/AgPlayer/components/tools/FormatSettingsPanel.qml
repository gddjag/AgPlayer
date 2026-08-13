import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    property var converter
    property string outputDirectory: ""
    property string outputFormat: converter.selectedFormat
    property string preset: presetBox.currentValue || "recommended"
    property int bitRate: converter.currentCapability.lossy === true
                          ? (bitRateBox.currentValue || 192000) : 0
    property int sampleRate: sampleRateBox.currentValue || 0
    property int channels: channelLayout === "mono" ? 1
                           : channelLayout === "stereo" ? 2 : 0
    property string bitrateMode: converter.currentCapability.lossy === true
                                 ? String(bitrateModeBox.currentValue || "cbr") : ""
    property int quality: qualityBox.value
    property string conflictPolicy: conflictBox.currentValue || "auto-number"
    property string sampleFormat: sampleFormatBox.currentValue || ""
    property string channelLayout: channelBox.currentValue || ""
    property bool keepMetadata: keepMetadataCheck.checked
    property bool keepCover: keepCoverCheck.checked
    property bool preserveDirectories: preserveDirectoriesCheck.checked
    property bool extractAudio: extractAudioCheck.checked
    property bool volumeNormalize: false
    signal chooseOutputDirectory()

    function indexForValue(model, key, value) {
        for (let index = 0; index < model.length; ++index) {
            if (model[index][key] === value)
                return index
        }
        return 0
    }

    function selectedPresetMap() {
        const presets = converter.currentCapability.presets || []
        const index = presetBox.currentIndex
        return index >= 0 && index < presets.length ? presets[index] : ({})
    }

    function applySelectedPreset() {
        const selected = selectedPresetMap()
        if (!selected || selected.key === "custom")
            return
        bitRateBox.currentIndex = indexForValue(bitRateBox.model, "value", selected.bitRate || 0)
        bitrateModeBox.currentIndex = indexForValue(bitrateModeBox.model, "value", selected.bitrateMode || "")
        sampleRateBox.currentIndex = indexForValue(sampleRateBox.model, "value", selected.sampleRate || 0)
        qualityBox.value = selected.quality === undefined ? 85 : selected.quality
    }

    function selectCustomPreset() {
        const presets = converter.currentCapability.presets || []
        const customIndex = indexForValue(presets, "key", "custom")
        if (presetBox.currentIndex !== customIndex)
            presetBox.currentIndex = customIndex
    }

    Connections {
        target: converter
        function onCurrentCapabilityChanged() {
            presetBox.currentIndex = 0
            Qt.callLater(root.applySelectedPreset)
        }
    }

    Component.onCompleted: Qt.callLater(root.applySelectedPreset)

    color: "#0b1721"
    border.color: "#203340"
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
                color: "#eef3f6"
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
                Text { text: qsTr("A. 输出格式"); color: "#c9d2d8"; font.pixelSize: 13 }
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
                rowSpacing: 6
                Text { text: qsTr("B. 编码参数"); color: "#c9d2d8"; font.pixelSize: 13; Layout.columnSpan: 2 }
                Text { text: qsTr("编码器"); color: "#aeb9c1" }
                ComboBox { objectName: "formatEncoderBox"; Layout.fillWidth: true; model: [converter.currentCapability.encoderLabel || "--"] }
                Text { text: qsTr("转换预设"); color: "#aeb9c1" }
                ComboBox {
                    id: presetBox
                    objectName: "formatPresetBox"
                    Layout.fillWidth: true
                    model: converter.currentCapability.presets || []
                    textRole: "label"
                    valueRole: "key"
                    onActivated: root.applySelectedPreset()
                }
                Text { text: qsTr("高级参数"); color: "#aeb9c1" }
                Switch {
                    id: advancedToggle
                    objectName: "formatAdvancedToggle"
                    text: checked ? qsTr("已展开") : qsTr("按需展开")
                }
                Text { text: qsTr("码率模式"); color: "#aeb9c1"; visible: advancedToggle.checked }
                ComboBox {
                    id: bitrateModeBox
                    objectName: "formatBitrateModeBox"
                    Layout.fillWidth: true
                    visible: advancedToggle.checked
                    enabled: converter.currentCapability.lossy === true
                    model: (converter.currentCapability.bitrateModes || []).map(function(value) {
                        return { text: String(value).toUpperCase(), value: value }
                    })
                    textRole: "text"
                    valueRole: "value"
                    onActivated: root.selectCustomPreset()
                }
                Text { text: qsTr("目标码率"); color: "#aeb9c1" }
                ComboBox {
                    id: bitRateBox
                    objectName: "formatBitRateBox"
                    Layout.fillWidth: true
                    enabled: converter.currentCapability.lossy === true
                    model: (converter.currentCapability.bitRates || []).map(function(value) {
                        return { text: (value / 1000) + " kbps", value: value }
                    })
                    textRole: "text"
                    valueRole: "value"
                    onActivated: root.selectCustomPreset()
                }
                Text { text: qsTr("质量"); color: "#aeb9c1"; visible: advancedToggle.checked }
                SpinBox {
                    id: qualityBox
                    Layout.fillWidth: true
                    visible: advancedToggle.checked
                    enabled: converter.currentCapability.lossy === true
                    from: 0
                    to: 100
                    value: 85
                    onValueModified: root.selectCustomPreset()
                }
                Text { text: qsTr("采样率"); color: "#aeb9c1"; visible: advancedToggle.checked }
                ComboBox {
                    id: sampleRateBox
                    Layout.fillWidth: true
                    visible: advancedToggle.checked
                    model: [{text:qsTr("原始采样率（自动）"), value:0}].concat(
                        (converter.currentCapability.sampleRates || []).map(function(value) {
                            return { text: (value / 1000) + " kHz", value: value }
                        }))
                    textRole: "text"; valueRole: "value"
                    onActivated: root.selectCustomPreset()
                }
                Text { text: qsTr("声道"); color: "#aeb9c1"; visible: advancedToggle.checked }
                ComboBox {
                    id: channelBox
                    objectName: "formatChannelBox"
                    Layout.fillWidth: true
                    visible: advancedToggle.checked
                    model: [{text:qsTr("自动"),value:""}].concat(
                        (converter.currentCapability.channelLayouts || []).map(function(value) {
                            return { text: value === "mono" ? qsTr("单声道") : qsTr("立体声"), value: value }
                        }))
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: 0
                    onActivated: root.selectCustomPreset()
                }
                Text { text: qsTr("位深 / 采样格式"); color: "#aeb9c1"; visible: advancedToggle.checked }
                ComboBox {
                    id: sampleFormatBox
                    Layout.fillWidth: true
                    visible: advancedToggle.checked
                    model: [{text:qsTr("自动"), value:""}].concat(
                        (converter.currentCapability.sampleFormats || []).map(function(value) {
                            return { text: value, value: value }
                        }))
                    textRole: "text"; valueRole: "value"
                    onActivated: root.selectCustomPreset()
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
                rowSpacing: 6
                Text { text: qsTr("C. 输出选项"); color: "#c9d2d8"; font.pixelSize: 13; Layout.columnSpan: 2 }
                Text { text: qsTr("输出目录"); color: "#aeb9c1" }
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
                Text { text: qsTr("文件冲突策略"); color: "#aeb9c1" }
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
                color: "#0a151d"
                border.color: "#263b49"
                radius: 6
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    Text { text: "ⓘ"; color: "#49b7ff"; font.pixelSize: 19 }
                    Text { Layout.fillWidth: true; text: qsTr("提示：转换任务采用本地处理模式。\n受系统性能影响，实际编码参数可能存在差异。"); color: "#9aa8b2"; wrapMode: Text.WordWrap; font.pixelSize: 12 }
                }
            }
        }
    }
}

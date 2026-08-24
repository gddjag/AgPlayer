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
    property int bitRate: bitRateBox.currentValue || 0
    property int quality: qualityBox.currentIndex >= 0
                          ? Number(qualityBox.currentValue)
                          : Number(capability.defaultQuality || 0)
    property int sampleRate: sampleRateBox.currentValue || 0
    property int channels: channelLayout === "mono" ? 1
                           : channelLayout === "stereo" ? 2 : 0
    property string bitrateMode: parameterKind === "bitrate"
                                  ? (cbrButton.checked ? "cbr" : "vbr") : ""
    property string conflictPolicy: conflictBox.currentValue || "auto-number"
    property string bitDepth: bitDepthBox.currentValue || ""
    property string sampleFormat: ""
    property string channelLayout: channelBox.currentValue || ""
    property bool keepMetadata: keepMetadataCheck.checked && keepMetadataCheck.enabled
    property bool keepCover: keepCoverCheck.checked && keepCoverCheck.enabled
    property bool preserveDirectories: preserveDirectoriesCheck.checked
    property bool extractAudio: extractAudioCheck.checked
    property bool volumeNormalize: false
    signal chooseOutputDirectory()
    signal outputDirectoryEdited(string directory)

    readonly property var capability: converter && converter.currentCapability
                                      ? converter.currentCapability : ({})
    readonly property string parameterKind: capability.parameterKind || "none"

    function normalizedIntChoices(values, formatter) {
        const result = []
        const source = values || []
        for (let i = 0; i < source.length; ++i) {
            const value = Number(source[i])
            result.push({ text: formatter(value), value: value })
        }
        return result
    }

    function sampleRateChoices() {
        let values = capability.sampleRateChoices
        if (values === undefined) {
            values = [0].concat(capability.sampleRates || [])
        }
        return normalizedIntChoices(values, function(value) {
            if (value === 0)
                return qsTr("原始采样率（自动）")
            const kiloHertz = value / 1000
            return kiloHertz + " kHz"
        })
    }

    function bitRateChoices() {
        return normalizedIntChoices(capability.bitRateChoices || [], function(value) {
            return (value / 1000) + " kbps"
        })
    }

    function qualityChoices() {
        return normalizedIntChoices(capability.qualityChoices || [], function(value) {
            return parameterKind === "quality" ? "Q" + value : String(value)
        })
    }

    function bitDepthChoices() {
        const source = capability.bitDepths || capability.bitDepthChoices || []
        const result = []
        for (let i = 0; i < source.length; ++i) {
            const choice = source[i]
            if (typeof choice === "object") {
                result.push({
                    text: choice.label || choice.key || qsTr("自动"),
                    value: choice.key || "",
                    isDefault: choice.default === true || choice.isDefault === true
                })
            } else {
                result.push({ text: String(choice), value: String(choice), isDefault: false })
            }
        }
        if (result.length === 0
                && (parameterKind === "bitrate" || parameterKind === "quality")) {
            result.push({ text: qsTr("自动"), value: "", isDefault: true })
        } else if (result.length === 0 && capability.sampleFormats !== undefined) {
            result.push({ text: qsTr("自动"), value: "", isDefault: true })
            for (let j = 0; j < capability.sampleFormats.length; ++j) {
                const value = capability.sampleFormats[j]
                result.push({ text: value, value: value, isDefault: false })
            }
        }
        return result
    }

    function indexOfValue(model, value) {
        for (let i = 0; i < model.length; ++i) {
            if (model[i].value === value)
                return i
        }
        return model.length > 0 ? 0 : -1
    }

    function defaultBitDepthIndex(model) {
        for (let i = 0; i < model.length; ++i) {
            if (model[i].isDefault === true)
                return i
        }
        return model.length > 0 ? 0 : -1
    }

    function syncRecommendedDefaults() {
        const rates = sampleRateBox.model || []
        sampleRateBox.currentIndex = indexOfValue(rates, rates.length > 0 && rates[0].value === 48000 ? 48000 : 0)
        bitRateBox.currentIndex = indexOfValue(bitRateBox.model || [], capability.defaultBitRate || 0)
        qualityBox.currentIndex = indexOfValue(qualityBox.model || [], capability.defaultQuality === undefined ? -1 : capability.defaultQuality)
        bitDepthBox.currentIndex = defaultBitDepthIndex(bitDepthBox.model || [])
    }

    Component.onCompleted: Qt.callLater(syncRecommendedDefaults)
    onOutputFormatChanged: Qt.callLater(syncRecommendedDefaults)

    color: "#101a21"
    border.color: "#203340"
    radius: 6
    clip: true

    component ReferenceCheckBox: CheckBox {
        id: control
        indicator: Rectangle {
            objectName: control.objectName.length > 0 ? control.objectName + "Indicator" : ""
            x: control.leftPadding
            y: (control.height - height) / 2
            width: 20
            height: 20
            radius: 3
            color: control.checked ? "#1688ff" : (control.enabled ? "#0c1821" : "#10181e")
            border.color: control.checked ? "#1688ff" : (control.enabled ? "#3a4a53" : "#26343c")
            ThemedIcon {
                objectName: control.objectName.length > 0 ? control.objectName + "Mark" : ""
                anchors.centerIn: parent
                visible: control.checked
                source: Theme.icon("check-line")
                tint: "#ffffff"
                sourceSize.width: 14
                sourceSize.height: 14
            }
        }
        contentItem: Text {
            text: control.text
            leftPadding: control.indicator.width + 8
            verticalAlignment: Text.AlignVCenter
            color: control.enabled ? "#d7e0e6" : "#667782"
            font.pixelSize: 13
        }
    }

    component ReferenceComboBox: ComboBox {
        id: control
        indicator: ThemedIcon {
            objectName: control.objectName.length > 0 ? control.objectName + "Chevron" : ""
            x: control.width - width - 10
            y: (control.height - height) / 2
            source: Theme.icon("arrow-down-s-line")
            tint: control.enabled ? "#d7e0e6" : "#667782"
            sourceSize.width: 18
            sourceSize.height: 18
        }
        contentItem: Text {
            text: control.displayText
            leftPadding: 12
            rightPadding: control.indicator.width + 16
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            color: control.enabled ? "#eef3f6" : "#667782"
            font.pixelSize: 13
        }
        background: Rectangle {
            color: "#0c1821"
            border.color: control.activeFocus ? "#1688ff" : "#263b49"
            radius: 5
        }
    }

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
                    id: outputFormatGrid
                    objectName: "formatOutputFormatGrid"
                    Layout.minimumWidth: 384
                    Layout.preferredWidth: 384
                    Layout.maximumWidth: 384
                    columns: 4
                    rowSpacing: 4
                    columnSpacing: 8
                    Repeater {
                        model: converter.outputCapabilities
                        Button {
                            objectName: "formatOutputFormatButton-" + modelData.key
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 90
                            Layout.maximumWidth: 90
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
                Layout.minimumHeight: 232
                Layout.preferredHeight: 232
                columns: 2
                columnSpacing: 11
                rowSpacing: 4
                Text { text: qsTr("B. 编码参数"); color: "#c9d2d8"; font.pixelSize: 13; Layout.columnSpan: 2 }
                Text { Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("编码器"); color: "#aeb9c1" }
                ReferenceComboBox { objectName: "formatEncoderBox"; Layout.fillWidth: true; Layout.preferredHeight: 32; model: [converter.currentCapability.encoderLabel || "--"] }
                Text { visible: root.parameterKind === "bitrate"; Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("码率模式"); color: "#aeb9c1" }
                RowLayout {
                    visible: root.parameterKind === "bitrate"
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
                Text { objectName: "formatBitRateLabel"; visible: root.parameterKind === "bitrate"; Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("目标码率"); color: "#aeb9c1" }
                ReferenceComboBox { id: bitRateBox; objectName: "formatBitRateBox"; visible: root.parameterKind === "bitrate"; Layout.fillWidth: true; Layout.preferredHeight: 32; model: root.bitRateChoices(); textRole:"text"; valueRole:"value" }
                Text {
                    objectName: "formatQualityLabel"
                    visible: root.parameterKind === "quality" || root.parameterKind === "compression"
                    Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122
                    text: root.parameterKind === "compression" ? qsTr("压缩等级") : qsTr("质量等级")
                    color: "#aeb9c1"
                }
                ReferenceComboBox {
                    id: qualityBox
                    objectName: "formatQualityBox"
                    visible: root.parameterKind === "quality" || root.parameterKind === "compression"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    model: root.qualityChoices()
                    textRole: "text"; valueRole: "value"
                }
                Text { Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("采样率"); color: "#aeb9c1" }
                ReferenceComboBox {
                    id: sampleRateBox
                    objectName: "formatSampleRateBox"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    model: root.sampleRateChoices()
                    textRole: "text"; valueRole: "value"
                }
                Text { Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("声道"); color: "#aeb9c1" }
                ReferenceComboBox { id: channelBox; objectName: "formatChannelBox"; Layout.fillWidth: true; Layout.preferredHeight: 32; model: [{text:qsTr("自动"),value:""},{text:qsTr("单声道"),value:"mono"},{text:qsTr("立体声"),value:"stereo"}]; textRole:"text"; valueRole:"value"; currentIndex:0 }
                Text { objectName: "formatBitDepthLabel"; visible: bitDepthBox.model.length > 0; Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("位深 / 采样格式"); color: "#aeb9c1" }
                ReferenceComboBox {
                    id: bitDepthBox
                    objectName: "formatBitDepthBox"
                    visible: model.length > 0
                    enabled: model.length > 1
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    model: root.bitDepthChoices()
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
                columnSpacing: 11
                rowSpacing: 4
                Text { text: qsTr("C. 输出选项"); color: "#c9d2d8"; font.pixelSize: 13; Layout.columnSpan: 2 }
                Text { Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("输出目录"); color: "#aeb9c1" }
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
                Text { Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("文件冲突策略"); color: "#aeb9c1" }
                ReferenceComboBox { id: conflictBox; Layout.fillWidth: true; Layout.preferredHeight: 32; model: [{text:qsTr("自动序号"),value:"auto-number"},{text:qsTr("跳过"),value:"skip"},{text:qsTr("覆盖"),value:"overwrite"},{text:qsTr("询问"),value:"ask"}]; textRole:"text"; valueRole:"value" }
                ReferenceCheckBox { id: keepMetadataCheck; objectName: "keepMetadataCheck"; Layout.preferredHeight: 28; text: qsTr("保留元数据"); checked: SettingsController.preserveMetadata; enabled: converter.currentCapability.supportsMetadata === true; onToggled: SettingsController.preserveMetadata = checked }
                ReferenceCheckBox { id: keepCoverCheck; Layout.preferredHeight: 28; text: qsTr("保留封面"); checked: true; enabled: converter.currentCapability.supportsCover === true }
                ReferenceCheckBox { id: preserveDirectoriesCheck; Layout.preferredHeight: 28; text: qsTr("保留目录结构"); checked: true }
                ReferenceCheckBox { id: extractAudioCheck; objectName: "extractAudioCheck"; Layout.preferredHeight: 28; text: qsTr("从视频中提取音频"); checked: false }
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
                ReferenceComboBox {
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

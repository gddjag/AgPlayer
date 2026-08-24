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
    readonly property var capability: converter.currentCapability || ({})
    readonly property string parameterKind: capability.parameterKind || "none"
    property int bitRate: parameterKind === "bitrate"
                          ? (bitRateBox.currentValue || 0) : 0
    property int quality: qualityBox.currentIndex >= 0
                          ? Number(qualityBox.currentValue)
                          : Number(capability.defaultQuality || 0)
    property int sampleRate: sampleRateBox.currentValue || 0
    property int channels: channelLayout === "mono" ? 1
                           : channelLayout === "stereo" ? 2 : 0
    property string bitrateMode: selectedBitrateMode
    property string conflictPolicy: conflictBox.currentValue || "auto-number"
    property string sampleFormat: sampleFormatBox.currentValue || ""
    property string bitDepth: bitDepthBox.currentValue || ""
    property string channelLayout: channelBox.currentValue || ""
    property bool keepMetadata: keepMetadataCheck.checked
    property bool keepCover: keepCoverCheck.checked
    property bool preserveDirectories: preserveDirectoriesCheck.checked
    property bool extractAudio: extractAudioCheck.checked
    property bool volumeNormalize: false
    property string selectedBitrateMode: ""
    signal chooseOutputDirectory()
    signal outputDirectoryEdited(string directory)

    function resetCapabilityParameters() {
        const modes = capability.bitrateModes || []
        selectedBitrateMode = ""
        for (let index = 0; index < modes.length; ++index) {
            if (modes[index].default) {
                selectedBitrateMode = modes[index].key
                break
            }
        }
        if (selectedBitrateMode === "" && modes.length > 0)
            selectedBitrateMode = modes[0].key

        bitRateBox.currentIndex = -1
        if (parameterKind === "bitrate" && bitRateBox.count > 0) {
            const recommended = (capability.bitRates || []).indexOf(192000)
            bitRateBox.currentIndex = recommended >= 0 ? recommended : 0
        }
        sampleRateBox.currentIndex = 0
        if (capability.key === "opus") {
            const opusIndex = sampleRateBox.indexOfValue(48000)
            sampleRateBox.currentIndex = opusIndex >= 0 ? opusIndex : 0
        }
        channelBox.currentIndex = 0
        sampleFormatBox.currentIndex = 0
        qualityBox.currentIndex = Math.max(0,
            (capability.qualityChoices || []).indexOf(capability.defaultQuality))
        bitDepthBox.currentIndex = 0
        if (capability.supportsMetadata !== true)
            SettingsController.preserveMetadata = false
        if (capability.supportsCover !== true)
            keepCoverCheck.checked = false
    }

    Connections {
        target: converter
        function onCurrentCapabilityChanged() {
            if (root.capability.supportsMetadata !== true)
                SettingsController.preserveMetadata = false
            if (root.capability.supportsCover !== true)
                keepCoverCheck.checked = false
            Qt.callLater(root.resetCapabilityParameters)
        }
    }
    Component.onCompleted: Qt.callLater(resetCapabilityParameters)

    ButtonGroup {
        id: bitrateModeGroup
        exclusive: true
    }

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
                columns: 2
                columnSpacing: 11
                rowSpacing: 4
                Text { text: qsTr("B. 编码参数"); color: "#c9d2d8"; font.pixelSize: 13; Layout.columnSpan: 2 }
                Text { Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("编码器"); color: "#aeb9c1" }
                ReferenceComboBox { objectName: "formatEncoderBox"; Layout.fillWidth: true; Layout.preferredHeight: 32; model: [converter.currentCapability.encoderLabel || "--"] }
                Text { visible: bitrateModeRow.visible; Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("码率模式"); color: "#aeb9c1" }
                RowLayout {
                    id: bitrateModeRow
                    objectName: "formatBitrateModeRow"
                    visible: (root.capability.bitrateModes || []).length > 0
                    Repeater {
                        model: root.capability.bitrateModes || []
                        Button {
                            required property var modelData
                            objectName: "formatBitrateModeButton-" + modelData.key
                            text: modelData.label
                            checkable: true
                            ButtonGroup.group: bitrateModeGroup
                            checked: root.selectedBitrateMode === modelData.key
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            onClicked: root.selectedBitrateMode = modelData.key
                            background: Rectangle { color: parent.checked ? "#0c63c8" : "#0c1821"; border.color: parent.checked ? "#1688ff" : "#263b49"; radius: 5 }
                            contentItem: Text { text: parent.text; color: "#eef3f6"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                }
                Text { visible: bitrateRow.visible; Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("目标码率"); color: "#aeb9c1" }
                RowLayout {
                    id: bitrateRow
                    objectName: "formatBitrateRow"
                    visible: root.parameterKind === "bitrate"
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 32 : 0
                    ReferenceComboBox {
                        id: bitRateBox
                        objectName: "formatBitrateBox"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        model: (root.capability.bitRates || []).map(function(value) {
                            return { text: (value / 1000) + " kbps", value: value }
                        })
                        textRole: "text"
                        valueRole: "value"
                    }
                }
                Text {
                    visible: qualityBox.visible
                    Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122
                    text: root.parameterKind === "compression" ? qsTr("压缩等级") : qsTr("质量等级")
                    color: "#aeb9c1"
                }
                ReferenceComboBox {
                    id: qualityBox
                    objectName: "formatQualityBox"
                    visible: root.parameterKind === "quality" || root.parameterKind === "compression"
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 32 : 0
                    model: (root.capability.qualityChoices || []).map(function(value) {
                        return { text: root.parameterKind === "quality" ? "Q" + value : String(value),
                                 value: value }
                    })
                    textRole: "text"; valueRole: "value"
                }
                Text { Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("采样率"); color: "#aeb9c1" }
                ReferenceComboBox {
                    id: sampleRateBox
                    objectName: "formatSampleRateBox"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    model: (root.capability.key === "opus" ? []
                            : [{text:qsTr("原始采样率（自动）"), value:0}]).concat(
                        (converter.currentCapability.sampleRates || []).map(function(value) {
                            return { text: (value / 1000) + " kHz", value: value }
                        }))
                    textRole: "text"; valueRole: "value"
                }
                Text { Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("声道"); color: "#aeb9c1" }
                ReferenceComboBox {
                    id: channelBox
                    objectName: "formatChannelBox"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    model: [{text:qsTr("自动"), value:""}].concat(
                        (root.capability.channelLayouts || []).filter(function(value) {
                            return value === "mono" || value === "stereo"
                        }).map(function(value) {
                            return { text: value === "mono" ? qsTr("单声道") : qsTr("立体声"),
                                     value: value }
                        }))
                    textRole: "text"
                    valueRole: "value"
                }
                Text { Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("位深 / 采样格式"); color: "#aeb9c1" }
                ReferenceComboBox {
                    id: bitDepthBox
                    objectName: "formatBitDepthBox"
                    visible: (root.capability.bitDepths || []).length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 32 : 0
                    model: root.capability.bitDepths || []
                    textRole: "label"; valueRole: "key"
                }
                ReferenceComboBox {
                    id: sampleFormatBox
                    objectName: "formatSampleFormatBox"
                    visible: (root.capability.bitDepths || []).length === 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 32 : 0
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
                ReferenceCheckBox { id: keepMetadataCheck; objectName: "keepMetadataCheck"; Layout.preferredHeight: 28; text: qsTr("保留元数据"); checked: SettingsController.preserveMetadata; enabled: root.capability.supportsMetadata === true; onToggled: SettingsController.preserveMetadata = checked }
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

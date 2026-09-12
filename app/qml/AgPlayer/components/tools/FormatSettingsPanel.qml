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
                          && isFinite(Number(bitRateBox.currentValue))
                          ? Number(bitRateBox.currentValue) : 0
    property int quality: parameterKind === "bitrate"
                          ? 75
                          : (qualityBox.currentIndex >= 0
                             ? Number(qualityBox.currentValue)
                             : Number(capability.defaultQuality || 0))
    property int sampleRate: isFinite(Number(sampleRateBox.currentValue))
                             ? Number(sampleRateBox.currentValue) : 0
    property int channels: channelLayout === "mono" ? 1
                           : channelLayout === "stereo" ? 2 : 0
    readonly property string bitrateMode: converter ? converter.bitrateMode : ""
    property string conflictPolicy: conflictBox.currentValue || "auto-number"
    property string sampleFormat: bitDepthBox.visible
                                  ? "" : (sampleFormatBox.currentValue || "")
    property string bitDepth: bitDepthBox.currentValue || ""
    property string channelLayout: channelBox.currentValue || ""
    property bool keepMetadata: keepMetadataCheck.checked
    property bool keepCover: keepCoverCheck.checked
    property bool preserveDirectories: preserveDirectoriesCheck.checked
    property bool extractAudio: extractAudioCheck.checked
    property bool volumeNormalize: false
    property string lastCapabilityKey: ""
    signal chooseOutputDirectory()
    signal outputDirectoryEdited(string directory)

    function bitRateValues() {
        return capability.bitRateChoices !== undefined
                ? capability.bitRateChoices : (capability.bitRates || [])
    }

    function sampleRateValues() {
        return capability.sampleRateChoices !== undefined
                ? capability.sampleRateChoices
                : [0].concat(capability.sampleRates || [])
    }

    function bitDepthChoices() {
        const depths = capability.bitDepths || []
        if (depths.length > 0)
            return depths
        if (parameterKind === "bitrate" || parameterKind === "quality")
            return [{label: qsTr("自动"), key: "", default: true}]
        return []
    }

    function resetCapabilityParameters() {
        const modes = capability.bitrateModes || []
        let defaultMode = ""
        for (let index = 0; index < modes.length; ++index) {
            if (modes[index].default) {
                defaultMode = modes[index].key
                break
            }
        }
        if (defaultMode === "" && modes.length > 0)
            defaultMode = modes[0].key
        if (converter && converter.bitrateMode !== defaultMode)
            converter.bitrateMode = defaultMode

        bitRateBox.currentIndex = -1
        if (parameterKind === "bitrate" && bitRateBox.count > 0) {
            const configured = bitRateBox.indexOfValue(
                SettingsController.transcodeBitrateKbps * 1000)
            const recommended = bitRateBox.indexOfValue(
                Number(capability.defaultBitRate || 0))
            bitRateBox.currentIndex = configured >= 0 ? configured
                : (recommended >= 0 ? recommended : 0)
        }
        const configuredRate = sampleRateBox.indexOfValue(
            SettingsController.transcodeSampleRateHz)
        sampleRateBox.currentIndex = configuredRate >= 0 ? configuredRate
            : (sampleRateBox.count > 0 ? 0 : -1)
        const configuredChannel = channelBox.indexOfValue(
            SettingsController.transcodeChannels === 1 ? "mono" : "stereo")
        channelBox.currentIndex = configuredChannel >= 0 ? configuredChannel : 0
        sampleFormatBox.currentIndex = 0
        const qualityChoices = capability.qualityChoices || []
        qualityBox.currentIndex = qualityChoices.length > 0
                ? Math.max(0, qualityChoices.indexOf(capability.defaultQuality))
                : -1
        const depths = bitDepthBox.model || []
        bitDepthBox.currentIndex = -1
        for (let depthIndex = 0; depthIndex < depths.length; ++depthIndex) {
            if (depths[depthIndex].default === true) {
                bitDepthBox.currentIndex = depthIndex
                break
            }
        }
        if (bitDepthBox.currentIndex < 0 && depths.length > 0)
            bitDepthBox.currentIndex = 0
    }

    function syncGlobalNumericDefaults() {
        if (parameterKind === "bitrate" && bitRateBox.count > 0) {
            const index = bitRateBox.indexOfValue(SettingsController.transcodeBitrateKbps * 1000)
            if (index >= 0) bitRateBox.currentIndex = index
        }
        const rate = sampleRateBox.indexOfValue(SettingsController.transcodeSampleRateHz)
        if (rate >= 0) sampleRateBox.currentIndex = rate
        const channels = channelBox.indexOfValue(SettingsController.transcodeChannels === 1 ? "mono" : "stereo")
        if (channels >= 0) channelBox.currentIndex = channels
    }

    Connections {
        target: converter
        function onCurrentCapabilityChanged() {
            const key = String(converter.selectedFormat || "")
            root.lastCapabilityKey = key
            // The notification is emitted before QML re-evaluates the
            // currentCapability binding. Reset on the next turn so the
            // controls use the new format, not the old one.
            Qt.callLater(function() {
                if (String(converter.selectedFormat || "") === key)
                    root.resetCapabilityParameters()
            })
        }
    }
    Component.onCompleted: {
        lastCapabilityKey = String(capability.key || "")
        resetCapabilityParameters()
    }

    Connections {
        target: SettingsController
        function onTranscodeBitrateKbpsChanged() { root.syncGlobalNumericDefaults() }
        function onTranscodeSampleRateHzChanged() { root.syncGlobalNumericDefaults() }
        function onTranscodeChannelsChanged() { root.syncGlobalNumericDefaults() }
    }

    ButtonGroup {
        id: bitrateModeGroup
        exclusive: true
    }

    color: Theme.panel
    border.color: Theme.border
    radius: Theme.radiusSm
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
                color: control.checked ? Theme.accent
                                       : (control.enabled ? Theme.elevated
                                                          : Theme.background)
            border.color: control.checked ? Theme.accent : Theme.border
            ThemedIcon {
                objectName: control.objectName.length > 0 ? control.objectName + "Mark" : ""
                anchors.centerIn: parent
                visible: control.checked
                source: Theme.icon("check-line")
                tint: Theme.accentText
                sourceSize.width: 14
                sourceSize.height: 14
            }
        }
        contentItem: Text {
            text: control.text
            leftPadding: control.indicator.width + 8
            verticalAlignment: Text.AlignVCenter
            color: control.enabled ? Theme.primaryText : Theme.secondaryText
            font.pixelSize: Theme.fontSizeBody
        }
    }

    component ReferenceComboBox: ComboBox {
        id: control
        indicator: ThemedIcon {
            objectName: control.objectName.length > 0 ? control.objectName + "Chevron" : ""
            x: control.width - width - 10
            y: (control.height - height) / 2
            source: Theme.icon("arrow-down-s-line")
            tint: control.enabled ? Theme.iconPrimary : Theme.iconSecondary
            sourceSize.width: 18
            sourceSize.height: 18
        }
        contentItem: Text {
            text: control.displayText
            leftPadding: 12
            rightPadding: control.indicator.width + 16
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            color: control.enabled ? Theme.primaryText : Theme.secondaryText
            font.pixelSize: Theme.fontSizeBody
        }
        background: Rectangle {
            color: Theme.elevated
            border.color: control.activeFocus ? Theme.focus : Theme.border
            radius: 5
        }
    }

    RowLayout {
        id: settingsHeader
        height: 44
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingSm
        spacing: Theme.spacingSm

        Text {
            visible: root.isExpanded
            Layout.fillWidth: true
            text: qsTr("转换设置")
            color: Theme.primaryText
            font.pixelSize: Theme.fontSizeSection
            font.weight: Font.DemiBold
        }
        ThemedIconButton {
            objectName: "formatSettingsAdvancedToggle"
            Layout.alignment: Qt.AlignRight
            icon.source: Theme.icon(root.isExpanded ? "arrow-up-s-line" : "arrow-go-forward-line")
            iconSource: icon.source
            accessibleName: root.isExpanded
                ? qsTr("收起转换设置") : qsTr("展开转换设置")
            onClicked: {
                if (!root.forceCollapsed)
                    root.expanded = !root.expanded
            }
        }
    }

    ScrollView {
        id: settingsScroll
        objectName: "formatSettingsScroll"
        visible: root.isExpanded
        anchors.top: settingsHeader.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        bottomPadding: Theme.spacingMd

        ColumnLayout {
            width: settingsScroll.availableWidth
            spacing: Theme.spacingXs

            ColumnLayout {
                id: formatGroup
                objectName: "formatOutputFormatGroup"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingLg
                Layout.rightMargin: Theme.spacingLg
                spacing: Theme.spacingXs
                Text {
                    text: qsTr("A. 输出格式")
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Font.DemiBold
                }
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
                            onClicked: converter.selectedFormat = modelData.key
                            ToolTip.visible: hovered && !modelData.available
                            ToolTip.text: modelData.reason
                            background: Rectangle {
                        color: parent.checked ? Theme.activeSelection : Theme.elevated
                                border.color: parent.checked ? Theme.accent : Theme.border
                                radius: 5
                            }
                            contentItem: Text {
                                text: parent.text
                                color: !parent.enabled ? Theme.secondaryText
                                    : parent.checked ? Theme.activeSelectionText
                                                     : Theme.primaryText
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: Theme.fontSizeBody
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
                Layout.leftMargin: Theme.spacingLg
                Layout.rightMargin: Theme.spacingLg
                columns: 2
                columnSpacing: 11
                rowSpacing: 4
                Text {
                    text: qsTr("B. 编码参数")
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Font.DemiBold
                    Layout.columnSpan: 2
                }
                Text { Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("编码器"); color: Theme.secondaryText }
                ReferenceComboBox { objectName: "formatEncoderBox"; Layout.fillWidth: true; Layout.preferredHeight: 32; model: [converter.currentCapability.encoderLabel || "--"] }
                Text { visible: bitrateModeRow.visible; Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("码率模式"); color: Theme.secondaryText }
                RowLayout {
                    id: bitrateModeRow
                    objectName: "formatBitrateModeRow"
                    visible: (root.capability.bitrateModes || []).length > 0
                    Repeater {
                        id: bitrateModeRepeater
                        objectName: "formatBitrateModeRepeater"
                        model: root.capability.bitrateModes || []
                        Button {
                            required property var modelData
                            objectName: "formatBitrateModeButton-" + modelData.key
                            text: modelData.label
                            checkable: true
                            ButtonGroup.group: bitrateModeGroup
                            checked: root.bitrateMode === modelData.key
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            onClicked: {
                                // Capability refresh may be delivered after the
                                // click; mark this key observed so that late
                                // notification cannot replace the user's mode.
                                root.lastCapabilityKey = String(root.capability.key || "")
                                root.converter.bitrateMode = modelData.key
                            }
                        background: Rectangle { color: parent.checked ? Theme.activeSelection : Theme.elevated; border.color: parent.checked ? Theme.accent : Theme.border; radius: 5 }
                            contentItem: Text {
                                text: parent.text
                                color: parent.checked ? Theme.activeSelectionText
                                                      : Theme.primaryText
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
                Text { objectName: "formatBitRateLabel"; visible: bitrateRow.visible; Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("目标码率"); color: Theme.secondaryText }
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
                        model: root.bitRateValues().map(function(value) {
                            return { text: (value / 1000) + " kbps", value: value }
                        })
                        textRole: "text"
                        valueRole: "value"
                        onActivated: SettingsController.transcodeBitrateKbps =
                            Math.round(Number(currentValue) / 1000)
                    }
                }
                Text {
                    objectName: "formatQualityLabel"
                    visible: qualityBox.visible
                    Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122
                    text: root.parameterKind === "compression" ? qsTr("压缩等级") : qsTr("质量等级")
                    color: Theme.secondaryText
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
                Text { Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("采样率"); color: Theme.secondaryText }
                ReferenceComboBox {
                    id: sampleRateBox
                    objectName: "formatSampleRateBox"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    model: root.sampleRateValues().map(function(value) {
                        return { text: value === 0 ? qsTr("原始采样率（自动）")
                                                  : (value / 1000) + " kHz",
                                 value: value }
                    })
                    textRole: "text"; valueRole: "value"
                    onActivated: SettingsController.transcodeSampleRateHz = currentValue
                }
                Text { Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("声道"); color: Theme.secondaryText }
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
                    onActivated: {
                        if (currentValue === "mono") SettingsController.transcodeChannels = 1
                        else if (currentValue === "stereo") SettingsController.transcodeChannels = 2
                    }
                }
                Text { objectName: "formatBitDepthLabel"; visible: bitDepthBox.visible; Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("位深 / 采样格式"); color: Theme.secondaryText }
                ReferenceComboBox {
                    id: bitDepthBox
                    objectName: "formatBitDepthBox"
                    visible: model.length > 0
                    enabled: model.length > 1
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 32 : 0
                    model: root.bitDepthChoices()
                    textRole: "label"; valueRole: "key"
                }
                ReferenceComboBox {
                    id: sampleFormatBox
                    objectName: "formatSampleFormatBox"
                    visible: root.bitDepthChoices().length === 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 32 : 0
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
                Layout.leftMargin: Theme.spacingLg
                Layout.rightMargin: Theme.spacingLg
                columns: 2
                columnSpacing: 11
                rowSpacing: 4
                Text {
                    text: qsTr("C. 输出选项")
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Font.DemiBold
                    Layout.columnSpan: 2
                }
                Text { Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("输出目录"); color: Theme.secondaryText }
                RowLayout {
                    ThemedTextField {
                        objectName: "formatOutputDirectoryRow"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        text: root.outputDirectory
                        placeholderText: qsTr("选择输出目录")
                        onTextEdited: root.outputDirectoryEdited(text)
                    }
                    ThemedIconButton {
                        icon.source: Theme.icon("folder-open-line")
                        iconSource: icon.source
                        accessibleName: qsTr("选择输出目录")
                        onClicked: root.chooseOutputDirectory()
                    }
                }
                Text { Layout.minimumWidth: 122; Layout.preferredWidth: 122; Layout.maximumWidth: 122; text: qsTr("文件冲突策略"); color: Theme.secondaryText }
                ReferenceComboBox { id: conflictBox; Layout.fillWidth: true; Layout.preferredHeight: 32; model: [{text:qsTr("自动序号"),value:"auto-number"},{text:qsTr("跳过"),value:"skip"},{text:qsTr("覆盖"),value:"overwrite"},{text:qsTr("询问"),value:"ask"}]; textRole:"text"; valueRole:"value" }
                ReferenceCheckBox { id: keepMetadataCheck; objectName: "keepMetadataCheck"; Layout.preferredHeight: 28; text: qsTr("保留元数据"); checked: SettingsController.preserveMetadata; enabled: root.capability.supportsMetadata === true; onToggled: SettingsController.preserveMetadata = checked }
                ReferenceCheckBox { id: keepCoverCheck; Layout.preferredHeight: 28; text: qsTr("保留封面"); checked: SettingsController.preserveCover; enabled: converter.currentCapability.supportsCover === true; onToggled: SettingsController.preserveCover = checked }
                ReferenceCheckBox { id: preserveDirectoriesCheck; Layout.preferredHeight: 28; text: qsTr("保留目录结构"); checked: SettingsController.preserveDirectoryStructure; onToggled: SettingsController.preserveDirectoryStructure = checked }
                ReferenceCheckBox { id: extractAudioCheck; objectName: "extractAudioCheck"; Layout.preferredHeight: 28; text: qsTr("从视频中提取音频"); checked: SettingsController.extractVideoAudio; onToggled: SettingsController.extractVideoAudio = checked }
            }

            Rectangle {
                objectName: "formatLocalProcessingHint"
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: Theme.spacingMd
                Layout.bottomMargin: 6
                Layout.preferredHeight: 54
                color: Theme.elevated
                border.color: Theme.border
                radius: 6
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    ThemedIcon { source: Theme.icon("information-line"); tint: Theme.accent; sourceSize.width: 20; sourceSize.height: 20 }
                    Text { Layout.fillWidth: true; text: qsTr("提示：转换任务采用本地处理模式。\n受系统性能影响，实际编码参数可能存在差异。"); color: Theme.secondaryText; wrapMode: Text.WordWrap; font.pixelSize: Theme.fontSizeCaption }
                }
            }

            GridLayout {
                objectName: "formatAdvancedSettings"
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: Theme.spacingLg
                Layout.bottomMargin: 14
                columns: 2
                columnSpacing: 10
                rowSpacing: 6
                Text {
                    text: qsTr("高级设置")
                    color: Theme.primaryText
                    font.pixelSize: Theme.fontSizeBody
                    Layout.columnSpan: 2
                }
                Text {
                    text: qsTr("并发数可在任务总进度旁调整")
                    color: Theme.secondaryText
                    font.pixelSize: Theme.fontSizeCaption
                    Layout.columnSpan: 2
                }
            }

        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    objectName: "losslessConclusionPanel"
    property var result: ({})
    signal exportRequested()
    signal locateRequested(string path)

    readonly property bool hasResult: !!(result && result.taskId)
    readonly property bool englishUi: SettingsController.language
                                       && SettingsController.language.toLowerCase()
                                          .startsWith("en")

    function verdictColor(code) {
        if (code === "credible_lossless" || code === "credible_native_dsd")
            return Theme.losslessVerdictCredible
        if (code === "suspected_lossy_transcode"
                || code === "suspected_lossy_upsample")
            return Theme.losslessVerdictTranscode
        if (code === "suspected_upsample"
                || code === "suspected_bit_depth_expansion"
                || code === "suspected_pcm_to_dsd")
            return Theme.losslessVerdictUpsample
        return Theme.losslessVerdictInconclusive
    }

    function sizeText(value) {
        const bytes = Number(value || 0)
        if (bytes <= 0)
            return qsTr("--")
        if (bytes >= 1073741824)
            return (bytes / 1073741824).toLocaleString(Qt.locale(), "f", 2) + " GiB"
        if (bytes < 1048576)
            return bytes >= 1024
                    ? (bytes / 1024).toLocaleString(Qt.locale(), "f", 1) + " KiB"
                    : Math.round(bytes) + " B"
        return (bytes / 1048576).toLocaleString(Qt.locale(), "f", 1) + " MiB"
    }

    function durationText(value) {
        const totalSeconds = Math.max(0, Math.floor(Number(value || 0) / 1000))
        if (totalSeconds <= 0)
            return qsTr("--")
        const hours = Math.floor(totalSeconds / 3600)
        const minutes = Math.floor((totalSeconds % 3600) / 60)
        const seconds = totalSeconds % 60
        return (hours > 0 ? String(hours).padStart(2, "0") + ":" : "")
                + String(minutes).padStart(2, "0") + ":"
                + String(seconds).padStart(2, "0")
    }

    function evidenceText(item) {
        const label = String(item.text || "")
        if (item.value === undefined || item.value === null
                || String(item.value).length === 0)
            return label
        const unit = String(item.unit || "").toLowerCase()
        if (!unit || unit === "boolean")
            return label
        const numeric = Number(item.value)
        if (!isFinite(numeric))
            return label + "  " + String(item.value)
        if (unit === "hz") {
            const frequency = numeric >= 1000
                    ? (numeric / 1000).toLocaleString(Qt.locale(), "f",
                                                     numeric % 1000 ? 1 : 0)
                      + " kHz"
                    : Math.round(numeric) + " Hz"
            return label + "  " + frequency
        }
        const digits = Math.abs(numeric - Math.round(numeric)) < 0.0005
                       ? 0 : Math.abs(numeric) < 1 ? 3 : 2
        // Keep a measured number together even beside CJK text.
        const formatted = numeric !== 0 && Math.abs(numeric) < 0.001
                          ? numeric.toExponential(2)
                          : numeric.toLocaleString(Qt.locale(), "f", digits)
        const value = formatted.split("").join("\u2060")
        const localizedUnit = unit === "ratio" ? qsTr("比率")
                            : unit === "score" ? qsTr("评分")
                            : unit === "normalized_entropy" ? qsTr("归一化熵")
                            : unit === "boolean" ? ""
                            : item.unit || ""
        return label + "  " + value
                + (localizedUnit ? "\u00a0" + localizedUnit : "")
    }

    function modifiedText(value) {
        if (!value)
            return qsTr("--")
        const date = new Date(String(value).replace(" ", "T"))
        return isNaN(date.getTime()) ? String(value)
                                   : Qt.formatDateTime(date, "yyyy-MM-dd HH:mm:ss")
    }

    component OutlineButton: ThemedButton {
        id: outlineButton
        property url iconSource
        contentItem: RowLayout {
            spacing: Theme.spacingSm
            Item { Layout.fillWidth: true }
            ThemedIcon {
                source: outlineButton.iconSource
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                tint: outlineButton.enabled ? Theme.textPrimary : Theme.textDisabled
            }
            Text {
                text: outlineButton.text
                color: outlineButton.enabled ? Theme.textPrimary : Theme.textDisabled
                font.family: Theme.fontPrimary
                font.pixelSize: outlineButton.labelPixelSize
                elide: Text.ElideRight
                Layout.maximumWidth: Math.max(0, outlineButton.width - 54)
            }
            Item { Layout.fillWidth: true }
        }
        background: Rectangle {
            radius: Theme.radiusSm
            color: !outlineButton.enabled ? Theme.losslessPanelSurface
                   : outlineButton.down ? Theme.surfacePressed
                   : outlineButton.hovered ? Theme.surfaceHover
                                           : Theme.losslessPanelSurface
            border.color: outlineButton.activeFocus
                          ? Theme.focus : Theme.opaqueBorder
            border.width: outlineButton.activeFocus ? 2 : 1
        }
    }

    color: Theme.losslessWorkspaceSurface
    border.color: Theme.opaqueBorder
    border.width: 1
    radius: Theme.radiusSm
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            objectName: "losslessConclusionHeader"
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            color: Theme.losslessPanelHeaderSurface
            border.color: Theme.opaqueBorder
            border.width: 1
            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("分析结论")
                color: Theme.textPrimary
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.losslessFontSizeSection
                font.weight: Font.Normal
            }
        }

        Flickable {
            id: detailsFlick
            objectName: "losslessDetailsScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: detailsColumn.y + detailsColumn.implicitHeight
                           + Theme.spacingXs
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ThemedScrollBar {
                width: 8
                implicitWidth: 8
                visible: detailsFlick.contentHeight > detailsFlick.height + 0.5
            }

            Column {
                id: detailsColumn
                x: Theme.spacingMd
                y: Theme.spacingXs
                width: detailsFlick.width - Theme.spacingMd * 2
                spacing: Theme.spacingXs

                Text {
                    id: verdictText
                    objectName: "losslessVerdictText"
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: root.hasResult
                          ? (root.result.verdictText || qsTr("无法确定"))
                          : qsTr("等待选择分析结果")
                    color: root.hasResult
                           ? root.verdictColor(root.result.verdictCode)
                           : Theme.textTertiary
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.losslessFontSizeTitle
                    font.weight: Font.Normal
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    height: 72
                    spacing: Theme.spacingMd
                    visible: root.hasResult

                    Text {
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("证据评分")
                        color: Theme.textSecondary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.losslessFontSizeBody
                    }
                    LosslessConfidenceRing {
                        width: 72
                        height: 72
                        value: Number(root.result.confidence || 0)
                        ringColor: root.verdictColor(root.result.verdictCode)
                    }
                }

                Column {
                    width: parent.width
                    spacing: Theme.spacingSm
                    visible: !!(root.hasResult
                                && root.result.candidates
                                && root.result.candidates.length > 0)
                    Text {
                        text: qsTr("可能来源")
                        color: Theme.textPrimary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.losslessFontSizeBody
                        font.weight: Font.DemiBold
                    }
                    Repeater {
                        model: root.hasResult && root.result.candidates
                               ? root.result.candidates : []
                        delegate: Rectangle {
                            required property var modelData
                            width: detailsColumn.width
                            height: candidateColumn.implicitHeight + Theme.spacingMd * 2
                            radius: Theme.radiusSm
                            color: Theme.losslessPanelHeaderSurface
                            border.color: Theme.opaqueBorder
                            border.width: 1
                            Column {
                                id: candidateColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: Theme.spacingMd
                                spacing: Theme.spacingXs
                                Row {
                                    width: parent.width
                                    Text {
                                        width: parent.width - candidateConfidence.width
                                        text: modelData.format || qsTr("未知来源")
                                        elide: Text.ElideRight
                                        color: Theme.textPrimary
                                        font.family: Theme.fontPrimary
                                        font.pixelSize: Theme.losslessFontSizeMeta
                                    }
                                    Text {
                                        id: candidateConfidence
                                        width: 46
                                        horizontalAlignment: Text.AlignRight
                                        text: qsTr("%1分").arg(Number(modelData.confidence || 0))
                                        color: Theme.accent
                                        font.family: Theme.fontPrimary
                                        font.pixelSize: Theme.losslessFontSizeMeta
                                    }
                                }
                                Text {
                                    width: parent.width
                                    text: modelData.limitation || ""
                                    visible: text.length > 0
                                    wrapMode: Text.Wrap
                                    color: Theme.textSecondary
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: Theme.losslessFontSizeMeta
                                }
                            }
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: Theme.spacingSm
                    visible: !!(root.hasResult && root.result.chain
                                && root.result.chain.length > 0)
                    Text {
                        text: qsTr("推测转换链")
                        color: Theme.textPrimary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.losslessFontSizeBody
                        font.weight: Font.DemiBold
                    }
                    Flow {
                        id: chainFlow
                        objectName: "losslessConversionChain"
                        width: parent.width
                        spacing: 0
                        readonly property int columns: Math.min(3, root.result.chain ? root.result.chain.length : 1)
                        readonly property real nodeWidth: Math.min(columns === 1 ? 112 : 180, (width - (columns - 1) * 40) / Math.max(1, columns))
                        Repeater {
                            model: root.hasResult && root.result.chain
                                   ? root.result.chain : []
                            delegate: Row {
                                required property int index
                                required property var modelData
                                spacing: 0
                                Rectangle {
                                    objectName: "losslessChainNode_" + index
                                    width: chainFlow.nodeWidth
                                    height: Math.max(72, chainText.implicitHeight + 16)
                                    radius: Theme.radiusSm
                                    color: Theme.losslessPanelHeaderSurface
                                    border.color: root.verdictColor(
                                                      root.result.verdictCode)
                                    border.width: 1
                                    Text {
                                        id: chainText
                                        anchors.centerIn: parent
                                        width: parent.width - 16
                                        text: String(modelData)
                                        wrapMode: Text.Wrap
                                        horizontalAlignment: Text.AlignHCenter
                                        color: Theme.textPrimary
                                        font.family: Theme.fontPrimary
                                        font.pixelSize: Theme.losslessFontSizeMeta
                                    }
                                }
                                Text {
                                    visible: index < root.result.chain.length - 1
                                             && (index + 1) % chainFlow.columns !== 0
                                    width: visible ? 40 : 0
                                    height: 72
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    text: "→"
                                    color: Theme.textSecondary
                                    font.family: Theme.fontFallback
                                    font.pixelSize: Theme.losslessFontSizeSection
                                }
                            }
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: Theme.spacingXs
                    visible: !!(root.hasResult && root.result.evidence
                                && root.result.evidence.length > 0)
                    Text {
                        text: qsTr("判定依据")
                        color: Theme.textPrimary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.losslessFontSizeBody
                        font.weight: Font.DemiBold
                    }
                    Repeater {
                        id: evidenceList
                        objectName: "losslessEvidenceList"
                        model: root.hasResult && root.result.evidence
                               ? root.result.evidence : []
                        delegate: Row {
                            id: evidenceRow
                            required property int index
                            required property var modelData
                            // Popup items live in the window overlay rather than the
                            // row's visual subtree once shown.
                            property alias detailPopup: evidenceTip
                            objectName: "losslessEvidenceRow_" + index
                            activeFocusOnTab: true
                            Accessible.role: Accessible.StaticText
                            Accessible.name: root.evidenceText(modelData)
                            Accessible.description: evidenceTip.text
                            width: detailsColumn.width
                            spacing: Theme.spacingSm
                            Rectangle {
                                width: 6
                                height: 6
                                radius: 3
                                anchors.top: parent.top
                                anchors.topMargin: 6
                                color: root.verdictColor(root.result.verdictCode)
                            }
                            Text {
                                width: parent.width - 14
                                text: root.evidenceText(modelData)
                                wrapMode: Text.WordWrap
                                color: Theme.textSecondary
                                font.family: Theme.fontPrimary
                                font.pixelSize: Theme.losslessFontSizeMeta
                            }
                            HoverHandler { id: evidenceHover }
                            ThemedToolTip {
                                id: evidenceTip
                                objectName: "losslessEvidenceTip_" + evidenceRow.index
                                visible: (evidenceHover.hovered || evidenceRow.activeFocus)
                                         && text.length > 0
                                width: Math.min(360, root.width - 24)
                                text: (evidenceRow.modelData.reference
                                      ? qsTr("参考阈值：%1").arg(evidenceRow.modelData.reference) : "")
                                      + (Number(evidenceRow.modelData.coverageEndSeconds) > 0
                                         ? "\n" + qsTr("分析区间：%1–%2 秒")
                                           .arg(Number(evidenceRow.modelData.coverageStartSeconds || 0).toFixed(2))
                                           .arg(Number(evidenceRow.modelData.coverageEndSeconds).toFixed(2)) : "")
                                contentItem: Text {
                                    text: evidenceTip.text
                                    wrapMode: Text.Wrap
                                    color: Theme.textPrimary
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: Theme.fontSizeMeta
                                }
                            }
                        }
                    }
                }

                Column {
                    id: fileInfoColumn
                    objectName: "losslessFileInformation"
                    width: parent.width
                    visible: root.hasResult
                    spacing: 0
                    Text {
                        text: qsTr("文件信息")
                        color: Theme.textPrimary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.losslessFontSizeBody
                        font.weight: Font.DemiBold
                    }
                    Repeater {
                        objectName: "losslessFileInformationRows"
                        model: [
                            { label: qsTr("路径"), value: root.result.path || qsTr("--") },
                            { label: qsTr("格式"), value: (root.result.formatName || qsTr("--")) + (root.result.codec ? " · " + root.result.codec : "") },
                            { label: qsTr("音频"), value: root.result.rawDsdSampleRate
                                ? (Number(root.result.rawDsdSampleRate) / 1000000).toLocaleString(Qt.locale(), "f", 4) + " MHz · 1-bit · " + (root.result.channels || "--") + qsTr(" 声道")
                                : root.result.sampleRate ? (Number(root.result.sampleRate) / 1000).toLocaleString(Qt.locale(), "f", 1) + " kHz · " + (root.result.bitsPerSample || "--") + "-bit · " + (root.result.channels || "--") + qsTr(" 声道") : qsTr("--") },
                            { label: qsTr("大小"), value: root.sizeText(root.result.fileSize) },
                            { label: qsTr("时长"), value: root.durationText(root.result.durationMs) },
                            { label: qsTr("修改"), value: root.modifiedText(root.result.modified) }
                        ]
                        delegate: Row {
                            required property var modelData
                            width: fileInfoColumn.width
                            spacing: Theme.spacingSm
                            Text {
                                width: root.englishUi ? 72 : 42
                                text: modelData.label
                                color: Theme.textTertiary
                                font.family: Theme.fontPrimary
                                font.pixelSize: Theme.losslessFontSizeMeta
                            }
                            Text {
                                width: parent.width
                                       - (root.englishUi ? 80 : 50)
                                text: modelData.value
                                elide: modelData.label === qsTr("路径")
                                       ? Text.ElideMiddle : Text.ElideRight
                                color: Theme.textSecondary
                                font.family: Theme.fontPrimary
                                font.pixelSize: Theme.losslessFontSizeMeta
                            }
                        }
                    }
                }

                Text {
                    width: parent.width
                    visible: !!(root.hasResult && root.result.error)
                    text: root.result.error || ""
                    wrapMode: Text.Wrap
                    color: Theme.error
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.losslessFontSizeMeta
                }

                Column {
                    width: parent.width
                    spacing: Theme.spacingXs
                    visible: !!(root.hasResult && root.result.warnings
                                && root.result.warnings.length > 0)
                    Repeater {
                        model: root.hasResult && root.result.warnings
                               ? root.result.warnings : []
                        delegate: Text {
                            required property var modelData
                            width: detailsColumn.width
                            text: qsTr("警告：%1").arg(String(modelData))
                            wrapMode: Text.Wrap
                            color: Theme.warning
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.losslessFontSizeMeta
                        }
                    }
                }
            }
        }

        Rectangle {
            objectName: "losslessCoverage"
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            color: Theme.losslessPanelSurface
            visible: root.hasResult
            Column {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMd
                anchors.rightMargin: Theme.spacingMd
                anchors.topMargin: Theme.spacingSm
                spacing: Theme.spacingXs
                Text {
                    text: qsTr("分析完整度")
                    color: Theme.textPrimary
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.losslessFontSizeMeta
                }
                RowLayout {
                    width: parent.width
                    spacing: Theme.spacingSm
                    Text {
                        id: coverageValue
                        Layout.preferredWidth: 44
                        text: Math.round(Math.max(0, Math.min(1,
                              Number(root.result.coverage || 0))) * 100) + "%"
                        color: Theme.accent
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.losslessFontSizeMeta
                    }
                    LosslessProgressBar {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 10
                        value: Number(root.result.coverage || 0)
                    }
                }
            }
        }

        Rectangle {
            objectName: "losslessDisclaimer"
            Layout.fillWidth: true
            Layout.preferredHeight: root.englishUi ? 58 : 44
            color: Theme.losslessPanelSurface
            border.color: Theme.opaqueDivider
            border.width: 1
            Text {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMd
                anchors.rightMargin: Theme.spacingMd
                verticalAlignment: Text.AlignVCenter
                text: qsTr("ⓘ 评分尚未校准，不代表正确率。结果为信号特征推断，不代表可恢复原始文件。")
                wrapMode: Text.Wrap
                color: Theme.textTertiary
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.losslessFontSizeMeta
            }
        }

        Rectangle {
            objectName: "losslessConclusionActions"
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            color: Theme.losslessPanelHeaderSurface
            border.color: Theme.opaqueBorder
            border.width: 1
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                spacing: Theme.spacingSm
                OutlineButton {
                    objectName: "losslessExportReportButton"
                    Layout.fillWidth: true
                    compact: true
                    Layout.fillHeight: true
                    labelPixelSize: Theme.losslessFontSizeBody
                    text: qsTr("导出报告")
                    iconSource: Theme.icon("file-copy-line")
                    enabled: root.hasResult
                    onClicked: root.exportRequested()
                }
                OutlineButton {
                    objectName: "losslessLocateButton"
                    Layout.fillWidth: true
                    compact: true
                    Layout.fillHeight: true
                    labelPixelSize: Theme.losslessFontSizeBody
                    text: qsTr("在播放器中定位")
                    iconSource: Theme.icon("folder-open-line")
                    enabled: !!(root.hasResult && root.result.path)
                    onClicked: root.locateRequested(String(root.result.path))
                }
            }
        }
    }
}

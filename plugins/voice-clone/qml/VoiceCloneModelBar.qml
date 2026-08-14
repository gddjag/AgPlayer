pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: Theme.panel
    border.color: Theme.border
    radius: Theme.radiusSm
    implicitHeight: 154

    property var models: []
    property int selectedIndex: models.length > 0 ? 0 : -1
    property var selectedModel: selectedIndex >= 0 && selectedIndex < models.length
                                ? models[selectedIndex] : ({})
    property bool licenseAcceptanceRequired: false
    property bool downloadInProgress: false
    property int downloadProgressPercent: -1
    property string downloadModelId: ""
    property string downloadState: "idle"
    property string downloadError: ""
    readonly property bool selectedDownload: !!selectedModel.stableId
                                             && selectedModel.stableId === downloadModelId
    signal modelSelected(int index, string stableId)
    signal refreshRequested()
    signal downloadRequested(string stableId)
    signal pauseDownloadRequested()
    signal resumeDownloadRequested()
    signal cancelDownloadRequested()
    signal retryDownloadRequested()
    signal openDirectoryRequested()
    signal licenseAcceptanceRequested()

    function installStateText(state) {
        if (state === "ready") return qsTr("已安装")
        if (state === "local-unverified") return qsTr("本地模型")
        return qsTr("未下载")
    }

    function capabilityText(capability) {
        switch (capability) {
        case "voice-clone": return qsTr("人声克隆")
        case "multilingual": return qsTr("多语言")
        case "higher-quality": return qsTr("高质量")
        case "expressive": return qsTr("情感控制")
        case "chinese-dialects": return qsTr("中文方言")
        default: return capability
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Repeater {
                model: root.models

                Button {
                    id: modelCard
                    required property int index
                    required property var modelData
                    objectName: "voiceCloneModelCard" + modelCard.index
                    Layout.fillWidth: true
                    Layout.preferredHeight: 68
                    checked: modelCard.index === root.selectedIndex
                    checkable: true
                    focusPolicy: Qt.StrongFocus
                    onClicked: root.modelSelected(modelCard.index,
                                                  modelCard.modelData.stableId || "")

                    contentItem: ColumnLayout {
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: modelCard.modelData.displayName || qsTr("未命名模型")
                            elide: Text.ElideRight
                            color: modelCard.checked ? Theme.primaryText : Theme.secondaryText
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Label {
                            objectName: "voiceCloneModelInstallState" + modelCard.index
                            Layout.fillWidth: true
                            text: (modelCard.modelData.provider || "") + "  ·  "
                                  + root.installStateText(modelCard.modelData.installState || "")
                            elide: Text.ElideRight
                            color: modelCard.modelData.installState === "ready"
                                   ? Theme.waveformGreen : Theme.secondaryText
                            font.pixelSize: 11
                        }
                    }
                    background: Rectangle {
                        color: modelCard.checked ? Qt.rgba(Theme.accent.r, Theme.accent.g,
                                                          Theme.accent.b, 0.16)
                                                 : (modelCard.hovered
                                                    ? Theme.hoverSurface : Theme.elevated)
                        border.color: modelCard.checked ? Theme.accent : Theme.border
                        radius: Theme.radiusSm
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Label {
                    Layout.fillWidth: true
                    text: root.selectedModel.description || qsTr("选择模型查看简介")
                    color: Theme.primaryText
                    elide: Text.ElideRight
                    font.pixelSize: 12
                }
                Label {
                    objectName: "voiceCloneCapabilitySummary"
                    Layout.fillWidth: true
                    text: (root.selectedModel.capabilityPreview || []).map(
                              capability => root.capabilityText(capability)).join("  ·  ")
                    color: Theme.secondaryText
                    elide: Text.ElideRight
                    font.pixelSize: 11
                }
            }

            CheckBox {
                id: licenseAuthorityCheck
                objectName: "voiceCloneLicenseAuthorityCheck"
                visible: !!root.selectedModel.requiresLicenseAcceptance
                text: qsTr("我已阅读许可并确认拥有合法授权")
                checked: visible && !root.licenseAcceptanceRequired
                enabled: root.licenseAcceptanceRequired
                nextCheckState: function() { return Qt.Unchecked }
                onClicked: if (root.licenseAcceptanceRequired)
                               root.licenseAcceptanceRequested()
                Accessible.name: text
            }
            Button {
                objectName: "voiceCloneOfficialProjectLink"
                text: qsTr("官方项目")
                enabled: !!root.selectedModel.officialProjectUrl
                onClicked: Qt.openUrlExternally(root.selectedModel.officialProjectUrl)
            }
            Button {
                objectName: "voiceCloneHuggingFaceLink"
                visible: !!root.selectedModel.huggingFaceUrl
                text: "Hugging Face"
                onClicked: Qt.openUrlExternally(root.selectedModel.huggingFaceUrl)
            }
            Button {
                objectName: "voiceCloneModelScopeLink"
                visible: !!root.selectedModel.modelScopeUrl
                text: "ModelScope"
                onClicked: Qt.openUrlExternally(root.selectedModel.modelScopeUrl)
            }
            Button {
                objectName: "voiceCloneLicenseLink"
                text: root.selectedModel.licenseName || qsTr("许可")
                enabled: !!root.selectedModel.licenseUrl
                onClicked: Qt.openUrlExternally(root.selectedModel.licenseUrl)
            }
            Button {
                objectName: "voiceCloneDownloadModelButton"
                text: root.selectedDownload && root.downloadInProgress
                      ? (root.downloadProgressPercent >= 0
                         ? qsTr("下载中 %1%").arg(root.downloadProgressPercent)
                         : qsTr("下载中…"))
                      : qsTr("下载模型")
                visible: !!root.selectedModel.stableId
                         && root.selectedModel.installState !== "ready"
                         && root.selectedModel.installState !== "local-unverified"
                enabled: !root.downloadInProgress
                onClicked: root.downloadRequested(root.selectedModel.stableId || "")
            }
            Button {
                objectName: "voiceClonePauseDownloadButton"
                text: qsTr("暂停")
                visible: root.selectedDownload
                         && (root.downloadState === "resolving"
                             || root.downloadState === "downloading")
                onClicked: root.pauseDownloadRequested()
            }
            Button {
                objectName: "voiceCloneResumeDownloadButton"
                text: qsTr("继续")
                visible: root.selectedDownload && root.downloadState === "paused"
                onClicked: root.resumeDownloadRequested()
            }
            Button {
                objectName: "voiceCloneCancelDownloadButton"
                text: qsTr("取消下载")
                visible: root.selectedDownload
                         && (root.downloadInProgress || root.downloadState === "paused"
                             || root.downloadState === "license-required")
                onClicked: root.cancelDownloadRequested()
            }
            Button {
                objectName: "voiceCloneRetryDownloadButton"
                text: qsTr("重试下载")
                visible: root.selectedDownload
                         && (root.downloadState === "failed"
                             || root.downloadState === "canceled")
                onClicked: root.retryDownloadRequested()
            }
            Label {
                objectName: "voiceCloneDownloadError"
                visible: root.selectedDownload && root.downloadState === "failed"
                         && root.downloadError.length > 0
                text: root.downloadError
                color: Theme.favoriteRed
                elide: Text.ElideRight
                Layout.maximumWidth: 260
            }
            ToolButton {
                objectName: "voiceCloneRefreshModelsButton"
                icon.source: Theme.icon("arrow-go-back-line")
                icon.color: Theme.iconPrimary
                text: qsTr("刷新模型")
                display: AbstractButton.TextBesideIcon
                onClicked: root.refreshRequested()
            }
            ToolButton {
                objectName: "voiceCloneOpenModelDirectoryButton"
                icon.source: Theme.icon("folder-open-line")
                icon.color: Theme.iconPrimary
                text: qsTr("打开目录")
                display: AbstractButton.TextBesideIcon
                onClicked: root.openDirectoryRequested()
            }
        }
    }
}

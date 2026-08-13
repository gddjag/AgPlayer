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
    property bool legalAuthorityConfirmed: false
    signal modelSelected(int index, string stableId)
    signal refreshRequested()
    signal openDirectoryRequested()

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
                    objectName: "voiceCloneModelCard" + index
                    Layout.fillWidth: true
                    Layout.preferredHeight: 68
                    checked: index === root.selectedIndex
                    checkable: true
                    focusPolicy: Qt.StrongFocus
                    onClicked: root.modelSelected(index, modelData.stableId || "")

                    contentItem: ColumnLayout {
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: modelData.displayName || qsTr("未命名模型")
                            elide: Text.ElideRight
                            color: checked ? Theme.primaryText : Theme.secondaryText
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Label {
                            Layout.fillWidth: true
                            text: (modelData.provider || "") + "  ·  "
                                  + (modelData.installState || qsTr("未知状态"))
                            elide: Text.ElideRight
                            color: modelData.installState === "ready"
                                   ? Theme.waveformGreen : Theme.secondaryText
                            font.pixelSize: 11
                        }
                    }
                    background: Rectangle {
                        color: checked ? Qt.rgba(Theme.accent.r, Theme.accent.g,
                                                Theme.accent.b, 0.16)
                                       : (parent.hovered ? Theme.hoverSurface : Theme.elevated)
                        border.color: checked ? Theme.accent : Theme.border
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
                    Layout.fillWidth: true
                    text: (root.selectedModel.capabilityPreview || []).join("  ·  ")
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
                checked: root.legalAuthorityConfirmed
                onToggled: root.legalAuthorityConfirmed = checked
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

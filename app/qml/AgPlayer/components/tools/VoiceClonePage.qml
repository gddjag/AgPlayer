import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: page
    objectName: "voiceClonePage"
    color: Theme.background
    focus: true

    readonly property string pluginId: "voice-clone"
    property var pluginInfo: PluginInstallManager.pluginInfo(pluginId)

    function refresh() {
        pluginInfo = PluginInstallManager.pluginInfo(pluginId);
    }

    Component.onCompleted: refresh()

    Connections {
        target: PluginInstallManager
        function onPluginStateChanged(changedPluginId) {
            if (changedPluginId === pluginId || changedPluginId.length === 0) {
                page.refresh();
            }
        }
        function onSharedRuntimePathChanged() { page.refresh() }
        function onBusyChanged() { page.refresh() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 74
            radius: Theme.radiusSm
            color: Theme.panel
            border.color: Theme.border
            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Label {
                        text: qsTr("人声克隆")
                        color: Theme.primaryText
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: pluginInfo.description
                        color: Theme.secondaryText
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
                Button {
                    Layout.preferredWidth: 110
                    Layout.preferredHeight: 38
                    text: pluginInfo.installed
                        ? qsTr("卸载模块")
                        : qsTr("安装模块")
                    enabled: !PluginInstallManager.busy
                    icon.source: Theme.icon(pluginInfo.installed
                                          ? "delete-bin-line"
                                          : "download-line")
                    onClicked: pluginInfo.installed
                        ? PluginInstallManager.uninstallPlugin(pluginId)
                        : PluginInstallManager.installPlugin(pluginId)
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            radius: Theme.radiusSm
            color: Theme.elevated
            border.color: Theme.border
            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10
                Label {
                    Layout.preferredWidth: 88
                    text: qsTr("状态")
                    color: Theme.secondaryText
                }
                Text {
                    Layout.fillWidth: true
                    text: pluginInfo.status
                    color: Theme.primaryText
                }
                ProgressBar {
                    Layout.preferredWidth: 180
                    visible: pluginInfo.downloading
                    value: pluginInfo.progress / 100
                }
                Label {
                    text: PluginInstallManager.statusText
                    color: Theme.secondaryText
                    visible: PluginInstallManager.statusText.length > 0
                        || pluginInfo.lastError.length > 0
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.panel
            radius: Theme.radiusSm
            border.color: Theme.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10
                Label {
                    text: qsTr("共享运行时")
                    color: Theme.primaryText
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Label {
                    text: PluginInstallManager.sharedRuntimePath
                    color: Theme.secondaryText
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.border
                }
                Label {
                    text: qsTr("安装目录")
                    color: Theme.primaryText
                }
                Label {
                    text: pluginInfo.path
                    color: Theme.secondaryText
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                Label {
                    text: qsTr("当前仅提供安装壳，后续版本接入运行时后可直接在此页执行转换任务。")
                    color: Theme.secondaryText
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
    }
}

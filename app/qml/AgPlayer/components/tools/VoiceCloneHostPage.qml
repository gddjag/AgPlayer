import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Item {
    id: root

    property var hostController: VoiceCloneHostController
    property url workspaceUrl: hostController ? hostController.mainQmlUrl : ""

    function stateText() {
        if (!hostController) return qsTr("插件状态不可用")
        switch (hostController.state) {
        case 0: return qsTr("人声克隆插件尚未安装")
        case 1: return qsTr("插件校验失败")
        case 2: return qsTr("插件已就绪，正在加载")
        case 3: return qsTr("插件已加载")
        case 4: return qsTr("插件已加载，可更新")
        default: return qsTr("插件加载失败")
        }
    }

    Component.onCompleted: {
        if (!hostController) return
        hostController.refresh()
        if (!hostController.pluginLoaded
                && (hostController.state === 2 || hostController.state === 4))
            hostController.openPlugin()
    }

    Loader {
        id: workspaceLoader
        objectName: "voiceCloneWorkspaceLoader"
        anchors.fill: parent
        active: root.hostController
                && root.hostController.pluginLoaded
                && root.workspaceUrl.toString() !== ""
        source: active ? root.workspaceUrl : ""
        onLoaded: {
            if (item && root.hostController)
                item.controller = root.hostController.pluginController
        }
    }

    Rectangle {
        objectName: "voiceCloneInstallState"
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 520)
        implicitHeight: stateColumn.implicitHeight + 48
        visible: !workspaceLoader.active || workspaceLoader.status === Loader.Error
        color: Theme.panel
        border.color: Theme.border
        radius: Theme.radiusMd

        ColumnLayout {
            id: stateColumn
            anchors.fill: parent
            anchors.margins: 24
            spacing: Theme.spacingMd

            ThemedIcon {
                Layout.alignment: Qt.AlignHCenter
                source: Theme.icon("information-line")
                tint: Theme.iconAccent
                sourceSize.width: 32
                sourceSize.height: 32
            }
            Label {
                Layout.fillWidth: true
                text: root.stateText()
                horizontalAlignment: Text.AlignHCenter
                color: Theme.primaryText
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                visible: root.hostController && root.hostController.errorString !== ""
                text: root.hostController ? root.hostController.errorString : ""
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                color: Theme.favoriteRed
            }
            Button {
                objectName: "voiceCloneRefreshPluginButton"
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("刷新安装状态")
                onClicked: {
                    root.hostController.refresh()
                    if (root.hostController.state === 2
                            || root.hostController.state === 4)
                        root.hostController.openPlugin()
                }
            }
        }
    }
}

import QtQuick
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: navigation
    objectName: "audioToolsTopNav"

    property bool referenceWorkbench: false
    property bool separationWorkbench: false
    property bool losslessWorkbench: false
    property int currentTool: 0
    property Window window
    readonly property color activeLabelColor: Theme.primaryText
    readonly property bool compactLayout: width < Math.max(960,
        visibleTools.length * 150
        + Theme.spacingMd + Theme.spacingLg + Theme.spacingSm * (visibleTools.length - 1))
    readonly property var visibleToolOrder: [0, 4, 1, 2, 3, 5]
    readonly property var visibleTools: [
        { toolId: 0, name: qsTr("音频编辑"), icon: "equalizer-line" },
        { toolId: 4, name: qsTr("人声伴奏分离"), icon: "music-2-line" },
        { toolId: 1, name: qsTr("格式转换"), icon: "briefcase-4-line" },
        { toolId: 2, name: qsTr("元数据编辑"), icon: "information-line" },
        { toolId: 3, name: qsTr("文件名处理"), icon: "file-copy-line" },
        { toolId: 5, name: qsTr("无损鉴别"), icon: "file-search-line" }
    ]

    signal toolSelected(int toolId)

    color: Theme.navigationSurface
    border.color: Theme.opaqueBorder
    border.width: 1
    radius: 0
    implicitHeight: Theme.settingsRowHeight

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingLg
        spacing: Theme.spacingSm

        Repeater {
            model: navigation.visibleTools

            ThemedTabButton {
                id: navButton
                objectName: "audioToolNav_" + modelData.toolId
                Layout.fillWidth: navigation.compactLayout
                Layout.preferredWidth: navigation.compactLayout ? 0 : 150
                Layout.minimumWidth: navigation.compactLayout ? 0 : 112
                Layout.preferredHeight: navigation.implicitHeight
                Layout.maximumHeight: Layout.preferredHeight
                text: modelData.name
                iconSource: Theme.icon(modelData.icon)
                selected: navigation.currentTool === modelData.toolId
                underlineSelection: false
                labelPixelSize: Theme.fontSizeBody
                leftPadding: navigation.compactLayout ? Theme.spacingSm
                                                      : Theme.spacingMd
                rightPadding: leftPadding
                iconSize: navigation.compactLayout ? Theme.iconSizeSm
                                                   : Theme.iconSizeMd

                onClicked: navigation.toolSelected(modelData.toolId)
            }
        }

        Item { Layout.fillWidth: true }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

RowLayout {
    id: root
    objectName: "experienceActions"
    property bool compact: false
    property bool showImmersive: true
    property bool showLyrics: true
    spacing: compact ? 0 : 2

    function buttonSize() { return compact ? 26 : 32 }

    ToolButton {
        objectName: "immersiveActionButton"
        visible: root.showImmersive
        Layout.preferredWidth: root.buttonSize()
        Layout.preferredHeight: root.buttonSize()
        flat: true
        checkable: true
        checked: PlayerExperienceController.immersiveMode
                 !== PlayerExperienceController.Off
        icon.source: Theme.icon("immersive-visual-mode")
        icon.color: checked ? Theme.iconAccent : Theme.iconPrimary
        icon.width: root.compact ? 15 : 18
        icon.height: root.compact ? 15 : 18
        Accessible.name: checked ? qsTr("关闭沉浸视觉") : qsTr("开启沉浸视觉")
        onClicked: PlayerExperienceController.toggleImmersiveMode()
        ToolTip.text: Accessible.name
        ToolTip.visible: hovered
        background: null
    }

    ToolButton {
        objectName: "lyricsActionButton"
        visible: root.showLyrics
        Layout.preferredWidth: root.buttonSize()
        Layout.preferredHeight: root.buttonSize()
        flat: true
        checkable: true
        checked: PlayerExperienceController.lyricsVisible
        icon.source: Theme.icon("lyrics")
        icon.color: checked ? Theme.iconAccent : Theme.iconPrimary
        icon.width: root.compact ? 15 : 18
        icon.height: root.compact ? 15 : 18
        Accessible.name: checked ? qsTr("隐藏歌词") : qsTr("显示歌词")
        onClicked: PlayerExperienceController.toggleLyricsVisible()
        ToolTip.text: Accessible.name
        ToolTip.visible: hovered
        background: null
    }
}

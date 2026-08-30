import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

RowLayout {
    id: root
    objectName: "experienceActions"
    property bool compact: false
    spacing: compact ? 0 : 2

    function buttonSize() { return compact ? 26 : 32 }

    ToolButton {
        objectName: "themeActionButton"
        Layout.preferredWidth: root.buttonSize()
        Layout.preferredHeight: root.buttonSize()
        flat: true
        icon.source: Theme.icon(PlayerExperienceController.immersiveMode
                                !== PlayerExperienceController.Off
                                ? "pulse-line"
                                : SettingsController.playerShellMode === 0
                                  ? "merge-cells-horizontal"
                                  : "fullscreen-fill")
        icon.color: PlayerExperienceController.immersiveMode
                    !== PlayerExperienceController.Off
                    ? Theme.iconAccent : Theme.iconPrimary
        icon.width: root.compact ? 15 : 18
        icon.height: root.compact ? 15 : 18
        Accessible.name: PlayerExperienceController.immersiveMode
                         !== PlayerExperienceController.Off
                         ? qsTr("切换到经典双窗口")
                         : SettingsController.playerShellMode === 0
                           ? qsTr("切换到集成单窗口")
                           : qsTr("切换到独立沉浸视觉")
        onClicked: PlayerExperienceController.cycleExperienceTheme()
        ToolTip.text: Accessible.name
        ToolTip.visible: hovered
        background: null
    }

    ToolButton {
        objectName: "lyricsActionButton"
        Layout.preferredWidth: root.buttonSize()
        Layout.preferredHeight: root.buttonSize()
        flat: true
        checkable: true
        checked: PlayerExperienceController.lyricsVisible
        icon.source: Theme.icon("music-2-line")
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

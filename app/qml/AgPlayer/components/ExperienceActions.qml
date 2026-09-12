import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer
import "PlayerPresentation.js" as PlayerPresentation

RowLayout {
    id: root
    objectName: "experienceActions"
    property var presentationProfile: PlayerPresentation.profile("classic")
    property bool allowImmersive: true
    property bool allowLyrics: true
    property bool compact: false
    readonly property bool showImmersive:
        allowImmersive && PlayerPresentation.hasAction(
            presentationProfile, "immersiveActionButton")
    readonly property bool showLyrics:
        allowLyrics && PlayerPresentation.hasAction(
            presentationProfile, "lyricsActionButton")
    spacing: compact ? 0 : 2

    function buttonSize() { return compact ? 32 : 34 }
    function keyboardFocused(control) {
        return control.activeFocus
            && (control.focusReason === Qt.TabFocusReason
                || control.focusReason === Qt.BacktabFocusReason)
    }

    ToolButton {
        objectName: root.showLyrics ? "lyricsActionButton" : ""
        visible: root.showLyrics
        Layout.preferredWidth: root.buttonSize()
        Layout.preferredHeight: root.buttonSize()
        flat: true
        checkable: true
        checked: PlayerExperienceController.lyricsVisible
        icon.source: Theme.icon("lyrics")
        icon.color: checked ? Theme.iconAccent : Theme.iconPrimary
        icon.width: 22
        icon.height: 22
        Accessible.name: checked ? qsTr("隐藏歌词") : qsTr("显示歌词")
        onClicked: PlayerExperienceController.toggleLyricsVisible()
        ToolTip.text: Accessible.name
        ToolTip.visible: hovered
        background: Rectangle {
            color: "transparent"
            border.width: root.keyboardFocused(parent) ? 2 : 0
            border.color: Theme.focus
            radius: Theme.radiusSm
        }
    }

    ToolButton {
        id: immersiveButton
        objectName: root.showImmersive ? "immersiveActionButton" : ""
        visible: root.showImmersive
        Layout.preferredWidth: root.buttonSize()
        Layout.preferredHeight: root.buttonSize()
        flat: true
        checkable: true
        checked: PlayerExperienceController.immersiveMode
                 !== PlayerExperienceController.Off
        contentItem: Item {
            AnimatedImmersiveIcon {
                anchors.centerIn: parent
                width: 20
                height: 20
                color: immersiveButton.checked ? Theme.iconAccent : Theme.iconPrimary
            }
        }
        Accessible.name: checked ? qsTr("关闭沉浸视觉") : qsTr("开启沉浸视觉")
        onClicked: PlayerExperienceController.toggleImmersiveMode()
        ToolTip.text: Accessible.name
        ToolTip.visible: hovered
        background: Rectangle {
            color: "transparent"
            border.width: root.keyboardFocused(parent) ? 2 : 0
            border.color: Theme.focus
            radius: Theme.radiusSm
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: titleBar
    property color surfaceColor: Theme.titleBarSurface
    color: surfaceColor
    radius: window && window.visibility === Window.Maximized
            ? 0 : Theme.windowRadius

    property Window window
    property bool showBrand: false

    signal openSettings()

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 0
        height: parent.height
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingMd
        spacing: Theme.spacingXs

        RowLayout {
            objectName: "titleBrand"
            visible: titleBar.showBrand
            spacing: Theme.spacingXs

            Image {
                source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
                sourceSize.width: 24
                sourceSize.height: 24
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                fillMode: Image.PreserveAspectFit
            }

            Text {
                objectName: "titleBrandText"
                text: "AgPlayer"
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeBody
                font.weight: Font.DemiBold
                font.italic: false
            }
        }

        Item { Layout.fillWidth: true }

        ToolButton {
            objectName: "settingsButton"
            text: qsTr("设置")
            display: AbstractButton.IconOnly
            Layout.preferredWidth: Theme.controlHeightCompact
            Layout.preferredHeight: Theme.controlHeightCompact
            icon.source: Theme.icon("settings-3-fill")
            icon.color: Theme.iconSecondary
            icon.width: 16
            icon.height: 16
            Accessible.name: text
            focusPolicy: Qt.StrongFocus
            onClicked: titleBar.openSettings()
            ToolTip.text: text
            ToolTip.visible: hovered

            background: Rectangle {
                color: !parent.enabled ? "transparent"
                      : parent.pressed ? Theme.surfacePressed
                      : parent.visualFocus ? Theme.surfaceHover
                      : parent.hovered ? Theme.surfaceHover
                      : "transparent"
                border.color: parent.visualFocus ? Theme.focus : "transparent"
                border.width: parent.visualFocus ? 2 : 0
                radius: Theme.radiusSm
            }
        }

        ToolButton {
            objectName: "minimizeButton"
            text: qsTr("最小化")
            display: AbstractButton.IconOnly
            Layout.preferredWidth: Theme.controlHeightCompact
            Layout.preferredHeight: Theme.controlHeightCompact
            icon.source: Theme.icon("subtract-line")
            icon.color: Theme.iconSecondary
            icon.width: 16
            icon.height: 16
            Accessible.name: text
            focusPolicy: Qt.StrongFocus
            onClicked: window.showMinimized()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered

            background: Rectangle {
                color: !parent.enabled ? "transparent"
                      : parent.pressed ? Theme.surfacePressed
                      : parent.visualFocus ? Theme.surfaceHover
                      : parent.hovered ? Theme.surfaceHover
                      : "transparent"
                border.color: parent.visualFocus ? Theme.focus : "transparent"
                border.width: parent.visualFocus ? 2 : 0
                radius: Theme.radiusSm
            }
        }

        ToolButton {
            objectName: "maximizeButton"
            text: window.visibility === Window.Maximized ? qsTr("还原") : qsTr("最大化")
            display: AbstractButton.IconOnly
            Layout.preferredWidth: Theme.controlHeightCompact
            Layout.preferredHeight: Theme.controlHeightCompact
            icon.source: window.visibility === Window.Maximized
                       ? Theme.icon("fullscreen-exit-fill")
                       : Theme.icon("checkbox-blank-line")
            icon.color: Theme.iconSecondary
            icon.width: 16
            icon.height: 16
            Accessible.name: text
            focusPolicy: Qt.StrongFocus
            onClicked: {
                if (window.visibility === Window.Maximized)
                    window.showNormal()
                else
                    window.showMaximized()
            }
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered

            background: Rectangle {
                color: !parent.enabled ? "transparent"
                      : parent.pressed ? Theme.surfacePressed
                      : parent.visualFocus ? Theme.surfaceHover
                      : parent.hovered ? Theme.surfaceHover
                      : "transparent"
                border.color: parent.visualFocus ? Theme.focus : "transparent"
                border.width: parent.visualFocus ? 2 : 0
                radius: Theme.radiusSm
            }
        }

        ToolButton {
            objectName: "closeButton"
            text: qsTr("关闭")
            display: AbstractButton.IconOnly
            Layout.preferredWidth: Theme.controlHeightCompact
            Layout.preferredHeight: Theme.controlHeightCompact
            icon.source: Theme.icon("close-fill")
            icon.color: Theme.iconSecondary
            icon.width: 16
            icon.height: 16
            Accessible.name: text
            focusPolicy: Qt.StrongFocus
            onClicked: WindowController.requestClose()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered

            background: Rectangle {
                color: !parent.enabled ? "transparent"
                      : parent.pressed ? Theme.critical
                      : parent.visualFocus ? Theme.surfaceHover
                      : parent.hovered ? Theme.danger
                      : "transparent"
                border.color: parent.visualFocus ? Theme.focus : "transparent"
                border.width: parent.visualFocus ? 2 : 0
                radius: Theme.radiusSm
            }
        }
    }

    // Drag the window from any empty area of the title bar. `z: -1` keeps
    // this MouseArea below the RowLayout so ToolButtons receive presses first;
    // no propagateComposedEvents / mouse.accepted forwarding is needed.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        z: -1
        onPressed: function(mouse) {
            if (window) window.startSystemMove()
        }
        onDoubleClicked: function(mouse) {
            if (window) {
                if (window.visibility === Window.Maximized)
                    window.showNormal()
                else
                    window.showMaximized()
            }
        }
    }
}

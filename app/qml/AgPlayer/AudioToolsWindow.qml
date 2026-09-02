import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Window {
    id: window
    objectName: "audioToolsWindow"
    visible: false
    // Reference workbench baseline. Layouts still contract below this size.
    width: 1672
    height: 941
    minimumWidth: 880
    minimumHeight: 560
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"
    title: qsTr("AgPlayer · 音频工具")
    readonly property bool metadataWorkbench:
        AudioToolsController.currentTool === 2
    readonly property bool separationWorkbench:
        AudioToolsController.currentTool === 4
    readonly property bool referenceWorkbench:
        metadataWorkbench || separationWorkbench

    function pageIndexForTool(toolId) {
        if (toolId === 4) return 1
        if (toolId === 1) return 2
        if (toolId === 2) return 3
        if (toolId === 3) return 4
        return 0
    }

    function editableTextHasFocus() {
        const active = window.activeFocusItem
        return active && active.readOnly !== true
            && (active.echoMode !== undefined
                || active.textDocument !== undefined
                || active.editable === true)
    }
    function playPauseFromSpace() {
        // QShortcut is resolved before the focused control receives the key.
        // Move focus to the window surface so Button/ComboBox cannot process
        // the same Space press as a second, unrelated activation.
        window.contentItem.forceActiveFocus()
        AudioEditorController.playPause()
    }
    function requestHide() {
        if (AudioToolsController.currentTool === 0
                && AudioEditorController.modified) {
            unsavedCloseDialog.open()
            return
        }
        WindowController.hideAudioTools()
    }
    onVisibleChanged: {
        if (visible && AudioToolsController.currentTool === 0)
            AudioEditorController.activate()
        else if (!visible)
            AudioEditorController.deactivate()
    }
    onClosing: function(close) {
        close.accepted = false
        requestHide()
    }
    palette.window: Theme.background
    palette.windowText: Theme.primaryText
    palette.base: Theme.surfaceElevated
    palette.alternateBase: Theme.surface
    palette.text: Theme.primaryText
    palette.button: Theme.surfaceElevated
    palette.buttonText: Theme.primaryText
    palette.highlight: Theme.highlight
    palette.highlightedText: Theme.highlightText
    palette.mid: Theme.opaqueBorder

    Connections {
        target: AudioToolsController
        function onCurrentToolChanged() {
            if (AudioToolsController.currentTool === 0)
                AudioEditorController.activate()
            else
                AudioEditorController.deactivate()
        }
    }

    Shortcut {
        objectName: "audioToolsSpaceShortcut"
        sequence: "Space"
        context: Qt.WindowShortcut
        enabled: window.visible
            && AudioToolsController.currentTool === 0
            && !window.editableTextHasFocus()
            && AudioEditorController.playbackSupported
            && AudioEditorController.hasDocument
        onActivated: window.playPauseFromSpace()
    }

    Dialog {
        id: unsavedCloseDialog
        parent: window.contentItem
        anchors.centerIn: parent
        title: qsTr("舍弃未保存更改？")
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            WindowController.hideAudioTools()
        }
        Label {
            text: qsTr("当前音频尚未保存。关闭窗口将舍弃这些更改。")
            color: Theme.primaryText
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.background
        border.color: Theme.border
        border.width: 1
        radius: window.visibility === Window.Maximized ? 0 : Theme.windowRadius

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                id: titleBar
                objectName: "audioToolsTitleBar"
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.navigationActionExtent
                    + Theme.spacingLg
                color: Theme.panel

                RowLayout {
                    z: 1
                    anchors.fill: parent
                    anchors.leftMargin: 24
                    anchors.rightMargin: 8
                    spacing: 14

                    Item {
                        objectName: "audioToolsLogo"
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        Image {
                            anchors.centerIn: parent
                            width: 28
                            height: 28
                            source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
                            fillMode: Image.PreserveAspectFit
                        }
                    }
                    Text {
                        objectName: "audioToolsWindowTitle"
                        text: qsTr("AgPlayer · 音频工具")
                        color: Theme.primaryText
                        font.family: Theme.fontFallback
                        font.pixelSize: 20
                        font.weight: Font.Medium
                    }

                    Item { Layout.fillWidth: true }

                    ToolButton {
                        objectName: "audioToolsMinimizeButton"
                        focusPolicy: Qt.NoFocus
                        Keys.onSpacePressed: function(event) { event.accepted = true }
                        Layout.preferredWidth: Theme.navigationActionExtent
                        Layout.preferredHeight: Theme.navigationActionExtent
                        icon.source: Theme.icon("subtract-line")
                        icon.color: Theme.iconPrimary
                        Accessible.name: qsTr("最小化")
                        Accessible.role: Accessible.Button
                        onClicked: window.showMinimized()
                        background: Rectangle {
                            color: parent.hovered ? Theme.surfaceHover : "transparent"
                            radius: 3
                        }
                    }
                    ToolButton {
                        objectName: "audioToolsMaximizeButton"
                        focusPolicy: Qt.NoFocus
                        Keys.onSpacePressed: function(event) { event.accepted = true }
                        Layout.preferredWidth: Theme.navigationActionExtent
                        Layout.preferredHeight: Theme.navigationActionExtent
                        icon.source: Theme.icon(window.visibility === Window.Maximized
                                                ? "fullscreen-exit-fill"
                                                : "checkbox-blank-line")
                        icon.color: Theme.iconPrimary
                        Accessible.name: window.visibility === Window.Maximized
                            ? qsTr("还原") : qsTr("最大化")
                        Accessible.role: Accessible.Button
                        onClicked: window.visibility === Window.Maximized
                                   ? window.showNormal() : window.showMaximized()
                        background: Rectangle {
                            color: parent.hovered ? Theme.surfaceHover : "transparent"
                            radius: 3
                        }
                    }
                    ToolButton {
                        objectName: "audioToolsCloseButton"
                        focusPolicy: Qt.NoFocus
                        Keys.onSpacePressed: function(event) { event.accepted = true }
                        Layout.preferredWidth: Theme.navigationActionExtent
                        Layout.preferredHeight: Theme.navigationActionExtent
                        icon.source: Theme.icon("close-fill")
                        icon.color: Theme.iconPrimary
                        Accessible.name: qsTr("关闭")
                        Accessible.role: Accessible.Button
                        onClicked: window.requestHide()
                        background: Rectangle {
                            color: parent.hovered ? Theme.danger : "transparent"
                            radius: 3
                        }
                    }
                }

                MouseArea {
                    objectName: "audioToolsMoveArea"
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    // Leave the complete three-button hit region to the
                    // controls: 3 button extents, 2 RowLayout gaps, and the
                    // title row's trailing margin.
                    anchors.rightMargin: 3 * Theme.navigationActionExtent
                        + 2 * 14 + 8
                    z: 2
                    acceptedButtons: Qt.LeftButton
                    onPressed: function(mouse) {
                        if (window.visibility !== Window.Maximized)
                            window.startSystemMove()
                        mouse.accepted = true
                    }
                }
            }

            ToolSidebar {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.navigationActionExtent
                    + Theme.spacingLg
                window: window
                currentTool: AudioToolsController.currentTool
                referenceWorkbench: window.referenceWorkbench
                separationWorkbench: window.separationWorkbench
                onToolSelected: function(toolId) {
                    AudioToolsController.selectTool(toolId)
                }
            }

            Rectangle {
                objectName: "audioToolsContentStack"
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.panel
                border.color: "transparent"
                border.width: 0
                radius: 0

                StackLayout {
                    anchors.fill: parent
                    currentIndex: window.pageIndexForTool(AudioToolsController.currentTool)

                    Loader {
                        objectName: "audioEditorPageLoader"
                        active: AudioToolsController.currentTool === 0
                        sourceComponent: Component {
                            AudioEditorPage { objectName: "audioEditorPage" }
                        }
                    }
                    Loader {
                        objectName: "vocalSeparationPageLoader"
                        active: AudioToolsController.currentTool === 4
                        sourceComponent: Component {
                            VocalSeparationPage {
                                objectName: "vocalSeparationPage"
                            }
                        }
                    }
                    Loader {
                        objectName: "formatConvertPageLoader"
                        active: AudioToolsController.currentTool === 1
                        sourceComponent: Component {
                            FormatConvertPage { objectName: "formatConvertPage" }
                        }
                    }
                    Loader {
                        objectName: "metadataEditPageLoader"
                        active: AudioToolsController.currentTool === 2
                        sourceComponent: Component { MetadataEditPage {} }
                    }
                    Loader {
                        objectName: "filenameProcessPageLoader"
                        active: AudioToolsController.currentTool === 3
                        sourceComponent: Component {
                            FilenameProcessPage { objectName: "filenameProcessPage" }
                        }
                    }
                }
            }
        }
    }

    WindowResizeHandles {
        objectName: "audioToolsResizeHandles"
        targetWindow: window
    }
}

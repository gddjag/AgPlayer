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
    readonly property bool losslessWorkbench: AudioToolsController.currentTool === 5
    readonly property bool referenceWorkbench:
        metadataWorkbench || separationWorkbench || losslessWorkbench
    property int previousTool: -1

    function pageIndexForTool(toolId) {
        if (toolId === 5) return 5
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
        if (!visible && losslessWorkbench && LosslessAnalysisController.running)
            LosslessAnalysisController.cancel()
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
            const current = AudioToolsController.currentTool
            if (window.previousTool === 5 && current !== 5
                    && LosslessAnalysisController.running)
                LosslessAnalysisController.cancel()
            window.previousTool = current
            if (AudioToolsController.currentTool === 0)
                AudioEditorController.activate()
            else
                AudioEditorController.deactivate()
        }
    }

    Component.onCompleted: previousTool = AudioToolsController.currentTool

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

    ThemedDialog {
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
            width: Math.min(420, window.width - 2 * Theme.spacing2Xl)
            text: qsTr("当前音频尚未保存。关闭窗口将舍弃这些更改。")
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.WordWrap
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.contentSurface
        border.color: Theme.opaqueBorder
        border.width: 1
        radius: window.visibility === Window.Maximized ? 0 : Theme.windowRadius

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                id: titleBar
                objectName: "audioToolsTitleBar"
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.titleBarHeight
                Layout.minimumHeight: Theme.titleBarHeight
                Layout.maximumHeight: Layout.minimumHeight
                color: Theme.titleBarSurface

                Row {
                    visible: Qt.platform.os === "osx"
                    anchors.centerIn: parent
                    spacing: Theme.spacingSm
                    Image {
                        width: Theme.controlHeightCompact
                        height: width
                        source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
                        fillMode: Image.PreserveAspectFit
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("AgPlayer · 音频工具")
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeSection
                        font.weight: Font.Medium
                    }
                }

                RowLayout {
                    z: 1
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingXl
                    anchors.rightMargin: Theme.spacingSm
                    spacing: Theme.spacingMd

                    ThemedMacWindowControls {
                        id: macWindowControls
                        targetWindow: window
                        onCloseRequested: window.requestHide()
                    }

                    Item {
                        objectName: "audioToolsLogo"
                        visible: Qt.platform.os !== "osx"
                        Layout.preferredWidth: Theme.controlHeightCompact
                        Layout.preferredHeight: Theme.controlHeightCompact
                        Image {
                            anchors.centerIn: parent
                            width: Theme.controlHeightCompact
                            height: Theme.controlHeightCompact
                            source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
                            fillMode: Image.PreserveAspectFit
                        }
                    }
                    Text {
                        objectName: "audioToolsWindowTitle"
                        visible: Qt.platform.os !== "osx"
                        text: qsTr("AgPlayer · 音频工具")
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeSection
                        font.weight: Font.Medium
                    }

                    Item { Layout.fillWidth: true }

                    ThemedIconButton {
                        objectName: "audioToolsMinimizeButton"
                        visible: Qt.platform.os !== "osx"
                        focusPolicy: Qt.NoFocus
                        Keys.onSpacePressed: function(event) { event.accepted = true }
                        Layout.preferredWidth: Theme.navigationActionExtent
                        Layout.preferredHeight: Theme.navigationActionExtent
                        iconSize: 14
                        iconSource: Theme.icon("subtract-line")
                        accessibleName: qsTr("最小化")
                        onClicked: window.showMinimized()
                    }
                    ThemedIconButton {
                        objectName: "audioToolsMaximizeButton"
                        visible: Qt.platform.os !== "osx"
                        focusPolicy: Qt.NoFocus
                        Keys.onSpacePressed: function(event) { event.accepted = true }
                        Layout.preferredWidth: Theme.navigationActionExtent
                        Layout.preferredHeight: Theme.navigationActionExtent
                        iconSize: 14
                        iconSource: Theme.icon(window.visibility === Window.Maximized
                                               ? "fullscreen-exit-fill"
                                               : "checkbox-blank-line")
                        accessibleName: window.visibility === Window.Maximized
                                        ? qsTr("还原") : qsTr("最大化")
                        onClicked: window.visibility === Window.Maximized
                                   ? window.showNormal() : window.showMaximized()
                    }
                    ThemedIconButton {
                        objectName: "audioToolsCloseButton"
                        visible: Qt.platform.os !== "osx"
                        focusPolicy: Qt.NoFocus
                        Keys.onSpacePressed: function(event) { event.accepted = true }
                        Layout.preferredWidth: Theme.navigationActionExtent
                        Layout.preferredHeight: Theme.navigationActionExtent
                        iconSize: 14
                        iconSource: Theme.icon("close-fill")
                        accessibleName: qsTr("关闭")
                        dangerOnHover: true
                        onClicked: window.requestHide()
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
                    anchors.leftMargin: Qt.platform.os === "osx"
                        ? Theme.spacingXl + macWindowControls.width : 0
                    anchors.rightMargin: Qt.platform.os === "osx" ? 0
                        : 3 * Theme.navigationActionExtent + 2 * Theme.spacingMd + Theme.spacingSm
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
                Layout.preferredHeight: Theme.settingsRowHeight
                window: window
                currentTool: AudioToolsController.currentTool
                referenceWorkbench: window.referenceWorkbench
                separationWorkbench: window.separationWorkbench
                losslessWorkbench: window.losslessWorkbench
                onToolSelected: function(toolId) {
                    AudioToolsController.selectTool(toolId)
                }
            }

            Rectangle {
                objectName: "audioToolsContentStack"
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.contentSurface
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
                    Loader {
                        objectName: "losslessIdentifyPageLoader"
                        active: window.losslessWorkbench
                        sourceComponent: Component {
                            LosslessIdentifyPage {
                                objectName: "losslessIdentifyPage"
                                onAddPlaylistRequested: AudioToolsController.addCurrentListToLossless()
                                onLocateRequested: function(path) { AudioToolsController.locateLosslessFile(path) }
                            }
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

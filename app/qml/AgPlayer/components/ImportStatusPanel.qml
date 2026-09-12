import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Item {
    id: root

    property var controller: ImportController
    readonly property bool showingProgress: controller && controller.busy
    readonly property bool showingErrors: controller && !controller.busy
                                          && controller.errors.length > 0
    readonly property bool active: controller
                                   && (showingProgress || showingErrors)

    signal retryRequested()

    visible: active

    function dismissErrors() {
        if (controller && typeof controller.clearErrors === "function")
            controller.clearErrors()
    }

    Timer {
        interval: 8000
        running: root.showingErrors
        repeat: false
        onTriggered: root.dismissErrors()
    }

    ColumnLayout {
        id: importProgress
        objectName: "importProgress"
        anchors.centerIn: parent
        spacing: Theme.spacingMd
        visible: root.showingProgress

        Text {
            text: qsTr("Importing...")
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeSection
            font.weight: Font.Medium
            Layout.alignment: Qt.AlignHCenter
        }

        ProgressBar {
            value: root.controller ? root.controller.progress : 0
            Layout.preferredWidth: 280
            Layout.alignment: Qt.AlignHCenter
        }
    }

    ColumnLayout {
        id: importErrors
        objectName: "importErrors"
        anchors.centerIn: parent
        spacing: Theme.spacingSm
        visible: root.showingErrors

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.spacingSm

            Text {
                text: qsTr("Some files could not be imported")
                color: Theme.error
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeBody
                font.weight: Font.Medium
            }

            Button {
                objectName: "dismissImportErrorsButton"
                text: "×"
                flat: true
                implicitWidth: 30
                implicitHeight: 30
                focusPolicy: Qt.StrongFocus
                Accessible.name: qsTr("Dismiss import errors")
                onClicked: root.dismissErrors()

                background: Rectangle {
                    radius: Theme.radiusSm
                    color: parent.hovered ? Theme.hoverSurface : "transparent"
                }
                contentItem: Text {
                    text: parent.text
                    color: Theme.primaryText
                    font.pixelSize: Theme.fontSizePageTitle
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        Repeater {
            model: {
                if (!root.controller)
                    return []
                var list = root.controller.errors
                if (list.length <= 5)
                    return list
                var shown = list.slice(0, 5)
                shown.push(qsTr("... and %1 more").arg(list.length - 5))
                return shown
            }

            Text {
                text: modelData
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeCaption
                elide: Text.ElideRight
                Layout.maximumWidth: 480
            }
        }

        Button {
            text: qsTr("Import audio")
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingMd
            focusPolicy: Qt.StrongFocus
            onClicked: root.retryRequested()

            background: Rectangle {
                color: parent.pressed || parent.hovered
                       ? Theme.cyan : "transparent"
                border.color: Theme.cyan
                border.width: parent.visualFocus ? 2 : 1
                radius: Theme.radiusSm
                implicitHeight: 36
            }

            contentItem: Text {
                text: parent.text
                color: parent.hovered ? Theme.background : Theme.cyan
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeBody
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}

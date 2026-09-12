import QtQuick
import QtQuick.Controls
import AgPlayer

Dialog {
    id: control

    modal: true
    padding: Theme.spacingLg
    font.family: Theme.fontPrimary
    font.pixelSize: Theme.fontSizeBody
    palette.window: Theme.surfaceElevated
    palette.windowText: Theme.textPrimary
    palette.text: Theme.textPrimary
    palette.button: Theme.surfaceElevated
    palette.buttonText: Theme.textPrimary
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText

    // Qt's standard labels use the Qt translation catalog, while AgPlayer's
    // language setting retranslates its own catalog. Keep labels in that catalog.
    readonly property var standardButtonLabels: [
        [Dialog.Ok, qsTr("确定")], [Dialog.Cancel, qsTr("取消")],
        [Dialog.Yes, qsTr("是")], [Dialog.No, qsTr("否")],
        [Dialog.Close, qsTr("关闭")]
    ]
    function updateStandardButtonLabels() {
        for (var i = 0; i < standardButtonLabels.length; ++i) {
            var entry = standardButtonLabels[i]
            var button = standardButton(entry[0])
            if (button)
                button.text = entry[1]
        }
    }
    onStandardButtonLabelsChanged: Qt.callLater(updateStandardButtonLabels)
    onStandardButtonsChanged: Qt.callLater(updateStandardButtonLabels)

    footer: DialogButtonBox {
        alignment: Qt.AlignRight
        visible: count > 0
        standardButtons: control.standardButtons
        spacing: Theme.spacingSm
        padding: Theme.spacingMd
        delegate: ThemedButton {
            compact: true
            implicitWidth: Math.max(64, contentItem.implicitWidth + leftPadding + rightPadding)
            Component.onCompleted: Qt.callLater(control.updateStandardButtonLabels)
        }
        background: Item {}
    }

    Connections {
        target: control
        function onAboutToShow() { control.updateStandardButtonLabels() }
    }

    background: Rectangle {
        color: Theme.surfaceElevated
        radius: Theme.radiusMd
        border.color: Theme.opaqueBorder
        border.width: 1
    }
}

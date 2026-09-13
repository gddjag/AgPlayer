import QtQuick
import QtQuick.Controls
import AgPlayer

Dialog {
    id: control

    modal: true
    padding: Theme.spacingLg
    // Measure plain text independently of the wrapped Label's live geometry.
    readonly property bool hasTextContent: contentItem
        && typeof contentItem.text === "string" && contentItem.font !== undefined
    TextMetrics {
        id: contentTextMetrics
        text: control.hasTextContent ? control.contentItem.text : ""
        font: control.hasTextContent ? control.contentItem.font : control.font
    }
    // Measure natural content, not contentWidth (which follows the assigned
    // width when text wraps). Include titles and actions before bounding it.
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            (hasTextContent ? contentTextMetrics.boundingRect.width
                                            : contentItem ? contentItem.implicitWidth : 0)
                                + leftPadding + rightPadding,
                            dialogTitle.visible ? dialogTitle.implicitWidth : 0,
                            dialogButtons.visible ? dialogButtons.implicitWidth : 0)
    // Apply measured width after text/layout notifications have settled, so
    // changing a visible message cannot re-enter Popup's height calculation.
    property real fittedContentWidth: implicitWidth
    width: fittedContentWidth
    Binding on fittedContentWidth {
        delayed: true
        value: Math.min(control.implicitWidth, control.parent && control.parent.width > 0
                       ? Math.max(0, control.parent.width - 2 * Theme.spacingLg)
                       : control.implicitWidth)
    }
    property real fittedContentHeight: 0
    implicitHeight: fittedContentHeight
    Binding on fittedContentHeight {
        delayed: true
        value: Math.max(control.implicitBackgroundHeight + control.topInset + control.bottomInset,
                        control.contentHeight + control.topPadding + control.bottomPadding
                        + (control.implicitHeaderHeight > 0 ? control.implicitHeaderHeight + control.spacing : 0)
                        + (control.implicitFooterHeight > 0 ? control.implicitFooterHeight + control.spacing : 0))
    }
    font.family: Theme.fontPrimary
    font.pixelSize: Theme.fontSizeBody
    palette.window: Theme.surfaceElevated
    palette.windowText: Theme.textPrimary
    palette.text: Theme.textPrimary
    palette.button: Theme.surfaceElevated
    palette.buttonText: Theme.textPrimary
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText

    header: Label {
        id: dialogTitle
        visible: text.length > 0
        text: control.title
        color: Theme.textPrimary
        font.bold: true
        padding: control.padding
        elide: Text.ElideRight
    }

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
        id: dialogButtons
        implicitWidth: {
            var buttonsWidth = 0
            for (var index = 0; index < count; ++index) {
                var button = itemAt(index)
                if (button) buttonsWidth += button.implicitWidth
            }
            return buttonsWidth + Math.max(0, count - 1) * spacing + leftPadding + rightPadding
        }
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

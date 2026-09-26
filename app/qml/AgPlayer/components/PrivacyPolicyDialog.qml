import QtQuick
import QtQuick.Controls
import AgPlayer

ThemedDialog {
    id: dialog
    objectName: "privacyPolicyDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    title: qsTr("AgPlayer 隐私政策")
    implicitWidth: 640
    height: Math.min(600, Math.max(0, (parent ? parent.height : 648) - 48))
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape
    property var returnFocusItem: null
    onClosed: { if (returnFocusItem) returnFocusItem.forceActiveFocus() }

    function openLink(link) {
        if (/^https?:\/\//i.test(link) || /^mailto:/i.test(link))
            return Qt.openUrlExternally(link)
        return false
    }

    onOpened: {
        policyScroll.contentItem.contentY = 0
        policyBody.cursorPosition = 0
        policyBody.forceActiveFocus()
    }
    contentItem: ScrollView {
        id: policyScroll
        objectName: "privacyPolicyScroll"
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AlwaysOn
        TextArea {
            id: policyBody
            objectName: "privacyPolicyBody"
            width: policyScroll.availableWidth
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            textFormat: TextEdit.RichText
            text: "<style>p { margin-top: 0; margin-bottom: 14px; line-height: 145%; }"
                + "h2 { font-size: 16px; margin-top: 18px; margin-bottom: 10px; }"
                + "a { color: " + Theme.accent + "; text-decoration: underline; }</style>"
                + SettingsController.privacyPolicyText
            color: Theme.primaryText
            palette.link: Theme.accent
            selectionColor: Theme.accent
            selectedTextColor: Theme.accentText
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeBody
            rightPadding: Theme.spacingMd
            background: null
            onLinkActivated: function(link) { dialog.openLink(link) }
        }
    }

    /* Supplied privacy policy is bundled as a shared offline HTML resource. */
    readonly property string policyHtml: SettingsController.privacyPolicyText
}

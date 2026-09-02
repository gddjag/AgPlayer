import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Popup {
    id: root
    objectName: "audioFileInfoPanel"

    property var rows: []
    property var details: ({})
    property string fullPath: ""
    property url coverUrl: ""
    signal copyRequested(string value)

    width: 130
    height: 438
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: {
        if (scrollView.contentItem.contentY !== undefined)
            scrollView.contentItem.contentY = 0
        closeButton.forceActiveFocus()
    }

    background: Rectangle {
        color: Theme.elevated
        border.color: Theme.border
        radius: Theme.radiusMd
    }

    contentItem: ScrollView {
        id: scrollView
        objectName: "audioFileInfoScroll"
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: scrollView.availableWidth
            spacing: 7

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: qsTr("文件信息")
                    color: Theme.primaryText
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                ToolButton {
                    id: closeButton
                    objectName: "audioFileInfoClose"
                    icon.source: Theme.icon("close-fill")
                    focusPolicy: Qt.StrongFocus
                    onClicked: root.close()
                    background: null
                    Keys.onEscapePressed: root.close()
                }
            }

            Repeater {
                model: root.rows

                delegate: ColumnLayout {
                    id: detailRow
                    required property int index
                    readonly property var row: root.rows[index] || ({})
                    width: scrollView.availableWidth
                    Layout.fillWidth: true

                    Text {
                        objectName: "audioFileInfoLabel-" + detailRow.row.key
                        text: detailRow.row.label || ""
                        color: Theme.secondaryText
                        font.pixelSize: Theme.fontSizeCaption
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                    ToolButton {
                        id: copyTarget
                        objectName: "audioFileInfoCopy-" + detailRow.row.key
                        enabled: Boolean(detailRow.row.copyable)
                        Layout.fillWidth: true
                        Layout.preferredHeight: valueText.implicitHeight
                        flat: true
                        padding: 0
                        focusPolicy: Qt.StrongFocus
                        background: null
                        onClicked: root.copyRequested(valueText.text)
                        Keys.onEscapePressed: root.close()

                        contentItem: Text {
                            id: valueText
                            objectName: "audioFileInfoValue-" + detailRow.row.key
                            text: detailRow.row.value || "—"
                            color: Theme.primaryText
                            elide: Text.ElideMiddle
                            font.pixelSize: Theme.fontSizeCaption
                            verticalAlignment: Text.AlignVCenter
                            Accessible.name: detailRow.row.key === "path"
                                             ? root.fullPath : text
                        }
                    }
                }
            }

            Image {
                visible: Boolean(root.coverUrl)
                source: root.coverUrl
                Layout.preferredWidth: 120
                Layout.preferredHeight: 120
                Layout.alignment: Qt.AlignHCenter
                fillMode: Image.PreserveAspectFit
            }
        }
    }
}

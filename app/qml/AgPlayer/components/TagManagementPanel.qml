import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Item {
    id: root
    objectName: "tagManagementPanel"

    property var tagModel: TagModel
    property var filterModel: null
    property alias searchText: tagSearchField.text
    readonly property int gridColumnCount: 3
    readonly property int visibleTagCount: tagGrid.count

    signal addTagRequested(string displayName)
    signal renameTagRequested(string key, string displayName)
    signal removeTagRequested(string key)
    signal changeTagColorRequested(string key, color tagColor)

    function addTag(displayName) {
        if (!tagModel)
            return false
        var created = tagModel.createTag(displayName)
        if (created)
            addTagRequested(displayName.trim())
        return created
    }

    function selectTag(key) {
        if (!tagModel || !filterModel)
            return
        var nextKey = tagModel.selectedKey === key ? "" : key
        tagModel.selectedKey = nextKey
        filterModel.tagKey = nextKey
    }

    TagFilterModel {
        id: filteredTags
        objectName: "tagFilterProxy"
        sourceModel: root.tagModel
        query: root.searchText
    }

    Dialog {
        id: addTagDialog
        title: qsTr("添加标签")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            newTagField.clear()
            newTagField.forceActiveFocus()
        }
        onAccepted: root.addTag(newTagField.text)
        contentItem: TextField {
            id: newTagField
            objectName: "newTagField"
            placeholderText: qsTr("标签名称")
            maximumLength: 96
        }
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.listWorkspaceBorder
            border.width: 1
            radius: Theme.radiusMd
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            spacing: 10

            TextField {
                id: tagSearchField
                objectName: "tagSearchField"
                Layout.fillWidth: true
                Layout.fillHeight: true
                placeholderText: qsTr("搜索标签")
                color: Theme.primaryText
                placeholderTextColor: Theme.tagSecondaryText
                leftPadding: 34
                rightPadding: 10
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                background: Rectangle {
                    color: "transparent"
                    border.color: tagSearchField.activeFocus
                                  ? Theme.accent : Theme.listWorkspaceBorder
                    border.width: 1
                    radius: Theme.radiusSm
                    ThemedIcon {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        source: Theme.icon("search-line")
                        tint: Theme.tagSecondaryText
                        sourceSize.width: 15
                        sourceSize.height: 15
                    }
                }
            }

            Button {
                id: addTagButton
                objectName: "addTagButton"
                Layout.preferredWidth: 102
                Layout.fillHeight: true
                text: qsTr("添加标签")
                icon.source: Theme.icon("add-line")
                icon.color: Theme.primaryText
                icon.width: 16
                icon.height: 16
                onClicked: addTagDialog.open()
                contentItem: RowLayout {
                    spacing: 6
                    ThemedIcon {
                        source: addTagButton.icon.source
                        tint: Theme.primaryText
                        sourceSize.width: 16
                        sourceSize.height: 16
                    }
                    Text {
                        text: addTagButton.text
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                    }
                }
                background: Rectangle {
                    color: addTagButton.down ? Theme.listSelectedSurface
                                             : Theme.tagAddSurface
                    border.color: Theme.listWorkspaceBorder
                    border.width: 1
                    radius: Theme.radiusSm
                }
            }
        }

        GridView {
            id: tagGrid
            objectName: "tagGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            cacheBuffer: 0
            model: filteredTags
            cellWidth: width / 3
            cellHeight: 38
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                id: tagCell
                required property string key
                required property string displayName
                required property int trackCount
                required property color color
                required property bool selected
                width: tagGrid.cellWidth
                height: tagGrid.cellHeight

                Rectangle {
                    id: tagPill
                    objectName: "tagPill-" + tagCell.key
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    height: 28
                    radius: 14
                    color: tagCell.selected ? Theme.listSelectedSurface
                                            : "transparent"
                    border.color: tagCell.color
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 9
                        spacing: 5
                        Text {
                            id: tagName
                            text: tagCell.displayName
                            color: Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: tagCell.trackCount
                            color: Theme.tagSecondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                        }
                    }
                    HoverHandler { id: tagHover }
                    TapHandler { onTapped: root.selectTag(tagCell.key) }
                    ToolTip.visible: tagHover.hovered && tagName.truncated
                    ToolTip.text: tagCell.displayName
                    ToolTip.delay: 350
                }
            }
        }

    }
}

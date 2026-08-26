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
    readonly property int visibleTagCount: tagRepeater.count
    readonly property int pillHorizontalPadding: 4
    readonly property int pillContentSpacing: 2
    readonly property int pillMinimumWidth: 48
    property string contextTagKey: ""
    property string contextTagName: ""
    property color contextTagColor: "transparent"

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

    function openTagMenu(key, displayName, tagColor) {
        contextTagKey = key
        contextTagName = displayName
        contextTagColor = tagColor
        tagMenu.popup()
    }

    function applyTagDrop(ids, displayName) {
        if (!ids || ids.length === 0)
            return false
        return LibraryModel.addTagToTracks(ids, displayName) > 0
    }

    function tagKeyForDisplayName(displayName) {
        if (!tagModel)
            return ""
        var expected = displayName.trim()
        for (var row = 0; row < tagModel.rowCount(); ++row) {
            var modelIndex = tagModel.index(row, 0)
            if (tagModel.data(modelIndex, TagModel.DisplayNameRole)
                    === expected)
                return tagModel.data(modelIndex, TagModel.KeyRole)
        }
        return ""
    }

    function hasTagKey(key) {
        if (!tagModel)
            return false
        for (var row = 0; row < tagModel.rowCount(); ++row) {
            if (tagModel.data(tagModel.index(row, 0), TagModel.KeyRole) === key)
                return true
        }
        return false
    }

    function displayNameForTagKey(key) {
        if (!tagModel)
            return ""
        for (var row = 0; row < tagModel.rowCount(); ++row) {
            var modelIndex = tagModel.index(row, 0)
            if (tagModel.data(modelIndex, TagModel.KeyRole) === key)
                return tagModel.data(modelIndex, TagModel.DisplayNameRole)
        }
        return ""
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

    Menu {
        id: tagMenu
        objectName: "tagContextMenu"
        width: 190
        palette.window: Theme.elevated
        palette.text: Theme.primaryText
        palette.highlight: Theme.activeSelection
        palette.highlightedText: Theme.activeSelectionText
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm
        }
        TagMenuItem {
            objectName: "tagMenuRename"
            text: qsTr("重命名")
            onTriggered: {
                renameTagField.text = root.contextTagName
                renameTagDialog.open()
                renameTagField.forceActiveFocus()
                renameTagField.selectAll()
            }
        }
        TagMenuItem {
            objectName: "tagMenuColor"
            text: qsTr("修改颜色")
            onTriggered: tagColorPicker.openForColor(root.contextTagColor)
        }
        MenuSeparator {}
        TagMenuItem {
            objectName: "tagMenuDelete"
            text: qsTr("删除标签")
            onTriggered: removeTagDialog.open()
        }
    }

    Dialog {
        id: renameTagDialog
        objectName: "renameTagDialog"
        title: qsTr("重命名标签")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            var nextName = renameTagField.text.trim()
            if (!nextName)
                return
            var previousKey = root.contextTagKey
            if (!root.hasTagKey(previousKey))
                return
            var previousName = root.displayNameForTagKey(previousKey)
            var updatesActiveFilter = root.filterModel
                    && root.filterModel.tagKey === previousKey
            root.tagModel.renameTag(previousKey, nextName)
            var nextKey = root.tagKeyForDisplayName(nextName)
            var renamed = nextKey.length > 0
                    && root.displayNameForTagKey(nextKey) === nextName
                    && (nextKey !== previousKey
                        ? !root.hasTagKey(previousKey)
                        : previousName !== nextName)
            if (!renamed)
                return
            if (updatesActiveFilter)
                root.filterModel.tagKey = nextKey
            root.renameTagRequested(previousKey, nextName)
        }
        contentItem: TextField {
            id: renameTagField
            objectName: "renameTagField"
            maximumLength: 96
        }
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
            radius: Theme.radiusMd
        }
    }

    AgColorPicker {
        id: tagColorPicker
        objectName: "tagColorPicker"
        onColorAccepted: function(selectedColor) {
            if (root.tagModel.setTagColor(root.contextTagKey,
                                          selectedColor.toString())) {
                root.changeTagColorRequested(root.contextTagKey,
                                             selectedColor)
            }
        }
    }

    Dialog {
        id: removeTagDialog
        objectName: "removeTagDialog"
        title: qsTr("删除标签")
        modal: true
        width: 430
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            var removedKey = root.contextTagKey
            var existed = root.hasTagKey(removedKey)
            if (existed)
                root.tagModel.removeTag(removedKey)
            if (existed && !root.hasTagKey(removedKey)) {
                if (root.filterModel && root.filterModel.tagKey === removedKey)
                    root.filterModel.tagKey = ""
                root.removeTagRequested(removedKey)
            }
        }
        contentItem: Label {
            objectName: "removeTagWarning"
            width: 390
            text: qsTr("确定删除这个标签？此操作只解除标签关系，不删除歌曲或磁盘文件。")
            color: Theme.primaryText
            wrapMode: Text.Wrap
        }
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
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
            Layout.maximumHeight: 38
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

        Flickable {
            id: tagFlickable
            objectName: "tagFlickable"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: tagFlow.height
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            // Existing QML interaction tests use this tiny adapter to find a
            // delegate by model index. The visible layout remains Flow-based.
            QtObject {
                objectName: "tagGrid"
                function positionViewAtBeginning() {
                    tagFlickable.contentY = 0
                }
                function itemAtIndex(index) {
                    return tagRepeater.itemAt(index)
                }
            }

            Flow {
                id: tagFlow
                objectName: "tagFlow"
                width: tagFlickable.width
                height: childrenRect.height
                spacing: 4

                Repeater {
                    id: tagRepeater
                    model: filteredTags

                    delegate: Item {
                        id: tagCell
                        required property string key
                        required property string displayName
                        required property int trackCount
                        required property color color
                        required property bool selected
                        property real dropLoadPulse: 0
                        implicitWidth: tagPill.implicitWidth
                        implicitHeight: tagPill.implicitHeight
                        width: implicitWidth
                        height: implicitHeight

                        Rectangle {
                            anchors.left: tagPill.left
                            anchors.right: tagPill.right
                            anchors.leftMargin: 4
                            anchors.rightMargin: 4
                            anchors.top: tagPill.bottom
                            anchors.topMargin: 1
                            height: 3
                            radius: 2
                            color: Theme.tagPillShadow
                        }

                        Rectangle {
                            id: tagPill
                            objectName: "tagPill-" + tagCell.key
                            property bool selectedVisual: tagCell.selected
                            property bool hoveredVisual: tagHover.hovered
                            property bool focusedVisual: tagPointer.activeFocus
                            property bool dropVisual: tagDropTarget.containsDrag
                            property color resolvedSurface: dropVisual
                                                           ? Theme.tagPillDropSurface
                                                           : selectedVisual
                                                             ? Theme.tagPillSelectedSurface
                                                             : hoveredVisual || focusedVisual
                                                               ? Theme.tagPillHoverSurface
                                                               : Theme.tagPillSurface
                            implicitWidth: Math.min(tagFlow.width,
                                                    Math.max(root.pillMinimumWidth,
                                                             tagNameMeasure.implicitWidth
                                                             + tagCount.implicitWidth
                                                             + root.pillHorizontalPadding * 2
                                                             + root.pillContentSpacing))
                            implicitHeight: 26
                            width: implicitWidth
                            height: implicitHeight
                            radius: 13
                            clip: true
                            color: resolvedSurface
                            border.color: dropVisual || selectedVisual
                                          || focusedVisual
                                          ? Theme.tagPillHighlightBorder
                                          : tagCell.color.a > 0
                                            ? tagCell.color : Theme.tagPillBorder
                            border.width: 1

                            Text {
                                id: tagNameMeasure
                                visible: false
                                text: tagCell.displayName
                                font.family: Theme.fontPrimary
                                font.pixelSize: 11
                            }
                            Row {
                                anchors.left: parent.left
                                anchors.leftMargin: root.pillHorizontalPadding
                                anchors.right: parent.right
                                anchors.rightMargin: root.pillHorizontalPadding
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: root.pillContentSpacing
                                Text {
                                    id: tagName
                                    width: Math.max(0, Math.min(implicitWidth,
                                                                tagPill.width
                                                                - tagCount.width
                                                                - root.pillHorizontalPadding * 2
                                                                - root.pillContentSpacing))
                                    text: tagCell.displayName
                                    color: Theme.tagPillText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                                Text {
                                    id: tagCount
                                    text: tagCell.trackCount
                                    color: Theme.tagPillSecondaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 10
                                }
                            }
                            HoverHandler { id: tagHover }
                            MouseArea {
                                id: tagPointer
                                objectName: "tagPillPointerArea-" + tagCell.key
                                anchors.fill: parent
                                z: 2
                                activeFocusOnTab: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                Keys.onReturnPressed: root.selectTag(tagCell.key)
                                onClicked: function(mouse) {
                                    forceActiveFocus()
                                    if (mouse.button === Qt.RightButton) {
                                        root.openTagMenu(tagCell.key,
                                                         tagCell.displayName,
                                                         tagCell.color)
                                    } else {
                                        root.selectTag(tagCell.key)
                                    }
                                }
                            }
                            DropArea {
                                id: tagDropTarget
                                objectName: "tagDropTarget-" + tagCell.key
                                property int acceptedAnimationCount: 0
                                anchors.fill: parent
                                keys: ["application/x-agplayer-track-ids"]
                                onDropped: function(drop) {
                                    var encoded = drop.getDataAsString(
                                                "application/x-agplayer-track-ids")
                                    var ids = encoded ? JSON.parse(encoded) : []
                                    if (ids.length === 0 && drop.source
                                            && drop.source.dragTrackIds)
                                        ids = drop.source.dragTrackIds
                                    if (root.applyTagDrop(ids, tagCell.displayName)) {
                                        acceptedAnimationCount += 1
                                        tagLoadAnimation.restart()
                                        drop.acceptProposedAction()
                                    }
                                }
                            }
                            Rectangle {
                                objectName: "tagDropFeedback-" + tagCell.key
                                anchors.fill: parent
                                radius: parent.radius
                                visible: tagDropTarget.containsDrag
                                         || tagCell.dropLoadPulse > 0
                                color: "transparent"
                                border.color: Theme.tagPillHighlightBorder
                                border.width: 2
                                opacity: tagDropTarget.containsDrag
                                         ? 1 : tagCell.dropLoadPulse
                                scale: 1 - tagCell.dropLoadPulse * 0.06
                                z: 1
                            }
                            ToolTip.visible: tagHover.hovered && tagName.truncated
                            ToolTip.text: tagCell.displayName
                            ToolTip.delay: 350
                        }
                        SequentialAnimation {
                            id: tagLoadAnimation
                            NumberAnimation {
                                target: tagCell
                                property: "dropLoadPulse"
                                from: 0
                                to: 1
                                duration: 90
                                easing.type: Easing.OutCubic
                            }
                            NumberAnimation {
                                target: tagCell
                                property: "dropLoadPulse"
                                from: 1
                                to: 0
                                duration: 150
                                easing.type: Easing.InCubic
                            }
                        }
                    }
                }
            }
        }

    }

    component TagMenuItem: MenuItem {
        id: menuItem
        width: 185
        implicitWidth: 185
        implicitHeight: 34
        contentItem: Text {
            text: menuItem.text
            color: menuItem.highlighted || menuItem.hovered
                   ? Theme.activeSelectionText
                   : menuItem.enabled ? Theme.primaryText
                                      : Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Math.max(13, Qt.application.font.pixelSize)
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: menuItem.highlighted || menuItem.hovered
                   ? Theme.activeSelection : "transparent"
            radius: Theme.radiusSm
        }
    }
}

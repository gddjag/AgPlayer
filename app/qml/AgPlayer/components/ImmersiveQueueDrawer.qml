import QtQuick
import QtQuick.Controls
import AgPlayer

Item {
    id: root
    objectName: "immersiveQueueDrawer"
    property var playback: PlaybackController
    property var library: LibraryModel
    property bool opened: false
    property bool dragSuppressed: false
    property bool drawerHovered: drawerHover.hovered
    property bool triggerHovered: triggerHover.hovered
    property int hoveredIndex: -1

    function requestOpen() {
        queueHideTimer.stop()
        if (!dragSuppressed)
            queueOpenTimer.restart()
    }

    function requestClose() {
        queueOpenTimer.stop()
        queueHideTimer.restart()
    }

    function durationText(milliseconds) {
        var seconds = Math.max(0, Math.floor(Number(milliseconds || 0) / 1000))
        return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0")
    }

    Timer {
        id: queueOpenTimer
        objectName: "queueOpenTimer"
        interval: 140
        onTriggered: if (!root.dragSuppressed) root.opened = true
    }

    Timer {
        id: queueHideTimer
        objectName: "queueHideTimer"
        interval: 2000
        onTriggered: {
            if (!root.dragSuppressed && !root.drawerHovered
                    && !root.triggerHovered && !queueList.moving
                    && !queueList.dragging && !queueList.activeFocus)
                root.opened = false
        }
    }

    Item {
        id: trigger
        objectName: "queueTriggerZone"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 20
        z: 3
        HoverHandler {
            id: triggerHover
            onHoveredChanged: hovered ? root.requestOpen() : root.requestClose()
        }
    }

    Rectangle {
        id: drawer
        objectName: "queueDrawerSurface"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: Math.max(52, parent.height * 0.08)
        anchors.bottomMargin: Math.max(72, parent.height * 0.10)
        width: Math.min(326, Math.max(270, parent.width * 0.20))
        x: root.opened ? root.width - width - 14 : root.width + 8
        radius: Theme.radiusLg
        color: Theme.glassSurfaceElevated
        border.width: 1
        border.color: Theme.glassBorder
        clip: true
        z: 4

        Behavior on x {
            NumberAnimation { duration: 260; easing.type: Easing.OutCubic }
        }

        HoverHandler {
            id: drawerHover
            onHoveredChanged: {
                if (hovered) {
                    queueHideTimer.stop()
                    queueOpenTimer.stop()
                } else {
                    root.hoveredIndex = -1
                    root.requestClose()
                }
            }
        }

        Text {
            id: drawerTitle
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacingLg
            text: qsTr("当前播放队列")
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeBody
            font.weight: Font.DemiBold
        }

        ListView {
            id: queueList
            objectName: "immersiveQueueList"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: drawerTitle.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: Theme.spacingSm
            anchors.bottomMargin: Theme.spacingMd
            anchors.leftMargin: Theme.spacingMd
            anchors.rightMargin: Theme.spacingMd
            clip: true
            spacing: 2
            model: root.playback ? root.playback.queueTrackIds : []
            cacheBuffer: 180
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ThemedScrollBar { id: queueScrollBar }
            onMovementStarted: queueHideTimer.stop()
            onMovementEnded: if (!drawerHover.hovered) root.requestClose()

            delegate: Item {
                id: row
                required property int index
                required property string modelData
                property string trackId: modelData
                property var details: root.library
                                      ? root.library.trackForId(trackId) : ({})
                property bool hoveredForQa: rowMouse.containsMouse
                width: Math.max(0, ListView.view.width - queueScrollBar.width)
                height: 56
                transformOrigin: Item.Center
                scale: hoveredForQa ? 1.12
                       : Math.abs(root.hoveredIndex - index) === 1 ? 1.03 : 1.0
                z: hoveredForQa ? 2 : 1

                function activateForQa() {
                    if (root.playback)
                        root.playback.playTrackIds(root.playback.queueTrackIds,
                                                   trackId)
                }

                Behavior on scale {
                    NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 5
                    anchors.rightMargin: 5
                    radius: Theme.radiusSm
                    color: row.hoveredForQa ? Theme.subtleGlassHover
                                             : Theme.subtleGlassFill
                    border.width: row.trackId === (root.playback
                                                   ? root.playback.currentTrackId : "") ? 1 : 0
                    border.color: Theme.iconAccent
                }

                Rectangle {
                    id: cover
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 38
                    height: 38
                    radius: Theme.radiusSm
                    color: Theme.panel
                    clip: true
                    Image {
                        objectName: "queueCoverImage"
                        anchors.fill: parent
                        asynchronous: true
                        sourceSize: Qt.size(64, 64)
                        source: row.details && row.details.coverUrl
                                ? row.details.coverUrl
                                : "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
                        fillMode: Image.PreserveAspectCrop
                        smooth: true
                    }
                }

                Text {
                    anchors.left: cover.right
                    anchors.leftMargin: 10
                    anchors.right: duration.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.details && row.details.title
                          ? row.details.title : row.trackId
                    color: row.trackId === (root.playback
                                            ? root.playback.currentTrackId : "")
                           ? Theme.iconAccent : Theme.primaryText
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontPrimary
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }

                Text {
                    id: duration
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.durationText(row.details
                                            ? row.details.durationMs : 0)
                    color: Theme.secondaryText
                    font.pixelSize: Theme.fontSizeCaption
                    font.family: Theme.fontPrimary
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: root.hoveredIndex = row.index
                    onExited: if (root.hoveredIndex === row.index)
                                  root.hoveredIndex = -1
                    onClicked: queueList.currentIndex = row.index
                    onDoubleClicked: row.activateForQa()
                }
            }
        }
    }
}

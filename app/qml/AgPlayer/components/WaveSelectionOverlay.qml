import QtQuick
import AgPlayer

Item {
    id: root
    objectName: "waveSelectionOverlay"
    clip: true

    // Selection is deliberately time-based.  Geometry is derived from the
    // visible waveform range on every resize/zoom/DPI change.
    property real durationMs: 0
    property real visibleStartMs: 0
    property real visibleEndMs: durationMs
    property int minSelectionMs: 100
    property int selectionStartMs: 0
    property int selectionEndMs: 0
    property bool selectionActive: false
    property int hoverPositionMs: -1
    property var dragAdapter: null
    property string currentTrackId: ""
    readonly property int selectionDurationMs: selectionActive
                                           ? Math.max(0, selectionEndMs
                                                      - selectionStartMs) : 0
    readonly property real selectionX: timeToX(selectionStartMs)
    readonly property real selectionWidth: selectionActive
                                           ? Math.max(0, timeToX(selectionEndMs)
                                                      - selectionX) : 0
    readonly property string selectionDurationText:
        (selectionDurationMs / 1000).toFixed(2) + "s"

    signal selectionChanged(int startMs, int endMs)
    signal selectionCommitted(int startMs, int endMs)
    signal selectionAdjusted(int startMs, int endMs)
    signal selectionLoopRequested(int startMs, int endMs)
    signal selectionClearRequested()
    signal seekRequested(int positionMs)
    signal zoomRequested(real x, real factor)
    // The host owns actual extraction and native QDrag.  This UI signal is
    // intentionally the only work performed before a genuine drag begins.
    signal dragClipRequested(int startMs, int endMs)

    property int _pressTimeMs: 0
    property bool _selecting: false
    property bool _moved: false

    function rangeStart() {
        return Math.max(0, Math.min(durationMs, visibleStartMs))
    }

    function rangeEnd() {
        var end = visibleEndMs > visibleStartMs ? visibleEndMs : durationMs
        return Math.max(rangeStart(), Math.min(durationMs, end))
    }

    function clampTime(timeMs) {
        return Math.round(Math.max(rangeStart(), Math.min(rangeEnd(), timeMs)))
    }

    function timeToX(timeMs) {
        var start = rangeStart()
        var end = rangeEnd()
        if (width <= 0 || end <= start)
            return 0
        return Math.max(0, Math.min(width, (clampTime(timeMs) - start)
                                    / (end - start) * width))
    }

    function timeForX(x) {
        var start = rangeStart()
        var end = rangeEnd()
        if (width <= 0 || end <= start)
            return Math.round(start)
        var fraction = Math.max(0, Math.min(1, x / width))
        return Math.round(start + fraction * (end - start))
    }

    function setSelection(firstMs, secondMs) {
        if (durationMs <= 0)
            return
        var first = clampTime(firstMs)
        var second = clampTime(secondMs)
        var start = Math.min(first, second)
        var end = Math.max(first, second)
        if (end - start < minSelectionMs) {
            if (start + minSelectionMs <= rangeEnd())
                end = start + minSelectionMs
            else
                start = Math.max(rangeStart(), end - minSelectionMs)
        }
        selectionStartMs = Math.round(start)
        selectionEndMs = Math.round(end)
        selectionActive = selectionEndMs > selectionStartMs
        if (selectionActive)
            selectionChanged(selectionStartMs, selectionEndMs)
    }

    function clearSelection() {
        selectionActive = false
        selectionStartMs = 0
        selectionEndMs = 0
    }

    function commitSelection() {
        if (!selectionActive)
            return
        selectionCommitted(selectionStartMs, selectionEndMs)
        selectionLoopRequested(selectionStartMs, selectionEndMs)
    }

    function requestDragClip() {
        if (selectionActive)
            dragClipRequested(selectionStartMs, selectionEndMs)
    }

    function forwardZoom(wheel, source) {
        if ((wheel.modifiers & Qt.ControlModifier) === 0) {
            wheel.accepted = false
            return
        }
        var point = source.mapToItem(root, wheel.x, wheel.y)
        var factor = wheel.angleDelta.y > 0 ? 1.1 : 1 / 1.1
        root.zoomRequested(point.x, factor)
        wheel.accepted = true
    }

    Rectangle {
        id: selectionRect
        objectName: "waveSelectionRegion"
        visible: root.selectionActive
        x: root.selectionX
        width: root.selectionWidth
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: Theme.highlightSoft
        border.color: Theme.highlightBorder
        border.width: 1
        radius: 3

        Rectangle {
            id: durationBadge
            objectName: "waveSelectionDuration"
            anchors.top: parent.top
            anchors.topMargin: 6
            anchors.right: parent.right
            anchors.rightMargin: 6
            height: 22
            width: durationLabel.implicitWidth + 12
            radius: 5
            color: Theme.selectionGlassFill
            border.color: Theme.selectionGlassBorder
            border.width: 1

            Text {
                id: durationLabel
                objectName: "waveSelectionDurationLabel"
                anchors.centerIn: parent
                text: root.selectionDurationText
                color: Theme.onBrandGradientText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeCaption
                font.weight: Font.DemiBold
            }
        }

        Rectangle {
            id: dragClipButton
            objectName: "waveSelectionDragClipButton"
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            height: 26
            width: dragClipLabel.implicitWidth + 16
            color: dragClipPointer.pressed
                   ? Theme.selectionGlassPressed : Theme.selectionGlassHover
            border.color: Theme.selectionGlassBorder
            border.width: 1
            radius: 5

            Text {
                id: dragClipLabel
                objectName: "waveSelectionDragClipLabel"
                anchors.centerIn: parent
                text: qsTr("拖出片段")
                color: Theme.onBrandGradientText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeCaption
            }

            MouseArea {
                id: dragClipPointer
                anchors.fill: parent
                cursorShape: Qt.DragCopyCursor
                onWheel: function(wheel) {
                    root.forwardZoom(wheel, dragClipPointer)
                }
                onPressed: function(mouse) {
                    var point = mapToItem(null, mouse.x, mouse.y)
                    if (!root.dragAdapter
                            || !root.dragAdapter.begin(
                                point.x, point.y, root.currentTrackId,
                                root.selectionStartMs, root.selectionEndMs)) {
                        mouse.accepted = false
                        return
                    }
                    root.requestDragClip()
                }
                onPositionChanged: function(mouse) {
                    if (!pressed || !root.dragAdapter)
                        return
                    var point = mapToItem(null, mouse.x, mouse.y)
                    root.dragAdapter.update(point.x, point.y)
                }
                onReleased: if (root.dragAdapter) root.dragAdapter.cancel()
                onCanceled: if (root.dragAdapter) root.dragAdapter.cancel()
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            hoverEnabled: true
            onPositionChanged: function(mouse) {
                root.hoverPositionMs = root.timeForX(
                            selectionRect.x + mouse.x)
            }
            onEntered: root.hoverPositionMs = root.timeForX(
                           selectionRect.x + mouseX)
            onExited: root.hoverPositionMs = -1
            onClicked: function(mouse) {
                if (mouse.button !== Qt.RightButton)
                    return
                root.clearSelection()
                root.selectionClearRequested()
            }
        }
    }

    Item {
        id: leftHandle
        objectName: "waveSelectionLeftHandle"
        visible: root.selectionActive
        x: root.selectionX - width / 2
        width: 14
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        Rectangle {
            objectName: "waveSelectionLeftHandleVisual"
            anchors.centerIn: parent
            width: 2
            height: parent.height
            color: Theme.highlight
            radius: 2
        }

        MouseArea {
            id: leftHandlePointer
            anchors.fill: parent
            cursorShape: Qt.SizeHorCursor
            onWheel: function(wheel) {
                root.forwardZoom(wheel, leftHandlePointer)
            }
            onPositionChanged: function(mouse) {
                root.setSelection(root.timeForX(parent.x + mouse.x),
                                                          root.selectionEndMs)
            }
            onReleased: root.selectionAdjusted(root.selectionStartMs,
                                                root.selectionEndMs)
        }
    }

    Item {
        id: rightHandle
        objectName: "waveSelectionRightHandle"
        visible: root.selectionActive
        x: root.selectionX + root.selectionWidth - width / 2
        width: 14
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        Rectangle {
            objectName: "waveSelectionRightHandleVisual"
            anchors.centerIn: parent
            width: 2
            height: parent.height
            color: Theme.highlight
            radius: 2
        }

        MouseArea {
            id: rightHandlePointer
            anchors.fill: parent
            cursorShape: Qt.SizeHorCursor
            onWheel: function(wheel) {
                root.forwardZoom(wheel, rightHandlePointer)
            }
            onPositionChanged: function(mouse) {
                root.setSelection(root.selectionStartMs,
                                  root.timeForX(parent.x + mouse.x))
            }
            onReleased: root.selectionAdjusted(root.selectionStartMs,
                                                root.selectionEndMs)
        }
    }

    MouseArea {
        id: selectionPointer
        objectName: "waveSelectionPointer"
        anchors.fill: parent
        z: -1
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        onWheel: function(wheel) {
            root.forwardZoom(wheel, selectionPointer)
        }
        onPressed: function(mouse) {
            root.hoverPositionMs = root.timeForX(mouse.x)
            root._pressTimeMs = root.timeForX(mouse.x)
            root._selecting = true
            root._moved = false
        }
        onPositionChanged: function(mouse) {
            root.hoverPositionMs = root.timeForX(mouse.x)
            if (!root._selecting)
                return
            root._moved = root._moved || Math.abs(mouse.x
                                                   - root.timeToX(root._pressTimeMs)) > 2
            if (root._moved)
                root.setSelection(root._pressTimeMs, root.timeForX(mouse.x))
        }
        onReleased: function(mouse) {
            if (root._moved) {
                root.setSelection(root._pressTimeMs, root.timeForX(mouse.x))
                root.commitSelection()
            } else {
                root.seekRequested(root.timeForX(mouse.x))
            }
            root._selecting = false
        }
        onCanceled: root._selecting = false
        onEntered: root.hoverPositionMs = root.timeForX(mouseX)
        onExited: root.hoverPositionMs = -1
    }
}

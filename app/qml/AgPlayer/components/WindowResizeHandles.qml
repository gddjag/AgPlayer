import QtQuick

Item {
    id: root
    required property Window targetWindow
    property int handleSize: 7
    anchors.fill: parent
    z: 10000

    function beginResize(edges) {
        targetWindow.startSystemResize(edges)
    }

    MouseArea {
        width: root.handleSize
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        cursorShape: Qt.SizeHorCursor
        onPressed: root.beginResize(Qt.LeftEdge)
    }
    MouseArea {
        width: root.handleSize
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
        cursorShape: Qt.SizeHorCursor
        onPressed: root.beginResize(Qt.RightEdge)
    }
    MouseArea {
        height: root.handleSize
        anchors { left: parent.left; right: parent.right; top: parent.top }
        cursorShape: Qt.SizeVerCursor
        onPressed: root.beginResize(Qt.TopEdge)
    }
    MouseArea {
        height: root.handleSize
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        cursorShape: Qt.SizeVerCursor
        onPressed: root.beginResize(Qt.BottomEdge)
    }
    MouseArea {
        width: root.handleSize * 2
        height: root.handleSize * 2
        anchors { left: parent.left; top: parent.top }
        cursorShape: Qt.SizeFDiagCursor
        onPressed: root.beginResize(Qt.LeftEdge | Qt.TopEdge)
    }
    MouseArea {
        width: root.handleSize * 2
        height: root.handleSize * 2
        anchors { right: parent.right; top: parent.top }
        cursorShape: Qt.SizeBDiagCursor
        onPressed: root.beginResize(Qt.RightEdge | Qt.TopEdge)
    }
    MouseArea {
        width: root.handleSize * 2
        height: root.handleSize * 2
        anchors { left: parent.left; bottom: parent.bottom }
        cursorShape: Qt.SizeBDiagCursor
        onPressed: root.beginResize(Qt.LeftEdge | Qt.BottomEdge)
    }
    MouseArea {
        width: root.handleSize * 2
        height: root.handleSize * 2
        anchors { right: parent.right; bottom: parent.bottom }
        cursorShape: Qt.SizeFDiagCursor
        onPressed: root.beginResize(Qt.RightEdge | Qt.BottomEdge)
    }
}

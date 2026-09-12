import QtQuick
import AgPlayer

Rectangle {
    id: root

    property string dockEdge: "none"
    property string windowRole: "main"
    property bool maximized: false
    property bool showFill: true
    property bool showBorders: true
    property real windowRadius: Theme.windowRadius

    readonly property string contactEdge: dockEdge === "none" ? "none"
                                          : windowRole === "main" ? dockEdge
                                          : dockEdge === "top" ? "bottom"
                                          : dockEdge === "bottom" ? "top"
                                          : dockEdge === "left" ? "right"
                                          : "left"
    readonly property bool topBorderVisible: contactEdge !== "top"
    readonly property bool bottomBorderVisible: contactEdge !== "bottom"
    readonly property bool leftBorderVisible: contactEdge !== "left"
    readonly property bool rightBorderVisible: contactEdge !== "right"

    color: !showFill ? "transparent" : Theme.background
    border.width: showBorders ? 1 : 0
    border.color: Theme.border
    clip: showFill
    topLeftRadius: maximized || contactEdge === "top"
                   || contactEdge === "left" ? 0 : windowRadius
    topRightRadius: maximized || contactEdge === "top"
                    || contactEdge === "right" ? 0 : windowRadius
    bottomLeftRadius: maximized || contactEdge === "bottom"
                      || contactEdge === "left" ? 0 : windowRadius
    bottomRightRadius: maximized || contactEdge === "bottom"
                       || contactEdge === "right" ? 0 : windowRadius

    // Rectangle's own border follows its rounded outline.  Cover only the
    // shared edge after docking; straight child borders would repaint the
    // otherwise transparent corner pixels.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: root.color
        visible: root.showBorders && !root.topBorderVisible
        z: 1000
    }
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: root.color
        visible: root.showBorders && !root.bottomBorderVisible
        z: 1000
    }
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: root.color
        visible: root.showBorders && !root.leftBorderVisible
        z: 1000
    }
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: root.color
        visible: root.showBorders && !root.rightBorderVisible
        z: 1000
    }
}

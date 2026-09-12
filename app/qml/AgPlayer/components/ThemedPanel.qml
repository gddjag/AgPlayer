import QtQuick
import AgPlayer

Rectangle {
    property bool elevated: false
    property bool outlined: false

    color: elevated ? Theme.surfaceElevated : Theme.surface
    radius: Theme.radiusMd
    border.color: outlined ? Theme.opaqueBorder : "transparent"
    border.width: outlined ? 1 : 0
}

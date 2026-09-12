import QtQuick
import QtQuick.Controls.impl
import AgPlayer

IconImage {
    id: root

    property color tint: Theme.iconSecondary

    color: tint
    fillMode: Image.PreserveAspectFit
}

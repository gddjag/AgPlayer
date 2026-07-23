import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: "transparent"

    property string searchText: ""
    property int minRating: 0
    property double minBpm: 0.0
    property double maxBpm: 300.0

    readonly property bool expanded: hoverArea.containsMouse || collapseTimer.running

    implicitWidth: expanded ? 260 : 44
    implicitHeight: expanded ? 220 : 44

    Behavior on implicitWidth {
        NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
    }
    Behavior on implicitHeight {
        NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
    }

    Timer {
        id: collapseTimer
        interval: 500
        onTriggered: {
            if (!hoverArea.containsMouse) {
                root.collapse()
            }
        }
    }

    function collapse() {
        searchField.focus = false
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        onEntered: collapseTimer.stop()
        onExited: collapseTimer.restart()
    }

    Rectangle {
        id: panel
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: root.expanded ? 260 : 44
        height: root.expanded ? 220 : 44
        color: Theme.panel
        border.color: Theme.border
        border.width: 1
        radius: Theme.radiusMd

        Behavior on width {
            NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
        }
        Behavior on height {
            NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
        }

        // Collapsed trigger
        Item {
            anchors.fill: parent
            visible: !root.expanded

            Image {
                anchors.centerIn: parent
                source: Theme.icon("equalizer-fill")
                sourceSize.width: 20
                sourceSize.height: 20
                width: 20
                height: 20
                fillMode: Image.PreserveAspectFit
            }
        }

        // Expanded content
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacingMd
            spacing: Theme.spacingMd
            visible: root.expanded

            // Search row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                TextField {
                    id: searchField
                    Layout.fillWidth: true
                    placeholderText: qsTr("歌名/艺术家/专辑 关键词")
                    text: root.searchText
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    background: Rectangle {
                        color: Theme.background
                        border.color: Theme.border
                        border.width: 1
                        radius: Theme.radiusSm
                    }
                    onTextChanged: root.searchText = text
                }

                Button {
                    text: qsTr("清空")
                    onClicked: {
                        root.searchText = ""
                        searchField.clear()
                    }

                    background: Rectangle {
                        color: parent.pressed ? Theme.cyan
                              : parent.hovered ? Theme.border
                              : "transparent"
                        radius: Theme.radiusSm
                    }

                    contentItem: Text {
                        text: parent.text
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // Rating filter
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Text {
                    text: qsTr("1-5 评分")
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                }

                RowLayout {
                    spacing: 2
                    Repeater {
                        model: 5
                        delegate: Image {
                            source: index < root.minRating
                                    ? Theme.icon("star-fill")
                                    : Theme.icon("star-line")
                            sourceSize.width: 16
                            sourceSize.height: 16
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            fillMode: Image.PreserveAspectFit

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    var next = index + 1
                                    root.minRating = (root.minRating === next) ? 0 : next
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }

            // Min BPM
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Text {
                    text: qsTr("最低 BPM")
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    Layout.preferredWidth: 56
                }

                TextField {
                    Layout.preferredWidth: 48
                    text: Math.round(root.minBpm)
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    background: Rectangle {
                        color: Theme.background
                        border.color: Theme.border
                        border.width: 1
                        radius: Theme.radiusSm
                    }
                    onEditingFinished: {
                        var value = parseInt(text, 10)
                        if (!isNaN(value)) {
                            root.minBpm = Math.max(0, Math.min(value, root.maxBpm))
                        }
                    }
                }

                Slider {
                    id: minBpmSlider
                    Layout.fillWidth: true
                    from: 0
                    to: 300
                    stepSize: 1
                    value: root.minBpm
                    onMoved: root.minBpm = Math.min(value, root.maxBpm)
                }
            }

            // Max BPM
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Text {
                    text: qsTr("最高 BPM")
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    Layout.preferredWidth: 56
                }

                TextField {
                    Layout.preferredWidth: 48
                    text: Math.round(root.maxBpm)
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    background: Rectangle {
                        color: Theme.background
                        border.color: Theme.border
                        border.width: 1
                        radius: Theme.radiusSm
                    }
                    onEditingFinished: {
                        var value = parseInt(text, 10)
                        if (!isNaN(value)) {
                            root.maxBpm = Math.max(root.minBpm, Math.min(value, 300))
                        }
                    }
                }

                Slider {
                    id: maxBpmSlider
                    Layout.fillWidth: true
                    from: 0
                    to: 300
                    stepSize: 1
                    value: root.maxBpm
                    onMoved: root.maxBpm = Math.max(value, root.minBpm)
                }
            }
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: Theme.panel
    border.color: Theme.border
    border.width: 1
    radius: Theme.radiusMd
    implicitHeight: 54

    property string searchText: ""
    property int minRating: 0
    property double minBpm: 60
    property double maxBpm: 160
    property double pendingMinBpm: minBpm
    property double pendingMaxBpm: maxBpm

    Timer {
        id: bpmDebounce
        interval: 200
        onTriggered: {
            root.minBpm = root.pendingMinBpm
            root.maxBpm = root.pendingMaxBpm
        }
    }

    function focusSearch() {
        searchField.forceActiveFocus()
        searchField.selectAll()
    }

    function clearFilters() {
        bpmDebounce.stop()
        searchField.clear()
        searchText = ""
        minRating = 0
        pendingMinBpm = 60
        pendingMaxBpm = 160
        minBpm = 60
        maxBpm = 160
    }

    onMinBpmChanged: if (!bpmDebounce.running) pendingMinBpm = minBpm
    onMaxBpmChanged: if (!bpmDebounce.running) pendingMaxBpm = maxBpm

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingSm
        spacing: Theme.spacingSm

        TextField {
            id: searchField
            objectName: "librarySearchField"
            Layout.preferredWidth: 210
            Layout.fillHeight: true
            placeholderText: qsTr("搜索歌曲、歌手或专辑")
            text: root.searchText
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 12
            onTextChanged: root.searchText = text
            background: Rectangle {
                color: Theme.background
                border.color: parent.activeFocus ? Theme.cyan : Theme.border
                border.width: 1
                radius: Theme.radiusSm
            }
        }

        Label {
            text: qsTr("评分")
            color: Theme.secondaryText
            font.pixelSize: 12
        }

        RowLayout {
            spacing: 1
            Repeater {
                model: 5
                delegate: Image {
                    required property int index
                    source: index < root.minRating
                            ? Theme.icon("star-fill") : Theme.icon("star-line")
                    sourceSize.width: 16
                    sourceSize.height: 16
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    fillMode: Image.PreserveAspectFit
                    TapHandler {
                        onTapped: {
                            var next = index + 1
                            root.minRating = root.minRating === next ? 0 : next
                        }
                    }
                }
            }
        }

        Label {
            text: "BPM"
            color: Theme.secondaryText
            font.pixelSize: 12
        }

        TextField {
            Layout.preferredWidth: 42
            text: Math.round(root.pendingMinBpm)
            color: Theme.primaryText
            horizontalAlignment: Text.AlignHCenter
            validator: IntValidator { bottom: 0; top: 300 }
            background: Rectangle {
                color: Theme.background
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm
            }
            onEditingFinished: {
                var parsed = parseInt(text, 10)
                if (!isNaN(parsed)) {
                    root.pendingMinBpm = Math.max(
                        0, Math.min(parsed, root.pendingMaxBpm))
                    bpmDebounce.restart()
                }
            }
        }

        RangeSlider {
            id: bpmRange
            Layout.fillWidth: true
            Layout.minimumWidth: 140
            from: 0
            to: 300
            stepSize: 1
            first.value: root.pendingMinBpm
            second.value: root.pendingMaxBpm
            first.onMoved: {
                root.pendingMinBpm = Math.min(first.value, root.pendingMaxBpm)
                bpmDebounce.restart()
            }
            second.onMoved: {
                root.pendingMaxBpm = Math.max(second.value, root.pendingMinBpm)
                bpmDebounce.restart()
            }
            background: Rectangle {
                x: bpmRange.leftPadding
                y: bpmRange.topPadding + bpmRange.availableHeight / 2 - height / 2
                width: bpmRange.availableWidth
                height: 4
                radius: 2
                color: Theme.border

                Rectangle {
                    x: bpmRange.first.visualPosition * parent.width
                    width: (bpmRange.second.visualPosition
                            - bpmRange.first.visualPosition) * parent.width
                    height: parent.height
                    radius: parent.radius
                    color: Theme.cyan
                }
            }
            first.handle: Rectangle {
                x: bpmRange.leftPadding + bpmRange.first.visualPosition
                   * (bpmRange.availableWidth - width)
                y: bpmRange.topPadding + bpmRange.availableHeight / 2 - height / 2
                width: 14
                height: 14
                radius: 7
                color: Theme.primaryText
                border.color: Theme.cyan
            }
            second.handle: Rectangle {
                x: bpmRange.leftPadding + bpmRange.second.visualPosition
                   * (bpmRange.availableWidth - width)
                y: bpmRange.topPadding + bpmRange.availableHeight / 2 - height / 2
                width: 14
                height: 14
                radius: 7
                color: Theme.primaryText
                border.color: Theme.cyan
            }
        }

        TextField {
            Layout.preferredWidth: 42
            text: Math.round(root.pendingMaxBpm)
            color: Theme.primaryText
            horizontalAlignment: Text.AlignHCenter
            validator: IntValidator { bottom: 0; top: 300 }
            background: Rectangle {
                color: Theme.background
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm
            }
            onEditingFinished: {
                var parsed = parseInt(text, 10)
                if (!isNaN(parsed)) {
                    root.pendingMaxBpm = Math.max(
                        root.pendingMinBpm, Math.min(parsed, 300))
                    bpmDebounce.restart()
                }
            }
        }

        Button {
            text: qsTr("清空")
            onClicked: root.clearFilters()
            palette.buttonText: Theme.primaryText
            background: Rectangle {
                color: parent.pressed ? Theme.cyan
                      : parent.hovered ? Theme.border : Theme.panel
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm
            }
        }
    }
}

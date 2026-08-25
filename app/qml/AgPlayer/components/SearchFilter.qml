import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: "transparent"
    border.width: 0
    implicitHeight: 54

    property string searchText: ""
    property int exactRating: 0
    property double minBpm: 60
    property double maxBpm: 160
    property double pendingMinBpm: minBpm
    property double pendingMaxBpm: maxBpm
    readonly property color moduleColor: Theme.isLight ? Theme.panel : Theme.elevated
    readonly property color moduleBorder: Theme.border

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
        exactRating = 0
        pendingMinBpm = 60
        pendingMaxBpm = 160
        minBpm = 60
        maxBpm = 160
    }

    onMinBpmChanged: if (!bpmDebounce.running) pendingMinBpm = minBpm
    onMaxBpmChanged: if (!bpmDebounce.running) pendingMaxBpm = maxBpm
    onPendingMinBpmChanged: if (!minimumBpmField.activeFocus)
                                minimumBpmField.text = Math.round(pendingMinBpm).toString()
    onPendingMaxBpmChanged: if (!maximumBpmField.activeFocus)
                                maximumBpmField.text = Math.round(pendingMaxBpm).toString()

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingSm
        spacing: 14

        Rectangle {
            objectName: "keywordModule"
            Layout.preferredWidth: 184
            Layout.fillHeight: true
            color: root.moduleColor
            border.color: root.moduleBorder
            border.width: 1
            radius: Theme.radiusSm
            ThemedIcon {
                id: searchIcon
                objectName: "librarySearchIcon"
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                source: Theme.icon("search-line")
                tint: Theme.secondaryText
                opacity: 0.55
            }
            TextField {
                id: searchField
                objectName: "librarySearchField"
                anchors.fill: parent
                placeholderText: qsTr("歌曲 · 艺术家 · 专辑 · 标签")
                text: root.searchText
                color: Theme.primaryText
                placeholderTextColor: Qt.rgba(Theme.secondaryText.r,
                                               Theme.secondaryText.g,
                                               Theme.secondaryText.b, 0.55)
                leftPadding: 34
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                onTextChanged: root.searchText = text
                background: null
            }
        }

        Rectangle {
            objectName: "ratingModule"
            Layout.preferredWidth: visible ? 132 : 0
            Layout.fillHeight: true
            visible: SettingsController.autoReadRating
            color: root.moduleColor
            border.color: root.moduleBorder
            border.width: 1
            radius: Theme.radiusSm
            onVisibleChanged: if (!visible) root.exactRating = 0
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 5
                Label { text: qsTr("评分"); color: Theme.secondaryText; font.pixelSize: 12 }
                RowLayout {
                    spacing: 1
                    Repeater {
                        model: 5
                        delegate: ThemedIcon {
                            required property int index
                            source: index < root.exactRating
                                    ? Theme.icon("star-fill") : Theme.icon("star-line")
                            tint: index < root.exactRating
                                  ? Theme.ratingColor(index) : Theme.iconSecondary
                            sourceSize.width: 14
                            sourceSize.height: 14
                            Layout.preferredWidth: 15
                            Layout.preferredHeight: 16
                            TapHandler {
                                onTapped: {
                                    var next = index + 1
                                    root.exactRating = root.exactRating === next ? 0 : next
                                }
                            }
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }
        }

        Rectangle {
            objectName: "bpmModule"
        Layout.preferredWidth: 216
            Layout.fillHeight: true
            color: root.moduleColor
            border.color: root.moduleBorder
            border.width: 1
            radius: Theme.radiusSm
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                spacing: 2
                Label { text: "BPM"; color: Theme.primaryText; font.pixelSize: 11; Layout.preferredWidth: 24 }
                TextField {
                    id: minimumBpmField
                    objectName: "minimumBpmField"
                    Layout.preferredWidth: 34
                    text: Math.round(root.pendingMinBpm).toString()
                    color: Theme.primaryText
                    font.pixelSize: 11
                    horizontalAlignment: TextInput.AlignHCenter
                    validator: IntValidator { bottom: 60; top: 160 }
                    background: null
                    onEditingFinished: {
                        var value = Math.max(60, Math.min(Number(text) || 60,
                                                         root.pendingMaxBpm))
                        root.pendingMinBpm = value
                        text = Math.round(value).toString()
                        bpmDebounce.restart()
                    }
                }
                ThemedRangeSlider {
                    id: bpmRange
                    objectName: "bpmRange"
                    Layout.preferredWidth: 104
                    Layout.minimumWidth: 104
                    Layout.maximumWidth: 104
                    from: 60; to: 160; stepSize: 1
                    first.value: root.pendingMinBpm
                    second.value: root.pendingMaxBpm
                    first.onMoved: { root.pendingMinBpm = Math.min(first.value, root.pendingMaxBpm); bpmDebounce.restart() }
                    second.onMoved: { root.pendingMaxBpm = Math.max(second.value, root.pendingMinBpm); bpmDebounce.restart() }
                }
                TextField {
                    id: maximumBpmField
                    objectName: "maximumBpmField"
                    Layout.preferredWidth: 34
                    text: Math.round(root.pendingMaxBpm).toString()
                    color: Theme.primaryText
                    font.pixelSize: 11
                    horizontalAlignment: TextInput.AlignHCenter
                    validator: IntValidator { bottom: 60; top: 160 }
                    background: null
                    onEditingFinished: {
                        var value = Math.min(160, Math.max(Number(text) || 160,
                                                          root.pendingMinBpm))
                        root.pendingMaxBpm = value
                        text = Math.round(value).toString()
                        bpmDebounce.restart()
                    }
                }
            }
        }

        Item { Layout.fillWidth: true }

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

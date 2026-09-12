import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: "transparent"
    border.width: 0
    implicitHeight: Theme.controlHeight
    readonly property int contentTopMargin: 3
    readonly property int contentBottomMargin: integratedStyle ? 1 : 5
    readonly property bool compactLayout: width < 640

    property string searchText: ""
    property int exactRating: 0
    property double minBpm: 60
    property double maxBpm: 160
    property double pendingMinBpm: minBpm
    property double pendingMaxBpm: maxBpm
    property bool integratedStyle: false
    readonly property color moduleColor: Theme.isLight ? Theme.panel : Theme.elevated
    readonly property color moduleBorder: integratedStyle
                                                   ? Theme.integratedSoftOutline
                                                   : Theme.controlSubtleBorder

    Timer {
        id: searchDebounce
        interval: 150
        onTriggered: root.commitSearch()
    }

    function commitSearch() {
        searchDebounce.stop()
        searchText = searchField.text
    }

    onSearchTextChanged: {
        searchDebounce.stop()
        if (searchField.text !== searchText)
            searchField.text = searchText
    }

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
        searchDebounce.stop()
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
        anchors.leftMargin: Theme.spacingSm
        anchors.rightMargin: Theme.spacingSm
        anchors.topMargin: root.contentTopMargin
        anchors.bottomMargin: root.contentBottomMargin
        spacing: Theme.spacingSm

        Rectangle {
            objectName: "keywordModule"
            Layout.preferredWidth: root.compactLayout ? 136 : 184
            Layout.minimumWidth: 112
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
                width: Theme.iconSizeSm
                height: Theme.iconSizeSm
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
                placeholderTextColor: Theme.textTertiary
                leftPadding: 34
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeCaption
                onTextChanged: if (text !== root.searchText) searchDebounce.restart()
                onAccepted: root.commitSearch()
                background: null
            }
        }

        Rectangle {
            objectName: "ratingModule"
            Layout.preferredWidth: visible
                                   ? (root.compactLayout ? 96 : 132) : 0
            Layout.minimumWidth: visible ? 88 : 0
            Layout.fillHeight: true
            visible: SettingsController.autoReadRating
            Accessible.name: qsTr("评分筛选")
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
                Label { visible: !root.compactLayout; text: qsTr("评分"); color: Theme.secondaryText; font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption }
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
            Layout.preferredWidth: root.compactLayout ? 184 : 216
            Layout.minimumWidth: 176
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
                Label { text: "BPM"; color: Theme.primaryText; font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption; Layout.preferredWidth: 28; Layout.alignment: Qt.AlignVCenter; verticalAlignment: Text.AlignVCenter }
                TextField {
                    id: minimumBpmField
                    objectName: "minimumBpmField"
                    Layout.preferredWidth: 34
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignVCenter
                    verticalAlignment: TextInput.AlignVCenter
                    text: Math.round(root.pendingMinBpm).toString()
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeCaption
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
                    glassStyle: root.integratedStyle
                    Layout.preferredWidth: root.compactLayout ? 72 : 104
                    Layout.minimumWidth: root.compactLayout ? 72 : 104
                    Layout.maximumWidth: root.compactLayout ? 72 : 104
                    Layout.alignment: Qt.AlignVCenter
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
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignVCenter
                    verticalAlignment: TextInput.AlignVCenter
                    text: Math.round(root.pendingMaxBpm).toString()
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeCaption
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

        Button {
            objectName: "clearFiltersButton"
            Layout.fillHeight: true
            Layout.minimumWidth: 44
            text: qsTr("清空")
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeCaption
            onClicked: root.clearFilters()
            palette.buttonText: Theme.primaryText
            background: Rectangle {
                color: parent.pressed ? Theme.surfacePressed
                      : parent.hovered ? Theme.surfaceHover : Theme.panel
                border.color: root.moduleBorder
                border.width: 1
                radius: Theme.radiusSm
            }
        }

        Item { Layout.fillWidth: true }
    }
}

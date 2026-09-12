import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    objectName: "losslessTaskPanel"
    property var controller
    property bool compact: false
    readonly property bool englishUi: SettingsController.language
                                       && SettingsController.language.toLowerCase()
                                          .startsWith("en")
    property int sortColumn: -1
    property bool sortAscending: true

    function sortTasks(column) {
        if (!controller || !controller.tasks || !controller.tasks.sort)
            return
        if (sortColumn === column)
            sortAscending = !sortAscending
        else {
            sortColumn = column
            sortAscending = true
        }
        controller.tasks.sort(sortColumn, sortAscending
                              ? Qt.AscendingOrder : Qt.DescendingOrder)
    }

    function countAt(index) {
        const values = controller && controller.counts ? controller.counts : []
        return index < values.length ? Number(values[index] || 0) : 0
    }

    function verdictColor(code) {
        if (code === "credible_lossless" || code === "credible_native_dsd")
            return Theme.losslessVerdictCredible
        if (code === "suspected_lossy_transcode"
                || code === "suspected_lossy_upsample")
            return Theme.losslessVerdictTranscode
        if (code === "suspected_upsample"
                || code === "suspected_bit_depth_expansion"
                || code === "suspected_pcm_to_dsd")
            return Theme.losslessVerdictUpsample
        return Theme.losslessVerdictInconclusive
    }

    function filterColor(code) {
        if (code === "credible")
            return Theme.losslessVerdictCredible
        if (code === "transcode")
            return Theme.losslessVerdictTranscode
        if (code === "upsample")
            return Theme.losslessVerdictUpsample
        if (code === "inconclusive")
            return Theme.losslessVerdictInconclusive
        return Theme.textPrimary
    }

    component LosslessCheckBox: ThemedCheckBox {
        id: losslessCheckBox
        indicator: Rectangle {
            implicitWidth: losslessCheckBox.indicatorSize
            implicitHeight: losslessCheckBox.indicatorSize
            x: 0
            y: Math.round((losslessCheckBox.height - height) / 2)
            radius: Theme.radiusXs
            color: losslessCheckBox.hovered && losslessCheckBox.enabled
                   ? Theme.surfaceHover
                   : losslessCheckBox.checked ? Theme.losslessPanelHeaderSurface
                                              : Theme.losslessWorkspaceSurface
            border.color: losslessCheckBox.activeFocus
                          ? Theme.focus : Theme.opaqueBorder
            border.width: losslessCheckBox.activeFocus ? 2 : 1
            Text {
                anchors.centerIn: parent
                text: "✓"
                visible: losslessCheckBox.checked
                color: losslessCheckBox.enabled
                       ? Theme.textPrimary : Theme.textDisabled
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.losslessFontSizeMeta
            }
        }
    }

    color: Theme.losslessWorkspaceSurface
    border.color: Theme.opaqueBorder
    border.width: 1
    radius: Theme.radiusSm
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 92

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingLg
                anchors.rightMargin: Theme.spacingLg
                anchors.topMargin: Theme.spacingSm
                anchors.bottomMargin: Theme.spacingSm
                spacing: Theme.spacingSm

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm
                    Text {
                        text: qsTr("任务列表")
                        color: Theme.textPrimary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.losslessFontSizeSection
                        font.weight: Font.Normal
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        elide: Text.ElideRight
                    }
                    ThemedIconButton {
                        objectName: "losslessRemoveButton"
                        iconSource: Theme.icon("delete-bin-line")
                        accessibleName: qsTr("删除选中")
                        dangerOnHover: true
                        enabled: root.controller && root.controller.selectedCount > 0
                                 && !root.controller.running
                        onClicked: root.controller.removeSelected()
                    }
                    ThemedIconButton {
                        objectName: "losslessClearButton"
                        iconSource: Theme.icon("close-line")
                        accessibleName: qsTr("清除全部")
                        enabled: root.controller && root.controller.totalCount > 0
                                 && !root.controller.running
                        onClicked: root.controller.clear()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs
                    Repeater {
                        model: [
                            { code: "all", label: qsTr("全部"), shortLabel: qsTr("全部") },
                            { code: "credible", label: qsTr("可信无损"), shortLabel: qsTr("可信") },
                            { code: "transcode", label: qsTr("疑似转码"), shortLabel: qsTr("转码") },
                            { code: "upsample", label: qsTr("疑似升频"), shortLabel: qsTr("升频") },
                            { code: "inconclusive", label: qsTr("无法确定"), shortLabel: qsTr("不确定") }
                        ]
                        delegate: ThemedButton {
                            id: filterButton
                            required property int index
                            required property var modelData
                            objectName: "losslessFilter_" + modelData.code
                            compact: true
                            labelPixelSize: Theme.losslessFontSizeMeta
                            Layout.preferredHeight: 30
                            text: root.compact
                                  ? String(root.countAt(index))
                                  : ((root.englishUi || root.width < 600) && modelData.shortLabel
                                     ? modelData.shortLabel : modelData.label)
                                    + "  " + root.countAt(index)
                            primary: root.controller
                                     && root.controller.filter === modelData.code
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: root.compact ? 40
                                                   : Math.min(118,
                                                     Math.max(84,
                                                       filterButton.implicitWidth))
                            Layout.maximumWidth: root.compact ? 40
                                                  : Math.min(118,
                                                    Math.max(84,
                                                      filterButton.implicitWidth))
                            onClicked: root.controller.filter = modelData.code

                            contentItem: Text {
                                text: filterButton.text
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                color: !filterButton.enabled ? Theme.textDisabled
                                       : filterButton.primary ? Theme.accentText
                                                              : root.filterColor(modelData.code)
                                font.family: Theme.fontPrimary
                                font.pixelSize: filterButton.labelPixelSize
                                font.weight: filterButton.primary
                                             ? Font.Medium : Font.Normal
                            }

                            background: Rectangle {
                                radius: Theme.radiusSm
                                color: !filterButton.enabled
                                       ? Theme.losslessPanelSurface
                                       : filterButton.primary
                                         ? (filterButton.down ? Theme.accentPressed
                                            : filterButton.hovered ? Theme.accentHover
                                                                   : Theme.accent)
                                       : filterButton.down ? Theme.surfacePressed
                                       : filterButton.hovered ? Theme.surfaceHover
                                                              : Theme.losslessPanelSurface
                                border.color: filterButton.activeFocus
                                              ? Theme.focus : Theme.opaqueBorder
                                border.width: filterButton.activeFocus ? 2
                                              : filterButton.primary ? 0 : 1
                            }
                        }
                    }
                }

            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.tableHeaderHeight
            color: Theme.losslessPanelHeaderSurface
            border.width: 0

            Row {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingSm
                anchors.rightMargin: Theme.spacingSm
                spacing: 0

                LosslessCheckBox {
                    width: 34
                    height: parent.height
                    indicatorSize: 24
                    checked: root.controller
                             && root.controller.totalCount > 0
                             && root.controller.selectedCount
                                === root.controller.totalCount
                    onClicked: root.controller.selectAll(checked)
                }
                Text {
                    width: Math.max(116, parent.width
                                    - 34 - formatHeader.width
                                    - audioHeader.width - verdictHeader.width
                                    - confidenceHeader.width)
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("文件名") + (root.sortColumn === 1
                          ? (root.sortAscending ? "  ↑" : "  ↓") : "")
                    elide: Text.ElideRight
                    clip: true
                    color: Theme.textSecondary
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.losslessFontSizeMeta
                    Accessible.name: qsTr("按文件名排序")
                    Accessible.role: Accessible.Button
                    TapHandler { onTapped: root.sortTasks(1) }
                }
                Text {
                    id: formatHeader
                    width: root.width >= 500 ? 74 : 0
                    height: parent.height
                    visible: width > 0
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("格式")
                    elide: Text.ElideRight
                    clip: true
                    color: Theme.textSecondary
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.losslessFontSizeMeta
                }
                Text {
                    id: audioHeader
                    width: root.width >= 560 ? 126 : 0
                    height: parent.height
                    visible: width > 0
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("采样率 / 位深")
                    elide: Text.ElideRight
                    clip: true
                    rightPadding: Theme.spacingSm
                    color: Theme.textSecondary
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.losslessFontSizeMeta
                }
                Text {
                    id: verdictHeader
                    width: root.width >= 420 ? 112 : 88
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("判定")
                    elide: Text.ElideRight
                    clip: true
                    rightPadding: Theme.spacingSm
                    color: Theme.textSecondary
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.losslessFontSizeMeta
                }
                Text {
                    id: confidenceHeader
                    width: 58
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignRight
                    text: qsTr("评分") + (root.sortColumn === 5
                          ? (root.sortAscending ? " ↑" : " ↓") : "")
                    elide: Text.ElideRight
                    clip: true
                    color: Theme.textSecondary
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.losslessFontSizeMeta
                    Accessible.name: qsTr("按证据评分排序")
                    Accessible.role: Accessible.Button
                    TapHandler { onTapped: root.sortTasks(5) }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.opaqueDivider
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: taskList
                objectName: "losslessTaskList"
                anchors.fill: parent
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.controller ? root.controller.tasks : null
                ScrollBar.vertical: ThemedScrollBar {
                    width: 8
                    implicitWidth: 8
                    visible: taskList.contentHeight > taskList.height + 0.5
                }

                delegate: Rectangle {
                    id: taskRow
                    required property int index
                    required property string taskId
                    required property string fileName
                    required property string filePath
                    required property string formatName
                    required property string audioFormat
                    required property string verdictCode
                    required property string verdictText
                    required property int confidence
                    required property string state
                    required property string stateText
                    required property bool checked
                    required property real progress

                    objectName: "losslessTaskRow_" + taskId
                    width: taskList.width
                    height: root.compact ? 44 : 54
                    activeFocusOnTab: true
                    Accessible.name: fileName + "，" + (verdictText || stateText)
                    Accessible.role: Accessible.ListItem
                    color: root.controller
                           && root.controller.selectedResult
                           && root.controller.selectedResult.taskId === taskId
                           ? Theme.selectedSurface
                           : rowHover.hovered ? Theme.surfaceHover
                           : index % 2 ? Theme.losslessTableAlternateSurface
                                       : Theme.losslessPanelSurface
                    border.color: Theme.focus
                    border.width: activeFocus ? 2 : 0

                    Keys.onReturnPressed: root.controller.selectTask(taskId)
                    Keys.onSpacePressed: root.controller.setChecked(taskId, !checked)

                    HoverHandler { id: rowHover }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.controller.selectTask(taskRow.taskId)
                    }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingSm
                        anchors.rightMargin: Theme.spacingSm

                        LosslessCheckBox {
                            width: 34
                            height: parent.height
                            indicatorSize: 24
                            checked: taskRow.checked
                            onClicked: root.controller.setChecked(taskRow.taskId, checked)
                        }
                        Item {
                            width: Math.max(116, parent.width
                                            - 34 - formatCell.width
                                            - audioCell.width - verdictCell.width
                                            - confidenceCell.width)
                            height: parent.height
                            Rectangle {
                                width: 6
                                height: 6
                                radius: 3
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                color: root.verdictColor(taskRow.verdictCode)
                            }
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 14
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: taskRow.fileName
                                elide: Text.ElideMiddle
                                color: Theme.textPrimary
                                font.family: Theme.fontPrimary
                                font.pixelSize: Theme.losslessFontSizeMeta
                            }
                        }
                        Text {
                            id: formatCell
                            width: root.width >= 500 ? 74 : 0
                            height: parent.height
                            visible: width > 0
                            verticalAlignment: Text.AlignVCenter
                            text: taskRow.formatName
                            elide: Text.ElideRight
                            rightPadding: Theme.spacingSm
                            clip: true
                            color: Theme.textPrimary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.losslessFontSizeMeta
                        }
                        Text {
                            id: audioCell
                            width: root.width >= 560 ? 126 : 0
                            height: parent.height
                            visible: width > 0
                            verticalAlignment: Text.AlignVCenter
                            text: taskRow.audioFormat
                            elide: Text.ElideRight
                            rightPadding: Theme.spacingSm
                            clip: true
                            color: Theme.textPrimary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.losslessFontSizeMeta
                        }
                        Text {
                            id: verdictCell
                            width: root.width >= 420 ? 112 : 88
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            text: taskRow.verdictText
                                  ? taskRow.verdictText : taskRow.stateText
                            elide: Text.ElideRight
                            rightPadding: Theme.spacingSm
                            clip: true
                            color: taskRow.verdictText
                                   ? root.verdictColor(taskRow.verdictCode)
                                   : Theme.textSecondary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.losslessFontSizeMeta
                        }
                        Text {
                            id: confidenceCell
                            width: 58
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: Text.AlignRight
                            text: taskRow.verdictText
                                  ? qsTr("%1分").arg(taskRow.confidence)
                                  : Math.round(taskRow.progress * 100) + "%"
                            color: taskRow.verdictText
                                   ? root.verdictColor(taskRow.verdictCode)
                                   : Theme.textSecondary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.losslessFontSizeMeta
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.opaqueDivider
                    }
                }
            }

            Column {
                id: emptyState
                objectName: "losslessEmptyState"
                anchors.centerIn: parent
                width: Math.min(parent.width - Theme.spacing2Xl, 300)
                spacing: Theme.spacingSm
                visible: taskList.count === 0

                ThemedIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Theme.iconSizeLg
                    height: width
                    source: Theme.icon("equalizer-line")
                    tint: Theme.textTertiary
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: root.controller && root.controller.totalCount > 0
                          ? qsTr("没有符合当前筛选的任务")
                          : qsTr("添加音频文件以开始无损鉴别")
                    wrapMode: Text.Wrap
                    color: Theme.textTertiary
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.losslessFontSizeBody
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            Layout.leftMargin: Theme.spacingMd
            verticalAlignment: Text.AlignVCenter
            text: qsTr("共 %1 个文件（已选择 %2 个）")
                  .arg(root.controller ? root.controller.totalCount : 0)
                  .arg(root.controller ? root.controller.selectedCount : 0)
            color: Theme.textSecondary
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.losslessFontSizeMeta
        }
    }
}

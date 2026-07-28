import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Popup {
    id: root

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1000, parent.width - 80)
    height: Math.min(760, parent.height - 80)
    modal: true
    dim: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 0

    property int selectedSection: 0
    property string searchText: ""
    property bool editResolved: true

    onOpened: {
        editResolved = false
        SettingsController.beginEdit()
    }

    onClosed: {
        if (!editResolved) {
            editResolved = true
            SettingsController.cancelEdit()
        }
    }

    function cancelAndClose() {
        if (!editResolved) {
            editResolved = true
            SettingsController.cancelEdit()
        }
        root.close()
    }

    function saveAndClose() {
        if (!editResolved) {
            editResolved = true
            SettingsController.commitEdit()
        }
        root.close()
    }

    function sectionList() {
        return [
            { index: 0, text: qsTr("常规"), subtitle: qsTr("常规"), icon: "\u2699" },
            { index: 1, text: qsTr("播放与音频"), subtitle: qsTr("播放"), icon: "\u25B6" },
            { index: 2, text: qsTr("外观与波形"), subtitle: qsTr("外观"), icon: "\u223F" },
            { index: 3, text: qsTr("音频工具预设"), subtitle: qsTr("音频工具"), icon: "\u2692" },
            { index: 4, text: qsTr("快捷键设置"), subtitle: qsTr("快捷键"), icon: "\u2328" },
            { index: 5, text: qsTr("缓存与数据"), subtitle: qsTr("缓存"), icon: "\u2672" },
            { index: 6, text: qsTr("关于"), subtitle: qsTr("关于"), icon: "\u2139" }
        ]
    }

    function filteredSections() {
        const all = sectionList()
        if (!searchText) {
            return all
        }
        return all.filter(function(item) {
            return item.text.toLowerCase().indexOf(searchText) >= 0
                || item.subtitle.toLowerCase().indexOf(searchText) >= 0
        })
    }

    onSearchTextChanged: {
        const filtered = filteredSections()
        let found = false
        for (let i = 0; i < filtered.length; ++i) {
            if (filtered[i].index === selectedSection) {
                found = true
                break
            }
        }
        if (!found && filtered.length > 0) {
            selectedSection = filtered[0].index
        }
    }

    background: Rectangle {
        color: Theme.panel
        radius: Theme.radiusLg
        border.color: Theme.border
        border.width: 1
    }

    MessageDialog {
        id: clearCacheConfirmDialog
        title: qsTr("确认清空缓存")
        text: qsTr("确定要一键清空全部缓存吗？此操作不可撤销。")
        buttons: MessageDialog.Yes | MessageDialog.No
        onAccepted: SettingsController.clearAllCache()
    }

    contentItem: ColumnLayout {
        spacing: 0

        // Header
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingMd

            Image {
                source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
                sourceSize.width: 28
                sourceSize.height: 28
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                fillMode: Image.PreserveAspectFit
            }

            Text {
                text: qsTr("AgPlayer · 设置")
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 18
                font.weight: Font.Bold
            }

            Item { Layout.fillWidth: true }

            TextField {
                id: searchField
                Layout.preferredWidth: 220
                Layout.preferredHeight: 32
                placeholderText: qsTr("搜索设置...")
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter

                background: Rectangle {
                    color: Theme.background
                    radius: Theme.radiusSm
                    border.color: Theme.border
                    border.width: 1
                }

                onTextChanged: root.searchText = text.toLowerCase()
            }

            Button {
                text: qsTr("恢复默认")
                focusPolicy: Qt.StrongFocus
                onClicked: SettingsController.resetToDefaults()

                contentItem: Text {
                    text: parent.text
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    color: parent.pressed ? Theme.border
                          : parent.hovered ? Theme.hoverSurface
                          : "transparent"
                    border.color: Theme.border
                    border.width: 1
                    radius: Theme.radiusSm
                    implicitWidth: 90
                    implicitHeight: 32
                }
            }

            ToolButton {
                objectName: "settingsCloseButton"
                icon.source: Theme.icon("close-fill")
                icon.color: Theme.secondaryText
                icon.width: 20
                icon.height: 20
                focusPolicy: Qt.StrongFocus
                onClicked: root.close()
                Accessible.name: qsTr("Close settings")

                background: Rectangle {
                    color: parent.pressed ? Theme.favoriteRed
                          : parent.hovered ? Theme.border
                          : "transparent"
                    radius: Theme.radiusSm
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        // Body
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Sidebar
            Rectangle {
                Layout.preferredWidth: 180
                Layout.fillHeight: true
                color: "transparent"

                ListView {
                    id: settingsSectionList
                    objectName: "settingsSectionList"
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    clip: true
                    spacing: Theme.spacingXs
                    model: filteredSections()
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    delegate: Rectangle {
                        required property var modelData
                        width: settingsSectionList.width
                        height: 42
                        radius: Theme.radiusSm
                        color: root.selectedSection === modelData.index
                               ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.15)
                               : (mouseArea.containsMouse ? Theme.border : "transparent")

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingMd
                            anchors.rightMargin: Theme.spacingMd
                            spacing: Theme.spacingMd

                            Text {
                                text: modelData.icon
                                color: root.selectedSection === modelData.index
                                       ? Theme.cyan
                                       : Theme.secondaryText
                                font.pixelSize: 16
                                font.family: "Segoe UI Symbol"
                                Layout.preferredWidth: 24
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Text {
                                text: modelData.text
                                color: root.selectedSection === modelData.index
                                       ? Theme.primaryText
                                       : Theme.secondaryText
                                font.family: Theme.fontPrimary
                                font.pixelSize: 14
                                Layout.fillWidth: true
                            }
                        }

                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.selectedSection = modelData.index
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.border
            }

            // Content
            ScrollView {
                id: settingsScroll
                objectName: "settingsScroll"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                StackLayout {
                    id: contentStack
                    width: settingsScroll.availableWidth
                    currentIndex: root.selectedSection
                    implicitHeight: {
                        const page = children[currentIndex]
                        return page ? page.implicitHeight + Theme.spacingLg : 0
                    }
                    height: Math.max(settingsScroll.availableHeight, implicitHeight)

                    GeneralSection {}
                    PlaybackSection {}
                    AppearanceSection {}
                    AudioToolsSection {}
                    HotkeysSection {}
                    CacheSection {}
                    AboutSection {}
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        // Footer
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: Theme.spacingMd

            Item { Layout.fillWidth: true }

            Button {
                objectName: "settingsCancelButton"
                text: qsTr("取消")
                focusPolicy: Qt.StrongFocus
                onClicked: root.cancelAndClose()

                contentItem: Text {
                    text: parent.text
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    color: parent.pressed ? Theme.border
                          : parent.hovered ? Theme.hoverSurface
                          : "transparent"
                    border.color: Theme.border
                    border.width: 1
                    radius: Theme.radiusSm
                    implicitWidth: 110
                    implicitHeight: 36
                }
            }

            Button {
                objectName: "settingsSaveButton"
                text: qsTr("保存更改")
                focusPolicy: Qt.StrongFocus
                onClicked: root.saveAndClose()

                contentItem: Text {
                    text: parent.text
                    color: Theme.accentText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    color: parent.pressed ? Qt.lighter(Theme.cyan, 1.1)
                          : parent.hovered ? Qt.lighter(Theme.cyan, 1.2)
                          : Theme.cyan
                    radius: Theme.radiusSm
                    implicitWidth: 110
                    implicitHeight: 36
                }
            }
        }
    }

    component SettingCard: Rectangle {
        property alias title: titleText.text
        default property alias content: contentContainer.children

        color: Theme.elevated
        radius: Theme.radiusMd
        border.color: Theme.border
        border.width: 1
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacingMd
            spacing: Theme.spacingMd

            Text {
                id: titleText
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 14
                font.weight: Font.Bold
                Layout.fillWidth: true
            }

            ColumnLayout {
                id: contentContainer
                Layout.fillWidth: true
                spacing: Theme.spacingSm
            }

            Item { Layout.fillHeight: true }
        }
    }

    component SectionHeader: RowLayout {
        property alias title: titleText.text
        property alias subtitle: subtitleText.text

        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingLg
        Layout.bottomMargin: Theme.spacingMd
        spacing: Theme.spacingMd

        Rectangle {
            Layout.preferredWidth: 4
            Layout.preferredHeight: 24
            color: Theme.cyan
            radius: 2
        }

        ColumnLayout {
            spacing: 2

            Text {
                id: titleText
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 18
                font.weight: Font.Bold
            }

            Text {
                id: subtitleText
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
            }
        }
    }

    component SettingRow: RowLayout {
        property alias label: labelText.text
        default property alias content: contentContainer.children

        Layout.fillWidth: true
        Layout.preferredHeight: 40
        spacing: Theme.spacingMd

        Text {
            id: labelText
            color: Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 14
            Layout.preferredWidth: 130
            Layout.alignment: Qt.AlignVCenter
        }

        Item {
            id: contentContainer
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    component SettingSwitch: Switch {
        property alias labelText: label.text

        indicator: Rectangle {
            implicitWidth: 40
            implicitHeight: 22
            radius: 11
            color: parent.checked ? Theme.cyan : Theme.border

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                x: parent.checked ? parent.width - width - 2 : 2
                width: 18
                height: 18
                radius: 9
                color: "#FFFFFF"

                Behavior on x {
                    NumberAnimation { duration: 120 }
                }
            }
        }

        contentItem: Text {
            id: label
            text: parent.text
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 14
            leftPadding: parent.indicator ? parent.indicator.width + parent.spacing : 0
            verticalAlignment: Text.AlignVCenter
        }
    }

    component SettingCombo: ComboBox {
        id: combo
        property var valueModel

        width: 180
        textRole: "text"
        valueRole: "value"
        model: valueModel

        contentItem: Text {
            text: combo.displayText
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 13
            verticalAlignment: Text.AlignVCenter
            leftPadding: Theme.spacingSm
        }

        background: Rectangle {
            color: Theme.background
            radius: Theme.radiusSm
            border.color: Theme.border
            border.width: 1
        }

        popup: Popup {
            y: combo.height + 2
            width: combo.width
            padding: 1

            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: combo.popup.visible ? combo.delegateModel : null
                currentIndex: combo.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator {}
            }

            background: Rectangle {
                color: Theme.panel
                radius: Theme.radiusSm
                border.color: Theme.border
                border.width: 1
            }
        }

        delegate: ItemDelegate {
            width: combo.width
            highlighted: combo.highlightedIndex === index

            contentItem: Text {
                text: modelData.text
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter
            }

            background: Rectangle {
                color: highlighted ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.15)
                                  : "transparent"
            }
        }
    }

    component FileAssociationCheck: CheckBox {
        property string extPrimary
        property string extSecondary: ""

        indicator: Rectangle {
            implicitWidth: 18
            implicitHeight: 18
            radius: 4
            color: parent.checked ? Theme.cyan : "transparent"
            border.color: parent.checked ? Theme.cyan : Theme.border
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: "\u2713"
                color: Theme.accentText
                font.pixelSize: 11
                visible: parent.parent.checked
            }
        }

        contentItem: Text {
            text: parent.text
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 12
            leftPadding: parent.indicator.width + parent.spacing
            verticalAlignment: Text.AlignVCenter
        }
    }

    component PathFieldRow: RowLayout {
        property alias path: pathField.text
        property alias dialogFolder: folderDialog.currentFolder
        signal pathSelected(string newPath)

        Layout.fillWidth: true
        spacing: Theme.spacingSm

        TextField {
            id: pathField
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 13
            verticalAlignment: Text.AlignVCenter
            background: Rectangle {
                color: Theme.background
                radius: Theme.radiusSm
                border.color: Theme.border
                border.width: 1
            }
            onEditingFinished: parent.pathSelected(text)
        }

        ToolButton {
            icon.source: Theme.icon("folder-open-fill")
            icon.color: Theme.secondaryText
            icon.width: 18
            icon.height: 18
            onClicked: folderDialog.open()

            background: Rectangle {
                color: parent.pressed ? Theme.border
                      : parent.hovered ? Theme.hoverSurface
                      : "transparent"
                radius: Theme.radiusSm
            }
        }

        FolderDialog {
            id: folderDialog
            onAccepted: parent.pathSelected(selectedFolder.toString().replace("file:///", ""))
        }
    }

    component WaveformThumbnail: Rectangle {
        id: thumb
        property int mode: 0
        property bool selected: false
        property string modeLabel: mode === 0 ? qsTr("纯色波形") : (mode === 1 ? qsTr("RGB波形") : qsTr("频谱波形"))
        signal clicked()

        width: 96
        height: 72
        radius: Theme.radiusSm
        color: selected ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.12)
                        : Theme.background
        border.color: selected ? Theme.cyan : Theme.border
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 4

            Canvas {
                Layout.fillWidth: true
                Layout.fillHeight: true
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    var cy = height / 2

                    // Draw subtle center line
                    ctx.strokeStyle = Theme.border
                    ctx.lineWidth = 1
                    ctx.beginPath()
                    ctx.moveTo(0, cy)
                    ctx.lineTo(width, cy)
                    ctx.stroke()

                    if (mode === 1) {
                        // RGB gradient waveform using project RGBA layers
                        var rGrad = ctx.createLinearGradient(0, 0, width, 0)
                        rGrad.addColorStop(0, "rgba(170,55,55,0.62)")
                        rGrad.addColorStop(0.35, "rgba(55,140,55,0.58)")
                        rGrad.addColorStop(0.7, "rgba(55,90,145,0.52)")
                        rGrad.addColorStop(1, "rgba(170,55,55,0.62)")
                        ctx.strokeStyle = rGrad
                        ctx.lineWidth = 2
                        ctx.beginPath()
                        for (var x = 0; x <= width; x += 2) {
                            var amp = Math.sin(x * 0.18) * Math.cos(x * 0.07) * Math.sin(x * 0.04)
                            var y = cy + amp * (height * 0.38)
                            if (x === 0) ctx.moveTo(x, y)
                            else ctx.lineTo(x, y)
                        }
                        ctx.stroke()
                    } else if (mode === 2) {
                        // Spectrum bars
                        var step = 3
                        var barCount = Math.floor(width / (step + 2))
                        for (var i = 0; i < barCount; ++i) {
                            var bx = i * (step + 2)
                            var barHeight = Math.max(2, Math.abs(Math.sin(i * 0.4) * Math.cos(i * 0.17)) * (height * 0.82))
                            var t = i / barCount
                            var rr = Math.round(170 * (1 - t) + 55 * t)
                            var gg = Math.round(55 * (1 - t) + 140 * t)
                            var bb = Math.round(55 + 90 * t)
                            ctx.fillStyle = "rgba(" + rr + "," + gg + "," + bb + ",0.72)"
                            ctx.fillRect(bx, cy - barHeight / 2, step, barHeight)
                        }
                    } else {
                        // Solid cyan filled waveform
                        ctx.fillStyle = Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.35)
                        ctx.beginPath()
                        ctx.moveTo(0, cy)
                        for (var sx = 0; sx <= width; sx += 2) {
                            var sa = Math.sin(sx * 0.18) * Math.cos(sx * 0.07) * Math.sin(sx * 0.04)
                            var sy = cy + sa * (height * 0.38)
                            ctx.lineTo(sx, sy)
                        }
                        ctx.lineTo(width, cy)
                        ctx.closePath()
                        ctx.fill()

                        ctx.strokeStyle = Theme.cyan
                        ctx.lineWidth = 1.5
                        ctx.beginPath()
                        for (sx = 0; sx <= width; sx += 2) {
                            sa = Math.sin(sx * 0.18) * Math.cos(sx * 0.07) * Math.sin(sx * 0.04)
                            sy = cy + sa * (height * 0.38)
                            if (sx === 0) ctx.moveTo(sx, sy)
                            else ctx.lineTo(sx, sy)
                        }
                        ctx.stroke()
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: thumb.modeLabel
                color: selected ? Theme.primaryText : Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
            }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 4
            width: 18
            height: 18
            radius: 9
            color: Theme.cyan
            visible: thumb.selected

            Text {
                anchors.centerIn: parent
                text: "\u2713"
                color: Theme.accentText
                font.pixelSize: 11
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: thumb.clicked()
        }
    }

    component SectionScroll: ScrollView {
        id: scroll
        clip: true
        contentWidth: availableWidth

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
    }

    component GeneralSection: ColumnLayout {
        spacing: Theme.spacingSm

        SectionHeader {
            title: qsTr("常规")
            subtitle: qsTr("常规")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingMd
            rowSpacing: Theme.spacingMd

            SettingCard {
                title: qsTr("开机与窗口")
                Layout.preferredHeight: 320

                SettingSwitch {
                    text: qsTr("开机自动启动")
                    checked: SettingsController.autoStartWithWindows
                    onToggled: SettingsController.autoStartWithWindows = checked
                }

                SettingSwitch {
                    text: qsTr("启动时自动恢复上次播放进度")
                    checked: SettingsController.restoreLastPlaybackOnStartup
                    onToggled: SettingsController.restoreLastPlaybackOnStartup = checked
                }

                SettingSwitch {
                    text: qsTr("双窗口歌单面板")
                    checked: SettingsController.showListWindowPanel
                    onToggled: SettingsController.showListWindowPanel = checked
                }

                SettingSwitch {
                    text: qsTr("窗口磁吸吸附对齐 (15px 阈值)")
                    checked: SettingsController.windowMagneticSnap
                    onToggled: SettingsController.windowMagneticSnap = checked
                }

                SettingRow {
                    label: qsTr("歌曲列表位置")
                    SettingCombo {
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: qsTr("上"), value: 0 },
                            { text: qsTr("下"), value: 1 },
                            { text: qsTr("左"), value: 2 },
                            { text: qsTr("右"), value: 3 }
                        ]
                        currentIndex: SettingsController.listWindowPosition
                        onActivated: SettingsController.listWindowPosition = currentValue
                    }
                }

                SettingRow {
                    label: qsTr("退出程序行为")
                    SettingCombo {
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: qsTr("最小化到托盘"), value: 0 },
                            { text: qsTr("直接关闭"), value: 1 }
                        ]
                        currentIndex: SettingsController.closeBehavior
                        onActivated: SettingsController.closeBehavior = currentValue
                    }
                }

                SettingRow {
                    label: qsTr("语言")
                    SettingCombo {
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: "中文", value: "zh" },
                            { text: "English", value: "en" },
                            { text: "\u0E20\u0E32\u0E29\u0E32\u0E44\u0E17\u0E22", value: "th" },
                            { text: "Ti\u1EBFng Vi\u1EC7t", value: "vi" }
                        ]
                        currentIndex: {
                            const values = ["zh", "en", "th", "vi"]
                            return values.indexOf(SettingsController.language)
                        }
                        onActivated: SettingsController.language = currentValue
                    }
                }
            }

            SettingCard {
                title: qsTr("文件关联")
                Layout.preferredHeight: 320

                SettingSwitch {
                    text: qsTr("设为系统默认音频播放器")
                    checked: SettingsController.setAsDefaultPlayer
                    onToggled: SettingsController.setAsDefaultPlayer = checked
                }

                SettingRow {
                    label: qsTr("关联格式")
                    RowLayout {
                        anchors.fill: parent
                        spacing: Theme.spacingMd

                        FileAssociationCheck {
                            text: "MP3"
                            extPrimary: "mp3"
                            checked: SettingsController.fileAssociations.indexOf("mp3") >= 0
                            onToggled: updateAssociation("mp3", checked)
                        }

                        FileAssociationCheck {
                            text: "WAV"
                            extPrimary: "wav"
                            checked: SettingsController.fileAssociations.indexOf("wav") >= 0
                            onToggled: updateAssociation("wav", checked)
                        }

                        FileAssociationCheck {
                            text: "FLAC"
                            extPrimary: "flac"
                            checked: SettingsController.fileAssociations.indexOf("flac") >= 0
                            onToggled: updateAssociation("flac", checked)
                        }

                        FileAssociationCheck {
                            text: "AAC/M4A"
                            extPrimary: "aac"
                            extSecondary: "m4a"
                            checked: SettingsController.fileAssociations.indexOf("aac") >= 0
                            onToggled: {
                                updateAssociation("aac", checked)
                                updateAssociation("m4a", checked)
                            }
                        }

                        FileAssociationCheck {
                            text: "OGG"
                            extPrimary: "ogg"
                            checked: SettingsController.fileAssociations.indexOf("ogg") >= 0
                            onToggled: updateAssociation("ogg", checked)
                        }
                    }
                }

                Button {
                    text: qsTr("重新绑定文件关联与图标")
                    onClicked: SettingsController.rebindFileAssociations()

                    contentItem: Text {
                        text: parent.text
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: parent.pressed ? Theme.border
                              : parent.hovered ? Theme.hoverSurface
                              : "transparent"
                        border.color: Theme.border
                        border.width: 1
                        radius: Theme.radiusSm
                        implicitWidth: 180
                        implicitHeight: 36
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    component PlaybackSection: ColumnLayout {
        spacing: Theme.spacingSm

        SectionHeader {
            title: qsTr("播放与音频")
            subtitle: qsTr("播放")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingMd
            rowSpacing: Theme.spacingMd

            SettingCard {
                title: qsTr("音频输出")
                Layout.preferredHeight: 180

                SettingRow {
                    label: qsTr("输出设备")
                    SettingCombo {
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: qsTr("自动 / 系统默认设备"), value: "\u81EA\u52A8 / \u7CFB\u7EDF\u9ED8\u8BA4\u8BBE\u5907" }
                        ]
                        currentIndex: {
                            for (let i = 0; i < valueModel.length; ++i) {
                                if (valueModel[i].value === SettingsController.outputDevice) return i
                            }
                            return 0
                        }
                        onActivated: SettingsController.outputDevice = currentValue
                    }
                }

                SettingSwitch {
                    text: qsTr("音频独占模式 (Exclusive Mode/WASAPI/ALSA) (降低延迟，但其他软件静音)")
                    checked: SettingsController.audioExclusiveMode
                    onToggled: SettingsController.audioExclusiveMode = checked
                }
            }

            SettingCard {
                title: qsTr("播放行为")
                Layout.preferredHeight: 260

                SettingSwitch {
                    text: qsTr("播放键 RGB 光晕")
                    checked: SettingsController.playButtonRgbGlow
                    onToggled: SettingsController.playButtonRgbGlow = checked
                }

                SettingRow {
                    label: qsTr("默认播放模式")
                    SettingCombo {
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: qsTr("顺序播放"), value: 0 },
                            { text: qsTr("随机播放"), value: 1 },
                            { text: qsTr("单曲循环"), value: 2 },
                            { text: qsTr("列表循环"), value: 3 }
                        ]
                        currentIndex: SettingsController.defaultPlaybackMode
                        onActivated: SettingsController.defaultPlaybackMode = currentValue
                    }
                }

                SettingSwitch {
                    text: qsTr("开启无间隙播放")
                    checked: SettingsController.gaplessPlayback
                    onToggled: SettingsController.gaplessPlayback = checked
                }

                SettingRow {
                    label: qsTr("歌曲切换淡入淡出")
                    SettingCombo {
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: qsTr("0ms (关闭)"), value: 0 },
                            { text: qsTr("200ms"), value: 1 },
                            { text: qsTr("500ms"), value: 2 }
                        ]
                        currentIndex: SettingsController.crossfadeMs
                        onActivated: SettingsController.crossfadeMs = currentValue
                    }
                }

                SettingSwitch {
                    text: qsTr("自动匹配采样率")
                    checked: SettingsController.autoMatchSampleRate
                    onToggled: SettingsController.autoMatchSampleRate = checked
                }

                SettingSwitch {
                    text: qsTr("自动读取歌曲 BPM")
                    checked: SettingsController.autoReadBpm
                    onToggled: SettingsController.autoReadBpm = checked
                }

                SettingSwitch {
                    text: qsTr("星级评分")
                    checked: SettingsController.autoReadRating
                    onToggled: SettingsController.autoReadRating = checked
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    component AppearanceSection: ColumnLayout {
        spacing: Theme.spacingSm

        SectionHeader {
            title: qsTr("外观与波形")
            subtitle: qsTr("外观")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingMd
            rowSpacing: Theme.spacingMd

            SettingCard {
                title: qsTr("主题样式")
                Layout.preferredHeight: 160

                SettingRow {
                    label: qsTr("主题模式")
                    RowLayout {
                        anchors.fill: parent
                        spacing: Theme.spacingSm

                        Repeater {
                            model: [
                                { text: qsTr("深色"), value: 0 },
                                { text: qsTr("浅色"), value: 1 },
                                { text: qsTr("跟随系统"), value: 2 }
                            ]

                            delegate: Button {
                                text: modelData.text
                                checked: SettingsController.themeMode === modelData.value
                                checkable: true
                                onClicked: SettingsController.themeMode = modelData.value

                                contentItem: Text {
                                    text: parent.text
                                    color: parent.checked ? Theme.accentText : Theme.primaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 13
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }

                                background: Rectangle {
                                    color: parent.checked ? Theme.cyan : "transparent"
                                    border.color: parent.checked ? Theme.cyan : Theme.border
                                    border.width: 1
                                    radius: Theme.radiusSm
                                    implicitWidth: 80
                                    implicitHeight: 32
                                }
                            }
                        }
                    }
                }

                SettingSwitch {
                    text: qsTr("毛玻璃 / 悬浮特效：开启迷你播放器与悬浮窗口模糊背景")
                    checked: SettingsController.glassEffect
                    onToggled: SettingsController.glassEffect = checked
                }
            }

            SettingCard {
                title: qsTr("Waveform RGB 波形设置")
                Layout.preferredHeight: 280

                SettingRow {
                    label: qsTr("默认波形模式")
                    Layout.preferredHeight: 90
                    RowLayout {
                        anchors.fill: parent
                        spacing: Theme.spacingMd

                        WaveformThumbnail {
                            mode: 0
                            selected: SettingsController.waveformMode === 0
                            onClicked: SettingsController.waveformMode = 0
                        }

                        WaveformThumbnail {
                            mode: 1
                            selected: SettingsController.waveformMode === 1
                            onClicked: SettingsController.waveformMode = 1
                        }

                        WaveformThumbnail {
                            mode: 2
                            selected: SettingsController.waveformMode === 2
                            onClicked: SettingsController.waveformMode = 2
                        }
                    }
                }

                SettingRow {
                    label: qsTr("波形渲染密度")
                    SettingCombo {
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: qsTr("稀疏"), value: 0 },
                            { text: qsTr("适中（推荐）"), value: 1 },
                            { text: qsTr("精细"), value: 2 }
                        ]
                        currentIndex: SettingsController.waveformDensity
                        onActivated: SettingsController.waveformDensity = currentValue
                    }
                }

                SettingRow {
                    label: qsTr("波形线条粗细")
                    SettingCombo {
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: qsTr("1px"), value: 1.0 },
                            { text: qsTr("1.5px"), value: 1.5 },
                            { text: qsTr("2px"), value: 2.0 },
                            { text: qsTr("3px"), value: 3.0 },
                            { text: qsTr("4px"), value: 4.0 },
                            { text: qsTr("6px"), value: 6.0 }
                        ]
                        currentIndex: {
                            const values = [1.0, 1.5, 2.0, 3.0, 4.0, 6.0]
                            for (let i = 0; i < values.length; ++i) {
                                if (Math.abs(values[i] - SettingsController.waveformThickness) < 0.01) return i
                            }
                            return 2
                        }
                        onActivated: SettingsController.waveformThickness = currentValue
                    }
                }

                SettingSwitch {
                    text: qsTr("鼠标悬停波形时显示时间预览胶囊")
                    checked: SettingsController.waveformHoverTimePreview
                    onToggled: SettingsController.waveformHoverTimePreview = checked
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    component AudioToolsSection: ColumnLayout {
        spacing: Theme.spacingSm

        SectionHeader {
            title: qsTr("音频工具预设")
            subtitle: qsTr("音频工具")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: Theme.spacingMd
            rowSpacing: Theme.spacingMd

            SettingCard {
                title: qsTr("通用导出")
                Layout.preferredHeight: 160

                SettingRow {
                    label: qsTr("默认导出路径")
                    PathFieldRow {
                        anchors.fill: parent
                        path: SettingsController.defaultOutputDirectory
                        dialogFolder: SettingsController.defaultOutputDirectory
                        onPathSelected: SettingsController.defaultOutputDirectory = newPath
                    }
                }

                SettingRow {
                    label: qsTr("文件覆盖策略")
                    SettingCombo {
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: qsTr("自动重命名 (例: _edited)"), value: 0 },
                            { text: qsTr("直接覆盖"), value: 1 }
                        ]
                        currentIndex: SettingsController.overwritePolicy
                        onActivated: SettingsController.overwritePolicy = currentValue
                    }
                }
            }

            SettingCard {
                title: qsTr("转码与剪辑预设")
                Layout.preferredHeight: 160

                SettingRow {
                    label: qsTr("默认转码输出格式")
                    SettingCombo {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 240
                        valueModel: [
                            { text: qsTr("MP3 / 320kbps / 44.1kHz / Stereo"), value: "MP3 / 320kbps / 44.1kHz / Stereo" },
                            { text: qsTr("MP3 / 256kbps / 44.1kHz / Stereo"), value: "MP3 / 256kbps / 44.1kHz / Stereo" },
                            { text: qsTr("MP3 / 192kbps / 44.1kHz / Stereo"), value: "MP3 / 192kbps / 44.1kHz / Stereo" },
                            { text: qsTr("FLAC / 44.1kHz / Stereo"), value: "FLAC / 44.1kHz / Stereo" },
                            { text: qsTr("WAV / 44.1kHz / Stereo"), value: "WAV / 44.1kHz / Stereo" }
                        ]
                        currentIndex: {
                            const values = [
                                "MP3 / 320kbps / 44.1kHz / Stereo",
                                "MP3 / 256kbps / 44.1kHz / Stereo",
                                "MP3 / 192kbps / 44.1kHz / Stereo",
                                "FLAC / 44.1kHz / Stereo",
                                "WAV / 44.1kHz / Stereo"
                            ]
                            return values.indexOf(SettingsController.defaultTranscodeFormat)
                        }
                        onActivated: SettingsController.defaultTranscodeFormat = currentValue
                    }
                }

                SettingSwitch {
                    text: qsTr("批量转码时保持原音频元数据")
                    checked: SettingsController.preserveMetadata
                    onToggled: SettingsController.preserveMetadata = checked
                }
            }

            SettingCard {
                title: qsTr("变调与变速预设")
                Layout.preferredHeight: 160

                SettingSwitch {
                    text: qsTr("变速时变调行为：保持原音高/语速")
                    checked: SettingsController.keepPitchWhileSpeedChange
                    onToggled: SettingsController.keepPitchWhileSpeedChange = checked
                }

                SettingSwitch {
                    text: qsTr("升降调人声保护")
                    checked: SettingsController.vocalProtection
                    onToggled: SettingsController.vocalProtection = checked
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    component HotkeysSection: ColumnLayout {
        spacing: Theme.spacingSm

        SectionHeader {
            title: qsTr("快捷键设置")
            subtitle: qsTr("快捷键")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingMd
            rowSpacing: Theme.spacingMd

            SettingCard {
                title: qsTr("全局快捷键")
                Layout.preferredHeight: 220

                HotkeyRow { label: qsTr("播放 / 暂停"); value: SettingsController.hkPlayPause }
                HotkeyRow { label: qsTr("上一首 / 下一首"); value: SettingsController.hkPrevNext }
                HotkeyRow { label: qsTr("音量加 / 减"); value: SettingsController.hkVolumeUpDown }
                HotkeyRow { label: qsTr("显示 / 隐藏迷你播放器"); value: SettingsController.hkToggleMiniPlayer }
            }

            SettingCard {
                title: qsTr("应用内快捷键")
                Layout.preferredHeight: 220

                HotkeyRow { label: qsTr("快速搜索歌曲"); value: SettingsController.hkSearch }
                HotkeyRow { label: qsTr("快速切换波形模式"); value: SettingsController.hkWaveformMode }
                HotkeyRow { label: qsTr("打开音频工具"); value: SettingsController.hkAudioTools }
            }
        }

        Item { Layout.fillHeight: true }
    }

    component HotkeyRow: RowLayout {
        property alias label: labelText.text
        property alias value: valueText.text

        Layout.fillWidth: true
        Layout.preferredHeight: 36
        spacing: Theme.spacingMd

        Text {
            id: labelText
            color: Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 14
            Layout.preferredWidth: 140
            Layout.alignment: Qt.AlignVCenter
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.background
            radius: Theme.radiusSm
            border.color: Theme.border
            border.width: 1

            Text {
                id: valueText
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMd
                anchors.rightMargin: Theme.spacingMd
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 13
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    component CacheSection: ColumnLayout {
        spacing: Theme.spacingSm

        SectionHeader {
            title: qsTr("缓存与数据")
            subtitle: qsTr("缓存")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingMd
            rowSpacing: Theme.spacingMd

            SettingCard {
                title: qsTr("路径与自动清理")
                Layout.preferredHeight: 220

                SettingRow {
                    label: qsTr("缓存路径")
                    PathFieldRow {
                        anchors.fill: parent
                        path: SettingsController.cacheDirectory
                        dialogFolder: SettingsController.cacheDirectory
                        onPathSelected: SettingsController.cacheDirectory = newPath
                    }
                }

                SettingSwitch {
                    text: qsTr("缓存自动清理（超出上限自动删旧文件，默认开启）")
                    checked: SettingsController.autoCleanCache
                    onToggled: SettingsController.autoCleanCache = checked
                }

                SettingSwitch {
                    text: qsTr("退出自动清理临时转码文件（默认开启）")
                    checked: SettingsController.cleanTempOnExit
                    onToggled: SettingsController.cleanTempOnExit = checked
                }

                SettingRow {
                    label: qsTr("当前缓存")
                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: SettingsController.currentCacheSizeMB + " MB"
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 14
                    }
                }
            }

            SettingCard {
                title: qsTr("清理按钮")
                Layout.preferredHeight: 220

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Button {
                        text: qsTr("波形缓存")
                        onClicked: SettingsController.clearWaveformCache()
                        contentItem: Text {
                            text: parent.text
                            color: Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: parent.pressed ? Theme.border
                                  : parent.hovered ? Theme.hoverSurface
                                  : "transparent"
                            border.color: Theme.border
                            border.width: 1
                            radius: Theme.radiusSm
                            implicitWidth: 90
                            implicitHeight: 32
                        }
                    }

                    Button {
                        text: qsTr("封面缓存")
                        onClicked: SettingsController.clearCoverCache()
                        contentItem: Text {
                            text: parent.text
                            color: Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: parent.pressed ? Theme.border
                                  : parent.hovered ? Theme.hoverSurface
                                  : "transparent"
                            border.color: Theme.border
                            border.width: 1
                            radius: Theme.radiusSm
                            implicitWidth: 90
                            implicitHeight: 32
                        }
                    }

                    Button {
                        text: qsTr("转码临时文件")
                        onClicked: SettingsController.clearTempFiles()
                        contentItem: Text {
                            text: parent.text
                            color: Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: parent.pressed ? Theme.border
                                  : parent.hovered ? Theme.hoverSurface
                                  : "transparent"
                            border.color: Theme.border
                            border.width: 1
                            radius: Theme.radiusSm
                            implicitWidth: 100
                            implicitHeight: 32
                        }
                    }
                }

                Button {
                    text: qsTr("一键清空全部缓存")
                    onClicked: clearCacheConfirmDialog.open()

                    contentItem: Text {
                        text: parent.text
                        color: "#FFFFFF"
                        font.family: Theme.fontPrimary
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: parent.pressed ? Qt.darker(Theme.favoriteRed, 1.2)
                              : parent.hovered ? Qt.lighter(Theme.favoriteRed, 1.1)
                              : Theme.favoriteRed
                        radius: Theme.radiusSm
                        implicitWidth: 180
                        implicitHeight: 36
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        Item { Layout.fillHeight: true }
    }

    component AboutSection: ColumnLayout {
        spacing: Theme.spacingLg

        SectionHeader {
            title: qsTr("关于")
            subtitle: qsTr("关于")
        }

        MessageDialog {
            id: updateDialog
            title: qsTr("检查更新")
            buttons: MessageDialog.Ok
            text: ""
        }

        Connections {
            target: SettingsController
            function onUpdateCheckFinished(message, success) {
                updateDialog.text = message
                updateDialog.open()
            }
        }

        SettingCard {
            title: ""
            Layout.fillWidth: true
            Layout.preferredHeight: 220

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingLg

                Image {
                    source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
                    sourceSize.width: 72
                    sourceSize.height: 72
                    Layout.preferredWidth: 72
                    Layout.preferredHeight: 72
                    fillMode: Image.PreserveAspectFit
                }

                ColumnLayout {
                    spacing: Theme.spacingXs

                    Text {
                        text: "AgPlayer"
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 22
                        font.weight: Font.Bold
                    }

                    Text {
                        text: SettingsController.version
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 13
                    }

                    Text {
                        text: qsTr("发布日期: ") + SettingsController.releaseDate
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 13
                    }

                    Text {
                        text: qsTr("轻量、纯粹、为音乐而生。")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 13
                    }
                }

                Item { Layout.fillWidth: true }

                ColumnLayout {
                    spacing: Theme.spacingMd

                    Button {
                        text: qsTr("检查更新")
                        onClicked: SettingsController.checkForUpdates()

                        contentItem: Text {
                            text: parent.text
                            color: Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            color: parent.pressed ? Theme.border
                                  : parent.hovered ? Theme.hoverSurface
                                  : "transparent"
                            border.color: Theme.border
                            border.width: 1
                            radius: Theme.radiusSm
                            implicitWidth: 120
                            implicitHeight: 36
                        }
                    }

                    Button {
                        text: qsTr("访问官网")
                        onClicked: SettingsController.openOfficialWebsite()

                        contentItem: Text {
                            text: parent.text
                            color: Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            color: parent.pressed ? Theme.border
                                  : parent.hovered ? Theme.hoverSurface
                                  : "transparent"
                            border.color: Theme.border
                            border.width: 1
                            radius: Theme.radiusSm
                            implicitWidth: 120
                            implicitHeight: 36
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    function updateAssociation(ext, checked) {
        var list = SettingsController.fileAssociations
        var idx = list.indexOf(ext)
        if (checked && idx < 0) {
            list.push(ext)
        } else if (!checked && idx >= 0) {
            list.splice(idx, 1)
        }
        SettingsController.fileAssociations = list
    }
}

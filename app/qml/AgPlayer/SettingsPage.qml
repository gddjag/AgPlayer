import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Item {
    id: root

    anchors.fill: parent
    visible: false
    focus: true

    // Compatibility properties keep SettingsWindow's public contract stable
    // while this page remains a plain fill component with no second frame.
    property bool modal: false
    property bool dim: false
    property int closePolicy: Popup.CloseOnEscape
    signal opened()
    signal aboutToHide()
    signal closed()

    property int selectedSection: 0
    property string searchText: ""
    property bool editResolved: true
    property var hostWindow
    property alias programmaticScroll: settingsScroll.programmaticScroll
    readonly property var frequencyWaveformSettings:
        SettingsController.frequencyColorWaveform

    function frequencyBandColor(index) {
        if (Number(index) <= 0)
            return frequencyWaveformSettings.lowColor
        if (Number(index) === 1)
            return frequencyWaveformSettings.midColor
        return frequencyWaveformSettings.highColor
    }

    function open() {
        if (visible)
            return
        if (hostWindow && !hostWindow.visible) {
            hostWindow.openSettings()
            return
        }
        visible = true
        forceActiveFocus()
        opened()
    }

    function close() {
        if (!visible)
            return
        aboutToHide()
        visible = false
        closed()
    }

    Keys.onEscapePressed: cancelAndClose()

    function shortcutText(event) {
        if (event.key === Qt.Key_Control || event.key === Qt.Key_Shift
                || event.key === Qt.Key_Alt || event.key === Qt.Key_Meta)
            return ""
        const parts = []
        if (event.modifiers & Qt.ControlModifier) parts.push("Ctrl")
        if (event.modifiers & Qt.AltModifier) parts.push("Alt")
        if (event.modifiers & Qt.ShiftModifier) parts.push("Shift")
        if (event.modifiers & Qt.MetaModifier) parts.push("Meta")
        let keyText = event.text ? event.text.toUpperCase() : ""
        if (event.key === Qt.Key_Space) keyText = "Space"
        else if (event.key === Qt.Key_Tab) keyText = "Tab"
        else if (event.key === Qt.Key_Escape) keyText = "Esc"
        else if (event.key === Qt.Key_Left) keyText = "Left"
        else if (event.key === Qt.Key_Right) keyText = "Right"
        else if (event.key === Qt.Key_Up) keyText = "Up"
        else if (event.key === Qt.Key_Down) keyText = "Down"
        else if (event.key === Qt.Key_MediaPlay) keyText = "MediaPlayPause"
        else if (event.key === Qt.Key_MediaPrevious) keyText = "MediaPrevTrack"
        else if (event.key === Qt.Key_MediaNext) keyText = "MediaNextTrack"
        else if (event.key === Qt.Key_VolumeUp) keyText = "VolumeUp"
        else if (event.key === Qt.Key_VolumeDown) keyText = "VolumeDown"
        else if (event.key >= Qt.Key_F1 && event.key <= Qt.Key_F12)
            keyText = "F" + (event.key - Qt.Key_F1 + 1)
        if (!keyText)
            return ""
        parts.push(keyText)
        return parts.join(" + ")
    }

    function shortcutConflicts(candidate, original) {
        if (!candidate || candidate === original)
            return false
        const comparableCandidate = String(candidate).trim()
                .replace(/\s*\+\s*/g, "+")
        const shortcuts = [
            SettingsController.hkPlayPause,
            SettingsController.hkPrevNext,
            SettingsController.hkVolumeUpDown,
            SettingsController.hkToggleMiniPlayer,
            SettingsController.hkSearch,
            SettingsController.hkWaveformMode,
            SettingsController.hkAudioTools
        ]
        const rolling = SettingsController.rollingKeyboardShortcuts
        for (var action in rolling)
            shortcuts.push(rolling[action])
        return shortcuts.some(function(shortcut) {
            if (shortcut === original)
                return false
            return String(shortcut).split("/").some(function(member) {
                return member.trim().replace(/\s*\+\s*/g, "+")
                        === comparableCandidate
            })
        })
    }

    function isAllowedGlobalShortcut(candidate) {
        return candidate.indexOf("Media") === 0
                || candidate.indexOf("Volume") === 0
                || candidate.indexOf(" + ") > 0
    }

    onOpened: {
        editResolved = false
        SettingsController.beginEdit()
        Qt.callLater(scrollToSelectedSection)
    }

    onSelectedSectionChanged: {
        Qt.callLater(scrollToSelectedSection)
    }

    function selectSection(index) {
        selectedSection = index
    }

    function scrollToSelectedSection() {
        if (!settingsScroll || !settingsScroll.contentItem)
            return
        settingsScroll.contentItem.contentY = 0
    }

    function updateSectionFromScroll() {}

    onAboutToHide: {
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
            { index: 0, text: qsTr("常规"), subtitle: qsTr("常规"), icon: "settings-3-fill" },
            { index: 1, text: qsTr("播放与音频"), subtitle: qsTr("播放"), icon: "play-fill" },
            { index: 2, text: qsTr("外观与波形"), subtitle: qsTr("外观"), icon: "waveform-switch" },
            { index: 3, text: qsTr("音频工具预设"), subtitle: qsTr("音频工具"), icon: "equalizer-line" },
            { index: 4, text: qsTr("键盘"), subtitle: qsTr("键盘"), icon: "list-unordered" },
            { index: 5, text: qsTr("缓存与数据"), subtitle: qsTr("缓存"), icon: "folder-open-line" },
            { index: 6, text: qsTr("关于"), subtitle: qsTr("关于"), icon: "information-line" }
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

    ThemedDialog {
        id: clearCacheConfirmDialog
        objectName: "clearCacheConfirmDialog"
        anchors.centerIn: Overlay.overlay
        width: 360
        title: qsTr("确认清空缓存")
        standardButtons: Dialog.Yes | Dialog.No
        contentItem: Label {
            text: qsTr("确定要一键清空全部缓存吗？此操作不可撤销。")
            color: Theme.textPrimary
            wrapMode: Text.Wrap
        }
        onAccepted: SettingsController.clearAllCache()
    }

    // This sits outside the header layout deliberately: it owns only the
    // native-window drag strip and must not participate in header sizing.
    MouseArea {
        objectName: "settingsHeaderDragArea"
        anchors.left: root.left
        anchors.top: root.top
        width: Math.max(0, root.width - 390)
        height: 56
        acceptedButtons: Qt.LeftButton
        z: 2
        onPressed: function(mouse) {
            if (root.hostWindow) root.hostWindow.startSystemMove()
        }
    }

    ColumnLayout {
        anchors.fill: parent
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
                font.pixelSize: Theme.fontSizeSection
                font.weight: Font.Bold
            }

            Item { Layout.fillWidth: true }

            ThemedTextField {
                id: searchField
                Layout.preferredWidth: 188
                Layout.preferredHeight: Theme.controlHeight
                placeholderText: qsTr("搜索设置...")
                accessibleName: qsTr("搜索设置")
                leftPadding: 34
                verticalAlignment: Text.AlignVCenter

                onTextChanged: root.searchText = text.toLowerCase()

                ThemedIcon {
                    objectName: "settingsSearchIcon"
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    source: Theme.icon("search-line")
                    tint: searchField.activeFocus ? Theme.iconAccent
                                                  : Theme.iconSecondary
                    sourceSize.width: 15
                    sourceSize.height: 15
                }
            }

            ThemedButton {
                text: qsTr("恢复默认")
                onClicked: SettingsController.resetToDefaults()
            }

            ThemedIconButton {
                objectName: "settingsCloseButton"
                iconSource: Theme.icon("close-fill")
                iconSize: 20
                dangerOnHover: true
                accessibleName: qsTr("Close settings")
                onClicked: root.close()
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
                id: settingsSidebar
                objectName: "settingsSidebar"
                property string designRole: "settingsCategoryRail"
                Layout.preferredWidth: 184
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
                        height: 32
                        radius: Theme.radiusSm
                        color: root.selectedSection === modelData.index
                               ? Theme.highlightSoft
                               : (mouseArea.containsMouse ? Theme.surfaceHover : "transparent")
                        Behavior on color { ColorAnimation { duration: 120 } }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingMd
                            anchors.rightMargin: Theme.spacingMd
                            spacing: Theme.spacingMd

                            ThemedIcon {
                                source: Theme.icon(modelData.icon)
                                tint: root.selectedSection === modelData.index
                                      ? Theme.highlight
                                      : Theme.iconSecondary
                                Behavior on tint { ColorAnimation { duration: 120 } }
                                sourceSize.width: 17
                                sourceSize.height: 17
                                Layout.preferredWidth: 20
                                Layout.preferredHeight: 17
                            }

                            Text {
                                objectName: "settingsSectionLabel-" + modelData.index
                                text: modelData.text
                                color: root.selectedSection === modelData.index
                                       ? Theme.highlight
                                       : Theme.secondaryText
                                Behavior on color { ColorAnimation { duration: 120 } }
                                font.family: Theme.fontPrimary
                                font.pixelSize: Theme.fontSizeBody
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                clip: true
                                ToolTip.text: text
                                ToolTip.visible: truncated && mouseArea.containsMouse
                            }
                        }

                        MouseArea {
                            id: mouseArea
                            objectName: "settingsSectionHoverArea-" + modelData.index
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.selectSection(modelData.index)
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
                property string designRole: "settingsContentSurface"
                property bool programmaticScroll: false
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    id: settingsContentColumn
                    objectName: "settingsContentColumn"
                    width: Math.min(760, Math.max(0,
                                                  settingsScroll.availableWidth - 48))
                    x: Math.max(24, (settingsScroll.availableWidth - width) / 2)
                    spacing: 6

                    GeneralSection {
                        objectName: "generalSettingsSection"
                        visible: root.selectedSection === 0
                        Layout.fillWidth: true
                    }
                    PlaybackSection {
                        objectName: "playbackSettingsSection"
                        visible: root.selectedSection === 1
                        Layout.fillWidth: true
                    }
                    AppearanceSection {
                        objectName: "appearanceSettingsSection"
                        visible: root.selectedSection === 2
                        Layout.fillWidth: true
                    }
                    AudioToolsSection {
                        objectName: "audioToolsSettingsSection"
                        visible: root.selectedSection === 3
                        Layout.fillWidth: true
                    }
                    HotkeysSection {
                        objectName: "hotkeysSettingsSection"
                        visible: root.selectedSection === 4
                        Layout.fillWidth: true
                    }
                    CacheSection {
                        objectName: "cacheSettingsSection"
                        visible: root.selectedSection === 5
                        Layout.fillWidth: true
                    }
                    AboutSection {
                        objectName: "aboutSettingsSection"
                        visible: root.selectedSection === 6
                        Layout.fillWidth: true
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.spacingXl
                    }
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
            Layout.preferredHeight: 52
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg
            spacing: 12

            Item { Layout.fillWidth: true }

            ThemedButton {
                objectName: "settingsCancelButton"
                Layout.preferredWidth: 110
                prominent: true
                text: qsTr("取消")
                onClicked: root.cancelAndClose()
            }

            ThemedButton {
                objectName: "settingsSaveButton"
                Layout.preferredWidth: 110
                prominent: true
                primary: true
                text: qsTr("保存更改")
                onClicked: root.saveAndClose()
            }
        }
    }

    component SettingCard: Rectangle {
        property alias title: titleText.text
        default property alias content: contentContainer.children

        color: "transparent"
        radius: 0
        border.width: 0
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        implicitHeight: titleText.implicitHeight + contentContainer.implicitHeight
                        + Theme.spacingXl
        Layout.preferredHeight: implicitHeight

        Column {
            // The card height follows this layout's content. Do not also force
            // its height from the card: wrapped text otherwise feeds a polish
            // loop back through contentContainer.implicitHeight.
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacingSm
            spacing: Theme.spacingSm

            Text {
                id: titleText
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeBodyStrong
                font.weight: Font.Bold
                width: parent.width
            }

            ColumnLayout {
                id: contentContainer
                width: parent.width
                spacing: Theme.spacingSm
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

    component SectionHeader: RowLayout {
        property alias title: titleText.text
        property alias subtitle: subtitleText.text

        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingXs
        Layout.bottomMargin: 0
        spacing: Theme.spacingSm

        Rectangle {
            Layout.preferredWidth: 4
            Layout.preferredHeight: 20
            color: Theme.accent
            radius: 2
        }

        ColumnLayout {
            spacing: subtitleText.visible ? 2 : 0

            Text {
                id: titleText
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeSection
                font.weight: Font.Bold
            }

            Text {
                id: subtitleText
                visible: false
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeCaption
            }
        }
    }

    component SettingRow: Item {
        property alias label: labelText.text
        property string labelObjectName: ""
        property real labelWidth: 176
        property bool fitLabelToContent: false
        default property alias content: contentContainer.children

        Layout.fillWidth: true
        Layout.preferredHeight: Theme.settingsRowHeight

        Text {
            id: labelText
            objectName: parent.labelObjectName
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeBody
            width: parent.fitLabelToContent
                   ? Math.min(parent.width - 190,
                              Math.max(parent.labelWidth, contentWidth))
                   : parent.labelWidth
        }

        Item {
            id: contentContainer
            anchors.left: labelText.right
            anchors.leftMargin: Theme.spacingMd
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
        }
    }

    component SettingSwitch: ThemedSwitch {
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.settingsRowHeight
        indicatorTrailing: true
        labelPixelSize: Theme.fontSizeBody
    }

    component SettingCombo: ThemedComboBox {
        id: combo
        property var valueModel
        property bool layoutManaged: false

        width: 180
        height: Theme.controlHeight
        anchors.right: !layoutManaged && parent ? parent.right : undefined
        anchors.verticalCenter: !layoutManaged && parent
            ? parent.verticalCenter : undefined
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        Layout.preferredWidth: width
        Layout.preferredHeight: height
        textRole: "text"
        valueRole: "value"
        model: valueModel

        contentItem: Text {
            text: combo.displayText
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeBody
            verticalAlignment: Text.AlignVCenter
            leftPadding: Theme.spacingSm
            rightPadding: (combo.indicator ? combo.indicator.width : 0)
                          + Theme.spacingSm
            elide: Text.ElideRight
        }

        background: Rectangle {
            color: Theme.surfaceElevated
            radius: Theme.radiusSm
            border.color: Theme.opaqueBorder
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
                color: Theme.surfaceElevated
                radius: Theme.radiusSm
                border.color: Theme.opaqueBorder
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
                font.pixelSize: Theme.fontSizeBody
                verticalAlignment: Text.AlignVCenter
            }

            background: Rectangle {
                color: highlighted ? Theme.highlightSoft
                                  : "transparent"
            }
        }
    }

    component SettingStepper: RowLayout {
        id: stepper
        property real value: 0
        property real minimumValue: 0
        property real maximumValue: 1
        property real stepSize: 0.1
        property int decimals: 1
        property string suffix: ""
        signal valueEdited(real nextValue)

        function normalized(nextValue) {
            const bounded = Math.max(minimumValue,
                                     Math.min(maximumValue, nextValue))
            return Number(bounded.toFixed(decimals))
        }

        function decrease() {
            valueEdited(normalized(value - stepSize))
        }

        function increase() {
            valueEdited(normalized(value + stepSize))
        }

        spacing: 0

        Button {
            text: "\u2212"
            enabled: stepper.value > stepper.minimumValue
            focusPolicy: Qt.StrongFocus
            onClicked: stepper.decrease()
            contentItem: Text {
                text: parent.text
                color: parent.enabled ? Theme.primaryText : Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeSection
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: parent.pressed ? Theme.border
                      : parent.hovered ? Theme.hoverSurface
                      : Theme.background
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm
                implicitWidth: 34
                implicitHeight: 32
            }
        }

        Rectangle {
            Layout.preferredWidth: 88
            Layout.preferredHeight: 32
            color: Theme.background
            border.color: Theme.border
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: stepper.value.toFixed(stepper.decimals) + stepper.suffix
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeBody
            }
        }

        Button {
            text: "+"
            enabled: stepper.value < stepper.maximumValue
            focusPolicy: Qt.StrongFocus
            onClicked: stepper.increase()
            contentItem: Text {
                text: parent.text
                color: parent.enabled ? Theme.primaryText : Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeSection
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: parent.pressed ? Theme.border
                      : parent.hovered ? Theme.hoverSurface
                      : Theme.background
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm
                implicitWidth: 34
                implicitHeight: 32
            }
        }
    }

    component FileAssociationCheck: ThemedCheckBox {
        id: associationCheck
        property string extPrimary
        property string extSecondary: ""
        implicitWidth: contentItem.implicitWidth
        implicitHeight: 32
    }

    component PathFieldRow: RowLayout {
        id: pathRow
        property string path: ""
        property alias dialogFolder: folderDialog.currentFolder
        signal pathSelected(string newPath)

        Layout.fillWidth: true
        spacing: Theme.spacingSm

        ThemedTextField {
            id: pathField
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.controlHeight
            Layout.alignment: Qt.AlignVCenter
            text: pathRow.path
            verticalAlignment: Text.AlignVCenter
            onEditingFinished: pathRow.pathSelected(text)
        }

        ThemedIconButton {
            iconSource: Theme.icon("folder-open-fill")
            iconSize: 18
            accessibleName: qsTr("选择文件夹")
            Layout.alignment: Qt.AlignVCenter
            onClicked: folderDialog.open()
        }

        FolderDialog {
            id: folderDialog
            onAccepted: pathRow.pathSelected(
                            selectedFolder.toString().replace("file:///", ""))
        }
    }

    component WaveformThumbnail: Rectangle {
        id: thumb
        property int mode: 0
        property bool selected: false
        property string modeLabel: mode === 0 ? qsTr("纯色波形")
                                   : mode === 3 ? qsTr("频彩波形")
                                   : mode === 1 ? qsTr("RGB波形")
                                                : qsTr("柱状频谱")
        signal clicked()

        width: 96
        height: 72
        radius: Theme.radiusSm
        color: selected ? Theme.highlightSoft
                        : Theme.background
        border.color: selected ? Theme.highlightBorder : Theme.border
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 4

            Canvas {
                id: waveformPreviewCanvas
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

                    if (mode === 3) {
                        // The native renderer below owns the frequency preview,
                        // so settings shows the same three-band color mixture
                        // and original amplitude geometry as the player.
                        return
                    } else if (mode === 1) {
                        var waveformGradient = ctx.createLinearGradient(0, 0, width, 0)
                        waveformGradient.addColorStop(0, SettingsController.waveformRgbStartColor)
                        waveformGradient.addColorStop(0.5, SettingsController.waveformRgbMiddleColor)
                        waveformGradient.addColorStop(1, SettingsController.waveformRgbEndColor)
                        ctx.strokeStyle = waveformGradient
                        ctx.lineWidth = SettingsController.waveformThickness
                        ctx.beginPath()
                        for (var x = 0; x <= width; x += 2) {
                            var amp = Math.sin(x * 0.18) * Math.cos(x * 0.07) * Math.sin(x * 0.04)
                            var y = cy + amp * (height * 0.38
                                               * SettingsController.waveformHeight)
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
                            var envelope = 0.22 + 0.78 * Math.sin(Math.PI * i / Math.max(1, barCount - 1))
                            var barHeight = Math.max(2, Math.abs(Math.sin(i * 0.4) * Math.cos(i * 0.17)) * (height * 0.82) * envelope)
                            if (SettingsController.spectrumColorMode === 0) {
                                ctx.fillStyle = SettingsController.spectrumSolidColor
                            } else {
                                var spectrumGradient = ctx.createLinearGradient(0, 0, width, 0)
                                spectrumGradient.addColorStop(0, SettingsController.spectrumRgbStartColor)
                                spectrumGradient.addColorStop(0.5, SettingsController.spectrumRgbMiddleColor)
                                spectrumGradient.addColorStop(1, SettingsController.spectrumRgbEndColor)
                                ctx.fillStyle = spectrumGradient
                            }
                            ctx.fillRect(bx, cy - barHeight / 2, step, barHeight)
                        }
                    } else {
                        // Solid cyan filled waveform
                        ctx.fillStyle = SettingsController.waveformSolidBaseColor
                        ctx.beginPath()
                        ctx.moveTo(0, cy)
                        for (var sx = 0; sx <= width; sx += 2) {
                            var sa = Math.sin(sx * 0.18) * Math.cos(sx * 0.07) * Math.sin(sx * 0.04)
                            var sy = cy + sa * (height * 0.38
                                               * SettingsController.waveformHeight)
                            ctx.lineTo(sx, sy)
                        }
                        ctx.lineTo(width, cy)
                        ctx.closePath()
                        ctx.fill()

                        ctx.strokeStyle = SettingsController.waveformSolidProgressColor
                        ctx.lineWidth = SettingsController.waveformThickness
                        ctx.beginPath()
                        for (sx = 0; sx <= width; sx += 2) {
                            sa = Math.sin(sx * 0.18) * Math.cos(sx * 0.07) * Math.sin(sx * 0.04)
                            sy = cy + sa * (height * 0.38
                                           * SettingsController.waveformHeight)
                            if (sx === 0) ctx.moveTo(sx, sy)
                            else ctx.lineTo(sx, sy)
                        }
                        ctx.stroke()
                    }
                }

                Loader {
                    anchors.fill: parent
                    anchors.margins: 1
                    active: thumb.mode === 3
                    sourceComponent: WaveformItem {
                        objectName: "frequencyWaveformThumbnailPreview"
                        anchors.fill: parent
                        enabled: false
                        pointerInteractionEnabled: false
                        visualMode: 3
                        layers: ({
                            "mix": [0.12, 0.30, 0.18, 0.66, 0.24, 0.48, 0.20, 0.78,
                                    0.34, 0.58, 0.16, 0.72, 0.26, 0.54, 0.20, 0.82,
                                    0.32, 0.62, 0.18, 0.74, 0.28, 0.52, 0.16, 0.68],
                            "bass": [0.88, 0.78, 0.62, 0.42, 0.22, 0.14, 0.18, 0.32,
                                     0.56, 0.76, 0.68, 0.40, 0.18, 0.12, 0.20, 0.46,
                                     0.72, 0.82, 0.58, 0.30, 0.16, 0.20, 0.44, 0.70],
                            "mid": [0.16, 0.28, 0.52, 0.78, 0.68, 0.42, 0.24, 0.18,
                                    0.28, 0.52, 0.82, 0.72, 0.44, 0.22, 0.16, 0.28,
                                    0.54, 0.80, 0.70, 0.42, 0.24, 0.18, 0.34, 0.62],
                            "high": [0.12, 0.18, 0.24, 0.34, 0.58, 0.82, 0.72, 0.46,
                                     0.24, 0.18, 0.30, 0.56, 0.84, 0.74, 0.48, 0.26,
                                     0.18, 0.30, 0.60, 0.86, 0.70, 0.44, 0.24, 0.16]
                        })
                        duration: 1000
                        position: 0
                        cursorPosition: 520
                        lowColor: root.frequencyWaveformSettings.lowColor
                        midColor: root.frequencyWaveformSettings.midColor
                        highColor: root.frequencyWaveformSettings.highColor
                        frequencyUnplayedOpacity:
                            root.frequencyWaveformSettings.unplayedOpacity
                        amplitudeScale: Math.min(
                                            1.0, SettingsController.waveformHeight)
                        density: 0.5
                        lineWidth: Math.max(
                                       1.0, SettingsController.waveformThickness)
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: thumb.modeLabel
                color: selected ? Theme.primaryText : Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeCaption
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
                font.pixelSize: Theme.fontSizeCaption
            }
        }

        Connections {
            target: SettingsController
            function onWaveformHeightChanged() { waveformPreviewCanvas.requestPaint() }
            function onWaveformThicknessChanged() { waveformPreviewCanvas.requestPaint() }
            function onWaveformSolidBaseColorChanged() { waveformPreviewCanvas.requestPaint() }
            function onWaveformSolidProgressColorChanged() { waveformPreviewCanvas.requestPaint() }
            function onWaveformRgbBaseColorChanged() { waveformPreviewCanvas.requestPaint() }
            function onWaveformRgbStartColorChanged() { waveformPreviewCanvas.requestPaint() }
            function onWaveformRgbMiddleColorChanged() { waveformPreviewCanvas.requestPaint() }
            function onWaveformRgbEndColorChanged() { waveformPreviewCanvas.requestPaint() }
            function onSpectrumColorModeChanged() { waveformPreviewCanvas.requestPaint() }
            function onSpectrumSolidColorChanged() { waveformPreviewCanvas.requestPaint() }
            function onSpectrumRgbStartColorChanged() { waveformPreviewCanvas.requestPaint() }
            function onSpectrumRgbMiddleColorChanged() { waveformPreviewCanvas.requestPaint() }
            function onSpectrumRgbEndColorChanged() { waveformPreviewCanvas.requestPaint() }
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
        spacing: 6

        SectionHeader {
            title: qsTr("常规")
            subtitle: ""
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 1
            columnSpacing: Theme.spacingMd
            rowSpacing: 8

            SettingCard {
                title: qsTr("开机与窗口")

                SettingSwitch {
                    text: Qt.platform.os === "osx" ? qsTr("登录时自动启动") : qsTr("开机自动启动")
                    checked: SettingsController.autoStartWithWindows
                    onToggled: SettingsController.autoStartWithWindows = checked
                }

                Text {
                    Layout.fillWidth: true
                    visible: SettingsController.systemIntegrationError.length > 0
                    text: SettingsController.systemIntegrationError
                    color: Theme.textSecondary
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.Wrap
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
                        objectName: "languageCombo"
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: "中文", value: "zh" },
                            { text: "English", value: "en" }
                        ]
                        currentIndex: {
                            const values = ["zh", "en"]
                            return values.indexOf(SettingsController.language)
                        }
                        onActivated: SettingsController.language = currentValue
                    }
                }
            }

            SettingCard {
                title: qsTr("文件关联")

                SettingSwitch {
                    text: qsTr("设为系统默认音频播放器")
                    checked: SettingsController.setAsDefaultPlayer
                    onToggled: SettingsController.setAsDefaultPlayer = checked
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs

                    Text {
                        text: qsTr("关联格式")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeBody
                    }

                    Flow {
                        objectName: "fileAssociationFlow"
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.controlHeight
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
                            objectName: "oggAssociationCheck"
                            text: "OGG"
                            extPrimary: "ogg"
                            checked: SettingsController.fileAssociations.indexOf("ogg") >= 0
                            onToggled: updateAssociation("ogg", checked)
                        }
                    }
                }

                ThemedButton {
                    prominent: true
                    text: qsTr("重新绑定文件关联与图标")
                    onClicked: SettingsController.rebindFileAssociations()
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    component PlaybackSection: ColumnLayout {
        spacing: 6
        Component.onCompleted: PlaybackController.refreshOutputDevices()

        SectionHeader {
            title: qsTr("播放与音频")
            subtitle: qsTr("播放")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 1
            columnSpacing: Theme.spacingMd
            rowSpacing: Theme.spacingMd

            SettingCard {
                title: qsTr("音频输出")

                SettingRow {
                    label: qsTr("输出设备")
                    SettingCombo {
                        objectName: "outputDeviceCombo"
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: {
                            let options = [
                                { text: qsTr("自动 / 系统默认设备"), value: "" }
                            ]
                            for (let i = 0;
                                 i < PlaybackController.outputDevices.length;
                                 ++i) {
                                let name = PlaybackController.outputDevices[i]
                                let id = PlaybackController.outputDeviceIds[i]
                                options.push({ text: name, value: id })
                            }
                            return options
                        }
                        currentIndex: {
                            for (let i = 0; i < valueModel.length; ++i) {
                                if (valueModel[i].value === SettingsController.outputDevice) return i
                            }
                            return 0
                        }
                        onActivated: SettingsController.outputDevice = currentValue
                    }
                }

                ThemedButton {
                    text: qsTr("打开系统默认应用设置")
                    onClicked: SettingsController.openDefaultAppsSettings()
                }

                SettingSwitch {
                    objectName: "exclusiveModeSwitch"
                    text: qsTr("独占模式")
                    checked: SettingsController.exclusiveMode
                    onToggled: SettingsController.exclusiveMode = checked
                }

                SettingSwitch {
                    objectName: "matchTrackSampleRateSwitch"
                    text: qsTr("自动匹配歌曲采样率")
                    checked: SettingsController.matchTrackSampleRate
                    onToggled: SettingsController.matchTrackSampleRate = checked
                }

                Label {
                    objectName: "exclusiveFallbackLabel"
                    visible: SettingsController.exclusiveMode
                             && PlaybackController.state
                                !== PlaybackController.Stopped
                             && !PlaybackController.exclusiveModeActive
                    text: qsTr("独占不可用，当前使用共享模式")
                    color: Theme.warning
                    font.pixelSize: Theme.fontSizeCaption
                    Layout.leftMargin: Theme.spacingMd
                }
            }

            SettingCard {
                title: qsTr("播放行为与响度")

                SettingRow {
                    label: qsTr("默认播放模式")
                    SettingCombo {
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: qsTr("顺序播放"), value: PlaybackController.Sequential },
                            { text: qsTr("随机播放"), value: PlaybackController.Shuffle },
                            { text: qsTr("单曲循环"), value: PlaybackController.RepeatOne },
                            { text: qsTr("列表循环"), value: PlaybackController.RepeatAll }
                        ]
                        currentIndex: {
                            for (let i = 0; i < valueModel.length; ++i) {
                                if (valueModel[i].value === SettingsController.defaultPlaybackMode) {
                                    return i
                                }
                            }
                            return 0
                        }
                        onActivated: SettingsController.defaultPlaybackMode = currentValue
                    }
                }

                SettingRow {
                    label: qsTr("自动切歌淡入淡出")
                    SettingCombo {
                        objectName: "transitionFadeCombo"
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: qsTr("关闭"), value: 0 },
                            { text: qsTr("200 毫秒"), value: 200 },
                            { text: qsTr("500 毫秒"), value: 500 }
                        ]
                        currentIndex: {
                            for (let i = 0; i < valueModel.length; ++i) {
                                if (valueModel[i].value
                                        === SettingsController.transitionFadeMs) {
                                    return i
                                }
                            }
                            return 1
                        }
                        onActivated: SettingsController.transitionFadeMs =
                                         currentValue
                    }
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

                SettingRow {
                    label: qsTr("ReplayGain 响度")
                    SettingCombo {
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: qsTr("关闭（原始电平）"), value: 0 },
                            { text: qsTr("单曲增益"), value: 1 },
                            { text: qsTr("专辑增益"), value: 2 }
                        ]
                        currentIndex: SettingsController.replayGainMode
                        onActivated: SettingsController.replayGainMode = currentValue
                    }
                }

                SettingSwitch {
                    text: qsTr("削波保护")
                    checked: SettingsController.replayGainClipProtection
                    onToggled: SettingsController.replayGainClipProtection = checked
                }

                Label {
                    visible: PlaybackController.replayGainClippingWarning
                    text: qsTr("当前 ReplayGain 增益可能削波，已按设置限制峰值")
                    color: Theme.warning
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    ThemedButton {
                        Layout.fillWidth: true
                        text: ReplayGainScanner.running
                              ? qsTr("正在扫描响度…") : qsTr("扫描当前歌曲响度")
                        enabled: !ReplayGainScanner.running
                                 && PlaybackController.currentTrackId.length > 0
                        onClicked: ReplayGainScanner.scanTrack(
                                       PlaybackController.currentTrackId)
                    }

                    ThemedButton {
                        Layout.fillWidth: true
                        text: ReplayGainScanner.running
                              ? qsTr("响度扫描 %1%").arg(
                                    Math.round(ReplayGainScanner.progress * 100))
                              : qsTr("扫描全部歌曲响度")
                        enabled: !ReplayGainScanner.running && LibraryModel.count > 0
                        onClicked: ReplayGainScanner.scanAll()
                    }
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
            columns: 1
            columnSpacing: Theme.spacingMd
            rowSpacing: Theme.spacingMd

            SettingCard {
                title: qsTr("主题样式")

                SettingRow {
                    label: qsTr("窗口主题")
                    SettingCombo {
                        objectName: "windowLayoutThemeCombo"
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: qsTr("双窗口主题"), value: "dual-window" },
                            { text: qsTr("单窗口主题"), value: "single-window" },
                            { text: qsTr("专业模式"), value: "rolling-player" }
                        ]
                        currentIndex:
                            SettingsController.windowLayoutTheme === "rolling-player"
                            ? 2 : SettingsController.windowLayoutTheme === "single-window"
                                  ? 1 : 0
                        onActivated: SettingsController.windowLayoutTheme = currentValue
                    }
                }

                SettingRow {
                    label: qsTr("主题模式")
                    RowLayout {
                        anchors.fill: parent
                        spacing: Theme.spacingSm

                        Repeater {
                            model: [
                                { text: qsTr("跟随系统"), value: 2,
                                  objectName: "themeModeSystem" },
                                { text: qsTr("浅色"), value: 1,
                                  objectName: "themeModeLight" },
                                { text: qsTr("深色"), value: 0,
                                  objectName: "themeModeDark" }
                            ]

                            delegate: Button {
                                objectName: modelData.objectName
                                text: modelData.text
                                checked: SettingsController.themeMode === modelData.value
                                checkable: true
                                onClicked: SettingsController.themeMode = modelData.value

                                contentItem: Text {
                                    text: parent.text
                                    color: parent.checked ? Theme.accentText : Theme.primaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: Theme.fontSizeBody
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

            }

            SettingCard {
                objectName: "listWaveformSettingsCard"
                title: qsTr("歌曲列表")

                SettingSwitch {
                    objectName: "listWaveformThumbnailEnabledControl"
                    text: qsTr("显示歌曲列表波形缩略图")
                    checked: SettingsController.listWaveformThumbnailEnabled
                    onToggled: SettingsController.listWaveformThumbnailEnabled = checked
                }

                SettingRow {
                    label: qsTr("缩略波形颜色")
                    RowLayout {
                        anchors.fill: parent
                        spacing: Theme.spacingSm

                        SettingCombo {
                            objectName: "listWaveformThumbnailModeControl"
                            layoutManaged: true
                            Layout.preferredWidth: 150
                            enabled: SettingsController.listWaveformThumbnailEnabled
                            valueModel: [
                                { text: qsTr("频彩"), value: "Spectral" },
                                { text: qsTr("纯色"), value: "Mono" }
                            ]
                            currentIndex: SettingsController.listWaveformThumbnailMode
                                          === "Mono" ? 1 : 0
                            onActivated: SettingsController.listWaveformThumbnailMode
                                         = currentValue
                        }

                        Text {
                            text: qsTr("明亮度")
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeCaption
                        }

                        ThemedSlider {
                            id: trackWaveformBrightnessSlider
                            objectName: "trackWaveformBrightnessSlider"
                            Layout.fillWidth: true
                            from: 20
                            to: 100
                            stepSize: 1
                            value: Math.round(
                                       SettingsController.trackWaveformBrightness
                                       * 100)
                            onMoved: SettingsController.trackWaveformBrightness
                                     = value / 100.0
                            Accessible.name: qsTr("歌曲列表波形显示明亮度")
                        }

                        Text {
                            Layout.preferredWidth: 38
                            text: Math.round(trackWaveformBrightnessSlider.value)
                                  + "%"
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }
            }

            SettingCard {
                title: qsTr("波形与频谱颜色")

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
                            mode: 3
                            selected: SettingsController.waveformMode === 3
                            onClicked: SettingsController.waveformMode = 3
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
                        visible: SettingsController.waveformMode !== 2
                    label: qsTr("波形高度")
                    SettingStepper {
                        objectName: "waveformHeightStepper"
                        anchors.verticalCenter: parent.verticalCenter
                        value: SettingsController.waveformHeight
                        minimumValue: 0.3
                        maximumValue: 1.5
                        stepSize: 0.1
                        decimals: 1
                        onValueEdited: nextValue =>
                            SettingsController.waveformHeight = nextValue
                    }
                }

                SettingRow {
                    label: qsTr("波形画布高度")
                    SettingStepper {
                        objectName: "waveformCanvasHeightStepper"
                        anchors.verticalCenter: parent.verticalCenter
                        value: SettingsController.waveformCanvasHeight
                        minimumValue: 48
                        maximumValue: 84
                        stepSize: 2
                        decimals: 0
                        suffix: " px"
                        onValueEdited: nextValue =>
                            SettingsController.waveformCanvasHeight = nextValue
                    }
                }

                SettingSwitch {
                    text: qsTr("锁定波形画布高度")
                    checked: SettingsController.waveformCanvasLocked
                    onToggled: SettingsController.waveformCanvasLocked = checked
                }

                SettingRow {
                    visible: SettingsController.waveformMode !== 2
                    labelObjectName: "waveformDensityRowLabel"
                    fitLabelToContent: true
                    label: qsTr("波形采样密度")
                    SettingStepper {
                        objectName: "waveformDensityStepper"
                        anchors.verticalCenter: parent.verticalCenter
                        value: SettingsController.waveformDensity
                        minimumValue: 0.5
                        maximumValue: 5.0
                        stepSize: 0.5
                        decimals: 1
                        onValueEdited: nextValue =>
                            SettingsController.waveformDensity = nextValue
                    }
                }

                SettingRow {
                    visible: SettingsController.waveformMode !== 2
                    label: qsTr("波形线条粗细")
                    SettingStepper {
                        objectName: "waveformThicknessStepper"
                        anchors.verticalCenter: parent.verticalCenter
                        value: SettingsController.waveformThickness
                        minimumValue: 0.3
                        maximumValue: 3.0
                        stepSize: 0.1
                        decimals: 1
                        suffix: " px"
                        onValueEdited: nextValue =>
                            SettingsController.waveformThickness = nextValue
                    }
                }

                SettingRow {
                    visible: SettingsController.waveformMode !== 2
                    labelObjectName: "waveformAggregationRowLabel"
                    fitLabelToContent: true
                    label: qsTr("波形峰值算法")
                    SettingCombo {
                        objectName: "waveformAggregationCombo"
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: qsTr("平均绝对值"), value: 0 },
                            { text: qsTr("均方根 (RMS)"), value: 1 }
                        ]
                        currentIndex: SettingsController.waveformPeakAlgorithm
                        onActivated: SettingsController.waveformPeakAlgorithm = currentValue
                    }
                }

                SettingRow {
                    visible: SettingsController.waveformMode === 0
                             || SettingsController.waveformMode === 1
                    label: SettingsController.waveformMode === 0
                           ? qsTr("底色 / 进度色")
                           : qsTr("底色 / RGB 渐变")
                    RowLayout {
                        anchors.fill: parent
                        spacing: Theme.spacingSm
                        ColorField { objectName: "waveformSolidBaseColorField"; visible: SettingsController.waveformMode === 0; colorValue: SettingsController.waveformSolidBaseColor; defaultColor: "#9098a6"; targetProperty: "waveformSolidBaseColor" } // theme-color-allow: user-editable waveform default
                        ColorField { objectName: "waveformSolidProgressColorField"; visible: SettingsController.waveformMode === 0; colorValue: SettingsController.waveformSolidProgressColor; defaultColor: "#d27722"; targetProperty: "waveformSolidProgressColor" } // theme-color-allow: user-editable waveform default
                        ColorField { objectName: "waveformRgbBaseColorField"; visible: SettingsController.waveformMode === 1; colorValue: SettingsController.waveformRgbBaseColor; defaultColor: "#00b4a0"; targetProperty: "waveformRgbBaseColor" } // theme-color-allow: user-editable waveform default
                        ColorField { objectName: "waveformRgbStartColorField"; visible: SettingsController.waveformMode === 1; colorValue: SettingsController.waveformRgbStartColor; defaultColor: "#00d4ff"; targetProperty: "waveformRgbStartColor" } // theme-color-allow: user-editable waveform default
                        ColorField { objectName: "waveformRgbMiddleColorField"; visible: SettingsController.waveformMode === 1; colorValue: SettingsController.waveformRgbMiddleColor; defaultColor: "#7b2ff7"; targetProperty: "waveformRgbMiddleColor" } // theme-color-allow: user-editable waveform default
                        ColorField { objectName: "waveformRgbEndColorField"; visible: SettingsController.waveformMode === 1; colorValue: SettingsController.waveformRgbEndColor; defaultColor: "#e62e9b"; targetProperty: "waveformRgbEndColor" } // theme-color-allow: user-editable waveform default
                    }
                }

                SettingRow {
                    visible: SettingsController.waveformMode === 3
                    label: qsTr("频彩调色板")

                    Item {
                        id: frequencyBandPalette
                        objectName: "frequencyBandPalette"
                        anchors.fill: parent
                        readonly property var bandNames: [
                            qsTr("低频红色"), qsTr("中频绿色"), qsTr("高频蓝色")
                        ]
                        readonly property int bandCount: bandNames.length

                        RowLayout {
                            anchors.fill: parent
                            spacing: 4

                            Repeater {
                                model: frequencyBandPalette.bandCount

                                delegate: Item {
                                    required property int index
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true

                                    RowLayout {
                                        anchors.fill: parent
                                        spacing: Theme.spacingSm

                                        Text {
                                            Layout.fillWidth: true
                                            text: frequencyBandPalette.bandNames[index]
                                            color: Theme.secondaryText
                                            font.family: Theme.fontPrimary
                                            font.pixelSize: Theme.fontSizeMeta
                                            horizontalAlignment: Text.AlignRight
                                            elide: Text.ElideRight
                                        }

                                        Rectangle {
                                            id: bandSwatch
                                            Layout.alignment: Qt.AlignHCenter
                                            Layout.preferredWidth: 28
                                            Layout.preferredHeight: 20
                                            radius: 3
                                            color: root.frequencyBandColor(index)
                                            border.color: Theme.borderStrong

                                            TapHandler {
                                                onTapped: {
                                                    if (index === 0)
                                                        frequencyLowColor.openPicker()
                                                    else if (index === 1)
                                                        frequencyMidColor.openPicker()
                                                    else
                                                        frequencyHighColor.openPicker()
                                                }
                                            }
                                            HoverHandler { id: bandHover }
                                            ToolTip.visible: bandHover.hovered
                                            ToolTip.text: qsTr("点击调整对应基础色")
                                        }
                                    }
                                }
                            }
                        }
                    }

                    ColorField {
                        id: frequencyLowColor
                        objectName: "frequencyLowColor"
                        visible: false
                        colorValue: root.frequencyWaveformSettings.lowColor
                        defaultColor: "#ff0000" // theme-color-allow: frequency palette domain
                        targetProperty: ""
                        onColorEdited: function(value) {
                            root.frequencyWaveformSettings.lowColor = value
                        }
                    }
                    ColorField {
                        id: frequencyMidColor
                        objectName: "frequencyMidColor"
                        visible: false
                        colorValue: root.frequencyWaveformSettings.midColor
                        defaultColor: "#00ff00" // theme-color-allow: frequency palette domain
                        targetProperty: ""
                        onColorEdited: function(value) {
                            root.frequencyWaveformSettings.midColor = value
                        }
                    }
                    ColorField {
                        id: frequencyHighColor
                        objectName: "frequencyHighColor"
                        visible: false
                        colorValue: root.frequencyWaveformSettings.highColor
                        defaultColor: "#0000ff" // theme-color-allow: frequency palette domain
                        targetProperty: ""
                        onColorEdited: function(value) {
                            root.frequencyWaveformSettings.highColor = value
                        }
                    }
                }

                SettingRow {
                    visible: SettingsController.waveformMode === 3
                    label: qsTr("未播放区明暗度")

                    RowLayout {
                        anchors.fill: parent
                        spacing: Theme.spacingSm

                        ThemedSlider {
                            id: frequencyUnplayedOpacitySlider
                            objectName: "frequencyUnplayedOpacitySlider"
                            Layout.fillWidth: true
                            from: 0
                            to: 100
                            stepSize: 1
                            value: Math.round(
                                root.frequencyWaveformSettings.unplayedDimness * 100)
                            onMoved: root.frequencyWaveformSettings.unplayedDimness
                                = value / 100.0
                            Accessible.name: qsTr("未播放区明暗度")
                        }

                        Text {
                            Layout.preferredWidth: 38
                            text: Math.round(frequencyUnplayedOpacitySlider.value) + "%"
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeCaption
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }

                ThemedButton {
                    objectName: "frequencyColorResetButton"
                    visible: SettingsController.waveformMode === 3
                    text: qsTr("恢复默认频彩")
                    onClicked: root.frequencyWaveformSettings.resetToDefault()
                }

                SettingRow {
                    visible: SettingsController.waveformMode === 1
                    label: qsTr("RGB 显示区域")
                    SettingCombo {
                        objectName: "waveformRgbRegionSelector"
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: qsTr("已播放区域为 RGB"), value: true },
                            { text: qsTr("未播放区域为 RGB"), value: false }
                        ]
                        currentIndex: SettingsController.waveformRgbProgress ? 0 : 1
                        onActivated: SettingsController.waveformRgbProgress = currentValue
                    }
                }

                SettingSwitch {
                    text: qsTr("鼠标悬停波形时显示时间预览胶囊")
                    checked: SettingsController.waveformHoverTimePreview
                    onToggled: SettingsController.waveformHoverTimePreview = checked
                }

                SettingSwitch {
                    objectName: "waveformPlaybackGuideSwitch"
                    text: qsTr("播放进度竖条")
                    checked: SettingsController.waveformPlaybackGuide
                    onToggled: SettingsController.waveformPlaybackGuide = checked
                }

                SettingRow {
                    visible: SettingsController.waveformMode === 2
                    label: qsTr("频谱颜色")
                    RowLayout {
                        anchors.fill: parent
                        SettingCombo {
                            objectName: "spectrumColorModeSelector"
                            layoutManaged: true
                            valueModel: [
                            { text: qsTr("单色"), value: 0 },
                            { text: qsTr("自定义 RGB"), value: 1 }
                            ]
                            currentIndex: SettingsController.spectrumColorMode
                            onActivated: SettingsController.spectrumColorMode = currentValue
                        }
                        ColorField {
                            objectName: "spectrumSolidColorField"
                            visible: SettingsController.spectrumColorMode === 0
                            colorValue: SettingsController.spectrumSolidColor
                            defaultColor: "#7b2ff7" // theme-color-allow: user-editable waveform default
                            targetProperty: "spectrumSolidColor"
                        }
                        ColorField {
                            objectName: "spectrumRgbStartColorField"
                            visible: SettingsController.spectrumColorMode === 1
                            colorValue: SettingsController.spectrumRgbStartColor
                            defaultColor: "#00d4ff" // theme-color-allow: user-editable waveform default
                            targetProperty: "spectrumRgbStartColor"
                        }
                        ColorField {
                            objectName: "spectrumRgbMiddleColorField"
                            visible: SettingsController.spectrumColorMode === 1
                            colorValue: SettingsController.spectrumRgbMiddleColor
                            defaultColor: "#7b2ff7" // theme-color-allow: user-editable waveform default
                            targetProperty: "spectrumRgbMiddleColor"
                        }
                        ColorField {
                            objectName: "spectrumRgbEndColorField"
                            visible: SettingsController.spectrumColorMode === 1
                            colorValue: SettingsController.spectrumRgbEndColor
                            defaultColor: "#e62e9b" // theme-color-allow: user-editable waveform default
                            targetProperty: "spectrumRgbEndColor"
                        }
                    }
                }

                ThemedButton {
                    objectName: "waveformResetButton"
                    text: qsTr("恢复波形默认")
                    onClicked: SettingsController.resetWaveformDefaults()
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    component AudioToolsSection: ColumnLayout {
        spacing: Theme.spacingSm

        SectionHeader {
            title: qsTr("音频工具预设")
            subtitle: qsTr("导出路径与转码参数")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 1
            columnSpacing: Theme.spacingMd
            rowSpacing: 8

            SettingCard {
                title: qsTr("通用导出")

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

                SettingSwitch {
                    text: qsTr("批量转码时保持原音频元数据")
                    checked: SettingsController.preserveMetadata
                    onToggled: SettingsController.preserveMetadata = checked
                }
            }

            SettingCard {
                title: qsTr("转码输出")

                SettingRow {
                    label: qsTr("格式")
                    SettingCombo {
                        id: transcodeFormatCombo
                        objectName: "transcodeFormatCombo"
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: "MP3", value: "MP3" },
                            { text: "WAV", value: "WAV" },
                            { text: "FLAC", value: "FLAC" },
                            { text: "AAC", value: "AAC" },
                            { text: "Opus", value: "OPUS" },
                            { text: "OGG", value: "OGG" },
                            { text: "ALAC", value: "ALAC" },
                            { text: "AIFF", value: "AIFF" }
                        ]
                        currentIndex: ["MP3", "WAV", "FLAC", "AAC", "OPUS", "OGG", "ALAC", "AIFF"].indexOf(
                                          SettingsController.transcodeFormat)
                        onActivated: SettingsController.transcodeFormat = currentValue
                    }
                }

                SettingRow {
                    label: qsTr("MP3 码率")
                    SettingCombo {
                        objectName: "transcodeBitrateCombo"
                        anchors.verticalCenter: parent.verticalCenter
                        enabled: SettingsController.transcodeFormat === "MP3"
                        opacity: enabled ? 1 : 0.45
                        valueModel: [
                            { text: "128 kbps", value: 128 },
                            { text: "192 kbps", value: 192 },
                            { text: "256 kbps", value: 256 },
                            { text: "320 kbps", value: 320 }
                        ]
                        currentIndex: [128, 192, 256, 320].indexOf(
                                          SettingsController.transcodeBitrateKbps)
                        onActivated: SettingsController.transcodeBitrateKbps = currentValue
                    }
                }

                SettingRow {
                    label: qsTr("采样率")
                    SettingCombo {
                        objectName: "transcodeSampleRateCombo"
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: "44.1 kHz", value: 44100 },
                            { text: "48 kHz", value: 48000 },
                            { text: "88.2 kHz", value: 88200 },
                            { text: "96 kHz", value: 96000 },
                            { text: "176.4 kHz", value: 176400 },
                            { text: "192 kHz", value: 192000 }
                        ]
                        currentIndex: [44100, 48000, 88200, 96000,
                                       176400, 192000].indexOf(
                                          SettingsController.transcodeSampleRateHz)
                        onActivated: SettingsController.transcodeSampleRateHz = currentValue
                    }
                }

                SettingRow {
                    label: qsTr("声道")
                    SettingCombo {
                        objectName: "transcodeChannelCombo"
                        anchors.verticalCenter: parent.verticalCenter
                        valueModel: [
                            { text: qsTr("单声道"), value: 1 },
                            { text: qsTr("立体声"), value: 2 }
                        ]
                        currentIndex: SettingsController.transcodeChannels === 1 ? 0 : 1
                        onActivated: SettingsController.transcodeChannels = currentValue
                    }
                }
            }

            SettingCard {
                title: qsTr("变调与变速预设")

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                SettingSwitch {
                    objectName: "keepPitchPresetSwitch"
                    Layout.fillWidth: true
                    text: qsTr("变速时保持原音高")
                    checked: SettingsController.keepPitchWhileSpeedChange
                    onToggled: SettingsController.keepPitchWhileSpeedChange = checked
                }

                SettingSwitch {
                    objectName: "vocalProtectionPresetSwitch"
                    Layout.fillWidth: true
                    text: qsTr("升降调人声保护")
                    checked: SettingsController.vocalProtection
                    onToggled: SettingsController.vocalProtection = checked
                }
                }
            }
        }
    }

    component HotkeysSection: ColumnLayout {
        spacing: Theme.spacingSm

        SectionHeader {
            title: qsTr("键盘")
            subtitle: qsTr("快捷键")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 1
            columnSpacing: Theme.spacingMd
            rowSpacing: Theme.spacingMd

            SettingCard {
                title: qsTr("全局快捷键")

                Text {
                    Layout.fillWidth: true
                    visible: SettingsController.globalHotkeyError.length > 0
                    text: SettingsController.globalHotkeyError
                    color: Theme.textSecondary
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.Wrap
                }

                HotkeyRow {
                    label: qsTr("播放 / 暂停")
                    value: SettingsController.hkPlayPause
                    globalShortcut: true
                    onCommitted: text => SettingsController.hkPlayPause = text
                }
                HotkeyRow {
                    label: qsTr("上一首 / 下一首")
                    value: SettingsController.hkPrevNext
                    globalShortcut: true
                    onCommitted: text => SettingsController.hkPrevNext = text
                }
                HotkeyRow {
                    label: qsTr("音量加 / 减")
                    value: SettingsController.hkVolumeUpDown
                    globalShortcut: true
                    onCommitted: text => SettingsController.hkVolumeUpDown = text
                }
                HotkeyRow {
                    label: qsTr("显示 / 隐藏迷你播放器")
                    value: SettingsController.hkToggleMiniPlayer
                    globalShortcut: true
                    onCommitted: text => SettingsController.hkToggleMiniPlayer = text
                }
            }

            SettingCard {
                title: qsTr("应用内快捷键")

                HotkeyRow {
                    label: qsTr("快速搜索歌曲")
                    value: SettingsController.hkSearch
                    onCommitted: text => SettingsController.hkSearch = text
                }
                HotkeyRow {
                    label: qsTr("快速切换波形模式")
                    value: SettingsController.hkWaveformMode
                    onCommitted: text => SettingsController.hkWaveformMode = text
                }
                HotkeyRow {
                    label: qsTr("打开音频工具")
                    value: SettingsController.hkAudioTools
                    onCommitted: text => SettingsController.hkAudioTools = text
                }
            }

            SettingCard {
                title: qsTr("专业模式快捷键")

                Text {
                    Layout.fillWidth: true
                    text: qsTr("macOS 的 Option 键在快捷键中显示为 Alt；清空输入可禁用动作。")
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeCaption
                    wrapMode: Text.Wrap
                }

                Repeater {
                    model: [
                        { action: "cue", label: qsTr("CUE 设置 / 预听") },
                        { action: "cueJump", label: qsTr("跳到 CUE") },
                        { action: "cueDelete", label: qsTr("删除 CUE") },
                        { action: "gridOrigin", label: qsTr("设置网格第一拍") },
                        { action: "gridLeft", label: qsTr("网格左移 1 ms") },
                        { action: "gridRight", label: qsTr("网格右移 1 ms") }
                    ]
                    delegate: HotkeyRow {
                        required property var modelData
                        objectName: "rollingShortcut-" + modelData.action
                        label: modelData.label
                        settingsAction: modelData.action
                        value: SettingsController.rollingKeyboardShortcuts[
                                   modelData.action] || ""
                    }
                }

                ThemedButton {
                    objectName: "rollingShortcutResetButton"
                    text: qsTr("恢复专业模式默认快捷键")
                    onClicked: SettingsController.resetRollingKeyboardShortcuts()
                }
            }

            SettingCard {
                title: qsTr("Hot Cue 1–8")

                Repeater {
                    model: 8
                    delegate: HotkeyRow {
                        required property int index
                        readonly property string actionName:
                            "hotCue" + (index + 1)
                        objectName: "rollingShortcut-" + actionName
                        label: qsTr("Hot Cue %1").arg(index + 1)
                        settingsAction: actionName
                        value: SettingsController.rollingKeyboardShortcuts[
                                   actionName] || ""
                    }
                }
            }

            SettingCard {
                title: qsTr("删除 Hot Cue")

                Repeater {
                    model: 8
                    delegate: HotkeyRow {
                        required property int index
                        readonly property string actionName:
                            "hotCueDelete" + (index + 1)
                        objectName: "rollingShortcut-" + actionName
                        label: qsTr("删除 Hot Cue %1").arg(index + 1)
                        settingsAction: actionName
                        value: SettingsController.rollingKeyboardShortcuts[
                                   actionName] || ""
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    component HotkeyRow: RowLayout {
        id: hotkeyRow
        property alias label: labelText.text
        property string value
        property bool globalShortcut: false
        property string settingsAction: ""
        property bool invalidShortcut: false
        signal committed(string text)

        Layout.fillWidth: true
        Layout.preferredHeight: 36
        spacing: Theme.spacingMd

        Text {
            id: labelText
            color: Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeBody
            Layout.preferredWidth: 140
            Layout.alignment: Qt.AlignVCenter
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.background
            radius: Theme.radiusSm
            border.color: parent.invalidShortcut ? Theme.error : Theme.border
            border.width: 1

            TextField {
                id: valueText
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMd
                anchors.rightMargin: Theme.spacingMd
                text: hotkeyRow.value
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeBody
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                selectByMouse: true
                placeholderText: qsTr("按下快捷键")
                background: null
                Keys.onPressed: function(event) {
                    const candidate = root.shortcutText(event)
                    if (!candidate)
                        return
                    event.accepted = true
                    if (settingsAction) {
                        invalidShortcut =
                                !SettingsController.setRollingKeyboardShortcut(
                                    settingsAction, candidate)
                        text = Qt.binding(function() {
                            return hotkeyRow.value
                        })
                        return
                    }
                    invalidShortcut =
                            root.shortcutConflicts(candidate, value)
                            || (globalShortcut
                                && !root.isAllowedGlobalShortcut(candidate))
                    if (!invalidShortcut) {
                        text = candidate
                        committed(candidate)
                        text = Qt.binding(function() {
                            return hotkeyRow.value
                        })
                    }
                }
                onEditingFinished: {
                    const candidate = text.trim()
                    if (settingsAction) {
                        invalidShortcut =
                                !SettingsController.setRollingKeyboardShortcut(
                                    settingsAction, candidate)
                        text = Qt.binding(function() {
                            return hotkeyRow.value
                        })
                        return
                    }
                    invalidShortcut =
                            root.shortcutConflicts(candidate, value)
                            || (globalShortcut
                                && !root.isAllowedGlobalShortcut(candidate))
                    if (!invalidShortcut && candidate)
                        committed(candidate)
                    text = Qt.binding(function() {
                        return hotkeyRow.value
                    })
                }
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
            columns: 1
            columnSpacing: Theme.spacingMd
            rowSpacing: Theme.spacingMd

            SettingCard {
                title: qsTr("路径与自动清理")

                SettingRow {
                    label: qsTr("缓存路径")
                    PathFieldRow {
                        anchors.fill: parent
                        path: SettingsController.cacheDirectory
                        dialogFolder: SettingsController.cacheDirectory
                        onPathSelected: SettingsController.cacheDirectory = newPath
                    }
                }

                SettingRow {
                    label: qsTr("缓存上限")
                    RowLayout {
                        anchors.fill: parent
                        spacing: 8
                        ThemedTextField {
                            objectName: "cacheSizeLimitField"
                            Layout.preferredWidth: 110
                            Layout.preferredHeight: Theme.controlHeight
                            text: (SettingsController.cacheSizeLimitMB / 1024).toFixed(0)
                            horizontalAlignment: Text.AlignRight
                            validator: DoubleValidator { bottom: 0.1; decimals: 1 }
                            onEditingFinished: {
                                const gb = Number(text)
                                if (isFinite(gb) && gb >= 0.1)
                                    SettingsController.cacheSizeLimitMB = Math.round(gb * 1024)
                                else
                                    text = (SettingsController.cacheSizeLimitMB / 1024).toFixed(0)
                            }
                        }
                        Text {
                            text: "GB"
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeBody
                        }
                        Item { Layout.fillWidth: true }
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
                        font.pixelSize: Theme.fontSizeBody
                    }
                }
            }

            SettingCard {
                title: qsTr("清理按钮")

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    ThemedButton {
                        objectName: "clearWaveformCacheButton"
                        Layout.preferredWidth: 90
                        text: qsTr("波形缓存")
                        onClicked: SettingsController.clearWaveformCache()
                    }

                    ThemedButton {
                        objectName: "clearCoverCacheButton"
                        Layout.preferredWidth: 90
                        text: qsTr("封面缓存")
                        onClicked: SettingsController.clearCoverCache()
                    }

                    ThemedButton {
                        objectName: "clearTempCacheButton"
                        Layout.preferredWidth: 108
                        text: qsTr("转码临时文件")
                        onClicked: SettingsController.clearTempFiles()
                    }

                    ThemedButton {
                        objectName: "clearAllCacheButton"
                        Layout.preferredWidth: 90
                        danger: true
                        text: qsTr("全部缓存")
                        onClicked: clearCacheConfirmDialog.open()
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        Item { Layout.fillHeight: true }
    }

    component AboutSection: ColumnLayout {
        id: aboutSection
        spacing: Theme.spacingLg
        readonly property var updateService: SettingsController.updateChecker
        onVisibleChanged: {
            if (visible && root.visible)
                updateService.check()
        }

        SectionHeader {
            title: qsTr("关于")
            subtitle: qsTr("关于")
        }

        SettingCard {
            title: ""
            Layout.fillWidth: true

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
                        objectName: "aboutProductLine"
                        text: "AgPlayer"
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizePageTitle
                        font.weight: Font.Bold
                    }

                    Text {
                        text: qsTr("让音乐·看得见")
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeBody
                        font.weight: Font.Medium
                    }

                    Text {
                        objectName: "aboutStandaloneVersion"
                        text: qsTr("版本号：") + SettingsController.version
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeBody
                    }

                    Text {
                        objectName: "aboutProductPromise"
                        text: qsTr("免费、轻便、纯净")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeBody
                    }
                }

                Item { Layout.fillWidth: true }

                RowLayout {
                    spacing: Theme.spacingSm

                    ThemedButton {
                        Layout.preferredWidth: 120
                        prominent: true
                        text: qsTr("访问官网")
                        onClicked: SettingsController.openOfficialWebsite()
                    }

                }
            }
        }

        SettingCard {
            title: qsTr("软件更新")
            Layout.fillWidth: true
            Text {
                objectName: "aboutUpdateStatus"
                Layout.fillWidth: true
                text: aboutSection.updateService.statusText
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: aboutSection.updateService.updateAvailable ? Theme.accent : Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeBody
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                ThemedButton {
                    objectName: "aboutCheckUpdates"
                    text: aboutSection.updateService.busy ? qsTr("正在检查…") : qsTr("检查更新")
                    enabled: !aboutSection.updateService.busy
                    onClicked: aboutSection.updateService.check()
                }
                ThemedButton {
                    objectName: "aboutDownloadUpdate"
                    prominent: true
                    text: qsTr("前往官网下载")
                    onClicked: SettingsController.openOfficialWebsite()
                }
                Item { Layout.fillWidth: true }
            }
            Text {
                objectName: "aboutUpdateDownloadHelp"
                Layout.fillWidth: true
                text: qsTr("下载由浏览器完成。若下载卡顿、超时或失败，请返回官网切换 GitHub / R2 线路。")
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: Theme.secondaryText
                font.pixelSize: Theme.fontSizeBody
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

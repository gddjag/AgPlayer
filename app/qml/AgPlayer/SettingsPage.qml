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

    background: Rectangle {
        color: Theme.panel
        radius: Theme.radiusLg
        border.color: Theme.border
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: 0

        // Header
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            Layout.leftMargin: Theme.spacingLg
            Layout.rightMargin: Theme.spacingLg

            Text {
                text: qsTr("设置")
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 20
                font.weight: Font.Bold
            }

            Item { Layout.fillWidth: true }

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

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    spacing: Theme.spacingXs

                    Repeater {
                        model: [
                            { text: qsTr("常规"), icon: "\u2699" },
                            { text: qsTr("外观主题"), icon: "\u2728" },
                            { text: qsTr("播放设置"), icon: "\u25B6" },
                            { text: qsTr("波形样式"), icon: "\u223F" },
                            { text: qsTr("音频工具"), icon: "\u2692" },
                            { text: qsTr("快捷键"), icon: "\u2328" },
                            { text: qsTr("缓存管理"), icon: "\u2672" },
                            { text: qsTr("关于"), icon: "\u2139" }
                        ]

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 42
                            radius: Theme.radiusSm
                            color: root.selectedSection === index
                                   ? Qt.rgba(SettingsController.accentColor.r, SettingsController.accentColor.g,
                                             SettingsController.accentColor.b, 0.15)
                                   : (mouseArea.containsMouse ? Theme.border : "transparent")

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spacingMd
                                anchors.rightMargin: Theme.spacingMd
                                spacing: Theme.spacingMd

                                Text {
                                    text: modelData.icon
                                    color: root.selectedSection === index
                                           ? SettingsController.accentColor
                                           : Theme.secondaryText
                                    font.pixelSize: 16
                                    font.family: "Segoe UI Symbol"
                                    Layout.preferredWidth: 24
                                    horizontalAlignment: Text.AlignHCenter
                                }

                                Text {
                                    text: modelData.text
                                    color: root.selectedSection === index
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
                                onClicked: root.selectedSection = index
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.border
            }

            // Content
            StackLayout {
                id: contentStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.selectedSection

                GeneralSection {}
                AppearanceSection {}
                PlaybackSection {}
                WaveformSection {}
                AudioToolsSection {}
                ShortcutsSection {}
                CacheSection {}
                AboutSection {}
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
                text: qsTr("恢复默认")
                focusPolicy: Qt.StrongFocus
                onClicked: SettingsController.resetToDefaults()

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
                          : parent.hovered ? Qt.rgba(1, 1, 1, 0.05)
                          : "transparent"
                    border.color: Theme.border
                    border.width: 1
                    radius: Theme.radiusSm
                    implicitWidth: 110
                    implicitHeight: 36
                }
            }

            Button {
                text: qsTr("应用")
                focusPolicy: Qt.StrongFocus
                onClicked: root.close()

                contentItem: Text {
                    text: parent.text
                    color: "#0A0A0F"
                    font.family: Theme.fontPrimary
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    color: parent.pressed ? Qt.lighter(SettingsController.accentColor, 1.1)
                          : parent.hovered ? Qt.lighter(SettingsController.accentColor, 1.2)
                          : SettingsController.accentColor
                    radius: Theme.radiusSm
                    implicitWidth: 110
                    implicitHeight: 36
                }
            }
        }
    }

    component SettingRow: RowLayout {
        property alias label: labelText.text
        property alias content: contentContainer.children

        Layout.fillWidth: true
        Layout.preferredHeight: 40
        spacing: Theme.spacingMd

        Text {
            id: labelText
            color: Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 14
            Layout.preferredWidth: 120
            Layout.alignment: Qt.AlignVCenter
        }

        Item {
            id: contentContainer
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    component SettingSlider: RowLayout {
        property alias value: slider.value
        property alias from: slider.from
        property alias to: slider.to
        property alias label: labelText.text
        property string suffix
        property int decimals: 0

        Layout.fillWidth: true
        Layout.preferredHeight: 40
        spacing: Theme.spacingMd

        Text {
            id: labelText
            color: Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 14
            Layout.preferredWidth: 120
            Layout.alignment: Qt.AlignVCenter
        }

        Slider {
            id: slider
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }

        Text {
            text: decimals > 0 ? (slider.value.toFixed(decimals) + suffix)
                               : (Math.round(slider.value) + suffix)
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 13
            Layout.preferredWidth: 56
            horizontalAlignment: Text.AlignRight
        }
    }

    component SectionTitle: Text {
        color: Theme.primaryText
        font.family: Theme.fontPrimary
        font.pixelSize: 16
        font.weight: Font.Bold
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingLg
        Layout.bottomMargin: Theme.spacingMd
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

        SectionTitle { text: qsTr("常规") }

        SettingRow {
            label: qsTr("启动时自动播放")
            Switch {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                checked: SettingsController.startupAutoPlay
                onToggled: SettingsController.startupAutoPlay = checked
            }
        }

        SettingRow {
            label: qsTr("启动后最小化")
            Switch {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                checked: SettingsController.minimizeOnStartup
                onToggled: SettingsController.minimizeOnStartup = checked
            }
        }

        SettingRow {
            label: qsTr("关闭窗口类型")
            ComboBox {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 180
                model: [
                    { text: qsTr("退出"), value: 0 },
                    { text: qsTr("最小化到托盘"), value: 1 },
                    { text: qsTr("最小化"), value: 2 }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: SettingsController.closeBehavior
                onActivated: SettingsController.closeBehavior = currentValue
            }
        }

        SettingRow {
            label: qsTr("记住窗口大小与位置")
            Switch {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                checked: SettingsController.rememberWindowState
                onToggled: SettingsController.rememberWindowState = checked
            }
        }

        SettingRow {
            label: qsTr("语言")
            ComboBox {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 180
                model: [
                    { text: qsTr("简体中文"), value: "zh" },
                    { text: qsTr("English"), value: "en" }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: SettingsController.language === "en" ? 1 : 0
                onActivated: SettingsController.language = currentValue
            }
        }

        SettingRow {
            label: qsTr("默认播放器")
            Switch {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                checked: SettingsController.setAsDefaultPlayer
                onToggled: SettingsController.setAsDefaultPlayer = checked
            }
        }

        SettingRow {
            label: qsTr("导出目录")
            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                TextField {
                    id: exportDirField
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: SettingsController.defaultExportDirectory
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 13
                    background: Rectangle {
                        color: Theme.background
                        radius: Theme.radiusSm
                        border.color: Theme.border
                        border.width: 1
                    }
                    onEditingFinished: SettingsController.defaultExportDirectory = text
                }

                ToolButton {
                    icon.source: Theme.icon("folder-open-fill")
                    icon.color: Theme.secondaryText
                    icon.width: 18
                    icon.height: 18
                    onClicked: exportFolderDialog.open()

                    background: Rectangle {
                        color: parent.pressed ? Theme.border
                              : parent.hovered ? Qt.rgba(1, 1, 1, 0.05)
                              : "transparent"
                        radius: Theme.radiusSm
                    }
                }
            }

            FolderDialog {
                id: exportFolderDialog
                currentFolder: SettingsController.defaultExportDirectory
                onAccepted: SettingsController.defaultExportDirectory = selectedFolder.toString().replace("file:///", "")
            }
        }

        Item { Layout.fillHeight: true }
    }

    component AppearanceSection: ColumnLayout {
        spacing: Theme.spacingSm

        SectionTitle { text: qsTr("外观主题") }

        SettingRow {
            label: qsTr("列表页位置")
            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                Repeater {
                    model: [
                        { text: qsTr("下"), value: 0 },
                        { text: qsTr("左"), value: 1 },
                        { text: qsTr("右"), value: 2 },
                        { text: qsTr("上"), value: 3 }
                    ]

                    delegate: Button {
                        text: modelData.text
                        checked: SettingsController.listWindowPosition === modelData.value
                        checkable: true
                        onClicked: SettingsController.listWindowPosition = modelData.value

                        contentItem: Text {
                            text: parent.text
                            color: parent.checked ? "#0A0A0F" : Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            color: parent.checked ? SettingsController.accentColor : "transparent"
                            border.color: parent.checked ? SettingsController.accentColor : Theme.border
                            border.width: 1
                            radius: Theme.radiusSm
                        }
                    }
                }
            }
        }

        SettingRow {
            label: qsTr("主题模式")
            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                Repeater {
                    model: [
                        { text: qsTr("深色"), value: 0 },
                        { text: qsTr("浅色"), value: 1 },
                        { text: qsTr("自动"), value: 2 }
                    ]

                    delegate: Button {
                        text: modelData.text
                        checked: SettingsController.themeMode === modelData.value
                        checkable: true
                        onClicked: SettingsController.themeMode = modelData.value

                        contentItem: Text {
                            text: parent.text
                            color: parent.checked ? "#0A0A0F" : Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            color: parent.checked ? SettingsController.accentColor : "transparent"
                            border.color: parent.checked ? SettingsController.accentColor : Theme.border
                            border.width: 1
                            radius: Theme.radiusSm
                            implicitWidth: 80
                        }
                    }
                }
            }
        }

        SettingRow {
            label: qsTr("主题色")
            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                Repeater {
                    model: [
                        "#00D4FF", "#1688FF", "#7B2FF7", "#E62E9B",
                        "#FF4057", "#FF9800", "#00E676", "#F5F7FA"
                    ]

                    delegate: Rectangle {
                        width: 24
                        height: 24
                        radius: width / 2
                        color: modelData
                        border.color: SettingsController.accentColor.toString().toUpperCase() === modelData.toUpperCase()
                                      ? Theme.primaryText
                                      : "transparent"
                        border.width: 2

                        MouseArea {
                            anchors.fill: parent
                            onClicked: SettingsController.accentColor = modelData
                        }
                    }
                }

                Button {
                    text: qsTr("自定义")
                    onClicked: accentColorDialog.open()

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
                              : parent.hovered ? Qt.rgba(1, 1, 1, 0.05)
                              : "transparent"
                        border.color: Theme.border
                        border.width: 1
                        radius: Theme.radiusSm
                    }
                }
            }

            ColorDialog {
                id: accentColorDialog
                selectedColor: SettingsController.accentColor
                onAccepted: SettingsController.accentColor = selectedColor
            }
        }

        SettingSlider {
            label: qsTr("界面透明度")
            from: 0.5
            to: 1.0
            value: SettingsController.windowTransparency
            decimals: 2
            suffix: ""
            onValueChanged: SettingsController.windowTransparency = value
        }

        SettingSlider {
            label: qsTr("字体透明度")
            from: 0.5
            to: 1.0
            value: SettingsController.fontTransparency
            decimals: 2
            suffix: ""
            onValueChanged: SettingsController.fontTransparency = value
        }

        SettingSlider {
            label: qsTr("圆角强度")
            from: 0
            to: 16
            value: SettingsController.cornerRadius
            decimals: 0
            suffix: ""
            onValueChanged: SettingsController.cornerRadius = Math.round(value)
        }

        Item { Layout.fillHeight: true }
    }

    component PlaybackSection: ColumnLayout {
        spacing: Theme.spacingSm

        SectionTitle { text: qsTr("播放设置") }

        SettingRow {
            label: qsTr("输出设备")
            ComboBox {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 260
                model: [qsTr("Default")]
                currentIndex: model.indexOf(SettingsController.outputDevice)
                onActivated: function(index) {
                    SettingsController.outputDevice = model[index]
                }
            }
        }

        SettingRow {
            label: qsTr("输出格式")
            ComboBox {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 220
                model: [
                    { text: "WASAPI", value: 0 },
                    { text: "DirectSound", value: 1 },
                    { text: "WaveOut", value: 2 }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: SettingsController.outputFormat
                onActivated: SettingsController.outputFormat = currentValue
            }
        }

        SettingRow {
            label: qsTr("自动匹配采样率")
            Switch {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                checked: SettingsController.autoSampleRate
                onToggled: SettingsController.autoSampleRate = checked
            }
        }

        SettingSlider {
            label: qsTr("默认音量")
            from: 0.0
            to: 1.0
            value: SettingsController.defaultVolume
            decimals: 2
            suffix: ""
            onValueChanged: SettingsController.defaultVolume = value
        }

        SettingSlider {
            label: qsTr("淡入时长")
            from: 0
            to: 5000
            value: SettingsController.fadeInDuration
            decimals: 0
            suffix: " ms"
            onValueChanged: SettingsController.fadeInDuration = Math.round(value)
        }

        SettingSlider {
            label: qsTr("淡出时长")
            from: 0
            to: 5000
            value: SettingsController.fadeOutDuration
            decimals: 0
            suffix: " ms"
            onValueChanged: SettingsController.fadeOutDuration = Math.round(value)
        }

        SettingRow {
            label: qsTr("关联文件")
            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingMd

                Repeater {
                    model: ["mp3", "wav", "flac", "aac", "m4a", "ogg", "ape"]

                    delegate: CheckBox {
                        text: modelData.toUpperCase()
                        checked: SettingsController.fileAssociations.indexOf(modelData) >= 0
                        onToggled: {
                            let list = SettingsController.fileAssociations
                            const idx = list.indexOf(modelData)
                            if (checked && idx < 0) {
                                list.push(modelData)
                            } else if (!checked && idx >= 0) {
                                list.splice(idx, 1)
                            }
                            SettingsController.fileAssociations = list
                        }

                        indicator: Rectangle {
                            implicitWidth: 18
                            implicitHeight: 18
                            radius: 4
                            color: parent.checked ? SettingsController.accentColor : "transparent"
                            border.color: parent.checked ? SettingsController.accentColor : Theme.border
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "\u2713"
                                color: "#0A0A0F"
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
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    component WaveformSection: ColumnLayout {
        spacing: Theme.spacingSm

        SectionTitle { text: qsTr("波形样式") }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            color: Qt.rgba(0, 0, 0, 0.2)
            radius: Theme.radiusMd
            border.color: Theme.border
            border.width: 1

            Canvas {
                id: waveformPreview
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)

                    var thickness = SettingsController.waveformThickness
                    var density = SettingsController.waveformDensity
                    var brightness = SettingsController.waveformBrightness
                    var step = Math.max(2, 6 - density)
                    var cx = width / 2
                    var cy = height / 2

                    ctx.globalAlpha = Math.min(1.0, brightness)
                    ctx.lineWidth = thickness
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"

                    if (SettingsController.waveformMode === 1) {
                        // RGB mode: rainbow gradient stroke
                        var grad = ctx.createLinearGradient(0, 0, width, 0)
                        grad.addColorStop(0, "#FF4057")
                        grad.addColorStop(0.33, "#00E676")
                        grad.addColorStop(0.66, "#1688FF")
                        grad.addColorStop(1, "#7B2FF7")
                        ctx.strokeStyle = grad
                    } else if (SettingsController.waveformMode === 2) {
                        // Spectrum mode: bar colors by frequency
                        ctx.fillStyle = SettingsController.waveformColor
                        for (var bx = 0; bx < width; bx += step * 2) {
                            var barHeight = Math.abs(Math.sin(bx * 0.05) * Math.cos(bx * 0.02)) * (height * 0.8)
                            var hue = (bx / width) * 280
                            ctx.fillStyle = "hsl(" + hue + ", 80%, 60%)"
                            ctx.fillRect(bx, cy - barHeight / 2, Math.max(2, step), barHeight)
                        }
                        return
                    } else {
                        ctx.strokeStyle = SettingsController.waveformColor
                    }

                    ctx.beginPath()
                    for (var x = 0; x <= width; x += step) {
                        var amp = Math.sin(x * 0.03) * Math.cos(x * 0.07) * Math.sin(x * 0.01 + 1.0)
                        var y = cy + amp * (height * 0.35)
                        if (x === 0) {
                            ctx.moveTo(x, y)
                        } else {
                            ctx.lineTo(x, y)
                        }
                    }
                    ctx.stroke()
                }

                Connections {
                    target: SettingsController
                    function onWaveformModeChanged() { waveformPreview.requestPaint() }
                    function onWaveformColorChanged() { waveformPreview.requestPaint() }
                    function onWaveformBrightnessChanged() { waveformPreview.requestPaint() }
                    function onWaveformThicknessChanged() { waveformPreview.requestPaint() }
                    function onWaveformDensityChanged() { waveformPreview.requestPaint() }
                }
            }
        }

        SettingRow {
            label: qsTr("波形模式")
            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                Repeater {
                    model: [
                        { text: qsTr("纯色"), value: 0 },
                        { text: qsTr("RGB"), value: 1 },
                        { text: qsTr("频谱"), value: 2 }
                    ]

                    delegate: Button {
                        text: modelData.text
                        checked: SettingsController.waveformMode === modelData.value
                        checkable: true
                        onClicked: SettingsController.waveformMode = modelData.value

                        contentItem: Text {
                            text: parent.text
                            color: parent.checked ? "#0A0A0F" : Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            color: parent.checked ? SettingsController.accentColor : "transparent"
                            border.color: parent.checked ? SettingsController.accentColor : Theme.border
                            border.width: 1
                            radius: Theme.radiusSm
                            implicitWidth: 80
                        }
                    }
                }
            }
        }

        SettingRow {
            label: qsTr("波形颜色")
            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                Repeater {
                    model: [
                        "#00D4FF", "#1688FF", "#00E676", "#7B2FF7",
                        "#E62E9B", "#FF4057", "#FF9800"
                    ]

                    delegate: Rectangle {
                        width: 24
                        height: 24
                        radius: width / 2
                        color: modelData
                        border.color: SettingsController.waveformColor.toString().toUpperCase() === modelData.toUpperCase()
                                      ? Theme.primaryText
                                      : "transparent"
                        border.width: 2

                        MouseArea {
                            anchors.fill: parent
                            onClicked: SettingsController.waveformColor = modelData
                        }
                    }
                }

                Button {
                    text: qsTr("自定义")
                    onClicked: waveformColorDialog.open()

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
                              : parent.hovered ? Qt.rgba(1, 1, 1, 0.05)
                              : "transparent"
                        border.color: Theme.border
                        border.width: 1
                        radius: Theme.radiusSm
                    }
                }
            }

            ColorDialog {
                id: waveformColorDialog
                selectedColor: SettingsController.waveformColor
                onAccepted: SettingsController.waveformColor = selectedColor
            }
        }

        SettingSlider {
            label: qsTr("波形亮度")
            from: 0.5
            to: 2.0
            value: SettingsController.waveformBrightness
            decimals: 2
            suffix: ""
            onValueChanged: SettingsController.waveformBrightness = value
        }

        SettingSlider {
            label: qsTr("波形粗细")
            from: 1
            to: 4
            value: SettingsController.waveformThickness
            decimals: 0
            suffix: ""
            onValueChanged: SettingsController.waveformThickness = Math.round(value)
        }

        SettingSlider {
            label: qsTr("波形密度")
            from: 1
            to: 4
            value: SettingsController.waveformDensity
            decimals: 0
            suffix: ""
            onValueChanged: SettingsController.waveformDensity = Math.round(value)
        }

        Item { Layout.fillHeight: true }
    }

    component AudioToolsSection: ColumnLayout {
        spacing: Theme.spacingSm

        SectionTitle { text: qsTr("音频工具") }

        SettingRow {
            label: qsTr("导出目录")
            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                TextField {
                    id: toolsOutputDirField
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: SettingsController.defaultOutputDirectory
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 13
                    background: Rectangle {
                        color: Theme.background
                        radius: Theme.radiusSm
                        border.color: Theme.border
                        border.width: 1
                    }
                    onEditingFinished: SettingsController.defaultOutputDirectory = text
                }

                ToolButton {
                    icon.source: Theme.icon("folder-open-fill")
                    icon.color: Theme.secondaryText
                    icon.width: 18
                    icon.height: 18
                    onClicked: toolsOutputFolderDialog.open()

                    background: Rectangle {
                        color: parent.pressed ? Theme.border
                              : parent.hovered ? Qt.rgba(1, 1, 1, 0.05)
                              : "transparent"
                        radius: Theme.radiusSm
                    }
                }
            }

            FolderDialog {
                id: toolsOutputFolderDialog
                currentFolder: SettingsController.defaultOutputDirectory
                onAccepted: SettingsController.defaultOutputDirectory = selectedFolder.toString().replace("file:///", "")
            }
        }

        SettingRow {
            label: qsTr("默认格式")
            ComboBox {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 120
                model: ["mp3", "wav", "flac"]
                currentIndex: model.indexOf(SettingsController.defaultOutputFormat)
                onActivated: SettingsController.defaultOutputFormat = currentValue
            }
        }

        SettingRow {
            label: qsTr("默认比特率")
            ComboBox {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 120
                model: [
                    { text: "64 Kbps", value: 64 },
                    { text: "128 Kbps", value: 128 },
                    { text: "192 Kbps", value: 192 },
                    { text: "256 Kbps", value: 256 },
                    { text: "320 Kbps", value: 320 }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: {
                    const v = SettingsController.defaultBitrate
                    return [64, 128, 192, 256, 320].indexOf(v)
                }
                onActivated: SettingsController.defaultBitrate = currentValue
            }
        }

        Item { Layout.fillHeight: true }
    }

    component ShortcutsSection: ColumnLayout {
        spacing: Theme.spacingSm

        SectionTitle { text: qsTr("快捷键") }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            ShortcutRow {
                label: qsTr("播放/暂停")
                value: SettingsController.shortcutPlayPause
                onEditingFinished: SettingsController.shortcutPlayPause = newValue
            }

            ShortcutRow {
                label: qsTr("停止")
                value: SettingsController.shortcutStop
                onEditingFinished: SettingsController.shortcutStop = newValue
            }

            ShortcutRow {
                label: qsTr("下一首")
                value: SettingsController.shortcutNext
                onEditingFinished: SettingsController.shortcutNext = newValue
            }

            ShortcutRow {
                label: qsTr("上一首")
                value: SettingsController.shortcutPrev
                onEditingFinished: SettingsController.shortcutPrev = newValue
            }

            ShortcutRow {
                label: qsTr("音量+")
                value: SettingsController.shortcutVolumeUp
                onEditingFinished: SettingsController.shortcutVolumeUp = newValue
            }

            ShortcutRow {
                label: qsTr("音量-")
                value: SettingsController.shortcutVolumeDown
                onEditingFinished: SettingsController.shortcutVolumeDown = newValue
            }
        }

        Item { Layout.fillHeight: true }
    }

    component ShortcutRow: RowLayout {
        property alias label: labelText.text
        property string value
        signal editingFinished(string newValue)

        Layout.fillWidth: true
        Layout.preferredHeight: 40
        spacing: Theme.spacingMd

        Text {
            id: labelText
            color: Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 14
            Layout.preferredWidth: 100
            Layout.alignment: Qt.AlignVCenter
        }

        TextField {
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: parent.value
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            background: Rectangle {
                color: Theme.background
                radius: Theme.radiusSm
                border.color: Theme.border
                border.width: 1
            }
            onEditingFinished: parent.editingFinished(text)
        }
    }

    component CacheSection: ColumnLayout {
        spacing: Theme.spacingSm

        SectionTitle { text: qsTr("缓存管理") }

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

        SettingSlider {
            label: qsTr("缓存上限")
            from: 100
            to: 10240
            value: SettingsController.cacheSizeLimitMB
            decimals: 0
            suffix: " MB"
            onValueChanged: SettingsController.cacheSizeLimitMB = Math.round(value)
        }

        SettingRow {
            label: qsTr("缓存目录")
            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                TextField {
                    id: cacheDirField
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: SettingsController.cacheDirectory
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 13
                    background: Rectangle {
                        color: Theme.background
                        radius: Theme.radiusSm
                        border.color: Theme.border
                        border.width: 1
                    }
                    onEditingFinished: SettingsController.cacheDirectory = text
                }

                ToolButton {
                    icon.source: Theme.icon("folder-open-fill")
                    icon.color: Theme.secondaryText
                    icon.width: 18
                    icon.height: 18
                    onClicked: cacheFolderDialog.open()

                    background: Rectangle {
                        color: parent.pressed ? Theme.border
                              : parent.hovered ? Qt.rgba(1, 1, 1, 0.05)
                              : "transparent"
                        radius: Theme.radiusSm
                    }
                }
            }

            FolderDialog {
                id: cacheFolderDialog
                currentFolder: SettingsController.cacheDirectory
                onAccepted: SettingsController.cacheDirectory = selectedFolder.toString().replace("file:///", "")
            }
        }

        SettingRow {
            label: qsTr("退出后清理")
            Switch {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                checked: SettingsController.clearCacheOnExit
                onToggled: SettingsController.clearCacheOnExit = checked
            }
        }

        SettingRow {
            label: ""
            Button {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("清理缓存")
                onClicked: SettingsController.clearCache()

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
                          : parent.hovered ? Qt.rgba(1, 1, 1, 0.05)
                          : "transparent"
                    border.color: Theme.border
                    border.width: 1
                    radius: Theme.radiusSm
                    implicitWidth: 120
                    implicitHeight: 36
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    component AboutSection: ColumnLayout {
        spacing: Theme.spacingLg

        SectionTitle { text: qsTr("关于") }

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
                    text: qsTr("版本: ") + SettingsController.version
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 13
                }

                Text {
                    text: qsTr("构建号: ") + SettingsController.buildNumber
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
            }
        }

        SettingRow {
            label: qsTr("启动时检查更新")
            Switch {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                checked: SettingsController.checkUpdatesOnStartup
                onToggled: SettingsController.checkUpdatesOnStartup = checked
            }
        }

        RowLayout {
            Layout.fillWidth: true
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
                          : parent.hovered ? Qt.rgba(1, 1, 1, 0.05)
                          : "transparent"
                    border.color: Theme.border
                    border.width: 1
                    radius: Theme.radiusSm
                    implicitWidth: 120
                    implicitHeight: 36
                }
            }

            Button {
                text: qsTr("官方网站")
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
                          : parent.hovered ? Qt.rgba(1, 1, 1, 0.05)
                          : "transparent"
                    border.color: Theme.border
                    border.width: 1
                    radius: Theme.radiusSm
                    implicitWidth: 120
                    implicitHeight: 36
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}

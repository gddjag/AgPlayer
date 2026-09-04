import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    objectName: "immersiveControlPanel"
    property bool collapsed: false
    property int currentTab: 0
    property string editingColorProperty: ""
    property var featureBands: []
    property real featureEnergy: 0
    property real featureSpectralFlux: 0
    property bool featureKick: false
    property real featureKickEnvelope: 0
    // Keep the panel narrow, but give every tab enough vertical room that its
    // lower controls are not hidden behind a short initial viewport.
    readonly property real expandedHeight: currentTab === 0 ? 700
                                                : currentTab === 1 ? 700 : 760
    signal pointerActivity()

    width: 320
    height: collapsed ? 52 : Math.min(expandedHeight,
                                      parent ? parent.height - 108
                                             : expandedHeight)
    radius: 22
    color: Theme.glassSurfaceElevated
    border.width: 1
    border.color: Theme.glassBorder
    clip: true

    readonly property var presetCards: [
        { "title": qsTr("音域回响"), "sub": qsTr("中央脉冲 · 多彩地形"),
          "from": "#4a2935", "to": "#82724c" }, // theme-color-allow: fixed immersive media preset thumbnail palette
        { "title": qsTr("霓虹雨夜"), "sub": qsTr("青蓝粉紫 · 高动态"),
          "from": "#252044", "to": "#7d2c88" }, // theme-color-allow: fixed immersive media preset thumbnail palette
        { "title": qsTr("水墨"), "sub": qsTr("低饱和 · 轻呼吸"),
          "from": "#2c353a", "to": "#8b7378" }, // theme-color-allow: fixed immersive media preset thumbnail palette
        { "title": qsTr("纯净舞台"), "sub": qsTr("中心聚焦 · 清晰结构"),
          "from": "#173a3b", "to": "#40777c" }, // theme-color-allow: fixed immersive media preset thumbnail palette
        { "title": qsTr("安静"), "sub": qsTr("低响应 · 柔和环境"),
          "from": "#17303c", "to": "#176a7b" }, // theme-color-allow: fixed immersive media preset thumbnail palette
        { "title": qsTr("星河"), "sub": qsTr("深空主题 · 流星冲击"),
          "from": "#442037", "to": "#91356d" }, // theme-color-allow: fixed immersive media preset thumbnail palette
        { "title": qsTr("多源霓虹"), "sub": qsTr("多点涟漪 · 清透流光"),
          "from": "#123a58", "to": "#7c316d" }, // theme-color-allow: fixed immersive media preset thumbnail palette
        { "title": qsTr("深海柔波"), "sub": qsTr("低负载 · 深蓝呼吸"),
          "from": "#082238", "to": "#176b72" }, // theme-color-allow: fixed immersive media preset thumbnail palette
        { "title": qsTr("琥珀电影"), "sub": qsTr("暖金顶光 · 克制旋转"),
          "from": "#351a10", "to": "#9b5c28" } // theme-color-allow: fixed immersive media preset thumbnail palette
    ]
    readonly property var dynamicsGroups: [
        {
            "key": "Terrain", "title": qsTr("地形"),
            "sliders": [
                { "label": qsTr("柱体跳动高度"), "key": "terrainAmplitude", "from": 0, "to": 100 },
                { "label": qsTr("输入压制"), "key": "inputCompression", "from": 20, "to": 150 },
                { "label": qsTr("音频响应"), "key": "audioResponse", "from": 20, "to": 200, "scale": 100, "decimals": 2 },
                { "label": qsTr("响应范围"), "key": "responseRange", "from": 50, "to": 220, "scale": 100, "decimals": 2 },
                { "label": qsTr("主体清晰度"), "key": "subjectClarity", "from": 20, "to": 140 }
            ],
            "effects": []
        },
        {
            "key": "Light", "title": qsTr("光影"),
            "sliders": [
                { "label": qsTr("中心高光"), "key": "centerHighlight", "from": 0, "to": 100, "scale": 100, "decimals": 2 },
                { "label": qsTr("画面景深"), "key": "depthOfField", "from": 0, "to": 150, "scale": 100, "decimals": 2 }
            ],
            "effects": [
                { "label": qsTr("歌曲换色"), "key": "songAdaptiveColorEnabled" },
                { "label": qsTr("流光高亮"), "key": "streamHighlightEnabled" }
            ]
        },
        {
            "key": "Motion", "title": qsTr("运动"),
            "sliders": [
                { "label": qsTr("自动旋转速度"), "key": "autoRotateSpeed", "from": 0, "to": 100, "scale": 100, "decimals": 2 },
                { "label": qsTr("律动灵敏度"), "key": "rhythmSensitivity", "from": 0, "to": 100, "scale": 100, "decimals": 2 }
            ],
            "effects": [
                { "label": qsTr("自动旋转"), "key": "autoRotate" },
                { "label": qsTr("空闲呼吸"), "key": "idleBreathingEnabled" },
                { "label": qsTr("漂浮晶体"), "key": "floatingCubesEnabled" }
            ]
        },
        {
            "key": "Impact", "title": qsTr("冲击"),
            "sliders": [
                { "label": qsTr("律动强度"), "key": "rhythmStrength", "from": 0, "to": 140, "scale": 100, "decimals": 2 }
            ],
            "effects": [
                { "label": qsTr("彩色冲击波"), "key": "ripplesEnabled" },
                { "label": qsTr("星尘喷发"), "key": "burstEnabled" },
                { "label": qsTr("8拍流星"), "key": "meteorsEnabled" }
            ]
        }
    ]

    function setEqGain(index, value) {
        var gains = PlayerExperienceController.visualEqGains.slice()
        gains[index] = Math.round(value)
        PlayerExperienceController.visualEqGains = gains
    }

    function setControllerValue(key, value) {
        PlayerExperienceController[key] = Math.round(value)
    }

    function displayValue(item) {
        var value = Number(PlayerExperienceController[item.key])
        if (item.scale)
            return (value / item.scale).toFixed(item.decimals || 2)
        return Math.round(value).toString()
    }

    function clampFeature(value) {
        return Math.max(0, Math.min(1, Number(value) || 0))
    }

    function band(index) {
        return featureBands && index >= 0 && index < featureBands.length
                ? clampFeature(featureBands[index]) : 0
    }

    function semanticFeatureValue(index) {
        var low = band(0) + band(1) + band(2) + band(3)
        var high = band(5) + band(6) + band(7)
        var tonalTotal = Math.max(0.001, low + high)
        if (index === 0)
            return clampFeature(low / tonalTotal)
        if (index === 1)
            return clampFeature(high / tonalTotal)
        if (index === 2)
            return clampFeature(band(4) * 0.5 + band(5) * 0.3
                                + featureKickEnvelope * 0.2)
        if (index === 3)
            return clampFeature(1.0 - band(6) * 0.35 - band(7) * 0.22
                                + band(3) * 0.16)
        return clampFeature(0.42 + band(6) * 0.3 + band(7) * 0.22
                            + featureKickEnvelope * 0.1)
    }

    onFeatureKickChanged: {
        if (!featureKick)
            return
        kickEnvelopeDecay.stop()
        featureKickEnvelope = 1
        kickEnvelopeDecay.restart()
    }

    NumberAnimation {
        id: kickEnvelopeDecay
        target: root
        property: "featureKickEnvelope"
        to: 0
        duration: 520
        easing.type: Easing.OutCubic
    }

    Behavior on height { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    HoverHandler { onPointChanged: root.pointerActivity() }

    RowLayout {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 16
        anchors.rightMargin: 10
        height: 52

        Column {
            Layout.fillWidth: true
            spacing: 2
            Text {
                text: qsTr("视觉反应控制")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSizeBody
                font.weight: Font.DemiBold
            }
            Text {
                text: "VISUAL REACTOR"
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeCaption
                font.letterSpacing: 1.1
            }
        }
        ToolButton {
            objectName: "immersivePanelCollapseButton"
            implicitWidth: 32
            implicitHeight: 30
            flat: true
            icon.source: Theme.icon(root.collapsed ? "arrow-down-s-line"
                                                    : "arrow-up-s-line")
            icon.color: Theme.iconPrimary
            icon.width: 16
            icon.height: 16
            Accessible.name: root.collapsed ? qsTr("展开视觉设置")
                                             : qsTr("收起视觉设置")
            onClicked: root.collapsed = !root.collapsed
            background: Rectangle {
                radius: 10
                color: parent.hovered ? Theme.subtleGlassHover
                                      : Theme.subtleGlassFill
                border.width: 1
                border.color: Theme.subtleGlassBorder
            }
        }
    }

    Rectangle {
        id: divider
        visible: !root.collapsed
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        height: 1
        color: Theme.glassDivider
    }

    RowLayout {
        id: tabs
        visible: !root.collapsed
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: divider.bottom
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        height: 48
        spacing: 4
        Repeater {
            model: [qsTr("预设"), qsTr("歌词"), qsTr("动态")]
            Button {
                required property int index
                required property string modelData
                objectName: ["immersivePresetTab", "immersiveLyricsTab",
                             "immersiveDynamicsTab"][index]
                Layout.fillWidth: true
                implicitHeight: 30
                text: modelData
                flat: true
                checkable: true
                checked: root.currentTab === index
                font.pixelSize: Theme.fontSizeCaption
                onClicked: root.currentTab = index
                background: Rectangle {
                    radius: 9
                    color: parent.checked ? Theme.selectionGlassFill
                                          : "transparent"
                }
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? Theme.textPrimary
                                          : Theme.textTertiary
                    font: parent.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    ScrollView {
        id: scroll
        objectName: "immersivePanelScroll"
        visible: !root.collapsed
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: 14
        anchors.rightMargin: 10
        anchors.bottomMargin: 12
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Binding {
            target: scroll.contentItem
            property: "contentX"
            value: 0
        }

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 9

            ColumnLayout {
                visible: root.currentTab === 0
                Layout.fillWidth: true
                spacing: 9
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: qsTr("预设与存储"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption }
                    Text { text: "Recent"; color: Theme.textTertiary; font.pixelSize: Theme.fontSizeCaption }
                }
                GridLayout {
                    id: presetGrid
                    Layout.fillWidth: true
                    columns: 3
                    rowSpacing: 7
                    columnSpacing: 7
                    Repeater {
                        model: root.presetCards
                        Button {
                            required property int index
                            required property var modelData
                            objectName: "immersivePresetCard" + index
                            Layout.row: Math.floor(index / 3)
                            Layout.column: index % 3
                            Layout.preferredWidth: 104
                            Layout.minimumWidth: 104
                            Layout.maximumWidth: 104
                            Layout.preferredHeight: 70
                            padding: 0
                            flat: true
                            onClicked: PlayerExperienceController.applyPreset(index)
                            background: Rectangle {
                                radius: 12
                                border.width: 1
                                border.color: parent.hovered ? Theme.borderStrong
                                                             : Theme.glassBorder
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: modelData.from }
                                    GradientStop { position: 1; color: modelData.to }
                                }
                            }
                            contentItem: Column {
                                spacing: 4
                                Text {
                                    width: parent.width
                                    text: modelData.title
                                    color: Theme.onBrandGradientText
                                    font.pixelSize: Theme.fontSizeCaption
                                    font.weight: Font.DemiBold
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    text: modelData.sub
                                    color: Theme.onBrandGradientText
                                    opacity: 0.65
                                    font.pixelSize: Theme.fontSizeCaption
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: qsTr("歌曲自适应配色"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption }
                    ThemedCheckBox {
                        objectName: "songColorToggle"
                        text: qsTr("自动")
                    checked: PlayerExperienceController.songAdaptiveColorEnabled
                    onToggled: PlayerExperienceController.songAdaptiveColorEnabled = checked
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    Repeater {
                        model: ["coolColor", "warmColor", "accentColor", "peakColor"]
                        AbstractButton {
                            id: colorSwatch
                            required property string modelData
                            required property int index
                            objectName: "immersiveColorSwatch" + index
                            Layout.fillWidth: true
                            Layout.preferredHeight: 34
                            hoverEnabled: true
                            Accessible.role: Accessible.Button
                            Accessible.name: qsTr("调整沉浸视觉颜色 %1").arg(index + 1)
                            onClicked: {
                                root.editingColorProperty = modelData
                                immersiveColorPicker.selectedColor =
                                        PlayerExperienceController[modelData]
                                immersiveColorPicker.open()
                            }
                            background: Rectangle {
                                radius: 8
                                color: PlayerExperienceController[colorSwatch.modelData] // theme-color-allow: user-selected immersive media palette swatch
                                border.width: colorSwatch.visualFocus ? 2 : 1
                                border.color: colorSwatch.visualFocus
                                              ? Theme.focus
                                              : Theme.borderStrong
                            }
                        }
                    }
                }

                ColorDialog {
                    id: immersiveColorPicker
                    objectName: "immersiveColorPicker"
                    title: qsTr("选择沉浸视觉颜色")
                    onAccepted: function() {
                        if (root.editingColorProperty.length === 0)
                            return
                        PlayerExperienceController[root.editingColorProperty]
                                = selectedColor.toString()
                        PlayerExperienceController.songAdaptiveColorEnabled = false
                        root.editingColorProperty = ""
                    }
                    onRejected: root.editingColorProperty = ""
                }
                Text { text: qsTr("显示宿主与性能"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: [qsTr("窗口"), qsTr("全屏"), qsTr("桌面")]
                        Button {
                            required property int index
                            required property string modelData
                            Layout.fillWidth: true
                            implicitHeight: 29
                            text: modelData
                            checkable: true
                            checked: PlayerExperienceController.hostMode === index
                            font.pixelSize: Theme.fontSizeCaption
                            onClicked: PlayerExperienceController.hostMode = index
                            background: Rectangle {
                                radius: 8
                                color: parent.checked ? Theme.accentSoft
                                                      : Theme.subtleGlassFill
                                border.width: 1
                                border.color: parent.checked ? Theme.accentBorder
                                                             : Theme.subtleGlassBorder
                            }
                        }
                    }
                }
                ThemedComboBox {
                    objectName: "immersiveQualityCombo"
                    Layout.fillWidth: true
                    implicitHeight: 30
                    model: ["Auto · 自适应", "Eco · 96² / 30 FPS",
                            "Balanced · 128² / 45 FPS", "High · 160² / 60 FPS",
                            "Ultra · 160² / 60 FPS"]
                    currentIndex: PlayerExperienceController.qualityPreset
                    onActivated: PlayerExperienceController.qualityPreset = currentIndex
                }
                ThemedCheckBox {
                    visible: PlayerExperienceController.hostMode === PlayerExperienceController.Desktop
                    text: qsTr("桌面窗口鼠标穿透")
                    checked: PlayerExperienceController.desktopMousePassthrough
                    onToggled: PlayerExperienceController.desktopMousePassthrough = checked
                }
            }

            ColumnLayout {
                visible: root.currentTab === 1
                Layout.fillWidth: true
                spacing: 9
                RowLayout {
                    Layout.fillWidth: true
                    Column {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: qsTr("歌词显示"); color: Theme.textPrimary; font.pixelSize: Theme.fontSizeCaption; font.weight: Font.DemiBold }
                        Text { text: qsTr("三行同步歌词 · 空间纵深"); color: Theme.textTertiary; font.pixelSize: Theme.fontSizeCaption }
                    }
                    ThemedSwitch {
                        objectName: "immersiveLyricsVisibleSwitch"
                        checked: PlayerExperienceController.lyricsVisible
                        onToggled: PlayerExperienceController.lyricsVisible = checked
                    }
                }
                Text { text: qsTr("显示位置"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: [qsTr("左"), qsTr("中"), qsTr("右")]
                        Button {
                            required property int index
                            required property string modelData
                            objectName: "lyricPositionButton" + index
                            Layout.fillWidth: true
                            implicitHeight: 30
                            text: modelData
                            checkable: true
                            checked: PlayerExperienceController.lyricPosition === index
                            onClicked: PlayerExperienceController.lyricPosition = index
                            background: Rectangle {
                                radius: 8
                                color: parent.checked ? Theme.highlightSoft
                                                      : Theme.subtleGlassFill
                                border.width: 1
                                border.color: parent.checked ? Theme.highlightBorder
                                                             : Theme.subtleGlassBorder
                            }
                        }
                    }
                }
                Repeater {
                    model: [
                        { "label": qsTr("水平位置"), "key": "lyricPositionX", "from": 0, "to": 100 },
                        { "label": qsTr("垂直位置"), "key": "lyricPositionY", "from": 0, "to": 100 },
                        { "label": qsTr("歌词大小"), "key": "lyricSize", "from": 60, "to": 140 },
                        { "label": qsTr("文字清晰"), "key": "lyricClarity", "from": 0, "to": 100 },
                        { "label": qsTr("空间纵深"), "key": "lyricDepth", "from": 0, "to": 100 },
                        { "label": qsTr("歌词透明"), "key": "lyricOpacity", "from": 10, "to": 100 }
                    ]
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 7
                        Text { Layout.preferredWidth: 62; text: modelData.label; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption }
                        ThemedSlider {
                            objectName: "lyricSlider_" + modelData.key
                            Layout.fillWidth: true
                            implicitHeight: 20
                            from: modelData.from
                            to: modelData.to
                            value: Number(PlayerExperienceController[modelData.key])
                            onMoved: root.setControllerValue(modelData.key, value)
                        }
                        Text { Layout.preferredWidth: 28; horizontalAlignment: Text.AlignRight; text: Math.round(PlayerExperienceController[modelData.key]); color: Theme.textPrimary; font.pixelSize: Theme.fontSizeCaption }
                    }
                }
            }

            ColumnLayout {
                visible: root.currentTab === 2
                Layout.fillWidth: true
                spacing: 7
                Text { text: qsTr("声音响应"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption }
                Repeater {
                    model: root.dynamicsGroups
                    ColumnLayout {
                        required property var modelData
                        property var groupData: modelData
                        objectName: "dynamics" + groupData.key + "Group"
                        Layout.fillWidth: true
                        spacing: 5
                        Text {
                            text: groupData.title
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.weight: Font.DemiBold
                        }
                        Repeater {
                            model: groupData.sliders
                            RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 7
                                Text { Layout.preferredWidth: 62; text: modelData.label; color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption }
                                ThemedSlider {
                                    objectName: "dynamicSlider_" + modelData.key
                                    Layout.fillWidth: true
                                    implicitHeight: 20
                                    from: modelData.from
                                    to: modelData.to
                                    value: Number(PlayerExperienceController[modelData.key])
                                    onMoved: root.setControllerValue(modelData.key, value)
                                }
                                Text { Layout.preferredWidth: 34; horizontalAlignment: Text.AlignRight; text: root.displayValue(modelData); color: Theme.textPrimary; font.pixelSize: Theme.fontSizeCaption }
                            }
                        }
                        Flow {
                            Layout.fillWidth: true
                            visible: groupData.effects.length > 0
                            spacing: 4
                            Repeater {
                                model: groupData.effects
                                ThemedCheckBox {
                                    required property var modelData
                                    objectName: "effectToggle_" + modelData.key
                                    text: modelData.label
                                    checked: modelData.key === "autoRotate"
                                             ? Number(PlayerExperienceController.autoRotate) > 0
                                             : !!PlayerExperienceController[modelData.key]
                                    onToggled: {
                                        if (modelData.key === "autoRotate")
                                            PlayerExperienceController.autoRotate = checked ? 54 : 0
                                        else
                                            PlayerExperienceController[modelData.key] = checked
                                }
                            }
                            }
                        }
                    }
                }
                Text { text: qsTr("视觉 EQ · 8 音域"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("音乐语义特征")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                    }
                    Text {
                        text: qsTr("自动计算")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontSizeCaption
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    rowSpacing: 6
                    columnSpacing: 6
                    Repeater {
                        model: ["Warmth", "Brightness", "Sharpness",
                                "Smoothness", "Density"]
                        Rectangle {
                            required property int index
                            required property string modelData
                            objectName: "semanticFeature" + index
                            Layout.columnSpan: index === 4 ? 2 : 1
                            Layout.fillWidth: true
                            Layout.preferredHeight: 43
                            radius: 8
                            color: Theme.subtleGlassFill
                            border.width: 1
                            border.color: Theme.subtleGlassBorder

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 7
                                spacing: 3
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeCaption
                                    }
                                    Text {
                                        text: root.semanticFeatureValue(index).toFixed(2)
                                        color: Theme.textTertiary
                                        font.pixelSize: Theme.fontSizeCaption
                                    }
                                }
                                ProgressBar {
                                    id: semanticBar
                                    objectName: "semanticFeatureBar" + index
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 3
                                    from: 0
                                    to: 1
                                    value: root.semanticFeatureValue(index)
                                    background: Rectangle {
                                        implicitHeight: 3
                                        radius: 2
                                        color: Theme.navigatorGlassTrack
                                    }
                                    contentItem: Item {
                                        implicitHeight: 3
                                        Rectangle {
                                            width: parent.width
                                                   * semanticBar.visualPosition
                                            height: parent.height
                                            radius: 2
                                            gradient: Gradient {
                                                orientation: Gradient.Horizontal
                                                GradientStop { position: 0; color: PlayerExperienceController.warmColor }
                                                GradientStop { position: 1; color: PlayerExperienceController.coolColor }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

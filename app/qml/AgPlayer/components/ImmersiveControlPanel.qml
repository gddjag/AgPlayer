import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    objectName: "immersiveControlPanel"
    property bool collapsed: false
    property bool colorPickerOpen: false
    property int currentTab: 0
    property var featureBands: []
    property real featureEnergy: 0
    property real featureSpectralFlux: 0
    property bool featureKick: false
    property real featureKickEnvelope: 0
    // Presets fit their content; the longer tabs retain a bounded scroll area.
    readonly property real expandedHeight: currentTab === 0
                                                ? tabs.y + tabs.height + presetPage.implicitHeight + Theme.spacingMd
                                                : currentTab === 1 ? 700 : 760
    signal pointerActivity()

    width: 320
    height: collapsed ? 52 : Math.min(expandedHeight,
                                      parent ? parent.height - 108
                                             : expandedHeight)
    radius: Theme.radiusLg
    color: Qt.rgba(Theme.contentSurface.r, Theme.contentSurface.g,
                   Theme.contentSurface.b, Theme.isLight ? 0.97 : 0.90)
    border.width: 1
    border.color: Theme.glassBorder
    clip: true

    readonly property var presetCards: PlayerExperienceController.builtInThemeChoices
    readonly property var paletteDefaults: ({
        "coolColor": "#8B4AF0", "warmColor": "#FF469E", // theme-color-allow: default custom media palette, not UI chrome
        "accentColor": "#BE6AFF", "peakColor": "#FF9ACD", // theme-color-allow: default custom media palette, not UI chrome
        "baseColor": "#05020A" // theme-color-allow: default custom media palette, not UI chrome
    })
    readonly property var dynamicsGroups: authoredDynamicsGroups.map(function(group) {
        if (!PlayerExperienceController.themeId.length)
            return {key: group.key, title: group.title, sliders: group.sliders,
                effects: group.effects.filter(function(item) {
                    return item.key !== "songAdaptiveColorEnabled"
                })}
        // Only expose controls consumed by the canonical material/EQ path.
        var supported = ["rippleStrength", "rippleWidth", "rippleDecay",
            "topographyDensity", "terrainAmplitude", "motionResponse",
            "reactorBrightness", "glowIntensity", "autoRotateSpeed",
            "rhythmSensitivity", "floatingBlockMinSize", "floatingBlockMaxSize",
            "floatingBlockSpeed", "floatingBlockIntensity"]
        return {key: group.key, title: group.title,
            sliders: group.sliders.filter(function(item) { return supported.indexOf(item.key) >= 0 }),
            effects: group.effects.filter(function(item) {
                return ["songAdaptiveColorEnabled", "streamHighlightEnabled",
                        "burstEnabled"].indexOf(item.key) < 0
            })}
    })
    readonly property var authoredDynamicsGroups: [
        {
            "key": "Ripple", "title": qsTr("波纹"),
            "sliders": [
                { "label": qsTr("波纹强度"), "key": "rippleStrength", "from": 0, "to": 200 },
                { "label": qsTr("波纹宽度"), "key": "rippleWidth", "from": 20, "to": 200 },
                { "label": qsTr("衰减速度"), "key": "rippleDecay", "from": 20, "to": 200 }
            ],
            "effects": []
        },
        {
            "key": "Terrain", "title": qsTr("柱体与地形"),
            "sliders": [
                PlayerExperienceController.themeId.length > 0
                  ? { "label": qsTr("地形密度"), "key": "topographyDensity", "from": 0, "to": 100, "step": 1 }
                  : { "label": qsTr("柱体数量"), "key": "columnDensity", "from": 50, "to": 200, "step": 5, "suffix": "%" },
                { "label": qsTr("柱体高度"), "key": "terrainAmplitude", "from": 0, "to": 100 },
                { "label": qsTr("起伏速度"), "key": "motionResponse", "from": 0, "to": 100 },
                { "label": qsTr("柱体清晰度"), "key": "subjectClarity", "from": 20, "to": 140 },
                { "label": qsTr("弱音细节"), "key": "inputCompression", "from": 20, "to": 150 },
                { "label": qsTr("音频响应"), "key": "audioResponse", "from": 20, "to": 200, "scale": 100, "decimals": 2 },
                { "label": qsTr("高频细节"), "key": "peakBoost", "from": 0, "to": 100 },
                { "label": qsTr("响应范围"), "key": "responseRange", "from": 50, "to": 220, "scale": 100, "decimals": 2 }
            ],
            "effects": []
        },
        {
            "key": "Light", "title": qsTr("反应堆光影"),
            "sliders": [
                { "label": qsTr("整体亮度"), "key": "reactorBrightness", "from": 0, "to": 200, "scale": 100, "decimals": 2 },
                { "label": qsTr("柱内光芯"), "key": "columnInnerLight", "from": 0, "to": 200, "suffix": "%" },
                { "label": qsTr("照亮周围"), "key": "columnLightSpill", "from": 0, "to": 200, "suffix": "%" },
                { "label": qsTr("照明范围"), "key": "columnLightRadius", "from": 20, "to": 200, "suffix": "%" },
                { "label": qsTr("中心高光"), "key": "centerHighlight", "from": 0, "to": 100, "scale": 100, "decimals": 2 },
                { "label": qsTr("表面流光"), "key": "glowIntensity", "from": 0, "to": 100 },
                { "label": qsTr("远近层次"), "key": "depthOfField", "from": 0, "to": 150, "scale": 100, "decimals": 2 }
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
                { "label": qsTr("镜头冲击"), "key": "cinemaShake", "from": 0, "to": 1.8, "decimals": 2 },
                { "label": qsTr("律动灵敏度"), "key": "rhythmSensitivity", "from": 0, "to": 100, "scale": 100, "decimals": 2 }
            ],
            "effects": [
                { "label": qsTr("自动旋转"), "key": "autoRotate" },
                { "label": qsTr("空闲呼吸"), "key": "idleBreathingEnabled" },
                { "label": qsTr("漂浮晶体"), "key": "floatingCubesEnabled" }
            ]
        },
        {
            "key": "Floating", "title": qsTr("漂浮晶体"),
            "sliders": [
                { "label": qsTr("最小尺寸"), "key": "floatingBlockMinSize", "from": 0, "to": 100, "step": 1 },
                { "label": qsTr("最大尺寸"), "key": "floatingBlockMaxSize", "from": 0, "to": 100, "step": 1 },
                { "label": qsTr("跟随速度"), "key": "floatingBlockSpeed", "from": 0, "to": 100, "step": 1 },
                { "label": qsTr("响应强度"), "key": "floatingBlockIntensity", "from": 0, "to": 100, "step": 1 }
            ],
            "effects": []
        },
        {
            "key": "Impact", "title": qsTr("节奏与冲击"),
            "sliders": [
                { "label": qsTr("节奏强度"), "key": "rhythmStrength", "from": 0, "to": 140, "scale": 100, "decimals": 2 }
            ],
            "effects": [
                { "label": qsTr("彩色冲击波"), "key": "ripplesEnabled" },
                { "label": qsTr("星尘喷发"), "key": "burstEnabled" },
                { "label": qsTr("高频流星"), "key": "meteorsEnabled" }
            ]
        }
    ]

    function setEqGain(index, value) {
        var gains = PlayerExperienceController.visualEqGains.slice()
        // The persisted gain normalizer accepts integers/canonical integer
        // strings, not the Double QVariant produced by JavaScript Math.round.
        gains[index] = String(Math.round(value))
        PlayerExperienceController.visualEqGains = gains
    }

    function setControllerValue(key, value) {
        PlayerExperienceController[key] = key === "cinemaShake"
                ? value : Math.round(value)
    }

    function controlEnabled(key) {
        if (["rippleStrength", "rippleWidth", "rippleDecay"].indexOf(key) >= 0)
            return PlayerExperienceController.ripplesEnabled
        if (key.indexOf("floatingBlock") === 0)
            return PlayerExperienceController.floatingCubesEnabled
        if (key === "autoRotateSpeed")
            return PlayerExperienceController.autoRotate > 0
        if (key.indexOf("lyric") === 0)
            return PlayerExperienceController.lyricsVisible
        return true
    }

    function displayValue(item) {
        var value = Number(PlayerExperienceController[item.key])
        if (item.scale)
            return (value / item.scale).toFixed(item.decimals || 2)
        if (item.decimals)
            return value.toFixed(item.decimals)
        return Math.round(value).toString() + (item.suffix || "")
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
                font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeBody
                font.weight: Font.DemiBold
            }
            Text {
                text: "VISUAL REACTOR"
                color: Theme.textTertiary
                font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption
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
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingMd
        height: 48
        spacing: Theme.spacingXs
        Repeater {
            model: [qsTr("预设"), qsTr("歌词"), qsTr("动态")]
            Button {
                required property int index
                required property string modelData
                objectName: ["immersivePresetTab", "immersiveLyricsTab",
                             "immersiveDynamicsTab"][index]
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                text: modelData
                flat: true
                checkable: true
                checked: root.currentTab === index
                font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption
                onClicked: root.currentTab = index
                background: Rectangle {
                    radius: Theme.radiusSm
                    color: parent.down ? Theme.accentSoft
                         : parent.checked ? Theme.subtleGlassActive
                         : parent.hovered ? Theme.subtleGlassHover : "transparent"
                    border.width: parent.visualFocus ? 2 : 0
                    border.color: Theme.focus
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width - Theme.spacingMd * 2
                        height: 2
                        radius: 1
                        visible: parent.parent.checked
                        color: Theme.accent
                    }
                }
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? Theme.textPrimary
                                          : Theme.textSecondary
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
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingMd
        anchors.bottomMargin: Theme.spacingMd
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
            spacing: Theme.spacingSm

            ColumnLayout {
                id: presetPage
                visible: root.currentTab === 0
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: qsTr("预设与存储"); color: Theme.textSecondary; font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption }
                }
                GridLayout {
                    id: presetGrid
                    Layout.fillWidth: true
                    columns: 3
                    rowSpacing: Theme.spacingSm
                    columnSpacing: Theme.spacingSm
                    Repeater {
                        model: PlayerExperienceController.builtInThemeChoices
                        Button {
                            id: presetCard
                            required property int index
                            required property var modelData
                            readonly property color gradientFrom: modelData.from
                            readonly property color gradientTo: modelData.to
                            readonly property color labelColor: Theme.tagCapsuleFilledText(
                                Qt.rgba((gradientFrom.r + gradientTo.r) / 2,
                                        (gradientFrom.g + gradientTo.g) / 2,
                                        (gradientFrom.b + gradientTo.b) / 2, 1))
                            objectName: "immersivePresetCard" + index
                            Layout.row: Math.floor(index / 3)
                            Layout.column: index % 3
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: (scroll.availableWidth - presetGrid.columnSpacing * 2) / 3
                            Layout.maximumWidth: Layout.preferredWidth
                            Layout.preferredHeight: contentItem.implicitHeight + topPadding + bottomPadding
                            padding: Theme.spacingSm
                            flat: true
                            ToolTip.visible: hovered || visualFocus
                            ToolTip.text: modelData.title
                            onClicked: PlayerExperienceController.applyTheme(modelData.id)
                            background: Rectangle {
                                radius: Theme.radiusSm
                                border.width: parent.down || parent.visualFocus
                                              || PlayerExperienceController.themeId === modelData.id ? 2 : 1
                                border.color: parent.visualFocus ? Theme.focus
                                             : parent.down || PlayerExperienceController.themeId === modelData.id ? Theme.onBrandGradientText
                                             : parent.hovered ? Theme.borderStrong
                                                             : Theme.glassBorder
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: modelData.from }
                                    GradientStop { position: 1; color: modelData.to }
                                }
                            }
                            contentItem: Text {
                                objectName: "immersivePresetTitle" + presetCard.index
                                text: presetCard.modelData.title
                                color: presetCard.labelColor
                                font.family: Theme.fontPrimary
                                font.pixelSize: Theme.fontSizeCaption
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                wrapMode: Text.NoWrap
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("预设自动轮换")
                        color: Theme.textSecondary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeCaption
                    }
                    ThemedCheckBox {
                        objectName: "themeTimedCycleToggle"
                        text: qsTr("定时")
                        checked: PlayerExperienceController.themeCycleEnabled
                        onToggled: PlayerExperienceController.themeCycleEnabled = checked
                    }
                    ThemedCheckBox {
                        objectName: "themeSongCycleToggle"
                        text: qsTr("换歌")
                        checked: PlayerExperienceController.themeSongCycleEnabled
                        onToggled: PlayerExperienceController.themeSongCycleEnabled = checked
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm
                    enabled: PlayerExperienceController.themeCycleEnabled
                    opacity: enabled ? 1 : 0.45
                    Text {
                        text: qsTr("间隔")
                        color: Theme.textTertiary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeCaption
                    }
                    ThemedSlider {
                        objectName: "themeCycleIntervalSlider"
                        Layout.fillWidth: true
                        from: 3
                        to: 120
                        stepSize: 1
                        value: PlayerExperienceController.themeCycleIntervalSeconds
                        onMoved: PlayerExperienceController.themeCycleIntervalSeconds =
                                 Math.round(value)
                    }
                    Text {
                        text: PlayerExperienceController.themeCycleIntervalSeconds
                              + qsTr(" 秒")
                        color: Theme.textSecondary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeCaption
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: qsTr("歌曲自适应配色"); color: Theme.textSecondary; font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption }
                    visible: !PlayerExperienceController.themeId.length
                    ThemedCheckBox {
                        objectName: "songColorToggle"
                        text: qsTr("自动")
                    checked: PlayerExperienceController.songAdaptiveColorEnabled
                    onToggled: PlayerExperienceController.songAdaptiveColorEnabled = checked
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("自定义颜色")
                        color: Theme.textSecondary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeCaption
                    }
                    Button {
                        objectName: "immersiveCustomColorsButton"
                        text: PlayerExperienceController.themeId === "custom"
                              ? qsTr("正在使用") : qsTr("使用自定义")
                        implicitHeight: 28
                        onClicked: PlayerExperienceController.applyCustomColors()
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm
                    Repeater {
                        model: ["coolColor", "warmColor", "accentColor", "peakColor", "baseColor"]
                        ColorField {
                            required property string modelData
                            required property int index
                            objectName: "immersiveColorField" + index
                            Layout.alignment: Qt.AlignHCenter
                            showText: false
                            livePreview: true
                            colorLabel: [qsTr("柱体颜色"), qsTr("鼓点内光"), qsTr("冲击波颜色"),
                                         qsTr("高光边缘"), qsTr("环境颜色")][index]
                            colorValue: PlayerExperienceController.customColors[modelData]
                            defaultColor: root.paletteDefaults[modelData]
                            onPickerVisibleChanged: {
                                root.colorPickerOpen = pickerVisible
                                // Opening selects the independent custom palette;
                                // closing restores its saved value after cancel.
                                PlayerExperienceController.applyCustomColors()
                            }
                            onColorPreviewed: function(value) {
                                PlayerExperienceController.previewCustomColor(modelData, value)
                            }
                            onColorEdited: function(value) {
                                PlayerExperienceController.setCustomColor(modelData, value)
                            }
                        }
                    }
                }
                Text { text: qsTr("显示宿主与性能"); color: Theme.textSecondary; font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm
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
                            font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption
                            onClicked: PlayerExperienceController.hostMode = index
                            contentItem: Text {
                                text: parent.text
                                color: Theme.textPrimary
                                font: parent.font
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
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
                    model: ["Auto · 自适应", "Eco · 30 FPS",
                            "Balanced · 45 FPS", "High · 60 FPS",
                            "Ultra · 60 FPS"]
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
                spacing: Theme.spacingSm
                RowLayout {
                    Layout.fillWidth: true
                    Column {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: qsTr("歌词显示"); color: Theme.textPrimary; font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption; font.weight: Font.DemiBold }
                        Text { text: qsTr("三行同步歌词 · 空间纵深"); color: Theme.textTertiary; font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption }
                    }
                    ThemedSwitch {
                        objectName: "immersiveLyricsVisibleSwitch"
                        checked: PlayerExperienceController.lyricsVisible
                        onToggled: PlayerExperienceController.lyricsVisible = checked
                    }
                }
                Text { text: qsTr("显示位置"); color: Theme.textSecondary; font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm
                    Repeater {
                        model: [qsTr("左"), qsTr("中"), qsTr("右")]
                        ThemedButton {
                            id: positionButton
                            required property int index
                            required property string modelData
                            objectName: "lyricPositionButton" + index
                            Layout.fillWidth: true
                            implicitHeight: 30
                            labelPixelSize: Theme.fontSizeCaption
                            text: modelData
                            checkable: true
                            autoExclusive: true
                            checked: PlayerExperienceController.lyricPosition === index
                            onClicked: PlayerExperienceController.lyricPosition = index
                            background: Rectangle {
                                radius: Theme.radiusSm
                                color: positionButton.down ? Theme.surfacePressed
                                       : positionButton.checked ? Theme.highlightSoft
                                       : positionButton.hovered ? Theme.surfaceHover
                                                                : Theme.subtleGlassFill
                                border.width: positionButton.activeFocus ? 2 : 1
                                border.color: positionButton.activeFocus ? Theme.focus
                                              : positionButton.checked ? Theme.highlightBorder
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
                        { "label": qsTr("空间纵深"), "key": "lyricDepth", "from": 0, "to": 100 },
                        { "label": qsTr("歌词透明"), "key": "lyricOpacity", "from": 10, "to": 100 }
                    ]
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm
                        Text { Layout.preferredWidth: 62; text: modelData.label; color: Theme.textSecondary; font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption }
                        ThemedSlider {
                            objectName: "lyricSlider_" + modelData.key
                            enabled: root.controlEnabled(modelData.key)
                            Layout.fillWidth: true
                            implicitHeight: 20
                            from: modelData.from
                            to: modelData.to
                            value: Number(PlayerExperienceController[modelData.key])
                            onMoved: root.setControllerValue(modelData.key, value)
                        }
                        Text { Layout.preferredWidth: 28; horizontalAlignment: Text.AlignRight; text: Math.round(PlayerExperienceController[modelData.key]); color: Theme.textPrimary; font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption }
                    }
                }
            }

            ColumnLayout {
                visible: root.currentTab === 2
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("声音响应")
                        color: Theme.textSecondary
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeCaption
                    }
                    ThemedButton {
                        objectName: "restoreImmersiveDynamicsDefaults"
                        implicitHeight: 32
                        text: qsTr("还原默认")
                        Accessible.name: text
                        onClicked: PlayerExperienceController.restoreDynamicDefaults()
                    }
                }
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
                            font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption
                            font.weight: Font.DemiBold
                        }
                        Repeater {
                            model: groupData.sliders
                            RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: Theme.spacingSm
                                Text { Layout.preferredWidth: 62; text: modelData.label; color: Theme.textSecondary; font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption }
                                ThemedSlider {
                                    objectName: "dynamicSlider_" + modelData.key
                                    enabled: root.controlEnabled(modelData.key)
                                    Layout.fillWidth: true
                                    implicitHeight: 20
                                    from: modelData.from
                                    to: modelData.to
                                    stepSize: modelData.step || 0
                                    value: Number(PlayerExperienceController[modelData.key])
                                    onMoved: root.setControllerValue(modelData.key, value)
                                }
                                Text { objectName: "dynamicValue_" + modelData.key; Layout.preferredWidth: 34; horizontalAlignment: Text.AlignRight; text: root.displayValue(modelData); color: Theme.textPrimary; font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption }
                                ThemedButton {
                                    objectName: modelData.key === "columnDensity" ? "densityDecrease" : ""
                                    visible: modelData.key === "columnDensity"
                                    Layout.preferredWidth: 24
                                    Layout.preferredHeight: 24
                                    leftPadding: 0; rightPadding: 0
                                    text: "−"
                                    Accessible.name: qsTr("减少柱体数量")
                                    enabled: PlayerExperienceController.columnDensity > 50
                                    onClicked: PlayerExperienceController.columnDensity -= 5
                                }
                                ThemedButton {
                                    objectName: modelData.key === "columnDensity" ? "densityIncrease" : ""
                                    visible: modelData.key === "columnDensity"
                                    Layout.preferredWidth: 24
                                    Layout.preferredHeight: 24
                                    leftPadding: 0; rightPadding: 0
                                    text: "+"
                                    Accessible.name: qsTr("增加柱体数量")
                                    enabled: PlayerExperienceController.columnDensity < 200
                                    onClicked: PlayerExperienceController.columnDensity += 5
                                }
                            }
                        }
                        Text {
                            visible: groupData.key === "Terrain"
                            Layout.fillWidth: true
                            text: qsTr("数量为相对密度，实际柱数受画质与性能预算限制")
                            wrapMode: Text.WordWrap
                            color: Theme.textTertiary
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeCaption
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
                Text {
                    Layout.fillWidth: true
                    text: qsTr("灰色滑条需先开启对应效果；设置即时生效")
                    wrapMode: Text.WordWrap
                    color: Theme.textTertiary
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeCaption
                }
                Text { text: qsTr("视觉 EQ · 8 音域"); color: Theme.textSecondary; font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("仅调视觉响应，不改变音效；频带估计不等于人声分离或乐器识别")
                    wrapMode: Text.WordWrap
                    color: Theme.textTertiary
                    font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption
                }
                Repeater {
                    model: [qsTr("低频 · 整体起伏"), qsTr("低频 · 鼓点区域"),
                            qsTr("中低频"), qsTr("中频 · 人声频段"),
                            qsTr("中高频 · 细节"), qsTr("高频 · 顶面"),
                            qsTr("高频 · 亮片"), qsTr("极高频 · 空气感")]
                    ColumnLayout {
                        required property int index
                        required property string modelData
                        property int bandIndex: index
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: modelData
                                color: Theme.textSecondary
                                font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption
                            }
                            ThemedCheckBox {
                                objectName: "visualEqEnabled_" + bandIndex
                                checked: !!PlayerExperienceController.visualEqEnabled[bandIndex]
                                Accessible.name: modelData + qsTr("启用")
                                onToggled: {
                                    var values = PlayerExperienceController.visualEqEnabled.slice()
                                    values[bandIndex] = checked
                                    PlayerExperienceController.visualEqEnabled = values
                                }
                            }
                            Text {
                                objectName: "visualEqValue_" + bandIndex
                                text: Math.round(Number(PlayerExperienceController.visualEqGains[bandIndex])) + "%"
                                color: Theme.textPrimary
                                font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption
                            }
                        }
                        ThemedSlider {
                            objectName: "visualEqSlider_" + bandIndex
                            enabled: PlayerExperienceController.visualEqEnabled[bandIndex]
                            Layout.fillWidth: true
                            implicitHeight: 20
                            from: 0; to: 100; stepSize: 1
                            value: Number(PlayerExperienceController.visualEqGains[bandIndex])
                            Accessible.name: modelData
                            onMoved: root.setEqGain(bandIndex, value)
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("音乐视觉概览")
                        color: Theme.textSecondary
                        font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption
                    }
                    Text {
                        text: qsTr("频带估计")
                        color: Theme.textTertiary
                        font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption
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
                                        font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption
                                    }
                                    Text {
                                        text: root.semanticFeatureValue(index).toFixed(2)
                                        color: Theme.textTertiary
                                        font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption
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

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    objectName: "videoTransportBar"
    property var playback: PlaybackController
    property bool fullscreen: false
    signal fullscreenRequested()
    signal returnRequested()

    readonly property var speedRatios: [0.75, 1.0, 1.25, 1.5]
    color: Theme.panel
    implicitHeight: controlsColumn.implicitHeight + Theme.spacingMd * 2

    function formatTime(milliseconds) {
        var totalSeconds = Math.max(0, Math.floor(Number(milliseconds) / 1000))
        var hours = Math.floor(totalSeconds / 3600)
        var minutes = Math.floor((totalSeconds % 3600) / 60)
        var seconds = totalSeconds % 60
        var minuteText = (hours > 0 && minutes < 10 ? "0" : "") + minutes
        return (hours > 0 ? hours + ":" : "") + minuteText + ":"
                + (seconds < 10 ? "0" : "") + seconds
    }

    function speedIndex(value) {
        var current = Number(value || 1)
        var bestIndex = 0
        var bestDistance = Number.MAX_VALUE
        for (var index = 0; index < speedRatios.length; ++index) {
            var distance = Math.abs(speedRatios[index] - current)
            if (distance < bestDistance) {
                bestIndex = index
                bestDistance = distance
            }
        }
        return bestIndex
    }

    component VideoActionButton: ToolButton {
        implicitWidth: 40
        implicitHeight: 36
        flat: true
        focusPolicy: Qt.StrongFocus
        icon.color: Theme.iconPrimary
        icon.width: 19
        icon.height: 19
        ToolTip.text: Accessible.name
        ToolTip.visible: hovered
        background: Rectangle {
            color: parent.down ? Theme.surfacePressed
                               : parent.hovered ? Theme.hoverSurface
                                                : "transparent"
            border.width: parent.activeFocus ? 1 : 0
            border.color: Theme.focus
            radius: Theme.radiusSm
        }
    }

    ColumnLayout {
        id: controlsColumn
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Label {
                objectName: "videoCurrentTimeLabel"
                Layout.preferredWidth: 58
                horizontalAlignment: Text.AlignRight
                text: root.formatTime(root.playback ? root.playback.positionMs : 0)
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeCaption
                Accessible.name: qsTr("当前时间 %1").arg(text)
            }

            Slider {
                id: seekSlider
                objectName: "videoSeekSlider"
                Layout.fillWidth: true
                focusPolicy: Qt.StrongFocus
                from: 0
                to: Math.max(1, root.playback ? root.playback.durationMs : 0)
                onMoved: if (root.playback && root.playback.seek !== undefined)
                    root.playback.seek(value)
                Accessible.name: qsTr("视频进度")
                Binding on value {
                    value: root.playback ? root.playback.positionMs : 0
                    when: !seekSlider.pressed
                    restoreMode: Binding.RestoreBindingOrValue
                }
                background: Rectangle {
                    x: seekSlider.leftPadding
                    y: seekSlider.topPadding
                       + seekSlider.availableHeight / 2 - height / 2
                    width: seekSlider.availableWidth
                    height: Theme.sliderTrackHeight
                    radius: height / 2
                    color: Theme.border
                    Rectangle {
                        width: seekSlider.visualPosition * parent.width
                        height: parent.height
                        radius: parent.radius
                        color: Theme.cyan
                    }
                }
                handle: Rectangle {
                    x: seekSlider.leftPadding + seekSlider.visualPosition
                       * (seekSlider.availableWidth - width)
                    y: seekSlider.topPadding
                       + seekSlider.availableHeight / 2 - height / 2
                    width: seekSlider.activeFocus
                           ? Theme.sliderHandleExtent + 2
                           : Theme.sliderHandleExtent
                    height: width
                    radius: width / 2
                    color: Theme.controlHandle
                    border.width: seekSlider.activeFocus ? 2 : 1
                    border.color: seekSlider.activeFocus ? Theme.focus
                                                         : Theme.border
                }
            }

            Label {
                objectName: "videoTotalTimeLabel"
                Layout.preferredWidth: 58
                text: root.formatTime(root.playback ? root.playback.durationMs : 0)
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeCaption
                Accessible.name: qsTr("总时长 %1").arg(text)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: root.width < 720 ? Theme.spacingXs : Theme.spacingSm

            Item { Layout.fillWidth: true }

            TransportControls {
                id: sharedTransport
                objectName: "videoSharedTransport"
                playback: root.playback
                compact: true
                dense: true
                showEqualizer: false
                showWaveformMode: false
                showPlaybackMode: false
                highlightKeyboardFocus: true
            }

            PlayerVolumeControl {
                playback: root.playback
                emptyMode: false
                maximumExpandedWidth: root.width < 860 ? 96 : 152
            }

            ThemedComboBox {
                id: speedControl
                objectName: "videoSpeedControl"
                Layout.minimumWidth: 120
                Layout.preferredWidth: 120
                model: ["0.75×", "1.00×", "1.25×", "1.50×"]
                currentIndex: root.speedIndex(
                                  root.playback ? root.playback.speedRatio : 1)
                Accessible.name: qsTr("播放速度")
                contentItem: Text {
                    leftPadding: Theme.spacingSm
                    rightPadding: 20
                    text: speedControl.displayText
                    color: speedControl.enabled ? Theme.primaryText
                                                : Theme.secondaryText
                    opacity: speedControl.enabled ? 1.0 : 0.55
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeBody
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                onActivated: function(index) {
                    if (root.playback
                            && root.playback.setSpeedRatio !== undefined)
                        root.playback.setSpeedRatio(root.speedRatios[index])
                }
            }

            VideoActionButton {
                objectName: "videoFullscreenButton"
                icon.source: root.fullscreen
                             ? Theme.icon("fullscreen-exit-fill")
                             : Theme.icon("fullscreen-fill")
                Accessible.name: root.fullscreen ? qsTr("退出全屏")
                                                 : qsTr("全屏")
                onClicked: root.fullscreenRequested()
            }

            VideoActionButton {
                objectName: "videoReturnButton"
                icon.source: Theme.icon("arrow-go-back-line")
                Accessible.name: qsTr("返回音频播放器")
                onClicked: root.returnRequested()
            }

            Item { Layout.fillWidth: true }
        }
    }
}

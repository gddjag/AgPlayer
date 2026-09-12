import QtQuick
import QtQuick.Controls
import AgPlayer

Item {
    id: root
    objectName: "videoPlaybackView"
    property var playback: PlaybackController
    // Loading/error state can be observed independently, while VideoFrameItem
    // type-checks the frame authority. Both default to the production singleton.
    property var videoPlayback: VideoPlaybackController
    property var frameController: VideoPlaybackController
    property bool fullscreen: false
    signal fullscreenRequested()
    signal returnRequested()

    Rectangle {
        id: videoSurface
        objectName: "videoSurface"
        anchors.fill: parent
        color: "black"

        VideoFrameItem {
            objectName: "videoFrameItem"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: transportBar.top
            controller: root.frameController
        }

        Label {
            id: statusText
            objectName: "videoStatusText"
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - Theme.spacing2Xl * 2)
            visible: root.videoPlayback
                     && (root.videoPlayback.loading
                         || String(root.videoPlayback.errorMessage || "").length > 0)
            text: root.videoPlayback
                  && String(root.videoPlayback.errorMessage || "").length > 0
                  ? root.videoPlayback.errorMessage : qsTr("正在加载视频画面…")
            color: "white"
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeBody
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            Accessible.name: text
        }

        VideoTransportBar {
            id: transportBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            playback: root.playback
            fullscreen: root.fullscreen
            onFullscreenRequested: root.fullscreenRequested()
            onReturnRequested: root.returnRequested()
        }
    }
}

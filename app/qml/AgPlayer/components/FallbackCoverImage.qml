import QtQuick

Image {
    id: root

    property url requestedSource: ""
    property url fallbackSource:
        "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
    property bool fallbackActive: false
    readonly property bool usingFallback:
        fallbackActive || requestedSource.toString().length === 0

    source: usingFallback ? fallbackSource : requestedSource

    onRequestedSourceChanged: fallbackActive = false
    onStatusChanged: {
        if (status !== Image.Error || fallbackActive
                || requestedSource.toString().length === 0)
            return

        const failedSource = requestedSource.toString()
        Qt.callLater(function() {
            if (requestedSource.toString() === failedSource
                    && status === Image.Error && !fallbackActive)
                fallbackActive = true
        })
    }
}

import QtCore
import QtQuick
import QtTest
import AgPlayer

TestCase {
    name: "EqualizerVisual"
    when: windowShown
    visible: true
    width: 160
    height: 120

    property var equalizer: null
    property bool captureDone: false

    Component {
        id: equalizerComponent
        EqualizerWindow { visible: true }
    }

    function findChildrenByPrefix(parent, prefix) {
        var matches = []
        var children = parent.contentItem ? parent.contentItem.children : parent.children
        if (!children)
            return matches
        for (var i = 0; i < children.length; ++i) {
            var child = children[i]
            if (child.objectName
                    && child.objectName.indexOf(prefix) === 0
                    && /^equalizerBand-\d+$/.test(child.objectName))
                matches.push(child)
            matches = matches.concat(findChildrenByPrefix(child, prefix))
        }
        return matches
    }

    function findChild(parent, objectName) {
        var children = parent.contentItem ? parent.contentItem.children : parent.children
        if (!children)
            return null
        var legacyObjectName = objectName.replace(/^equalizerBand-(\d+)(-.+)$/,
                                                   "eqBandSlider-$1$2")
        for (var i = 0; i < children.length; ++i) {
            var child = children[i]
            if (child.objectName === objectName || child.objectName === legacyObjectName)
                return child
            var match = findChild(child, objectName)
            if (match)
                return match
        }
        return null
    }

    function capture(path, size) {
        captureDone = false
        equalizer.contentItem.grabToImage(function(result) {
            captureDone = result.saveToFile(path)
        }, size)
        tryVerify(function() { return captureDone }, 5000)
    }

    function test_compact_window_contract() {
        equalizer = equalizerComponent.createObject(null)
        verify(equalizer)
        compare(equalizer.width, 860)
        compare(equalizer.height, 520)
        compare(equalizer.minimumWidth, 760)
        compare(equalizer.minimumHeight, 480)
        compare(findChildrenByPrefix(equalizer, "equalizerBand-").length, 18)
        compare(findChild(equalizer, "equalizerTitle").font.pixelSize, 20)
        compare(findChild(equalizer, "equalizerBand-0-frequency").font.pixelSize, 14)
        compare(findChild(equalizer, "equalizerBand-0-value").font.pixelSize, 12)
        equalizer.destroy()
        equalizer = null
    }

    function test_minimum_width_keeps_bands_scrollable() {
        equalizer = equalizerComponent.createObject(null)
        verify(equalizer)
        equalizer.width = 760
        wait(100)
        var bandFlickable = findChild(equalizer, "equalizerBandScroller")
        verify(bandFlickable)
        verify(bandFlickable.contentWidth > bandFlickable.width)
        compare(bandFlickable.interactive, true)
        equalizer.destroy()
        equalizer = null
    }

    function test_reference_and_minimum_viewports_render() {
        var previousThemeMode = SettingsController.themeMode
        SettingsController.themeMode = 0
        equalizer = equalizerComponent.createObject(null)
        verify(equalizer)
        compare(equalizer.width, 860)
        compare(equalizer.height, 520)
        compare(equalizer.minimumWidth, 760)
        compare(equalizer.minimumHeight, 480)

        equalizer.width = 1675
        equalizer.height = 943
        wait(100)
        compare(equalizer.width, 1675)
        compare(equalizer.height, 943)
        var temp = StandardPaths.writableLocation(StandardPaths.TempLocation)
        capture(temp + "/AgPlayer-equalizer-1675x943.png", Qt.size(1675, 943))

        equalizer.width = 1180
        equalizer.height = 680
        wait(100)
        compare(equalizer.width, 1180)
        compare(equalizer.height, 680)
        capture(temp + "/AgPlayer-equalizer-1180x680.png", Qt.size(1180, 680))

        equalizer.width = 760
        equalizer.height = 480
        wait(100)
        compare(equalizer.width, 760)
        compare(equalizer.height, 480)
        capture(temp + "/AgPlayer-equalizer-760x480.png", Qt.size(760, 480))
        equalizer.destroy()
        equalizer = null
        SettingsController.themeMode = previousThemeMode
    }
}

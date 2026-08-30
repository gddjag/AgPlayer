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

    function capture(path, size) {
        captureDone = false
        equalizer.contentItem.grabToImage(function(result) {
            captureDone = result.saveToFile(path)
        }, size)
        tryVerify(function() { return captureDone }, 5000)
    }

    function test_reference_and_minimum_viewports_render() {
        var previousThemeMode = SettingsController.themeMode
        SettingsController.themeMode = 0
        equalizer = equalizerComponent.createObject(null)
        verify(equalizer)

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

        equalizer.width = 960
        equalizer.height = 580
        wait(100)
        compare(equalizer.width, 960)
        compare(equalizer.height, 580)
        capture(temp + "/AgPlayer-equalizer-960x580.png", Qt.size(960, 580))
        equalizer.destroy()
        equalizer = null
        SettingsController.themeMode = previousThemeMode
    }
}

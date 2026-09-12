import QtQuick
import QtTest

TestCase {
    name: "FormatConverterVisualFixture"
    when: windowShown
    visible: true
    width: 1672
    height: 941

    Loader {
        id: fixtureLoader
        anchors.fill: parent
        source: "FormatConverterVisualFixture.qml"
    }

    property bool captureFinished: false

    function test_referenceFixtureLoadsAtExactViewport() {
        tryCompare(fixtureLoader, "status", Loader.Ready, 1000)
        verify(fixtureLoader.item)
        compare(Math.round(fixtureLoader.item.width), 1672)
        compare(Math.round(fixtureLoader.item.height), 941)
        if (visualFixtureOutput.length > 0) {
            fixtureLoader.item.grabToImage(function(result) {
                captureFinished = result.saveToFile(visualFixtureOutput)
            }, Qt.size(1672, 941))
            tryVerify(function() { return captureFinished }, 5000)
        }
    }
}

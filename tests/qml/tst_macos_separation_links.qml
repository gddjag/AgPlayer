import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: suite
    name: "MacSeparationLinks"
    when: windowShown
    width: 1100
    height: 700

    Component {
        id: macPage
        VocalSeparationPage {
            // Select the production platform branch without replacing its
            // controller or changing the host's native platform identity.
            macOS: true
            width: 1100; height: 700
        }
    }
    Component {
        id: windowsPage
        VocalSeparationPage {
            macOS: false
            width: 1100; height: 700
        }
    }

    function test_backupModelInfoIncludesExistingBaiduMirror_data() {
        return [{tag: "macOS", mac: true}, {tag: "Windows", mac: false}]
    }

    function test_backupModelInfoIncludesExistingBaiduMirror(data) {
        const page = createTemporaryObject(data.mac ? macPage : windowsPage, suite)
        verify(page)
        compare(page.macOS, data.mac)
        const info = findChild(page, "separationBackupModelText")
        verify(info)
        verify(info.text.indexOf("https://pan.baidu.com/s/1dTojqRg2QLrB7D9I4dYUcA?pwd=8888") >= 0,
               "The existing Baidu mirror must be available on both platforms")
        verify(info.text.indexOf("提取码: 8888") >= 0)
        if (data.mac) verify(info.text.indexOf("macOS") >= 0, "Unexpected platform help: " + info.text.slice(0, 100))
    }

    function test_macHelpDoesNotAppendWindowsRuntime() {
        const page = createTemporaryObject(macPage, suite)
        verify(page)
        const info = findChild(page, "separationBackupModelText")
        verify(info)
        verify(info.text.indexOf("python-vr-1") < 0,
               "macOS help must not append Windows runtime/python-vr-1 instructions")
        verify(info.text.indexOf("MPS") >= 0)
    }
}

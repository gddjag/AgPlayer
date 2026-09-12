import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Pane {
    id: fixture
    objectName: "formatConverterVisualFixture"
    width: 1672
    height: 941
    padding: 0
    palette.window: Theme.background
    palette.windowText: Theme.primaryText
    palette.base: Theme.elevated
    palette.alternateBase: Theme.panel
    palette.text: Theme.primaryText
    palette.button: Theme.elevated
    palette.buttonText: Theme.primaryText
    palette.highlight: Theme.cyan
    palette.highlightedText: Theme.accentText
    palette.mid: Theme.border
    background: Rectangle {
        color: Theme.background
        border.color: Theme.border
        border.width: 1
        radius: Theme.windowRadius
    }

    // This facade exists only in the QuickTest source tree.  It describes the
    // supplied screenshot state; it never starts, simulates, or reports a
    // conversion.  Production continues to bind FormatConvertPage to the real
    // registered FormatConverter singleton.
    QtObject {
        id: visualConverter

        property string selectedFormat: "mp3"
        property int parallelJobs: 2
        property bool busy: false
        property int fileCount: 24
        property int checkedCount: 12
        property int convertingCount: 3
        property int completedCount: 16
        property int doneCount: 16
        property int failedCount: 1
        property int cancelledCount: 1
        property real progress: 0.66
        property string etaText: "03:42"
        property var files: []
        property var pendingPlan: ({ taskCount: 0, requiresConfirmation: false })
        property var currentCapability: capabilityForFormat(selectedFormat)
        property var outputCapabilities: [
            { key: "mp3", label: "MP3", available: true, reason: "" },
            { key: "flac", label: "FLAC", available: true, reason: "" },
            { key: "wav", label: "WAV", available: true, reason: "" },
            { key: "aac", label: "AAC", available: true, reason: "" },
            { key: "opus", label: "Opus", available: true, reason: "" },
            { key: "ogg", label: "OGG", available: true, reason: "" },
            { key: "alac", label: "ALAC", available: true, reason: "" },
            { key: "aiff", label: "AIFF", available: true, reason: "" }
        ]
        property var filteredTaskModel: visualFormatTaskModel

        signal errorOccurred(string message)

        function capabilityForFormat(format) {
            const losslessRates = [0, 44100, 48000, 88200, 96000, 176400, 192000]
            const source16And24 = [
                { key: "", label: qsTr("原始位深（自动）"), isDefault: true },
                { key: "s16", label: "16-bit PCM", isDefault: false },
                { key: "s24", label: "24-bit PCM", isDefault: false }
            ]
            if (format === "mp3")
                return { encoderLabel: "LAME MP3", parameterKind: "bitrate", supportsCover: true, sampleRateChoices: [0, 44100, 48000], bitRateChoices: [128000, 192000, 256000, 320000], defaultBitRate: 320000, bitrateModes: [{key:"cbr",label:"CBR",default:true},{key:"vbr",label:"VBR",default:false}], qualityChoices: [], bitDepths: [] }
            if (format === "aac")
                return { encoderLabel: "AAC", parameterKind: "bitrate", supportsCover: true, sampleRateChoices: [0, 44100, 48000], bitRateChoices: [96000, 128000, 192000, 256000, 320000], defaultBitRate: 256000, bitrateModes: [{key:"cbr",label:"CBR",default:true},{key:"vbr",label:"VBR",default:false}], qualityChoices: [], bitDepths: [] }
            if (format === "opus")
                return { encoderLabel: "Opus", parameterKind: "bitrate", supportsCover: false, sampleRateChoices: [48000], bitRateChoices: [64000, 96000, 128000, 160000, 192000, 256000, 320000], defaultBitRate: 320000, bitrateModes: [{key:"cbr",label:"CBR",default:true},{key:"vbr",label:"VBR",default:false}], qualityChoices: [], bitDepths: [] }
            if (format === "ogg")
                return { encoderLabel: "Vorbis", parameterKind: "quality", supportsCover: false, sampleRateChoices: [0, 44100, 48000], bitRateChoices: [], qualityChoices: [0,2,4,6,8,10], defaultQuality: 8, bitDepths: [] }
            if (format === "flac")
                return { encoderLabel: "FLAC", parameterKind: "compression", supportsCover: true, sampleRateChoices: losslessRates, bitRateChoices: [], qualityChoices: [0,3,5,8], defaultQuality: 5, bitDepths: source16And24 }
            if (format === "wav")
                return { encoderLabel: "PCM", parameterKind: "none", supportsCover: false, sampleRateChoices: losslessRates, bitRateChoices: [], qualityChoices: [], bitDepths: source16And24.concat([{ key: "s32", label: "32-bit PCM", isDefault: false }, { key: "flt", label: "32-bit Float", isDefault: false }]) }
            if (format === "alac")
                return { encoderLabel: "ALAC", parameterKind: "none", supportsCover: true, sampleRateChoices: losslessRates, bitRateChoices: [], qualityChoices: [], bitDepths: source16And24 }
            return { encoderLabel: "PCM", parameterKind: "none", supportsCover: false, sampleRateChoices: losslessRates, bitRateChoices: [], qualityChoices: [], bitDepths: source16And24.concat([{ key: "s32", label: "32-bit PCM", isDefault: false }]) }
        }

        function formatDuration(milliseconds) {
            const totalSeconds = Math.floor(milliseconds / 1000)
            const minutes = Math.floor(totalSeconds / 60)
            const seconds = totalSeconds % 60
            return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
        }
        function setAllVisibleChecked() {}
        function setTaskChecked() {}
        function removeFile() {}
        function removeChecked() {}
        function clear() {}
        function addUrls() {}
        function addFolder() {}
        function loadFiles() {}
        function retryFailed() {}
        function retryTask(taskId) {}
        function cancelTask() {}
        function cancelAll() {}
        function copyText() {}
        function buildPreflight() {
            return { ready: false, error: "视觉夹具不执行转换" }
        }
        function confirmPendingPlan() {}
        function rejectPendingPlan() {}
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The production title shell is reproduced only around the injected
        // test page so the capture has the source's 48 + 55 px frame.  It has
        // no window operations and is never packaged with the application.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 8
                spacing: 7

                Image {
                    source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 36
                    fillMode: Image.PreserveAspectFit
                }
                Text { text: "Agplayer"; color: Theme.primaryText; font.family: Theme.fontFallback; font.pixelSize: 17; font.weight: Font.Medium }
                Text { text: "·"; color: Theme.secondaryText; font.pixelSize: 14 }
                Text { text: qsTr("音频工具"); color: Theme.primaryText; font.family: Theme.fontPrimary; font.pixelSize: 16 }
                Item { Layout.fillWidth: true }
                ToolButton { Layout.preferredWidth: 32; Layout.preferredHeight: 32; icon.source: Theme.icon("subtract-line") }
                ToolButton { Layout.preferredWidth: 32; Layout.preferredHeight: 32; icon.source: Theme.icon("checkbox-blank-line") }
                ToolButton { Layout.preferredWidth: 32; Layout.preferredHeight: 32; icon.source: Theme.icon("close-fill") }
            }
        }

        ToolSidebar {
            Layout.fillWidth: true
            Layout.preferredHeight: 55
            currentTool: 1
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 2
            Layout.rightMargin: 2
            Layout.bottomMargin: 3
            color: Theme.background

            FormatConvertPage {
                anchors.fill: parent
                converter: visualConverter
                outputDirectory: "D:\\AG Player\\输出\\转换"
            }
        }
    }
}

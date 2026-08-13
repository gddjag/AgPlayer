import QtQuick
import QtTest
import AgPlayer
import "../../plugins/voice-clone/qml" as VoiceClone

TestCase {
    id: testCase
    name: "VoiceClonePluginUi"
    when: windowShown
    visible: true
    width: 1672
    height: 942

    QtObject {
        id: fakeController

        property var models: [
            {
                stableId: "Qwen/Qwen3-TTS-0.6B-Base",
                displayName: "Qwen3-TTS 0.6B Base",
                description: "Base multilingual voice cloning model.",
                provider: "Qwen",
                installState: "ready",
                capabilityPreview: ["voice-clone", "multilingual"],
                licenseName: "Apache-2.0",
                licenseUrl: "https://huggingface.co/Qwen/Qwen3-TTS-0.6B-Base",
                officialProjectUrl: "https://github.com/QwenLM/Qwen3-TTS",
                huggingFaceUrl: "https://huggingface.co/Qwen/Qwen3-TTS-0.6B-Base",
                modelScopeUrl: "https://modelscope.cn/models/Qwen/Qwen3-TTS-0.6B-Base",
                requiresLicenseAcceptance: false
            },
            {
                stableId: "IndexTeam/IndexTTS-2.5",
                displayName: "IndexTTS 2.5",
                description: "Expressive multilingual voice cloning model.",
                provider: "IndexTeam",
                installState: "ready",
                capabilityPreview: ["voice-clone", "expressive"],
                licenseName: "bilibili Model Use License Agreement",
                licenseUrl: "https://huggingface.co/IndexTeam/IndexTTS-2.5",
                officialProjectUrl: "https://github.com/index-tts/index-tts",
                huggingFaceUrl: "https://huggingface.co/IndexTeam/IndexTTS-2.5",
                modelScopeUrl: "https://modelscope.cn/models/IndexTeam/IndexTTS-2.5",
                requiresLicenseAcceptance: true
            },
            {
                stableId: "Qwen/Qwen3-TTS-1.7B-Base",
                displayName: "Qwen3-TTS 1.7B Base",
                description: "Higher-quality multilingual voice cloning model.",
                provider: "Qwen",
                installState: "local-unverified",
                capabilityPreview: ["voice-clone", "higher-quality"],
                licenseName: "Apache-2.0",
                licenseUrl: "https://huggingface.co/Qwen/Qwen3-TTS-1.7B-Base",
                officialProjectUrl: "https://github.com/QwenLM/Qwen3-TTS",
                huggingFaceUrl: "https://huggingface.co/Qwen/Qwen3-TTS-1.7B-Base",
                modelScopeUrl: "https://modelscope.cn/models/Qwen/Qwen3-TTS-1.7B-Base",
                requiresLicenseAcceptance: false
            },
            {
                stableId: "FunAudioLLM/Fun-CosyVoice3-0.5B-2512",
                displayName: "Fun-CosyVoice3 0.5B 2512",
                description: "Multilingual and Chinese dialect voice cloning model.",
                provider: "FunAudioLLM",
                installState: "built-in",
                capabilityPreview: ["voice-clone", "chinese-dialects"],
                licenseName: "Apache-2.0",
                licenseUrl: "https://huggingface.co/FunAudioLLM/Fun-CosyVoice3-0.5B-2512",
                officialProjectUrl: "https://github.com/FunAudioLLM/CosyVoice",
                huggingFaceUrl: "https://huggingface.co/FunAudioLLM/Fun-CosyVoice3-0.5B-2512",
                modelScopeUrl: "https://modelscope.cn/models/FunAudioLLM/Fun-CosyVoice3-0.5B-2512",
                requiresLicenseAcceptance: false
            }
        ]
        property var basicParameters: [
            { key: "normalize", label: "Normalize", description: "Normalize text", type: "bool", group: "basic", default: true },
            { key: "language", label: "Language", description: "Output language", type: "enum", group: "basic", default: "zh", options: ["zh", "en"] },
            { key: "seed", label: "Seed", description: "Random seed", type: "int", group: "basic", default: 7, minimum: 0, maximum: 99, step: 1 },
            { key: "speed", label: "Speed", description: "Speech speed", type: "double", group: "basic", default: 1.0, minimum: 0.5, maximum: 2.0, step: 0.1 },
            { key: "style", label: "Style", description: "Style prompt", type: "string", group: "basic", default: "" },
            { key: "lexicon", label: "Lexicon", description: "Optional lexicon", type: "file", group: "basic", default: "", extensions: ["txt"] }
        ]
        property var advancedParameters: []
        property bool advancedSettingsAvailable: advancedParameters.length > 0
        property bool workerReady: true
        property bool modelLoaded: true
        property string errorString: ""
        property int refreshCalls: 0
        property int openDirectoryCalls: 0
        property int selectCalls: 0
        property int generateCalls: 0
        property int cancelCalls: 0
        property int saveCalls: 0
        property int deleteCalls: 0
        property int sendCalls: 0
        property string lastSelectedId: ""
        property var lastParameters: ({})

        signal generationFinished(string requestId, string outputPath)
        signal requestFailed(string requestId, string code, string message)

        function refreshModels() { ++refreshCalls }
        function openModelDirectory() { ++openDirectoryCalls; return true }
        function selectModel(stableId) { ++selectCalls; lastSelectedId = stableId; return true }
        function generate(text, referencePath, parameters) {
            ++generateCalls
            lastParameters = parameters
            return "request-1"
        }
        function cancel(requestId) { ++cancelCalls; return requestId === "request-1" }
        function saveResult(path, destinationPath) {
            ++saveCalls
            return path !== "" && destinationPath !== ""
        }
        function deleteResult(path) { ++deleteCalls; return path !== "" }
        function sendResultToEditor(path) { ++sendCalls; return path !== "" }
    }

    QtObject {
        id: fakeHost
        property int state: 0
        property string errorString: ""
        property string pluginVersion: ""
        property string availableVersion: ""
        property bool pluginLoaded: false
        property var pluginController: fakeController
        property url mainQmlUrl: Qt.resolvedUrl("../../plugins/voice-clone/qml/VoiceCloneWorkspace.qml")
        property int refreshCalls: 0
        property int openCalls: 0
        function refresh() { ++refreshCalls }
        function openPlugin() { ++openCalls; return pluginLoaded }
    }

    VoiceClone.VoiceCloneWorkspace {
        id: workspace
        anchors.fill: parent
        controller: fakeController
    }

    Component {
        id: hostPageComponent
        VoiceCloneHostPage {
            width: 1200
            height: 700
            hostController: fakeHost
            workspaceUrl: fakeHost.mainQmlUrl
        }
    }

    function init() {
        fakeController.advancedParameters = []
        fakeController.refreshCalls = 0
        fakeController.openDirectoryCalls = 0
        fakeController.selectCalls = 0
        fakeController.generateCalls = 0
        fakeController.cancelCalls = 0
        fakeController.saveCalls = 0
        fakeController.deleteCalls = 0
        fakeController.sendCalls = 0
        fakeController.lastSelectedId = ""
        fakeHost.state = 0
        fakeHost.pluginLoaded = false
        fakeHost.refreshCalls = 0
        fakeHost.openCalls = 0
        workspace.resetUiState()
    }

    function test_stableToolIdSelectsVoiceCloneWithoutChangingLegacyIds() {
        compare(AudioToolsController.toolIdForIndex(0), "audio-editor")
        compare(AudioToolsController.toolIdForIndex(1), "format-converter")
        compare(AudioToolsController.toolIdForIndex(2), "metadata-editor")
        compare(AudioToolsController.toolIdForIndex(3), "filename-processor")
        compare(AudioToolsController.toolIndexForId("voice-clone"), 4)
        AudioToolsController.selectToolById("voice-clone")
        compare(AudioToolsController.currentToolId, "voice-clone")
        compare(AudioToolsController.currentTool, 4)
    }

    function test_hostShowsAbsentStateAndOnlyLoadsValidatedPlugin() {
        const host = createTemporaryObject(hostPageComponent, testCase)
        verify(host)
        const status = findChild(host, "voiceCloneInstallState")
        const loader = findChild(host, "voiceCloneWorkspaceLoader")
        verify(status && status.visible)
        verify(loader)
        compare(loader.item, null)

        fakeHost.state = 3
        fakeHost.pluginLoaded = true
        tryVerify(function() { return loader.item !== null })
        compare(loader.item.controller, fakeController)
    }

    function test_registryCardsExposeModelInfoLinksAndMaintenanceActions() {
        for (let index = 0; index < 4; ++index)
            verify(findChild(workspace, "voiceCloneModelCard" + index))
        compare(workspace.modelCount, 4)
        compare(workspace.selectedModel.description,
                "Base multilingual voice cloning model.")
        compare(workspace.selectedModel.officialProjectUrl,
                "https://github.com/QwenLM/Qwen3-TTS")
        verify(findChild(workspace, "voiceCloneOfficialProjectLink"))
        verify(findChild(workspace, "voiceCloneLicenseLink"))

        mouseClick(findChild(workspace, "voiceCloneRefreshModelsButton"))
        mouseClick(findChild(workspace, "voiceCloneOpenModelDirectoryButton"))
        compare(fakeController.refreshCalls, 1)
        compare(fakeController.openDirectoryCalls, 1)
    }

    function test_schemaDelegatesAllTypesAndOnlyDisclosesLiveAdvancedFields() {
        verify(findChild(workspace, "voiceCloneParameter_bool"))
        verify(findChild(workspace, "voiceCloneParameter_enum"))
        verify(findChild(workspace, "voiceCloneParameter_int"))
        verify(findChild(workspace, "voiceCloneParameter_double"))
        verify(findChild(workspace, "voiceCloneParameter_string"))
        verify(findChild(workspace, "voiceCloneParameter_file"))
        compare(findChild(workspace, "voiceCloneAdvancedButton").visible, false)

        fakeController.advancedParameters = [
            { key: "temperature", label: "Temperature", description: "Sampling temperature", type: "double", group: "advanced", default: 0.8, minimum: 0.1, maximum: 2.0, step: 0.1 },
            { key: "conditionalStyle", label: "Conditional style", description: "Only when normalization is off", type: "string", group: "advanced", default: "", visibleWhen: { key: "normalize", equals: false } }
        ]
        tryCompare(findChild(workspace, "voiceCloneAdvancedButton"), "visible", true)
        mouseClick(findChild(workspace, "voiceCloneAdvancedButton"))
        tryVerify(function() { return findChild(workspace, "voiceCloneAdvancedParameter_temperature") !== null })
        const conditional = findChild(workspace, "voiceCloneAdvancedParameter_conditionalStyle")
        verify(conditional)
        compare(conditional.visible, false)
        const values = Object.assign({}, workspace.parameterValues)
        values.normalize = false
        workspace.parameterValues = values
        tryCompare(conditional, "visible", true)
    }

    function test_indexLicenseGateGenerationCancelAndResultActions() {
        mouseClick(findChild(workspace, "voiceCloneModelCard1"))
        compare(fakeController.lastSelectedId, "IndexTeam/IndexTTS-2.5")
        const gate = findChild(workspace, "voiceCloneLicenseAuthorityCheck")
        verify(gate && gate.visible)
        workspace.referenceAudioPath = "C:/fixtures/reference.wav"
        workspace.cloneText = "测试人声克隆"
        compare(workspace.canGenerate, false)
        mouseClick(gate)
        compare(workspace.canGenerate, true)

        const generateButton = findChild(workspace, "voiceCloneGenerateButton")
        mouseClick(generateButton)
        compare(fakeController.generateCalls, 1)
        compare(workspace.activeRequestId, "request-1")
        mouseClick(findChild(workspace, "voiceCloneCancelButton"))
        compare(fakeController.cancelCalls, 1)
        compare(workspace.activeRequestId, "")

        const resultPanel = findChild(workspace, "voiceCloneResultPanel")
        compare(resultPanel.hasResult, false)
        mouseClick(generateButton)
        fakeController.generationFinished("request-1", "C:/results/generated.wav")
        tryCompare(resultPanel, "hasResult", true)
        wait(20)
        mouseClick(findChild(workspace, "voiceCloneSaveResultButton"))
        const saveDialog = findChild(workspace, "voiceCloneSaveDialog")
        verify(saveDialog)
        tryCompare(saveDialog, "visible", true)
        resultPanel.saveTo("C:/results/saved.wav")
        mouseClick(findChild(workspace, "voiceCloneSendToEditorButton"))
        mouseClick(findChild(workspace, "voiceCloneDeleteResultButton"))
        compare(fakeController.saveCalls, 1)
        compare(fakeController.sendCalls, 1)
        compare(fakeController.deleteCalls, 1)
        compare(resultPanel.hasResult, false)
    }
}

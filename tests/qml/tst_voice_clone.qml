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
        property var initialBasicParameters: basicParameters
        property bool advancedSettingsAvailable: advancedParameters.length > 0
        property bool workerReady: true
        property bool modelLoaded: true
        property string errorString: ""
        property string activationState: "ready"
        property string activationMessage: "Model ready"
        property bool licenseAcceptanceRequired: false
        property string currentLicenseName: ""
        property url currentLicenseUrl: ""
        property string currentLicenseRevision: ""
        property var currentLicenseRequirements: []
        property string downloadModelId: ""
        property string downloadState: "idle"
        property string downloadError: ""
        property int downloadProgressPercent: -1
        property bool downloadInProgress: false
        property bool licenseIdentityValid: true
        property int refreshCalls: 0
        property int openDirectoryCalls: 0
        property int selectCalls: 0
        property int generateCalls: 0
        property int cancelCalls: 0
        property int saveCalls: 0
        property int deleteCalls: 0
        property int acceptLicenseCalls: 0
        property int pauseDownloadCalls: 0
        property int resumeDownloadCalls: 0
        property int cancelDownloadCalls: 0
        property int retryDownloadCalls: 0
        property string lastSelectedId: ""
        property var lastParameters: ({})

        signal generationFinished(string requestId, string outputPath)
        signal requestFailed(string requestId, string code, string message)
        signal capabilitiesChanged()
        signal activationChanged()
        signal licenseChanged()

        function refreshModels() { ++refreshCalls }
        function openModelDirectory() { ++openDirectoryCalls; return true }
        function pauseDownload() { ++pauseDownloadCalls; return true }
        function resumeDownload() { ++resumeDownloadCalls; return true }
        function cancelDownload() { ++cancelDownloadCalls; return true }
        function retryDownload() { ++retryDownloadCalls; return true }
        function activateModel(stableId) {
            ++selectCalls
            lastSelectedId = stableId
            licenseAcceptanceRequired = stableId === "IndexTeam/IndexTTS-2.5"
            currentLicenseName = licenseAcceptanceRequired
                    ? "bilibili Model Use License Agreement" : "Apache-2.0"
            currentLicenseUrl = licenseAcceptanceRequired
                    ? "https://huggingface.co/IndexTeam/IndexTTS-2.5" : ""
            currentLicenseRevision = licenseAcceptanceRequired
                    ? "c39ce5ba981572cb187443877ff559dfb246ce63" : ""
            currentLicenseRequirements = licenseAcceptanceRequired ? [
                { id: "bilibili-model-use-license", name: "bilibili Model Use License Agreement", url: "https://huggingface.co/IndexTeam/IndexTTS-2.5/blob/c39ce5ba981572cb187443877ff559dfb246ce63/LICENSE", revision: "c39ce5ba981572cb187443877ff559dfb246ce63", spdx: "LicenseRef-Bilibili-Model-Use", useRestriction: "custom-terms" },
                { id: "maskgct-cc-by-nc-4.0", name: "CC-BY-NC-4.0", url: "https://huggingface.co/amphion/MaskGCT/blob/265c6cef07625665d0c28d2faafb1415562379dc/README.md", revision: "265c6cef07625665d0c28d2faafb1415562379dc", spdx: "CC-BY-NC-4.0", useRestriction: "non-commercial-only" }
            ] : []
            return true
        }
        function acceptSelectedLicense() {
            ++acceptLicenseCalls
            if (!licenseIdentityValid) return false
            licenseAcceptanceRequired = false
            return true
        }
        function acceptSelectedLicenses(ids) {
            ++acceptLicenseCalls
            if (!licenseIdentityValid || ids.length !== 2) return false
            licenseAcceptanceRequired = false
            return true
        }
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
        function resultFileUrl(path) {
            return path === specialAudioFixturePath() ? testSpecialAudioUrl : testAudioUrl
        }
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

    Component {
        id: realHostPageComponent
        VoiceCloneHostPage {
            width: 1200
            height: 700
            hostController: realVoiceCloneHost
            workspaceUrl: realVoiceCloneHost.mainQmlUrl
        }
    }

    Component {
        id: activationSpyComponent
        SignalSpy { signalName: "activationChanged" }
    }

    function init() {
        fakeController.basicParameters = fakeController.initialBasicParameters
        fakeController.advancedParameters = []
        fakeController.refreshCalls = 0
        fakeController.openDirectoryCalls = 0
        fakeController.selectCalls = 0
        fakeController.generateCalls = 0
        fakeController.cancelCalls = 0
        fakeController.saveCalls = 0
        fakeController.deleteCalls = 0
        fakeController.acceptLicenseCalls = 0
        fakeController.pauseDownloadCalls = 0
        fakeController.resumeDownloadCalls = 0
        fakeController.cancelDownloadCalls = 0
        fakeController.retryDownloadCalls = 0
        fakeController.downloadModelId = ""
        fakeController.downloadState = "idle"
        fakeController.downloadError = ""
        fakeController.downloadProgressPercent = -1
        fakeController.downloadInProgress = false
        fakeController.licenseAcceptanceRequired = false
        fakeController.licenseIdentityValid = true
        fakeController.errorString = ""
        fakeController.activationState = "ready"
        fakeController.activationMessage = "Model ready"
        fakeController.lastSelectedId = ""
        fakeHost.state = 0
        fakeHost.pluginLoaded = false
        fakeHost.refreshCalls = 0
        fakeHost.openCalls = 0
        workspace.resetUiState()
    }

    function audioFixturePath() {
        return decodeURIComponent(testAudioUrl.toString().replace(/^file:\/\/\//, ""))
    }

    function specialAudioFixturePath() {
        return testSpecialAudioUrl.toLocalFile
                ? testSpecialAudioUrl.toLocalFile()
                : decodeURIComponent(testSpecialAudioUrl.toString().replace(/^file:\/\/\//, ""))
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

    function test_smallViewportKeepsAllSectionsReachable() {
        testCase.width = 1280
        testCase.height = 720
        wait(0)
        const scroll = findChild(workspace, "voiceCloneWorkspaceScroll")
        verify(scroll)
        verify(scroll.contentHeight > scroll.availableHeight)
        const verticalBar = findChild(workspace, "voiceCloneWorkspaceVerticalScrollBar")
        verify(verticalBar.visible)
        verify(verticalBar.x + verticalBar.width >= workspace.width - 12)
        verify(findChild(workspace, "voiceCloneResultPanel"))
        testCase.width = 1672
        testCase.height = 942
    }

    function test_needsDownloadStateUsesLocalizedNonDuplicatedCopy() {
        fakeController.activationState = "needs-download"
        fakeController.errorString = "Model files are not installed or not ready; download them before activation"
        wait(0)
        const output = findChild(workspace, "voiceCloneOutputPanel")
        verify(output)
        compare(output.statusText, "模型或运行时未就绪，需要下载")
        compare(output.errorText, "")
        compare(findChild(workspace, "voiceCloneModelInstallState3").text,
                "FunAudioLLM  ·  未下载")
    }

    function test_modelCapabilitiesUseChineseProductCopy() {
        compare(findChild(workspace, "voiceCloneCapabilitySummary").text,
                "人声克隆  ·  多语言")
    }

    function test_realPluginHostLoadsWorkspaceAndClickedModelBecomesReady() {
        compare(realVoiceCloneStageError, "")
        verify(realVoiceCloneHost)
        realVoiceCloneHost.refresh()
        compare(realVoiceCloneHost.state, 2)
        verify(realVoiceCloneHost.openPlugin())
        compare(realVoiceCloneHost.pluginLoaded, true)
        verify(realVoiceCloneHost.mainQmlUrl.toString().indexOf("qrc:/AgPlayer/VoiceClone/") === 0)

        const host = createTemporaryObject(realHostPageComponent, testCase)
        verify(host)
        const loader = findChild(host, "voiceCloneWorkspaceLoader")
        tryVerify(function() { return loader.item !== null })
        compare(loader.item.controller, realVoiceCloneHost.pluginController)
        compare(loader.item.controller.models[0].stableId,
                "Qwen/Qwen3-TTS-12Hz-0.6B-Base")
        compare(loader.item.controller.models[0].installState, "local-unverified")
        tryCompare(loader.item, "selectedModelIndex", 0)
        compare(loader.item.selectedModel.stableId,
                "Qwen/Qwen3-TTS-12Hz-0.6B-Base")
        tryVerify(function() {
            return realVoiceCloneHost.pluginController.workerReady
                    && realVoiceCloneHost.pluginController.modelLoaded
        }, 8000)
        const activationSpy = createTemporaryObject(
                    activationSpyComponent, testCase,
                    { target: realVoiceCloneHost.pluginController })
        verify(activationSpy)
        activationSpy.clear()
        const installedId = "Qwen/Qwen3-TTS-12Hz-0.6B-Base"
        wait(100)
        const installedCard = loader.item.modelCardForStableId(installedId)
        verify(installedCard)
        mouseClick(installedCard)
        verify(activationSpy.count >= 2)
        verify(realVoiceCloneHost.pluginController.activationState === "starting-worker",
               realVoiceCloneHost.pluginController.errorString + " / "
               + realVoiceCloneHost.pluginController.activationMessage)
        compare(realVoiceCloneHost.pluginController.modelLoaded, false)
        tryVerify(function() {
            return realVoiceCloneHost.pluginController.workerReady
                    && realVoiceCloneHost.pluginController.modelLoaded
        }, 8000)
        compare(realVoiceCloneHost.pluginController.activationMessage, "Model ready")
        compare(realVoiceCloneHost.pluginController.activationState, "ready")
        compare(realVoiceCloneHost.pluginController.workerReady, true)
        compare(realVoiceCloneHost.pluginController.modelLoaded, true)
        compare(realVoiceCloneHost.pluginController.advancedParameters.length, 1)
        verify(findChild(loader.item, "voiceCloneAdvancedParameter_style"))

        let missingIndex = -1
        for (let index = 0; index < realVoiceCloneHost.pluginController.models.length; ++index) {
            if (realVoiceCloneHost.pluginController.models[index].stableId
                    === "Qwen/Qwen3-TTS-12Hz-1.7B-Base") {
                missingIndex = index
                break
            }
        }
        verify(missingIndex >= 0)
        const missingCard = loader.item.modelCardForStableId(
                    "Qwen/Qwen3-TTS-12Hz-1.7B-Base")
        verify(missingCard)
        mouseClick(missingCard)
        tryCompare(realVoiceCloneHost.pluginController, "activationState", "needs-download")
        compare(realVoiceCloneHost.pluginController.basicParameters.length, 0)
        compare(realVoiceCloneHost.pluginController.advancedParameters.length, 0)
        tryVerify(function() {
            return findChild(loader.item, "voiceCloneAdvancedParameter_style") === null
        })
        compare(findChild(loader.item, "voiceCloneAdvancedButton").visible, false)

        loader.active = false
        tryCompare(loader, "item", null)
        host.destroy()
        wait(20)
    }

    function test_schemaDelegatesAllTypesAndOnlyDisclosesLiveAdvancedFields() {
        const normalizeControl = findChild(workspace, "voiceCloneParameterControl_normalize")
        const languageControl = findChild(workspace, "voiceCloneParameterControl_language")
        const seedControlBeforeSwitch = findChild(workspace, "voiceCloneParameterControl_seed")
        const speedControl = findChild(workspace, "voiceCloneParameterControl_speed")
        const styleControl = findChild(workspace, "voiceCloneParameterControl_style")
        const lexiconControl = findChild(workspace, "voiceCloneParameterControl_lexicon")
        verify(normalizeControl && languageControl && seedControlBeforeSwitch
               && speedControl && styleControl && lexiconControl)
        compare(normalizeControl.checked, true)
        compare(languageControl.currentIndex, 0)
        compare(seedControlBeforeSwitch.value, 7)
        compare(speedControl.value, 1000)
        compare(styleControl.text, "")

        const edited = Object.assign({}, workspace.parameterValues)
        edited.normalize = false
        edited.language = "en"
        edited.seed = 19
        edited.speed = 1.25
        edited.style = "warm"
        edited.lexicon = "C:/fixtures/lexicon.txt"
        workspace.parameterValues = edited
        tryCompare(normalizeControl, "checked", false)
        tryCompare(languageControl, "currentIndex", 1)
        tryCompare(seedControlBeforeSwitch, "value", 19)
        tryCompare(speedControl, "value", 1250)
        tryCompare(styleControl, "text", "warm")
        const lexiconText = findChild(lexiconControl, "voiceCloneParameterFileText_lexicon")
        verify(lexiconText)
        tryCompare(lexiconText, "text", "C:/fixtures/lexicon.txt")

        workspace.resetParameterDefaults()
        tryCompare(normalizeControl, "checked", true)
        tryCompare(languageControl, "currentIndex", 0)
        tryCompare(seedControlBeforeSwitch, "value", 7)
        tryCompare(speedControl, "value", 1000)
        tryCompare(styleControl, "text", "")
        tryCompare(lexiconText, "text", "")
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
        workspace.resetParameterDefaults()
        compare(workspace.parameterValues.conditionalStyle, undefined)

        workspace.referenceAudioPath = audioFixturePath()
        workspace.cloneText = "hidden parameter payload"
        workspace.startGeneration()
        compare(fakeController.generateCalls, 1)
        compare(fakeController.lastParameters.conditionalStyle, undefined)

        const values = Object.assign({}, workspace.parameterValues)
        values.normalize = false
        workspace.parameterValues = values
        tryCompare(conditional, "visible", true)

        fakeController.basicParameters = [
            { key: "seed", label: "Seed", description: "Random seed", type: "int", group: "basic", default: 42, minimum: 0, maximum: 99, step: 1 }
        ]
        fakeController.advancedParameters = []
        fakeController.capabilitiesChanged()
        tryVerify(function() { return workspace.parameterValues.seed === 42 })
        const seedDelegate = findChild(workspace, "voiceCloneParameter_int")
        verify(seedDelegate)
        tryCompare(seedDelegate, "renderedValue", 42)
        const seedControl = findChild(seedDelegate, "voiceCloneParameterControl_seed")
        verify(seedControl)
        tryCompare(seedControl, "value", 42)
    }

    function test_switchingModelClearsActiveGenerationState() {
        workspace.referenceAudioPath = "C:/fixtures/reference.wav"
        workspace.cloneText = "switch during generation"
        workspace.startGeneration()
        compare(workspace.activeRequestId, "request-1")
        compare(workspace.running, true)

        mouseClick(findChild(workspace, "voiceCloneModelCard2"))
        compare(workspace.activeRequestId, "")
        compare(workspace.running, false)
        compare(workspace.canGenerate, true)
        workspace.startGeneration()
        compare(fakeController.generateCalls, 2)
        compare(workspace.activeRequestId, "request-1")
    }

    function test_indexLicenseGateGenerationCancelAndResultActions() {
        mouseClick(findChild(workspace, "voiceCloneModelCard1"))
        compare(fakeController.lastSelectedId, "IndexTeam/IndexTTS-2.5")
        const gate = findChild(workspace, "voiceCloneLicenseAuthorityCheck")
        verify(gate && gate.visible)
        workspace.referenceAudioPath = audioFixturePath()
        workspace.cloneText = "测试人声克隆"
        const referencePanel = findChild(workspace, "voiceCloneReferencePanel")
        verify(referencePanel)
        compare(workspace.canGenerate, false)
        mouseClick(gate)
        const licenseDialog = findChild(workspace, "voiceCloneLicenseDialog")
        verify(licenseDialog)
        tryCompare(licenseDialog, "visible", true)
        workspace.licenseRequirementCheck(0).checked = true
        workspace.licenseRequirementCheck(1).checked = true
        mouseClick(findChild(workspace, "voiceCloneLicenseConfirmButton"))
        compare(fakeController.acceptLicenseCalls, 1)
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
        fakeController.generationFinished("request-1", specialAudioFixturePath())
        tryCompare(resultPanel, "hasResult", true)
        tryCompare(resultPanel, "waveformReady", true, 5000)
        referencePanel.loadWaveform()
        tryCompare(referencePanel, "waveformReady", true, 5000)
        const referenceWaveform = findChild(workspace,
                                             "voiceCloneReferenceWaveform")
        verify(referenceWaveform.visible)
        verify(referenceWaveform.peakCount > 0)
        const resultWaveform = findChild(workspace, "voiceCloneResultWaveform")
        verify(resultWaveform && resultWaveform.visible)
        verify(resultWaveform.peakCount > 0)
        const resultPlayButton = findChild(workspace, "voiceCloneResultPlayButton")
        verify(resultPlayButton && resultPlayButton.visible && resultPlayButton.enabled)
        mouseClick(resultPlayButton)
        tryCompare(AudioPreviewController, "hasSource", true)
        mouseClick(findChild(workspace, "voiceCloneSaveResultButton"))
        const saveDialog = findChild(workspace, "voiceCloneSaveDialog")
        verify(saveDialog)
        tryCompare(saveDialog, "visible", true)
        resultPanel.saveTo("C:/results/saved.wav")
        mouseClick(findChild(workspace, "voiceCloneSendToEditorButton"))
        tryCompare(AudioEditorController, "hasDocument", true)
        compare(AudioEditorController.filePath.replace(/\\/g, "/"),
                specialAudioFixturePath().replace(/\\/g, "/"))
        compare(AudioToolsController.currentToolId, "audio-editor")
        mouseClick(findChild(workspace, "voiceCloneDeleteResultButton"))
        compare(fakeController.saveCalls, 1)
        compare(fakeController.deleteCalls, 1)
        compare(resultPanel.hasResult, false)
    }

    function test_licenseDialogStaysOpenUntilEveryRequiredLicenseIsChecked() {
        mouseClick(findChild(workspace, "voiceCloneModelCard1"))
        mouseClick(findChild(workspace, "voiceCloneLicenseAuthorityCheck"))
        const dialog = findChild(workspace, "voiceCloneLicenseDialog")
        verify(dialog)
        tryCompare(dialog, "visible", true)
        workspace.licenseRequirementCheck(0).checked = true
        mouseClick(findChild(workspace, "voiceCloneLicenseConfirmButton"))
        compare(dialog.visible, true)
        compare(fakeController.acceptLicenseCalls, 0)
        const warning = findChild(workspace, "voiceCloneLicenseIncompleteWarning")
        verify(warning && warning.visible)

        workspace.licenseRequirementCheck(1).checked = true
        mouseClick(findChild(workspace, "voiceCloneLicenseConfirmButton"))
        tryCompare(dialog, "visible", false)
        compare(fakeController.acceptLicenseCalls, 1)
    }

    function test_downloadStatusAndControlsStayBoundToActualModelId() {
        mouseClick(findChild(workspace, "voiceCloneModelCard3"))
        fakeController.downloadModelId = "FunAudioLLM/Fun-CosyVoice3-0.5B-2512"
        fakeController.downloadState = "downloading"
        fakeController.downloadProgressPercent = 42
        fakeController.downloadInProgress = true
        const downloadButton = findChild(workspace, "voiceCloneDownloadModelButton")
        tryVerify(function() { return downloadButton.visible && downloadButton.text.indexOf("42") >= 0 })
        const pauseButton = findChild(workspace, "voiceClonePauseDownloadButton")
        const cancelButton = findChild(workspace, "voiceCloneCancelDownloadButton")
        tryVerify(function() { return pauseButton.visible && cancelButton.visible })
        pauseButton.clicked()
        cancelButton.clicked()
        compare(fakeController.pauseDownloadCalls, 1)
        compare(fakeController.cancelDownloadCalls, 1)

        mouseClick(findChild(workspace, "voiceCloneModelCard0"))
        tryVerify(function() { return !pauseButton.visible && !cancelButton.visible })

        mouseClick(findChild(workspace, "voiceCloneModelCard3"))
        fakeController.downloadState = "failed"
        fakeController.downloadError = "network failed"
        fakeController.downloadInProgress = false
        const errorLabel = findChild(workspace, "voiceCloneDownloadError")
        const retryButton = findChild(workspace, "voiceCloneRetryDownloadButton")
        tryVerify(function() { return errorLabel.visible && retryButton.visible })
        compare(errorLabel.text, "network failed")
        retryButton.clicked()
        compare(fakeController.retryDownloadCalls, 1)
    }
}

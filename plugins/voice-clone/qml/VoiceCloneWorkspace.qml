import QtQuick
import QtQuick.Layouts
import AgPlayer

Item {
    id: root
    objectName: "voiceCloneWorkspace"

    property var controller: null
    property alias referenceAudioPath: referencePanel.referencePath
    property alias cloneText: textPanel.cloneText
    property alias resultPath: resultPanel.resultPath
    property string activeRequestId: ""
    property var parameterValues: ({})
    property int selectedModelIndex: controller && controller.models.length > 0 ? 0 : -1
    readonly property int modelCount: controller ? controller.models.length : 0
    readonly property var selectedModel: selectedModelIndex >= 0 && controller
                                         ? controller.models[selectedModelIndex] : ({})
    readonly property bool running: activeRequestId !== ""
    readonly property bool canGenerate: !!controller
                                        && controller.workerReady
                                        && controller.modelLoaded
                                        && cloneText.trim().length > 0
                                        && referenceAudioPath.trim().length > 0
                                        && (!selectedModel.requiresLicenseAcceptance
                                            || modelBar.legalAuthorityConfirmed)

    signal saveResultRequested(string path)
    signal deleteResultRequested(string path)
    signal sendResultToEditorRequested(string path)

    function resetParameterDefaults() {
        const next = ({})
        if (controller) {
            const all = (controller.basicParameters || []).concat(controller.advancedParameters || [])
            for (let index = 0; index < all.length; ++index)
                next[all[index].key] = all[index]["default"]
        }
        parameterValues = next
    }

    function resetUiState() {
        activeRequestId = ""
        resultPath = ""
        cloneText = ""
        referenceAudioPath = ""
        selectedModelIndex = modelCount > 0 ? 0 : -1
        modelBar.legalAuthorityConfirmed = false
        parameterPanel.advancedExpanded = false
        resetParameterDefaults()
    }

    function selectModel(index, stableId) {
        if (!controller || index < 0 || index >= modelCount) return
        selectedModelIndex = index
        modelBar.legalAuthorityConfirmed = false
        resetParameterDefaults()
        if (controller.selectModel) controller.selectModel(stableId)
    }

    function startGeneration() {
        if (!canGenerate || !controller || !controller.generate) return
        const requestId = controller.generate(cloneText, referenceAudioPath, parameterValues)
        if (requestId) activeRequestId = requestId
    }

    function cancelGeneration() {
        if (!running || !controller || !controller.cancel) return
        if (controller.cancel(activeRequestId)) activeRequestId = ""
    }

    Component.onCompleted: resetParameterDefaults()
    onControllerChanged: resetUiState()

    Connections {
        target: root.controller
        ignoreUnknownSignals: true
        function onCapabilitiesChanged() { root.resetParameterDefaults() }
        function onGenerationFinished(requestId, outputPath) {
            if (requestId !== root.activeRequestId) return
            root.activeRequestId = ""
            root.resultPath = outputPath
        }
        function onRequestFailed(requestId, code, message) {
            if (requestId === root.activeRequestId) root.activeRequestId = ""
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        VoiceCloneModelBar {
            id: modelBar
            objectName: "voiceCloneModelBar"
            Layout.fillWidth: true
            models: root.controller ? root.controller.models : []
            selectedIndex: root.selectedModelIndex
            onModelSelected: function(index, stableId) { root.selectModel(index, stableId) }
            onRefreshRequested: if (root.controller && root.controller.refreshModels) root.controller.refreshModels()
            onOpenDirectoryRequested: if (root.controller && root.controller.openModelDirectory) root.controller.openModelDirectory()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 250
            spacing: 10
            VoiceCloneReferencePanel {
                id: referencePanel
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 32
            }
            VoiceCloneTextPanel {
                id: textPanel
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 38
            }
            VoiceCloneOutputPanel {
                id: outputPanel
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 30
                canGenerate: root.canGenerate
                running: root.running
                errorText: root.controller ? root.controller.errorString : ""
                onGenerateRequested: root.startGeneration()
                onCancelRequested: root.cancelGeneration()
            }
        }

        VoiceCloneParameterPanel {
            id: parameterPanel
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            basicParameters: root.controller ? root.controller.basicParameters : []
            advancedParameters: root.controller ? root.controller.advancedParameters : []
            values: root.parameterValues
            onValueEdited: function(key, value) {
                const next = Object.assign({}, root.parameterValues)
                next[key] = value
                root.parameterValues = next
            }
        }

        VoiceCloneResultPanel {
            id: resultPanel
            Layout.fillWidth: true
            Layout.preferredHeight: 190
            onSaveRequested: function(path, destinationPath) {
                root.saveResultRequested(path)
                if (root.controller && root.controller.saveResult)
                    root.controller.saveResult(path, destinationPath)
            }
            onDeleteRequested: function(path) {
                root.deleteResultRequested(path)
                if (!root.controller || !root.controller.deleteResult
                        || root.controller.deleteResult(path))
                    root.resultPath = ""
            }
            onSendToEditorRequested: function(path) {
                root.sendResultToEditorRequested(path)
                if (root.controller && root.controller.sendResultToEditor)
                    root.controller.sendResultToEditor(path)
                else {
                    AudioToolsController.selectToolById("audio-editor")
                    const normalized = path.replace(/\\/g, "/")
                    AudioEditorController.openFile(Qt.resolvedUrl("file:///" + normalized))
                }
            }
        }
    }
}

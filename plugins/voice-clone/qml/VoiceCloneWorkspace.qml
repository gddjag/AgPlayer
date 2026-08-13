import QtQuick
import QtQuick.Controls
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
    property bool initialized: false
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
                                        && !controller.licenseAcceptanceRequired

    signal saveResultRequested(string path)
    signal deleteResultRequested(string path)
    signal sendResultToEditorRequested(string path)

    function supportedParameter(field) {
        return ["bool", "enum", "int", "double", "string", "file"].indexOf(field.type) >= 0
    }

    function fieldVisibleForValues(field, values, controls) {
        if (!field.visibleWhen) return true
        let actual = values[field.visibleWhen.key]
        if (actual === undefined) {
            for (let index = 0; index < controls.length; ++index) {
                if (controls[index].key === field.visibleWhen.key) {
                    actual = controls[index]["default"]
                    break
                }
            }
        }
        return actual === field.visibleWhen.equals
    }

    function resetParameterDefaults() {
        const next = ({})
        if (controller) {
            const all = (controller.basicParameters || []).concat(controller.advancedParameters || [])
            for (let index = 0; index < all.length; ++index) {
                const field = all[index]
                if (supportedParameter(field) && fieldVisibleForValues(field, next, all))
                    next[field.key] = field["default"]
            }
        }
        parameterValues = next
    }

    function visibleGenerationParameters() {
        const next = ({})
        if (!controller) return next
        const all = (controller.basicParameters || []).concat(controller.advancedParameters || [])
        for (let index = 0; index < all.length; ++index) {
            const field = all[index]
            if (!supportedParameter(field)
                    || !fieldVisibleForValues(field, parameterValues, all)) continue
            next[field.key] = parameterValues[field.key] === undefined
                    ? field["default"] : parameterValues[field.key]
        }
        return next
    }

    function activationStatusText() {
        if (!controller) return ""
        switch (controller.activationState) {
        case "selecting": return qsTr("正在选择模型")
        case "starting-worker": return qsTr("正在启动模型 Worker")
        case "loading-model": return qsTr("Worker 已连接，正在加载模型")
        case "ready": return qsTr("模型已就绪")
        case "needs-download": return qsTr("模型或运行时未就绪，需要下载")
        case "error": return controller.activationMessage || controller.errorString
        default: return controller.activationMessage || ""
        }
    }

    function resetUiState() {
        activeRequestId = ""
        resultPath = ""
        cloneText = ""
        referenceAudioPath = ""
        const count = controller && controller.models ? controller.models.length : 0
        selectedModelIndex = count > 0 ? 0 : -1
        parameterPanel.advancedExpanded = false
        resetParameterDefaults()
    }

    function activateSelectedModel() {
        if (!controller || selectedModelIndex < 0 || selectedModelIndex >= modelCount
                || !controller.activateModel) return
        controller.activateModel(selectedModel.stableId || "")
    }

    function selectModel(index, stableId) {
        if (!controller || index < 0 || index >= modelCount) return
        activeRequestId = ""
        selectedModelIndex = index
        resetParameterDefaults()
        if (controller.activateModel) controller.activateModel(stableId)
    }

    function startGeneration() {
        if (!canGenerate || !controller || !controller.generate) return
        const requestId = controller.generate(cloneText, referenceAudioPath,
                                              visibleGenerationParameters())
        if (requestId) activeRequestId = requestId
    }

    function cancelGeneration() {
        if (!running || !controller || !controller.cancel) return
        if (controller.cancel(activeRequestId)) activeRequestId = ""
    }

    Component.onCompleted: {
        initialized = true
        resetUiState()
        Qt.callLater(activateSelectedModel)
    }
    onControllerChanged: {
        if (initialized) {
            Qt.callLater(function() {
                root.resetUiState()
                root.activateSelectedModel()
            })
        } else {
            resetUiState()
        }
    }

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
            licenseAcceptanceRequired: root.controller
                                               ? root.controller.licenseAcceptanceRequired : false
            onModelSelected: function(index, stableId) { root.selectModel(index, stableId) }
            onLicenseAcceptanceRequested: licenseDialog.open()
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
                statusText: root.activationStatusText()
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
                // qmllint disable unqualified
                AudioToolsController.selectToolById("audio-editor")
                const normalized = path.replace(/\\/g, "/")
                AudioEditorController.openFile(Qt.resolvedUrl("file:///" + normalized))
                // qmllint enable unqualified
            }
        }
    }

    Dialog {
        id: licenseDialog
        objectName: "voiceCloneLicenseDialog"
        anchors.centerIn: parent
        modal: true
        title: qsTr("确认模型许可与合法授权")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: licenseAuthorityConfirmation.checked = false
        onAccepted: {
            if (!licenseAuthorityConfirmation.checked || !root.controller
                    || !root.controller.acceptSelectedLicense) return
            root.controller.acceptSelectedLicense()
        }

        contentItem: ColumnLayout {
            spacing: 10
            Label {
                Layout.preferredWidth: 460
                text: qsTr("使用此模型前，请阅读当前许可，并确认参考音频与待克隆声音均已获得合法授权。")
                wrapMode: Text.Wrap
                color: Theme.primaryText
            }
            Button {
                text: root.controller && root.controller.currentLicenseName
                      ? root.controller.currentLicenseName : qsTr("查看模型许可")
                enabled: root.controller && root.controller.currentLicenseUrl
                onClicked: Qt.openUrlExternally(root.controller.currentLicenseUrl)
            }
            CheckBox {
                id: licenseAuthorityConfirmation
                objectName: "voiceCloneLicenseDialogAuthorityCheck"
                text: qsTr("我已阅读许可，并确认拥有合法授权")
            }
        }
    }
}

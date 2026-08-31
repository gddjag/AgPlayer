import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "VocalSeparationWorkbench"
    when: windowShown
    visible: true
    width: 1672
    height: 941

    VocalSeparationPage {
        id: page
        anchors.fill: parent
    }

    Component {
        id: viewportPage
        VocalSeparationPage { }
    }

    Component {
        id: navigationComponent
        ToolSidebar { width: 1200; height: 55 }
    }

    function test_navigationUsesStableVisibleToolIds() {
        const navigation = createTemporaryObject(navigationComponent, testCase)
        verify(navigation)
        compare(navigation.visibleTools.map(function(tool) { return tool.toolId }),
                [0, 4, 1, 2, 3])
        navigation.currentTool = 4
        compare(navigation.currentTool, 4)
    }

    function test_realEmptyStateAndRequiredRegions() {
        const input = findChild(page, "separationInputPanel")
        const waveform = findChild(page, "separationInputWaveform")
        const models = findChild(page, "separationModelDeck")
        const settings = findChild(page, "separationSettingsPanel")
        const stems = findChild(page, "separationStemSelector")
        const timeline = findChild(page, "separationTimeline")
        const history = findChild(page, "separationHistoryPanel")
        const bottom = findChild(page, "separationBottomBar")
        const primary = findChild(page, "separationPrimaryAction")
        verify(input && waveform && models && settings && stems && timeline
               && history && bottom && primary)
        verify(!findChild(page, "separationBackupModelAction"))
        verify(!findChild(page, "separationBackupModelDialog"))
        verify(!findChild(page, "separationCustomModelCard"))
        compare(VocalSeparationController.inputInfo.name, undefined)
        verify(!primary.enabled)
        verify(primary.Accessible.name.length > 0)
    }

    function test_referenceWorkbenchGivesResultWaveformsTheRecoveredHeight() {
        const input = findChild(page, "separationInputPanel")
        const models = findChild(page, "separationModelDeck")
        const settings = findChild(page, "separationSettingsPanel")
        const timeline = findChild(page, "separationTimeline")
        verify(input && models && settings && timeline)
        verify(input.height <= 124,
               "the file and source preview row must stay compact")
        verify(models.height <= 178,
               "the horizontal model deck must stay compact")
        verify(settings.height <= 160,
               "output settings must align with the compact source row")
        verify(timeline.height >= 276,
               "the five result waveforms need the recovered vertical space")
    }

    function test_inputAndStemTracksUseTheSharedWaveformRenderer() {
        separationTestDriver.reset()
        verify(separationTestDriver.setInput(testAudioUrl))
        const inputWaveform = findChild(page, "separationInputWaveform")
        const inputPreview = findChild(page, "separationInputPreview")
        const vocalsWaveform = findChild(
            page, "separationStemWaveform-" + VocalSeparationController.Vocals)
        verify(inputWaveform && inputPreview && vocalsWaveform)
        compare(inputWaveform.visualMode, SettingsController.waveformMode)
        compare(inputWaveform.density,
                SettingsController.waveformMode === 2
                ? 1.0 : SettingsController.waveformDensity)
        compare(vocalsWaveform.visualMode, SettingsController.waveformMode)
        verify(inputWaveform.pointerInteractionEnabled)
        compare(inputWaveform.duration,
                VocalSeparationController.inputInfo.durationMs || 0)
        verify(inputWaveform.width >= inputPreview.width * 0.78,
               "removing the cover must leave most of the preview width to the waveform")
        separationTestDriver.reset()
    }

    function test_sourcePreviewAndBottomBarExposeRealTransportControls() {
        separationTestDriver.reset()
        AudioPreviewController.stop()
        const sourcePlay = findChild(page, "separationInputPreviewPlay")
        const transport = findChild(page, "separationTransport")
        const transportPlay = findChild(page, "separationTransportPlay")
        const transportTime = findChild(page, "separationTransportTime")
        verify(sourcePlay && transport && transportPlay && transportTime)
        verify(sourcePlay.Accessible.name.length > 0)
        verify(transportPlay.Accessible.name.length > 0)
        verify(transportTime.text.indexOf(":") >= 0)
        compare(sourcePlay.iconTint, Theme.warning)
        compare(transportPlay.iconTint, page.success)
        verify(!transportPlay.enabled,
               "the result transport must stay disabled before stems exist")
    }

    function test_modelDeckPublishesOnlyCompleteRealModels() {
        const list = findChild(page, "separationModelList")
        const scrollBar = findChild(page, "separationModelScrollBar")
        const firstCard = findChild(page, "separationModelCard-uvr-mdxnet-kara")
        verify(list && scrollBar && firstCard)
        compare(list.orientation, ListView.Horizontal)
        compare(list.count, VocalSeparationController.models.length)
        verify(!findChild(page, "separationCustomModelCard"),
               "unimplemented custom-model workflows must not be exposed")
        for (let index = 0; index < VocalSeparationController.models.length; ++index) {
            const model = VocalSeparationController.models[index]
            verify(model.tierLabel && model.tierLabel.length > 0)
            verify(model.provider && model.provider.length > 0)
            verify(model.description && model.description.length > 0)
            verify(model.repositoryUrl && model.repositoryUrl.length > 0)
        }
        const scrollPoint = scrollBar.mapToItem(page, 0, 0)
        const cardPoint = firstCard.mapToItem(page, 0, 0)
        verify(scrollPoint.y >= cardPoint.y + firstCard.height,
               "the horizontal scrollbar must sit below the model-card frames")
        verify(!controlWithText(firstCard, "选择"),
               "model selection is performed by clicking the card, not a nested button")

        const firstAction = findChild(firstCard, "separationModelAction-uvr-mdxnet-kara")
        verify(firstAction && firstAction.height <= 26,
               "model download controls must stay compact")
        verify(firstCard.height - (firstAction.mapToItem(firstCard, 0, 0).y
                                   + firstAction.height) >= 5,
               "model controls need breathing room above the card edge")

        separationTestDriver.selectModel("uvr-mdxnet-kara")
        const secondCard = findChild(page, "separationModelCard-uvr-mdx-net-inst-hq3")
        verify(secondCard)
        mouseClick(secondCard, secondCard.width / 2, 18)
        compare(VocalSeparationController.selectedModelId, "uvr-mdx-net-inst-hq3")
        separationTestDriver.selectModel("uvr-mdxnet-kara")
    }

    function test_outputTracksShowIconsChecksAndUnavailableStates() {
        const vocals = findChild(
            page, "separationStemOption-" + VocalSeparationController.Vocals)
        const vocalsIcon = findChild(
            page, "separationStemIcon-" + VocalSeparationController.Vocals)
        const vocalsCheck = findChild(
            page, "separationStemCheck-" + VocalSeparationController.Vocals)
        const drums = findChild(
            page, "separationStemOption-" + VocalSeparationController.Drums)
        const drumsCheck = findChild(
            page, "separationStemCheck-" + VocalSeparationController.Drums)
        verify(vocals && vocalsIcon && vocalsCheck && drums && drumsCheck)
        verify(vocals.enabled)
        verify(vocalsCheck.visible)
        verify(drumsCheck.visible)
        verify(!drums.enabled)
        verify(vocals.width <= 180,
               "five output-track choices must remain compact")
        verify(!findChild(page, "separationBackupModelAction"),
               "unverified backup downloads must not be presented as actions")
    }

    function test_sourcePlaybackDoesNotAnimateResultWaveforms() {
        verify(separationTestDriver.setReady(testAudioUrl))
        separationTestDriver.setCompleted()
        const inputWaveform = findChild(page, "separationInputWaveform")
        const vocalsWaveform = findChild(
            page, "separationStemWaveform-" + VocalSeparationController.Vocals)
        verify(inputWaveform && vocalsWaveform)
        AudioPreviewController.play(testAudioUrl)
        tryVerify(function() { return AudioPreviewController.positionMs > 0 }, 2000)
        verify(inputWaveform.position > 0)
        compare(vocalsWaveform.position, 0)
        AudioPreviewController.stop()
        separationTestDriver.reset()
    }

    function test_sourceAndResultTransportsOwnSeparateSources() {
        verify(separationTestDriver.setReady(testAudioUrl))
        verify(separationTestDriver.setCompletedWithAudio(testAudioUrl))
        const sourcePlay = findChild(page, "separationInputPreviewPlay")
        const resultPlay = findChild(page, "separationTransportPlay")
        verify(sourcePlay && resultPlay && resultPlay.enabled)

        mouseClick(sourcePlay)
        tryVerify(function() {
            return AudioPreviewController.sourcePath
                   === VocalSeparationController.inputInfo.path
        }, 2000)
        verify(page.inputPreviewCurrent)
        verify(!page.resultPreviewCurrent)
        compare(resultPlay.iconName, "play-fill")
        tryVerify(function() { return page.inputPreviewPositionMs > 0 }, 2000)

        const accompanimentPath = page.stemInfo(
            VocalSeparationController.Accompaniment).path
        verify(accompanimentPath.length > 0
               && accompanimentPath !== VocalSeparationController.inputInfo.path)
        mouseClick(resultPlay)
        tryVerify(function() {
            return VocalSeparationController.resultPreviewMode
                   === VocalSeparationController.Mix
        }, 2000)
        verify(!page.inputPreviewCurrent)
        verify(page.resultPreviewCurrent)
        compare(separationTestDriver.activeResultMixKinds(),
                [VocalSeparationController.Vocals,
                 VocalSeparationController.Accompaniment])
        compare(sourcePlay.iconName, "play-fill")

        const inputWaveform = findChild(page, "separationInputWaveform")
        const vocalsWaveform = findChild(
            page, "separationStemWaveform-" + VocalSeparationController.Vocals)
        const accompanimentWaveform = findChild(
            page, "separationStemWaveform-" + VocalSeparationController.Accompaniment)
        verify(inputWaveform && vocalsWaveform && accompanimentWaveform)
        tryVerify(function() { return page.resultPreviewPositionMs > 0 }, 2000)
        verify(page.inputPreviewPositionMs > 0)
        compare(inputWaveform.position, page.inputPreviewPositionMs)
        compare(vocalsWaveform.position, page.resultPreviewPositionMs)
        compare(accompanimentWaveform.position, page.resultPreviewPositionMs)

        AudioPreviewController.stop()
        separationTestDriver.reset()
    }

    function test_sourceAndResultTransportsRestoreTheirOwnPositions() {
        verify(separationTestDriver.setReady(testAudioUrl))
        verify(separationTestDriver.setCompletedWithAudio(testAudioUrl))
        const sourcePlay = findChild(page, "separationInputPreviewPlay")
        const resultPlay = findChild(page, "separationTransportPlay")
        verify(sourcePlay && resultPlay && resultPlay.enabled)

        mouseClick(sourcePlay)
        tryVerify(function() { return AudioPreviewController.positionMs > 100 }, 2000)
        const sourcePosition = AudioPreviewController.positionMs

        mouseClick(resultPlay)
        tryVerify(function() {
            return page.resultPreviewCurrent && AudioPreviewController.positionMs > 100
        }, 2000)
        const resultPosition = AudioPreviewController.positionMs

        mouseClick(sourcePlay)
        tryVerify(function() { return page.inputPreviewCurrent }, 2000)
        verify(AudioPreviewController.positionMs >= sourcePosition - 100,
               "source preview must resume near its stored position")

        mouseClick(resultPlay)
        tryVerify(function() { return page.resultPreviewCurrent }, 2000)
        verify(AudioPreviewController.positionMs >= resultPosition - 100,
               "result preview must resume near its stored position")

        AudioPreviewController.stop()
        separationTestDriver.reset()
    }

    function test_clickingAResultWaveformStartsSoloAndBottomRestoresTheMixPosition() {
        verify(separationTestDriver.setReady(testAudioUrl))
        verify(separationTestDriver.setCompletedWithAudio(testAudioUrl))
        const resultPlay = findChild(page, "separationTransportPlay")
        const vocalsWaveform = findChild(
            page, "separationStemWaveform-" + VocalSeparationController.Vocals)
        verify(resultPlay && resultPlay.enabled && vocalsWaveform)

        mouseClick(resultPlay)
        tryCompare(VocalSeparationController, "resultPreviewMode",
                   VocalSeparationController.Mix, 2000)
        tryVerify(function() { return AudioPreviewController.positionMs > 100 }, 2000)

        const expectedSoloPosition = Math.round(
            (VocalSeparationController.inputInfo.durationMs || 0) * 0.64)
        mouseClick(vocalsWaveform, vocalsWaveform.width * 0.64,
                   vocalsWaveform.height / 2)
        tryCompare(VocalSeparationController, "resultPreviewMode",
                   VocalSeparationController.Solo, 2000)
        compare(VocalSeparationController.resultPreviewSoloKind,
                VocalSeparationController.Vocals)
        verify(AudioPreviewController.playing,
               "clicking a result row must immediately play that stem")
        verify(Math.abs(AudioPreviewController.positionMs - expectedSoloPosition) <= 180,
               "solo playback must start at the clicked waveform position")
        const soloPosition = AudioPreviewController.positionMs

        mouseClick(resultPlay)
        tryCompare(VocalSeparationController, "resultPreviewMode",
                   VocalSeparationController.Mix, 2000)
        compare(separationTestDriver.activeResultMixKinds(),
                [VocalSeparationController.Vocals,
                 VocalSeparationController.Accompaniment])
        verify(AudioPreviewController.playing)
        verify(AudioPreviewController.positionMs >= soloPosition - 100,
               "switching from solo back to the bottom mix must preserve position")

        AudioPreviewController.stop()
        separationTestDriver.reset()
    }

    function test_resultRowsUseOneTransportAndPutVolumeBeforeWaveform() {
        separationTestDriver.reset()
        separationTestDriver.setCompleted()
        const vocalsVolume = findChild(
            page, "stemPreviewVolume-" + VocalSeparationController.Vocals)
        const vocalsWaveform = findChild(
            page, "separationStemWaveform-" + VocalSeparationController.Vocals)
        verify(vocalsVolume && vocalsWaveform)
        verify(vocalsVolume.mapToItem(page, 0, 0).x
               < vocalsWaveform.mapToItem(page, 0, 0).x)
        verify(!controlWithAccessibleName(page, "人声试听"),
               "result rows must not expose per-track play buttons")
        separationTestDriver.reset()
    }

    function test_stemVolumeKeyboardChangesReachTheController() {
        separationTestDriver.reset()
        separationTestDriver.setCompleted()
        const vocalsVolume = findChild(
            page, "stemPreviewVolume-" + VocalSeparationController.Vocals)
        verify(vocalsVolume && vocalsVolume.enabled)
        const before = page.stemInfo(
            VocalSeparationController.Vocals).previewVolume
        vocalsVolume.forceActiveFocus()
        verify(vocalsVolume.activeFocus)
        keyClick(Qt.Key_Left)
        tryVerify(function() {
            return page.stemInfo(VocalSeparationController.Vocals).previewVolume
                   < before
        }, 1000)
        separationTestDriver.reset()
    }

    function test_stemVolumeSupportsPointerDragAndMouseWheel() {
        separationTestDriver.reset()
        separationTestDriver.setCompleted()
        const kind = VocalSeparationController.Vocals
        const volume = findChild(page, "stemPreviewVolume-" + kind)
        const pointer = findChild(page, "stemPreviewVolumePointer-" + kind)
        verify(volume && pointer && volume.enabled)

        page.setStemVolumeFromPointer(kind, pointer.width * 0.3, pointer.width)
        page.setStemVolumeFromPointer(kind, pointer.width * 0.7, pointer.width)
        tryVerify(function() {
            return page.stemInfo(kind).previewVolume >= 0.65
                   && page.stemInfo(kind).previewVolume <= 0.75
        })

        const beforeWheel = page.stemInfo(kind).previewVolume
        page.adjustStemVolume(kind, 120)
        tryVerify(function() {
            return page.stemInfo(kind).previewVolume > beforeWheel
        })
        separationTestDriver.reset()
    }

    function test_referenceSettingsAndHistoryActionsAreDiscoverable() {
        const autoPlaylist = findChild(page, "separationAutoPlaylist")
        const autoOpen = findChild(page, "separationAutoOpenDirectory")
        const history = findChild(page, "separationHistoryList")
        verify(autoPlaylist && autoOpen && history)
        verify(autoPlaylist.Accessible.name.length > 0)
        verify(autoOpen.Accessible.name.length > 0)
        compare(autoOpen.checked, false,
                "the auto-open option remains available but is unchecked by default")
        mouseClick(autoOpen)
        verify(page.autoOpenOutputDirectory)
        mouseClick(autoOpen)
        verify(!page.autoOpenOutputDirectory)

        separationTestDriver.setHistoryRecord()
        wait(0)
        verify(controlWithAccessibleName(page, "打开该记录输出目录"))
        verify(!controlWithAccessibleName(page, "记录操作"),
               "history rows must not expose the unused ellipsis menu")
        separationTestDriver.clearHistory()
    }

    function test_progressBarAndBottomControlsUseTheRecoveredSpace() {
        separationTestDriver.reset()
        const progress = findChild(page, "separationJobProgress")
        const progressBackground = findChild(
            page, "separationJobProgressBackground")
        const percentage = findChild(page, "separationJobPercentage")
        const bottom = findChild(page, "separationBottomBar")
        const play = findChild(page, "separationTransportPlay")
        const time = findChild(page, "separationTransportTime")
        verify(progress && progressBackground && percentage && bottom && play && time)
        verify(progressBackground.border.color.a <= 0.4)
        verify(Math.abs(progressBackground.border.color.r
                        - progressBackground.border.color.g) <= 0.08)
        verify(Math.abs(progressBackground.border.color.g
                        - progressBackground.border.color.b) <= 0.08)
        compare(progress.value, 0)
        compare(percentage.text, "0%")
        compare(percentage.color, page.success)

        separationTestDriver.setJobProgress(0.64, "separating")
        compare(progress.value, 0.64)
        compare(percentage.text, "64%")
        verify(bottom.height >= 78)
        verify(play.width >= 44)
        verify(time.font.pixelSize >= 20)
        const reSeparate = controlWithText(bottom, "重新分离")
        verify(reSeparate && reSeparate.height >= 36)
        separationTestDriver.reset()
    }

    function test_gpuCandidateCanBeSelectedAfterProbe() {
        separationTestDriver.reset()
        separationTestDriver.setDevices("candidate")
        const gpu = findChild(page, "separationDevice-GPU")
        verify(gpu && gpu.enabled)
        verify(gpu.deviceReason.indexOf("NVIDIA GeForce RTX 4070 Ti SUPER") >= 0)
        mouseClick(gpu)
        compare(VocalSeparationController.deviceMode,
                VocalSeparationController.GPU)
        separationTestDriver.reset()
    }

    function test_narrowDesktopHistoryUsesAReadableStack() {
        separationTestDriver.setHistoryRecord()
        const instance = createTemporaryObject(viewportPage, testCase,
                                               { width: 1280, height: 720 })
        verify(instance)
        wait(0)
        const history = findChild(instance, "separationHistoryList")
        const fileCell = findChild(instance, "separationHistoryFileCell")
        const modelCell = findChild(instance, "separationHistoryModelCell")
        const detailCell = findChild(instance, "separationHistoryDetailCell")
        verify(history && fileCell && modelCell && detailCell)
        compare(history.count, 1)
        verify(fileCell.width >= 100,
               "the filename must retain readable width at 1280")
        verify(!modelCell.visible)
        verify(detailCell.visible)
        separationTestDriver.clearHistory()
    }

    function test_catalogStemCapabilitiesAreBoundToTheRealCatalog() {
        compare(VocalSeparationController.models.length, 3)
        verify(page.modelSupports("uvr-mdxnet-kara", "vocals"))
        verify(!page.modelSupports("uvr-mdxnet-kara", "drums"))
        verify(page.modelSupports("htdemucs-ft-fp16", "drums"))
        verify(page.modelSupports("htdemucs-ft-fp16", "other"))
        verify(findChild(page, "modelStemSummary-uvr-mdxnet-kara").text
               .indexOf("人声 / 伴奏") >= 0)
        verify(findChild(page, "modelStemSummary-htdemucs-ft-fp16").text
               .indexOf("鼓组") >= 0)
    }

    function test_twoStemTimelineKeepsUnsupportedStemsVisibleAndLocalized() {
        compare(VocalSeparationController.stems.length, 5)
        const drums = page.stemInfo(VocalSeparationController.Drums)
        verify(!drums.supported && !drums.selected && !drums.available)
        compare(drums.path, "")
        compare(drums.waveform.length, 0)
        compare(page.stemLabel(VocalSeparationController.Vocals), "人声")
        compare(page.stemLabel(VocalSeparationController.Accompaniment), "伴奏")
        compare(page.stemLabel(VocalSeparationController.Drums), "鼓组")
        verify(findChild(page, "separationTimelineStem-" + VocalSeparationController.Drums))
        verify(findChild(page, "separationTimelineStem-" + VocalSeparationController.Other))
        const vocalsVolume = findChild(
            page, "stemPreviewVolume-" + VocalSeparationController.Vocals)
        const drumsVolume = findChild(
            page, "stemPreviewVolume-" + VocalSeparationController.Drums)
        verify(vocalsVolume && vocalsVolume.enabled)
        compare(vocalsVolume.Accessible.name, "人声预览音量")
        verify(drumsVolume && !drumsVolume.enabled)
    }

    function test_playlistGateRequiresEverySelectedStemToBeAvailable() {
        verify(page.allSelectedStemsAvailable([
            { selected: true, available: true },
            { selected: false, available: false }
        ]))
        verify(!page.allSelectedStemsAvailable([
            { selected: true, available: true },
            { selected: true, available: false }
        ]))
        verify(!page.allSelectedStemsAvailable([]))
    }

    function test_nativeDropRoutesToTheRealSeparationController() {
        VocalSeparationController.clearInput()
        AudioToolsController.selectTool(4)
        verify(nativeDropHelper.sendUrls(page, [testAudioUrl]))
        tryVerify(function() {
            return VocalSeparationController.inputInfo.name !== undefined
                   && VocalSeparationController.inputInfo.name.length > 0
        }, 5000)
        VocalSeparationController.clearInput()
    }

    function descendants(root) {
        const found = []
        if (!root || !root.children)
            return found
        for (let index = 0; index < root.children.length; ++index)
            found.push(root.children[index])
        for (let index = 0; index < found.length; ++index) {
            const child = found[index]
            if (!child.children)
                continue
            for (let childIndex = 0; childIndex < child.children.length; ++childIndex)
                found.push(child.children[childIndex])
        }
        return found
    }

    function controlWithText(root, expectedText) {
        const all = descendants(root)
        for (let index = 0; index < all.length; ++index) {
            if (all[index].text === expectedText && all[index].enabled !== undefined)
                return all[index]
        }
        return null
    }

    function controlWithAccessibleName(root, expectedName) {
        const all = descendants(root)
        for (let index = 0; index < all.length; ++index) {
            if (all[index].Accessible && all[index].Accessible.name === expectedName)
                return all[index]
        }
        return null
    }

    function hasActiveFocus(root) {
        if (root && root.activeFocus)
            return true
        const all = descendants(root)
        for (let index = 0; index < all.length; ++index) {
            if (all[index].activeFocus)
                return true
        }
        return false
    }

    function test_downloadStateBindingsKeepJobProgressIndependent() {
        separationTestDriver.reset()
        separationTestDriver.setDownloadState("uvr-mdxnet-kara",
                                              VocalSeparationController.Downloading,
                                              0.42, "")
        compare(VocalSeparationController.downloadingModelId, "uvr-mdxnet-kara")
        compare(VocalSeparationController.downloadBusy, true)
        compare(VocalSeparationController.downloadProgress, 0.42)
        compare(VocalSeparationController.progress, 0)
        const active = VocalSeparationController.models.filter(function(model) {
            return model.id === "uvr-mdxnet-kara"
        })[0]
        compare(active.state, VocalSeparationController.Downloading)

        separationTestDriver.setDownloadState("uvr-mdxnet-kara",
                                              VocalSeparationController.Paused,
                                              0.42, "")
        compare(VocalSeparationController.models.filter(function(model) {
            return model.id === "uvr-mdxnet-kara"
        })[0].state, VocalSeparationController.Paused)

        separationTestDriver.setDownloadState("uvr-mdxnet-kara",
                                              VocalSeparationController.ModelFailed,
                                              0.42, "SHA-256 校验失败")
        compare(VocalSeparationController.error, "SHA-256 校验失败")
        compare(VocalSeparationController.models.filter(function(model) {
            return model.id === "uvr-mdxnet-kara"
        })[0].state, VocalSeparationController.ModelFailed)
        separationTestDriver.reset()
    }

    function test_runtimeMissingAndProviderFallbackExposeTruthfulReasons() {
        separationTestDriver.reset()
        separationTestDriver.setInput(testAudioUrl)
        separationTestDriver.markSelectedModelInstalled()
        separationTestDriver.setRuntimeMissing()
        verify(!VocalSeparationController.canStart)
        compare(VocalSeparationController.startDisabledReason, "ONNX Runtime 尚未安装")

        separationTestDriver.setDevices("fallback")
        compare(VocalSeparationController.availableDevices[0].available, true)
        compare(VocalSeparationController.availableDevices[2].available, false)
        compare(VocalSeparationController.availableDevices[2].reason,
                "DirectML 提供程序不可用，已回退 CPU")
        separationTestDriver.setDevices("none")
        compare(VocalSeparationController.availableDevices[0].available, false)
        compare(VocalSeparationController.availableDevices[0].reason,
                "CPU 和 GPU 均未通过设备探测")
        separationTestDriver.reset()
    }

    function test_probeRunningCancellingAndCompletedLockTheRealControls() {
        separationTestDriver.reset()
        const chooseFile = controlWithText(page, "选择文件")
        const outputFormat = controlWithAccessibleName(page, "输出格式")
        const primary = findChild(page, "separationPrimaryAction")
        verify(chooseFile && outputFormat && primary)

        separationTestDriver.setJobState(VocalSeparationController.Probing, "probe")
        verify(!chooseFile.enabled)
        verify(!outputFormat.enabled)
        compare(page.contextLockReason, "正在探测设备，请稍候")

        separationTestDriver.setJobState(VocalSeparationController.Running, "separating")
        verify(!chooseFile.enabled)
        verify(!outputFormat.enabled)
        compare(primary.text, "取消分离")
        verify(primary.enabled)
        compare(page.contextLockReason, "分离任务进行中，暂不能更改输入或设置")

        separationTestDriver.setJobState(VocalSeparationController.Cancelling, "cancelling")
        compare(primary.text, "正在取消")
        verify(!primary.enabled)

        separationTestDriver.setCompleted()
        compare(VocalSeparationController.jobState, VocalSeparationController.Completed)
        verify(page.hasAvailableSelectedStems())
        compare(primary.text, "开始分离")
        separationTestDriver.reset()
    }

    function test_failureRetryAndModelFamiliesAreVisible() {
        separationTestDriver.reset()
        separationTestDriver.setJobFailure("Worker 意外退出")
        const errorPanel = findChild(page, "separationErrorPanel")
        const retry = controlWithText(errorPanel, "重试")
        verify(errorPanel.visible)
        verify(retry && retry.enabled)
        compare(VocalSeparationController.jobState, VocalSeparationController.JobFailed)

        separationTestDriver.selectModel("uvr-mdxnet-kara")
        compare(VocalSeparationController.stems.filter(function(stem) {
            return stem.supported
        }).length, 2)
        separationTestDriver.selectModel("htdemucs-ft-fp16")
        compare(VocalSeparationController.stems.filter(function(stem) {
            return stem.supported
        }).length, 5)
        separationTestDriver.reset()
    }

    function test_previewLabelsFollowTheActualSharedPreviewSource() {
        separationTestDriver.reset()
        separationTestDriver.setInput(testAudioUrl)
        const source = VocalSeparationController.inputInfo.path
        AudioPreviewController.play(testAudioUrl)
        tryVerify(function() { return AudioPreviewController.sourcePath === source }, 2000)
        compare(page.previewActionText(source),
                AudioPreviewController.playing ? "暂停" : "继续")
        compare(page.previewActionText(source + ".other"), "试听")
        AudioPreviewController.stop()
        separationTestDriver.reset()
    }

    function test_keyboardFocusIsVisibleAndControlsAreAccessible() {
        verify(separationTestDriver.setReady(testAudioUrl))
        verify(separationTestDriver.setCompletedWithAudio(testAudioUrl))
        const primary = findChild(page, "separationPrimaryAction")
        const preview = controlWithAccessibleName(page, "输入播放或暂停")
        verify(primary)
        verify(preview)
        verify(primary.enabled)
        verify(primary.Accessible.name.length > 0)
        compare(primary.Accessible.role, Accessible.Button)
        primary.forceActiveFocus()
        verify(primary.activeFocus)
        keyClick(Qt.Key_Tab)
        verify(hasActiveFocus(page))

        page.forceActiveFocus()
        verify(page.activeFocus)
        keyClick(Qt.Key_Space)
        tryVerify(function() {
            return VocalSeparationController.resultPreviewMode
                   === VocalSeparationController.Mix
        }, 2000)
        compare(separationTestDriver.activeResultMixKinds(),
                [VocalSeparationController.Vocals,
                 VocalSeparationController.Accompaniment])
        keyClick(Qt.Key_Space)
        compare(VocalSeparationController.resultPreviewMode,
                VocalSeparationController.Mix)
        verify(!AudioPreviewController.playing)
        AudioPreviewController.stop()
        separationTestDriver.reset()
    }

    function test_bottomBarContainsOnlyTheRequestedActions() {
        const bottom = findChild(page, "separationBottomBar")
        verify(bottom)
        compare(bottom.border.width, 0)
        verify(controlWithText(bottom, "重新分离"))
        verify(controlWithText(bottom, "导出伴奏"))
        verify(controlWithText(bottom, "导出人声"))
        verify(controlWithText(bottom, "导出所有音轨"))
        verify(controlWithText(bottom, "开始分离"))
        verify(!controlWithText(bottom, "导出所选轨"))
        verify(!controlWithText(bottom, "加入播放列表"))
    }

    function test_responsiveViewportsKeepThePrimaryActionReachable() {
        const viewports = [[1672, 941], [1280, 720], [880, 560], [1920, 1080]]
        for (let index = 0; index < viewports.length; ++index) {
            const instance = createTemporaryObject(viewportPage, testCase,
                                                   { width: viewports[index][0],
                                                     height: viewports[index][1] })
            verify(instance)
            wait(0)
            const primary = findChild(instance, "separationPrimaryAction")
            const bottom = findChild(instance, "separationBottomBar")
            const models = findChild(instance, "separationModelDeck")
            verify(primary && bottom && models)
            const bottomPoint = bottom.mapToItem(instance, 0, 0)
            const actionPoint = primary.mapToItem(instance, 0, 0)
            verify(bottomPoint.y >= 0)
            verify(actionPoint.y + primary.height <= instance.height,
                   "primary CTA is clipped at " + viewports[index])
            if (viewports[index][0] >= 1100)
                verify(primary.width >= bottom.width * 0.25,
                       "desktop CTA stays prominent at " + viewports[index])
            if (viewports[index][0] < 1100)
                verify(bottom.height >= 96 && actionPoint.y >= bottomPoint.y + bottom.height / 2,
                       "compact actions use two rows at " + viewports[index])
            if (viewports[index][0] < 1100) {
                const controls = [
                    findChild(instance, "separationTransport"),
                    controlWithText(bottom, "重新分离"),
                    controlWithText(bottom, "导出伴奏"),
                    controlWithText(bottom, "导出人声"),
                    controlWithText(bottom, "导出所有音轨"),
                    primary
                ]
                for (let first = 0; first < controls.length; ++first) {
                    verify(controls[first])
                    const firstPoint = controls[first].mapToItem(bottom, 0, 0)
                    if (first === 0)
                        verify(controls[first].height >= 44,
                               "compact transport must contain its buttons at "
                               + viewports[index])
                    for (let second = first + 1; second < controls.length; ++second) {
                        const secondPoint = controls[second].mapToItem(bottom, 0, 0)
                        const separated = firstPoint.x + controls[first].width <= secondPoint.x
                            || secondPoint.x + controls[second].width <= firstPoint.x
                            || firstPoint.y + controls[first].height <= secondPoint.y
                            || secondPoint.y + controls[second].height <= firstPoint.y
                        verify(separated,
                               "compact bottom controls overlap at " + viewports[index]
                               + ": " + first + " and " + second)
                    }
                }
            }
        }
    }

    function test_desktopSideColumnKeepsTheReferenceThirtyPercentRatio() {
        const widths = [1440, 1672, 1920]
        for (let index = 0; index < widths.length; ++index) {
            const instance = createTemporaryObject(viewportPage, testCase,
                                                   { width: widths[index], height: 941 })
            verify(instance)
            wait(0)
            const side = findChild(instance, "separationSideColumn")
            verify(side)
            verify(side.width / instance.width >= 0.28
                   && side.width / instance.width <= 0.31,
                   "side column stays near 30% at " + widths[index])
        }
    }
}

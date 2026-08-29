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
        const playlistAction = findChild(page, "separationPlaylistAction")
        verify(input && waveform && models && settings && stems && timeline
               && history && bottom && primary && playlistAction)
        compare(VocalSeparationController.inputInfo.name, undefined)
        verify(!primary.enabled)
        verify(!playlistAction.enabled)
        verify(primary.Accessible.name.length > 0)
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
        const primary = findChild(page, "separationPrimaryAction")
        const preview = controlWithAccessibleName(page, "输入预览：试听")
        verify(primary)
        verify(preview)
        verify(primary.enabled)
        verify(primary.Accessible.name.length > 0)
        compare(primary.Accessible.role, Accessible.Button)
        primary.forceActiveFocus()
        verify(primary.activeFocus)
        keyClick(Qt.Key_Tab)
        verify(hasActiveFocus(page))

        preview.forceActiveFocus()
        keyClick(Qt.Key_Space)
        tryVerify(function() {
            return AudioPreviewController.sourcePath
                   === VocalSeparationController.inputInfo.path
        }, 2000)
        keyClick(Qt.Key_Space)
        verify(AudioPreviewController.sourcePath
               === VocalSeparationController.inputInfo.path)
        verify(!AudioPreviewController.playing)
        AudioPreviewController.stop()
        separationTestDriver.reset()
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

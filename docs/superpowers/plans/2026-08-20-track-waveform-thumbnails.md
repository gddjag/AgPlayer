# Track Waveform Thumbnails Implementation Plan

> **Supplementary plan:** Execute through `2026-08-20-tag-waveform-ui-master.md`. The master plan owns worktree selection and all shared-QML file ownership.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Add real 128-point, cache-only waveform thumbnails to visible TrackList rows, with stable Color36/Mono rendering and a disabled state with no thumbnail work.

**Architecture:** Preserve the existing player waveform pipeline. TrackWaveformThumbnailProvider reads only existing valid .agwf v2 cache files on one low-priority worker, reduces WaveformCacheData.mix into 128 quantized bytes, bounds memory to 256 tracks, and guards asynchronous replies by ID plus generation. TrackWaveformThumbnailItem draws those bytes with one Scene Graph geometry node; TrackList owns the conditional Loader.

**Tech Stack:** C++17, Qt 6 Core/Concurrent/Quick/QML/QuickTest, Qt Quick Scene Graph, existing agplayer::WaveformCache v2, CMake/CTest, QML.

**Spec:** docs/superpowers/specs/2026-08-20-track-waveform-tag-management-design.md; C:/Users/Administrator/Desktop/AgPlayer 歌曲列表波形缩略图开发提示词｜极致轻量版_歌单标签整合版.md

## Global Constraints

- Work in D:/ai/AgPlayer/.worktrees/tag-waveform-ui; the root checkout is not the production source tree.
- Preserve WaveformProvider, WaveformItem, playback, seek, hover, BPM, FFmpeg decode, and audio callbacks.
- Read only valid existing .agwf v2 via agplayer::WaveformCache::load_v2. Miss, malformed cache, source mismatch, or empty mix returns empty data and never calls ag_track_analysis, ag_track_analysis_with_aggregation, FFmpeg, or a decoder.
- Completed data is exactly 128 unsigned amplitude bytes. Do not write images, databases, files, textures, PCM, FFT, or another cache directory.
- Use one low-priority worker maximum, 256 LRU entries maximum, and one in-flight job per track ID; no dedicated persistent thread.
- Reject completion unless both trackId and generation match the requesting delegate.
- Color36 is the supplied 36-color palette indexed by FNV-1a 32-bit of persistent trackId modulo 36. Never use row index, qHash, or random state.
- TrackWaveformThumbnailItem uses exactly one QSGGeometryNode: no Canvas, QQuickPaintedItem, Image, shader, animation, Repeater, or per-peak QML items.
- Geometry rebuilds only for peak bytes, width, or height. Playback position cannot update thumbnail geometry.
- Disabled means no Loader instance, provider request, cache read, color calculation, node, or geometry, and 42 logical-pixel rows.
- Mode values are exactly Color36 and Mono. Changing mode affects only color, never peak data, reads, or geometry.
- Add no dependencies, platform-private drawing, unbounded cache, background scanning, or unrelated refactor.

---

## File Structure

- Create: qt/src/track_waveform_thumbnail_provider.hpp and .cpp — cache-only read, 128-byte reduction, palette, generation, LRU, single-worker scheduling.
- Create: qt/src/track_waveform_thumbnail_item.hpp and .cpp — one-node static Scene Graph renderer.
- Create: tests/qt/track_waveform_thumbnail_provider_test.cpp — reduction, cache-only, coalescing, generation, LRU, palette tests.
- Create: tests/qt/track_waveform_thumbnail_item_test.cpp — node count, geometry reuse, color-only tests.
- Create: tests/stress/track_waveform_thumbnail_stress_test.cpp — 10,000 logical-row pressure test.
- Create: tests/scripts/track_waveform_thumbnail_contract_test.ps1 — source guard against analysis/decode calls.
- Modify: qt/CMakeLists.txt, tests/CMakeLists.txt — build and CTest registration.
- Modify: qt/src/qml_registration.hpp, qt/src/qml_registration.cpp, app/main.cpp, tests/qt/qml_main_window_test_main.cpp — ownership and QML registration.
- Modify: qt/src/settings_controller.hpp, .cpp, tests/qt/settings_controller_test.cpp, app/qml/AgPlayer/SettingsPage.qml — persisted toggle and mode.
- Modify: app/qml/AgPlayer/components/TrackList.qml and tests/qml/tst_main_window.qml — Loader, row height, generation safety.
- Create: docs/development/2026-08-20-track-waveform-thumbnails.md; modify design-qa.md — evidence record.

### Task 1: Establish deterministic 128-byte data and the fixed palette

**Files:**
- Create: qt/src/track_waveform_thumbnail_provider.hpp
- Create: qt/src/track_waveform_thumbnail_provider.cpp
- Create: tests/qt/track_waveform_thumbnail_provider_test.cpp
- Modify: qt/CMakeLists.txt
- Modify: tests/CMakeLists.txt

**Interfaces:**
- Produces: TrackWaveformThumbnailProvider with kPeakCount = 128; static quantizeMixPeaks(vector<float>), fnv1a32(QString), and colorForTrackId(QString).
- Consumes: only WaveformCacheData.mix; no decoder and no WaveformProvider.
- Used by: Tasks 2, 3, and 5.

- [ ] **Step 1: Write failing data-contract tests**

~~~cpp
void TrackWaveformThumbnailProviderTest::quantizes128FiniteBuckets()
{
    const std::vector<float> mix{0.0F, -0.25F,
        std::numeric_limits<float>::quiet_NaN(), 1.5F,
        -std::numeric_limits<float>::infinity()};
    const QByteArray bytes = TrackWaveformThumbnailProvider::quantizeMixPeaks(mix);
    QCOMPARE(bytes.size(), 128);
    QCOMPARE(static_cast<unsigned char>(bytes.at(0)), 0U);
    QCOMPARE(static_cast<unsigned char>(bytes.at(76)), 255U);
}

void TrackWaveformThumbnailProviderTest::keepsImpulseInContinuousTimeBucket()
{
    std::vector<float> mix(256, 0.0F);
    mix[129] = 0.8F;
    QCOMPARE(static_cast<unsigned char>(
        TrackWaveformThumbnailProvider::quantizeMixPeaks(mix).at(64)), 204U);
}
~~~

Test all 36 exact supplied colors using known FNV-1a inputs; assert the same ID returns the same QColor across repeated calls.

- [ ] **Step 2: Register and run RED**

~~~cmake
add_executable(track_waveform_thumbnail_provider_test
    qt/track_waveform_thumbnail_provider_test.cpp)
target_link_libraries(track_waveform_thumbnail_provider_test PRIVATE agplayer_qt Qt6::Test)
add_test(NAME track_waveform_thumbnail_provider_test COMMAND track_waveform_thumbnail_provider_test)
~~~

Run: cmake --build build/msvc-release --target track_waveform_thumbnail_provider_test --parallel 4; ctest --test-dir build/msvc-release -R '^track_waveform_thumbnail_provider_test$' --output-on-failure

Expected: compilation fails because the provider API is absent.

- [ ] **Step 3: Implement the minimum deterministic helpers**

~~~cpp
QByteArray TrackWaveformThumbnailProvider::quantizeMixPeaks(
    const std::vector<float>& mix)
{
    QByteArray out(kPeakCount, '\0');
    for (int bucket = 0; bucket < kPeakCount; ++bucket) {
        const auto first = mix.size() * static_cast<std::size_t>(bucket) / kPeakCount;
        const auto last = mix.size() * static_cast<std::size_t>(bucket + 1) / kPeakCount;
        float maximum = 0.0F;
        for (auto i = first; i < last; ++i)
            if (std::isfinite(mix[i])) maximum = std::max(maximum, std::abs(mix[i]));
        out[bucket] = static_cast<char>(std::lround(
            std::clamp(maximum, 0.0F, 1.0F) * 255.0F));
    }
    return out;
}
~~~

Store exactly the desktop requirement palette in std::array<QColor, 36>. Hash UTF-8 trackId bytes with FNV-1a and index modulo 36. Empty input must produce 128 zeros.

- [ ] **Step 4: Run GREEN**

Run: ctest --test-dir build/msvc-release -R '^track_waveform_thumbnail_provider_test$' --output-on-failure

Expected: PASS for silent, endpoint, non-finite, impulse, deterministic, and all-palette cases.

- [ ] **Step 5: Refactor and re-run**

Extract only private quantizeAmplitude(float). Retain every public name and test value.

Run: ctest --test-dir build/msvc-release -R '^track_waveform_thumbnail_provider_test$' --output-on-failure

Expected: PASS.

- [ ] **Step 6: Commit**

~~~bash
git add qt/src/track_waveform_thumbnail_provider.hpp qt/src/track_waveform_thumbnail_provider.cpp qt/CMakeLists.txt tests/qt/track_waveform_thumbnail_provider_test.cpp tests/CMakeLists.txt
git commit -m "feat: add deterministic track waveform thumbnail data"
~~~

### Task 2: Deliver cache-only thumbnails through a bounded generation-safe queue

**Files:**
- Modify: qt/src/track_waveform_thumbnail_provider.hpp
- Modify: qt/src/track_waveform_thumbnail_provider.cpp
- Modify: tests/qt/track_waveform_thumbnail_provider_test.cpp
- Create: tests/scripts/track_waveform_thumbnail_contract_test.ps1
- Modify: tests/CMakeLists.txt

**Interfaces:**
- Consumes: SettingsController.cacheDirectory(), WaveformCache.key_for, and WaveformCache.load_v2.
- Produces: invokables request(QString trackId, QString sourcePath, quint64 generation), cancel(QString trackId, quint64 generation); signal thumbnailReady(QString trackId, quint64 generation, QByteArray peaks).
- Produces: diagnostics() returning cacheEntries, inFlightTracks, queuedJobs, and maxActiveWorkers.
- Used by: Tasks 5 and 6.

- [ ] **Step 1: Write failing cache/queue tests**

~~~cpp
void TrackWaveformThumbnailProviderTest::readsExistingV2CacheOnly();
void TrackWaveformThumbnailProviderTest::missingOrCorruptCacheReturnsEmpty();
void TrackWaveformThumbnailProviderTest::coalescesSameTrackIntoOneJob();
void TrackWaveformThumbnailProviderTest::staleGenerationCannotPublish();
void TrackWaveformThumbnailProviderTest::limitsLruTo256Tracks();
~~~

Create fixtures via WaveformCache::save_v2(cachePath, sourcePath, WaveformCacheData{.mix = peaks}). For a miss, supply an existing source fixture without .agwf, wait for empty thumbnailReady, and assert diagnostics cacheEntries equals zero.

- [ ] **Step 2: Run RED**

Run: cmake --build build/msvc-release --target track_waveform_thumbnail_provider_test --parallel 4; ctest --test-dir build/msvc-release -R '^(track_waveform_thumbnail_provider_test|track_waveform_thumbnail_contract_test)$' --output-on-failure

Expected: FAIL because queue API, cache lookup, and guard are missing.

- [ ] **Step 3: Implement cache-only single-worker scheduling**

~~~cpp
void TrackWaveformThumbnailProvider::request(const QString& trackId,
                                             const QString& sourcePath,
                                             quint64 generation)
{
    latestGeneration_[trackId] = generation;
    if (const auto it = lru_.find(trackId); it != lru_.end()) {
        touchLru(trackId);
        emit thumbnailReady(trackId, generation, it->peaks);
        return;
    }
    if (inFlight_.contains(trackId)) return;
    inFlight_.insert(trackId);
    QtConcurrent::run(&workerPool_, [this, trackId, sourcePath, generation] {
        const QByteArray peaks = loadFromV2CacheOnly(sourcePath);
        QMetaObject::invokeMethod(this, [this, trackId, generation, peaks] {
            finishRequest(trackId, generation, peaks);
        }, Qt::QueuedConnection);
    });
}
~~~

Set workerPool max thread count to 1 and submit priority -1. loadFromV2CacheOnly derives cache path from settings cache directory and WaveformCache.key_for, calls only WaveformCache.load_v2, then quantizes data.mix. finishRequest removes in-flight state, rejects superseded generation, stores only non-empty entries, evicts down to 256, then emits one current result. Do not reference WaveformProvider or a prefetch/analyzer API.

- [ ] **Step 4: Add the no-analysis source contract**

~~~powershell
$provider = Join-Path $SourceRoot 'qt/src/track_waveform_thumbnail_provider.cpp'
$forbidden = 'ag_track_analysis\s*\(', 'ag_track_analysis_with_aggregation\s*\(', 'avformat_open_input\s*\('
foreach ($pattern in $forbidden) {
    if (Select-String -LiteralPath $provider -Pattern $pattern -Quiet) {
        throw "Forbidden thumbnail analysis call: $pattern"
    }
}
~~~

Register CTest track_waveform_thumbnail_contract_test with -SourceRoot CMAKE_SOURCE_DIR.

- [ ] **Step 5: Run GREEN**

Run: ctest --test-dir build/msvc-release -R '^(track_waveform_thumbnail_provider_test|track_waveform_thumbnail_contract_test)$' --output-on-failure

Expected: PASS; cache hit yields 128 bytes, miss/corruption yields empty, duplicate request yields one job, stale result is suppressed, cacheEntries is at most 256.

- [ ] **Step 6: Refactor and re-run**

Move only cachePathForSource, touchLru, and finishRequest to private helpers.

Run: ctest --test-dir build/msvc-release -R '^(track_waveform_thumbnail_provider_test|track_waveform_thumbnail_contract_test)$' --output-on-failure

Expected: PASS with maxActiveWorkers equal to 1.

- [ ] **Step 7: Commit**

~~~bash
git add qt/src/track_waveform_thumbnail_provider.hpp qt/src/track_waveform_thumbnail_provider.cpp tests/qt/track_waveform_thumbnail_provider_test.cpp tests/scripts/track_waveform_thumbnail_contract_test.ps1 tests/CMakeLists.txt
git commit -m "feat: load list waveform thumbnails from cache only"
~~~

### Task 3: Render static envelope with a single Scene Graph node

**Files:**
- Create: qt/src/track_waveform_thumbnail_item.hpp
- Create: qt/src/track_waveform_thumbnail_item.cpp
- Create: tests/qt/track_waveform_thumbnail_item_test.cpp
- Modify: qt/CMakeLists.txt
- Modify: tests/CMakeLists.txt

**Interfaces:**
- Produces: TrackWaveformThumbnailItem : QQuickItem with QML properties QByteArray peaks, QColor waveformColor, and read-only int geometryRevision.
- Produces: setPeaks, setWaveformColor, and updatePaintNode.
- Consumes: 128 bytes and color from Tasks 1–2.
- Used by: Task 5 Loader.

- [ ] **Step 1: Write failing item tests**

~~~cpp
void TrackWaveformThumbnailItemTest::createsOneGeometryNodeFor128Bytes();
void TrackWaveformThumbnailItemTest::reusesNodeAndGeometryWhenOnlyColorChanges();
void TrackWaveformThumbnailItemTest::rebuildsOnlyForPeaksOrSize();
void TrackWaveformThumbnailItemTest::hasNoPlaybackUpdatePath();
~~~

Expose updatePaintNode with a test subclass. Assert zero child nodes, 256 vertices, unchanged geometryRevision for color change, and no playback-position property.

- [ ] **Step 2: Run RED**

Run: cmake --build build/msvc-release --target track_waveform_thumbnail_item_test --parallel 4; ctest --test-dir build/msvc-release -R '^track_waveform_thumbnail_item_test$' --output-on-failure

Expected: compile failure because item is absent.

- [ ] **Step 3: Implement one-node geometry**

~~~cpp
QSGNode* TrackWaveformThumbnailItem::updatePaintNode(
    QSGNode* oldNode, UpdatePaintNodeData*)
{
    auto* node = static_cast<QSGGeometryNode*>(oldNode);
    if (width() <= 0 || height() <= 0 || peaks_.size() != kPeakCount) return nullptr;
    if (node == nullptr) node = makeGeometryNode();
    if (geometryDirty_) rebuildGeometry(node->geometry());
    if (colorDirty_) static_cast<QSGFlatColorMaterial*>(
        node->material())->setColor(waveformColor_);
    return node;
}
~~~

makeGeometryNode allocates one QSGGeometryNode, one QSGFlatColorMaterial, and 256 Point2D vertices. rebuildGeometry draws symmetric top/bottom values around half-height. setWaveformColor marks only material color dirty. Do not add a playback-position API.

- [ ] **Step 4: Run GREEN**

Run: ctest --test-dir build/msvc-release -R '^track_waveform_thumbnail_item_test$' --output-on-failure

Expected: PASS under QT_QPA_PLATFORM=offscreen: one node, 256 vertices, geometry persists on color-only change.

- [ ] **Step 5: Refactor and re-run**

Centralize invalidation in private markGeometryDirty and markColorDirty helpers.

Run: ctest --test-dir build/msvc-release -R '^track_waveform_thumbnail_item_test$' --output-on-failure

Expected: PASS with unchanged node count.

- [ ] **Step 6: Commit**

~~~bash
git add qt/src/track_waveform_thumbnail_item.hpp qt/src/track_waveform_thumbnail_item.cpp qt/CMakeLists.txt tests/qt/track_waveform_thumbnail_item_test.cpp tests/CMakeLists.txt
git commit -m "feat: render track waveform thumbnails with scene graph"
~~~

### Task 4: Persist controls and register the QML runtime types

**Files:**
- Modify: qt/src/settings_controller.hpp
- Modify: qt/src/settings_controller.cpp
- Modify: tests/qt/settings_controller_test.cpp
- Modify: qt/src/qml_registration.hpp
- Modify: qt/src/qml_registration.cpp
- Modify: app/main.cpp
- Modify: tests/qt/qml_main_window_test_main.cpp

**Interfaces:**
- Produces: bool listWaveformThumbnailEnabled Q_PROPERTY, default true.
- Produces: QString listWaveformThumbnailMode Q_PROPERTY, only Color36 or Mono, default/fallback Color36.
- Produces: appended TrackWaveformThumbnailProvider pointer argument to register_agplayer_qml_types, singleton registration, and TrackWaveformThumbnailItem type registration.
- Used by: Task 5 QML.

- [ ] **Step 1: Write failing persistence/reset and QML registration tests**

~~~cpp
void SettingsControllerTest::listWaveformThumbnailSettingsPersistAndReset()
{
    SettingsController settings;
    QCOMPARE(settings.listWaveformThumbnailEnabled(), true);
    QCOMPARE(settings.listWaveformThumbnailMode(), QStringLiteral("Color36"));
    settings.setListWaveformThumbnailEnabled(false);
    settings.setListWaveformThumbnailMode(QStringLiteral("Mono"));
    SettingsController reloaded;
    QCOMPARE(reloaded.listWaveformThumbnailEnabled(), false);
    QCOMPARE(reloaded.listWaveformThumbnailMode(), QStringLiteral("Mono"));
}
~~~

Add harness assertions that TrackWaveformThumbnailProvider and TrackWaveformThumbnailItem resolve after import AgPlayer.

- [ ] **Step 2: Run RED**

Run: cmake --build build/msvc-release --target settings_controller_test qml_main_window_test --parallel 4; ctest --test-dir build/msvc-release -R '^(settings_controller_test|qml_main_window_test)$' --output-on-failure

Expected: properties and QML names fail to resolve.

- [ ] **Step 3: Implement settings and ownership**

~~~cpp
void SettingsController::setListWaveformThumbnailMode(const QString& value)
{
    const QString normalized = value == QStringLiteral("Mono")
        ? QStringLiteral("Mono") : QStringLiteral("Color36");
    if (listWaveformThumbnailMode_ == normalized) return;
    listWaveformThumbnailMode_ = normalized;
    persistValue(QStringLiteral("appearance/listWaveformThumbnailMode"), normalized);
    emit listWaveformThumbnailModeChanged();
}
~~~

Load/save both settings in the current appearance QSettings group, include them in restoreDefaults and current all-property notification. Construct TrackWaveformThumbnailProvider thumbnailProvider(&settings) alongside main WaveformProvider in app/main.cpp and QML test harness; pass it to registration. Register provider singleton and item type exactly once.

- [ ] **Step 4: Run GREEN**

Run: ctest --test-dir build/msvc-release -R '^(settings_controller_test|qml_main_window_test)$' --output-on-failure

Expected: PASS for default, persistence, invalid fallback, reset, singleton, and item type.

- [ ] **Step 5: Refactor and re-run**

Group only these two appearance settings with existing main-waveform settings; do not connect them to waveformMode or WaveformProvider.

Run: ctest --test-dir build/msvc-release -R '^(settings_controller_test|qml_main_window_test)$' --output-on-failure

Expected: PASS.

- [ ] **Step 6: Commit**

~~~bash
git add qt/src/settings_controller.hpp qt/src/settings_controller.cpp tests/qt/settings_controller_test.cpp qt/src/qml_registration.hpp qt/src/qml_registration.cpp app/main.cpp tests/qt/qml_main_window_test_main.cpp
git commit -m "feat: add list waveform thumbnail settings"
~~~

### Task 5: Integrate Loader-only visible rows and setting controls

**Files:**
- Modify: app/qml/AgPlayer/components/TrackList.qml
- Modify: app/qml/AgPlayer/SettingsPage.qml
- Modify: tests/qml/tst_main_window.qml
- Modify: app/CMakeLists.txt

**Interfaces:**
- Consumes: request/cancel/thumbnailReady/diagnostics, item type, and settings.
- Produces: TrackList rowHeight 62 enabled and 42 disabled; delegate thumbnailGeneration guards every request/result.
- Produces: listWaveformThumbnailEnabledControl and listWaveformThumbnailModeControl.
- Used by: Task 6 tests.

- [ ] **Step 1: Write failing QML behavior tests**

~~~qml
function test_listWaveformDisabledCreatesNoLoaderOrRequest() {
    SettingsController.listWaveformThumbnailEnabled = false
    const list = trackListComponent.createObject(mainWindow.contentItem)
    verify(findChild(list, "trackWaveformThumbnailLoader") === null)
    compare(TrackWaveformThumbnailProvider.diagnostics().queuedJobs, 0)
    compare(list.rowHeight, 42)
}

function test_reusedDelegateRejectsStaleThumbnailGeneration() {
    // Bind A, recycle to B, deliver A generation, verify visible peaks are B's.
}
~~~

Also test stable Color36 across sort/filter, Mono color-only geometryRevision, miss empty/static baseline, and current selection/search/sort/menu tests.

- [ ] **Step 2: Run RED**

Run: cmake --build build/msvc-release --target qml_main_window_test --parallel 4; ctest --test-dir build/msvc-release -R '^qml_main_window_test$' --output-on-failure

Expected: Loader, controls, generation guard, and disabled contract fail.

- [ ] **Step 3: Add conditional generation-safe Loader**

~~~qml
readonly property int rowHeight:
    SettingsController.listWaveformThumbnailEnabled ? 62 : 42

Loader {
    id: thumbnailLoader
    objectName: "trackWaveformThumbnailLoader"
    active: SettingsController.listWaveformThumbnailEnabled && rowItem.visible
    sourceComponent: TrackWaveformThumbnailItem {
        peaks: rowItem.thumbnailPeaks
        waveformColor: SettingsController.listWaveformThumbnailMode === "Mono"
            ? Theme.waveformSolidBaseColor
            : TrackWaveformThumbnailProvider.colorForTrackId(rowItem.trackId)
    }
}
~~~

Place it below title in the existing title cell, 8–10 logical pixels high. Add required path, thumbnailGeneration, thumbnailPeaks. Request only after Loader activation. Accept signals only when ID and generation match; cancel previous pair on reuse/destruction. Mono binds theme solid waveform token and must not call colorForTrackId or request data.

- [ ] **Step 4: Add SettingsPage controls**

~~~qml
ThemedSwitch {
    objectName: "listWaveformThumbnailEnabledControl"
    text: qsTr("显示歌曲列表波形缩略图")
    checked: SettingsController.listWaveformThumbnailEnabled
    onToggled: SettingsController.listWaveformThumbnailEnabled = checked
}
ComboBox {
    objectName: "listWaveformThumbnailModeControl"
    enabled: SettingsController.listWaveformThumbnailEnabled
    model: ["Color36", "Mono"]
    currentIndex: SettingsController.listWaveformThumbnailMode === "Mono" ? 1 : 0
    onActivated: SettingsController.listWaveformThumbnailMode = currentText
}
~~~

Locate in 外观与波形 → 歌曲列表; preserve Chinese-first presentation and one TrackList component.

- [ ] **Step 5: Run GREEN**

Run: ctest --test-dir build/msvc-release -R '^qml_main_window_test$' --output-on-failure

Expected: disabled has 42-pixel rows and zero loader/request/color work; enabled cache data renders; stale result rejected; Mono preserves geometry; existing list behavior passes.

- [ ] **Step 6: Refactor and re-run**

Extract only local requestThumbnail and clearThumbnailRequest QML helpers.

Run: ctest --test-dir build/msvc-release -R '^qml_main_window_test$' --output-on-failure

Expected: no duplicate request for visible track/generation.

- [ ] **Step 7: Commit**

~~~bash
git add app/qml/AgPlayer/components/TrackList.qml app/qml/AgPlayer/SettingsPage.qml app/CMakeLists.txt tests/qml/tst_main_window.qml
git commit -m "feat: show cache-backed waveforms in track list"
~~~

### Task 6: Add complete CMake/QML and 10,000-row pressure coverage

**Files:**
- Create: tests/stress/track_waveform_thumbnail_stress_test.cpp
- Modify: tests/CMakeLists.txt
- Modify: qt/CMakeLists.txt
- Modify: tests/qml/tst_main_window.qml

**Interfaces:**
- Consumes: diagnostics, geometryRevision, Loader object name.
- Produces: CTest target track_waveform_thumbnail_stress_test, timeout 35 seconds.
- Used by: Task 7 evidence.

- [ ] **Step 1: Write failing pressure test**

~~~cpp
void TrackWaveformThumbnailStressTest::keepsTenThousandLogicalRowsBounded()
{
    TrackWaveformThumbnailProvider provider(&settings_);
    for (int row = 0; row < 10000; ++row)
        provider.request(QStringLiteral("track-%1").arg(row),
                         fixturePath(row % 300), row + 1);
    QTRY_COMPARE(provider.diagnostics().value("maxActiveWorkers").toInt(), 1);
    QTRY_VERIFY(provider.diagnostics().value("cacheEntries").toInt() <= 256);
    QTRY_VERIFY(provider.diagnostics().value("inFlightTracks").toInt() == 0);
}
~~~

Seed 300 v2 cache fixtures with WaveformCache::save_v2. Add a QML repeated-scroll check that active thumbnail Loaders are bounded to visible delegates plus ListView cache buffer.

- [ ] **Step 2: Register and run RED**

~~~cmake
add_executable(track_waveform_thumbnail_stress_test
    stress/track_waveform_thumbnail_stress_test.cpp)
target_link_libraries(track_waveform_thumbnail_stress_test PRIVATE agplayer_qt Qt6::Test)
add_test(NAME track_waveform_thumbnail_stress_test COMMAND track_waveform_thumbnail_stress_test)
set_tests_properties(track_waveform_thumbnail_stress_test PROPERTIES TIMEOUT 35)
~~~

Run: cmake --build build/msvc-release --target track_waveform_thumbnail_stress_test qml_main_window_test --parallel 4; ctest --test-dir build/msvc-release -R '^(track_waveform_thumbnail_stress_test|qml_main_window_test)$' --output-on-failure

Expected: FAIL until target, fixtures, and scroll assertions exist.

- [ ] **Step 3: Complete test/build registration**

Add production files to agplayer_qt. Add offscreen scene graph environment and existing Qt Core PATH property for tests. Register each QML type once. Do not introduce packages or another framework.

- [ ] **Step 4: Run GREEN focused suite**

Run: ctest --test-dir build/msvc-release -R '^(track_waveform_thumbnail_provider_test|track_waveform_thumbnail_item_test|track_waveform_thumbnail_stress_test|track_waveform_thumbnail_contract_test|settings_controller_test|qml_main_window_test)$' --output-on-failure

Expected: PASS; one worker, at most 256 entries, 10,000 logical rows without 10,000 resident thumbnails, disabled Loader absence.

- [ ] **Step 5: Refactor and re-run**

Use repository CTest property patterns only.

Run: cmake --build build/msvc-release --parallel 4; ctest --test-dir build/msvc-release -R '^(track_waveform_thumbnail_provider_test|track_waveform_thumbnail_item_test|track_waveform_thumbnail_stress_test|track_waveform_thumbnail_contract_test|settings_controller_test|qml_main_window_test)$' --output-on-failure

Expected: build and focused suite PASS.

- [ ] **Step 6: Commit**

~~~bash
git add qt/CMakeLists.txt tests/CMakeLists.txt tests/stress/track_waveform_thumbnail_stress_test.cpp tests/qml/tst_main_window.qml
git commit -m "test: cover bounded track waveform thumbnail loading"
~~~

### Task 7: Perform desktop acceptance and preserve traceable evidence

**Files:**
- Create: docs/development/2026-08-20-track-waveform-thumbnails.md
- Modify: design-qa.md

**Interfaces:**
- Consumes: Tasks 1–6 implementation and CTest targets.
- Produces: evidence-backed requirement matrix with explicit not-run entries.
- Produces: no unsupported completion claim.

- [ ] **Step 1: Create pre-run evidence matrix**

~~~markdown
| Requirement | Code/Test Evidence | Desktop Evidence | Result |
| --- | --- | --- | --- |
| Disabled zero work | tst_main_window.qml disabled Loader test | pending | not-run |
| Cache-only 128-byte v2 read | provider test and source contract | pending | not-run |
| One static Scene Graph node | item test | pending | not-run |
~~~

Add FNV Color36 stability, Mono color-only switch, stale generation, worker/LRU bound, 10K scroll, playback isolation, and Dark/Light 100/125/150/175/200% DPI rows.

- [ ] **Step 2: Run automated verification**

Run: cmake --build build/msvc-release --parallel 4; ctest --test-dir build/msvc-release -R '^(track_waveform_thumbnail_provider_test|track_waveform_thumbnail_item_test|track_waveform_thumbnail_stress_test|track_waveform_thumbnail_contract_test|settings_controller_test|qml_main_window_test|waveform_provider_test|waveform_item_test)$' --output-on-failure

Expected: every listed test PASS. Record command, build directory, timestamp, and count. If a main-waveform test fails, record playback isolation failed and stop release claims.

- [ ] **Step 3: Run real desktop checks**

Run: scripts/qa-main-smoke.ps1 -BuildDirectory build/msvc-release

Launch build/msvc-release/app/AgPlayer.exe with cache-hit and cache-miss tracks. Capture 1447×1087 Color36, Mono, disabled screenshots. Confirm disabled 42 px/no baseline; miss empty/static baseline without analysis; sort/search/filter color stability; rapid scroll does not disturb playback, seek, or main waveform; setting applies immediately.

- [ ] **Step 4: Measure scale/load cases**

At Dark/Light and 100/125/150/175/200% DPI capture Color36/Mono/disabled. At 1,000 and 10,000 tracks scroll, sort, search, playlist/tag/folder switch during lossless playback, seek, and change-track. Record CPU, memory, disk I/O, worker/cache/node counts before and after disable. Mark unavailable macOS/Linux/hardware cases not-run with reason.

- [ ] **Step 5: Correct demonstrated P0/P1/P2 defects and re-run**

Record screenshot, root cause, changed file, and rerun command per defect. Set design-qa.md passed only when every P0/P1/P2 defect has before/after evidence; otherwise preserve actual status.

- [ ] **Step 6: Commit**

~~~bash
git add docs/development/2026-08-20-track-waveform-thumbnails.md design-qa.md
git commit -m "docs: record track waveform thumbnail validation"
~~~

## Self-Review

- Spec coverage: Tasks 1–2 implement cache-only v2 reads, 128 bytes, FNV palette, LRU, single worker, coalescing, and generation. Task 3 gives one static Scene Graph node. Task 4 adds settings and registration. Task 5 gives Loader-only integration and modes. Task 6 adds CMake, QML, and 10K coverage. Task 7 covers visual, playback, theme, DPI, and evidence gates.
- Placeholder scan: every task specifies files, interfaces, test names, RED/GREEN commands, expected results, and commit.
- Type consistency: QML request(trackId, sourcePath, generation), signal thumbnailReady(trackId, generation, peaks), and item peaks/waveformColor all originate in Tasks 1–4.

## Execution Handoff

Plan complete and saved to docs/superpowers/plans/2026-08-20-track-waveform-thumbnails.md. Two execution options:

1. **Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

2. **Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints

Which approach?

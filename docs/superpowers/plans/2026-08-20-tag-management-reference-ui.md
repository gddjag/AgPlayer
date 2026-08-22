# AgPlayer 标签管理参考界面 Implementation Plan

> **Supplementary plan:** Execute through `2026-08-20-tag-waveform-ui-master.md`. The master plan owns task order, worktree selection, and shared-file integration.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在保持主播放器和可吸附 `ListWindow` 架构、播放核心及主波形链路不变的前提下，交付与 `标签管理.png` 同状态可对比的统一歌曲列表、标签管理、资源目录、cache-only 缩略波形与真实 Windows 验收证据。

**Architecture:** 以 `LibraryModel → LibraryFilterModel → TrackList` 为唯一歌曲数据链：`TagModel` 只维护由 `TrackRecord::tags` 派生的目录元数据与增量计数，`LibraryNavigationModel` 只投影可见导航节点，绝不复制歌曲表。列表缩略图通过一个低优先级、有界的 cache-only provider 读取既有 `.agwf` v2 mix Peak，再由一个 `QQuickItem` 用单一 `QSGGeometryNode` 绘制；QML 只负责连续三栏布局、交互和按需 Loader。

**Tech Stack:** Qt 6.7 / Qt Quick Scene Graph / QML、C++17、QtConcurrent、Qt Test、QML Test、CMake/CTest、Windows PowerShell。

**Spec:** `docs/superpowers/specs/2026-08-20-track-waveform-tag-management-design.md`（执行前必须重读）；桌面约束来源：`C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\开发要求.txt`；视觉真值：`C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\标签管理.png`（1447×1087）。

## Global Constraints

- 实施开始前用 `git status --short` 和 `git worktree list` 确认状态；当前 `D:\ai\AgPlayer` 存在其他工作的未提交改动，执行必须使用从当前基线创建的独立 worktree，禁止撤销、覆盖或提交这些改动。
- 保留现有主播放器窗口、`ListWindow`、`WindowController`、窗口吸附/分离、主播放器 `WaveformProvider`/`WaveformItem`、Seek、hover、BPM、播放速度及 FFmpeg/miniaudio 播放链路；下方三个区域只是在同一个 `ListWindow` 内容面中的连续容器，不得拆成三个窗口或三张卡片。
- 参考 1447×1087：下方工作区约 1420×684，左栏固定 256 logical px，右栏固定 328 logical px，中央自适应且最小约 680 logical px；共用背景与外描边，仅用 1 logical px 分隔线；表头 56–58、缩略图开启行高 62、关闭行高 42、筛选条 66–68、封面 34×34、缩略波形 8–10（最高 12）、标签胶囊高 28/圆角 14/三列/列间距 8–10。
- 固定歌曲列顺序为 `# | 歌曲 | 收藏 | 艺术家 | 专辑 | 评分 | BPM | 时长`；只保留一个 `TrackList.qml`，歌单、标签筛选和资源文件夹仅改变 `LibraryFilterModel` 条件。
- 标签关联真值始终是 `TrackRecord::tags` 和现有 `library.json` 原子保存；标签目录仅用 AppData 下很小的 JSON 保存空标签和显式颜色，不新增数据库，不写入音频文件。
- 标签大小写无关去重、显示名保留用户输入；重命名保持颜色；删除只解除标签关联并删除目录条目，绝不删除歌曲、音频或磁盘文件；删除当前筛选标签仅清除标签条件，保留搜索、评分、BPM、歌单和目录范围。
- `LibraryFilterModel` 标签条件必须与关键词、评分、BPM、歌单/文件夹范围组合，且仅使代理过滤；点击标签不得触发导入、扫描、音频解码或波形分析。
- 资源目录添加仅保存 AgPlayer 引用并调用现有后台扫描；移除只删除引用及 watcher，绝不删除磁盘目录；子目录在首次展开时只生成一层节点，数量由已导入 Track 路径索引增量维护，GUI 线程不得递归扫描。
- 允许 Windows/Qt 系统目录拖入资源区域，目录走监控根加异步扫描、普通音频走已有 `ImportController`；失败/取消不得改变文件、歌单、标签或监控目录。
- 列表缩略图只允许 cache-only 读取现有 `.agwf` v2 mix Peak：缓存未命中、损坏或过期只显示空白或 1 px 基线并写可诊断日志，绝不调用 `ag_track_analysis_with_aggregation`、解码或自动预分析。
- 缩略图为固定 128 字节；Provider 最大 256 首 LRU、相同 Track ID 请求去重、最多一个低优先级线程池 worker、读取/降采样/量化都在 GUI 线程外。异步回调必须校验对象生命周期、Track ID 和请求代际，delegate 复用不得串图。
- 关闭 `listWaveformThumbnailEnabled` 时不得实例化缩略图 Item、请求 Provider、读取缓存、计算颜色或创建 Scene Graph Node；开启时只为可见 delegate 创建。`Color36` 颜色为持久 `trackId` 的 FNV-1a 32-bit `% 36`，不得依赖行号、随机数或进程 Hash；`Mono` 仅变更渲染颜色。
- 禁止新第三方依赖、单独数据库、图片波形缓存、实时 FFT、Shader、Glow、Blur、QML Canvas、`QQuickPaintedItem`、每 Peak 一个 QML Item、无界缓存、常驻扫描线程或平台私有绘制；新 C++ 必须为 C++17、RAII、const-correct。
- 颜色、圆角、间距、描边、选中态进入 `Theme.qml` 语义 token；暗色参照近黑蓝背景、紫灰边线、暖白正文、棕金次要文字和低饱和深紫选中态；浅色主题保持语义并验证普通正文至少 4.5:1 对比度。全部可见按钮/图标必须有真实副作用，无占位控件。
- 开发遵循红—绿—重构。自动化通过不等于视觉、真实硬件、10,000 首滚动或听感验收；未实际执行的项目必须记录为待验收，不能写为已通过。此范围不制作 EXE 安装包。

---

## File Structure and Responsibility Map

- `qt/src/tag_directory_store.hpp`, `qt/src/tag_directory_store.cpp`：AppData 标签目录元数据的原子读取/保存、损坏文件恢复输入；不持有曲库。
- `qt/src/tag_model.hpp`, `qt/src/tag_model.cpp`：从 `LibraryModel` tags 派生的 `QAbstractListModel`、增量计数、选择和标签目录编辑 API。
- `qt/src/library_model.hpp`, `qt/src/library_model.cpp`：发出旧/新 tags 的精确变更信号，并提供按标签批量重命名/移除的最小受影响行更新。
- `qt/src/library_filter_model.hpp`, `qt/src/library_filter_model.cpp`：单一 `tagFilter` 与现有搜索、评分、BPM、歌单/分类过滤的逻辑与测试面。
- `qt/src/library_navigation_model.hpp`, `qt/src/library_navigation_model.cpp`：扁平、可虚拟化的可见导航节点投影和按需一层目录展开；复用 `PlaylistModel`、`LibraryModel`、`LibraryManagerController`。
- `qt/src/track_waveform_thumbnail_provider.hpp`, `qt/src/track_waveform_thumbnail_provider.cpp`：`.agwf` v2 cache-only 读取、128 bucket 量化、256 项 LRU、去重与低优先级异步结果。
- `qt/src/track_waveform_thumbnail_item.hpp`, `qt/src/track_waveform_thumbnail_item.cpp`：单 `QSGGeometryNode` 静态缩略图渲染，无音频读取与无 provider 线程逻辑。
- `qt/src/settings_controller.hpp`, `qt/src/settings_controller.cpp`、`app/qml/AgPlayer/SettingsPage.qml`、`app/qml/AgPlayer/theme/Theme.qml`：列表缩略图设置、恢复默认、持久化、即时生效和语义 token。
- `app/main.cpp`、`qt/src/qml_registration.cpp`、`qt/CMakeLists.txt`、`app/CMakeLists.txt`：新增模型/provider 的装配、QML 注册和编译资源清单；不替换既有窗口控制器。
- `app/qml/AgPlayer/ListWindow.qml`、`app/qml/AgPlayer/components/SideNavigation.qml`、`app/qml/AgPlayer/components/TrackList.qml`、`app/qml/AgPlayer/components/SearchFilter.qml`、`app/qml/AgPlayer/components/TagManagementPanel.qml`、`app/qml/AgPlayer/components/TrackWaveformThumbnail.qml`：连续三栏、虚拟化标签网格、共享列表、底部筛选、右键菜单、目录投放和一次性拖拽预览。
- `qt/src/native_drop_router.hpp`, `qt/src/native_drop_router.cpp`：只增加资源区域需要的落点路由信息；保持 Main/List/AudioTools 的既有文件导入行为。
- `tests/qt/tag_directory_store_test.cpp`、`tests/qt/tag_model_test.cpp`、`tests/qt/library_navigation_model_test.cpp`、`tests/qt/track_waveform_thumbnail_provider_test.cpp`、`tests/qt/track_waveform_thumbnail_item_test.cpp`、`tests/qt/library_filter_model_test.cpp`、`tests/qt/settings_controller_test.cpp`、`tests/qt/native_drop_router_test.cpp`、`tests/qml/tst_main_window.qml`、`tests/scripts/tag_management_layout_contract_test.ps1`、`tests/CMakeLists.txt`：模型、渲染契约和交互回归证据。
- `docs/development/2026-08-20-tag-management-reference-ui.md`、`docs/qa/2026-08-20-tag-management-reference-ui-acceptance.md`、`design-qa/tag-management-reference-1447x1087.png`、`design-qa/tag-management-comparison-1447x1087.png`、根目录 `design-qa.md`：需求追溯、真实运行截图、同尺寸比较和最终状态。

### Task 1: 标签目录持久化与曲库标签差量接口

**Files:**
- Create: `qt/src/tag_directory_store.hpp`
- Create: `qt/src/tag_directory_store.cpp`
- Create: `qt/src/tag_model.hpp`
- Create: `qt/src/tag_model.cpp`
- Modify: `qt/src/library_model.hpp`
- Modify: `qt/src/library_model.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/qt/tag_directory_store_test.cpp`
- Create: `tests/qt/tag_model_test.cpp`

**Interfaces:**
- Produces: `struct TagDirectoryEntry { QString canonicalName; QString displayName; QColor color; bool hasExplicitColor; };` and `class TagDirectoryStore { public: static QList<TagDirectoryEntry> load(const QString& path, bool* recovered); static bool save(const QString& path, const QList<TagDirectoryEntry>& entries); };`.
- Produces: `class TagModel : public QAbstractListModel` with roles `CanonicalNameRole`, `DisplayNameRole`, `TrackCountRole`, `ColorRole`, `SelectedRole`; `setLibraryModel(LibraryModel*)`, `setStoragePath(const QString&)`, `setSelectedTag(const QString&)`, `Q_INVOKABLE bool addTag(const QString&)`, `renameTag(const QString&, const QString&)`, `removeTag(const QString&)`, and `setTagColor(const QString&, const QColor&)`.
- Produces: `LibraryModel::tagsChanged(const QString& trackId, const QStringList& oldTags, const QStringList& newTags)`, `Q_INVOKABLE bool renameTagEverywhere(const QString& oldTag, const QString& newTag)`, and `Q_INVOKABLE int removeTagEverywhere(const QString& tag)`; each changed track emits only its own `dataChanged(..., {TagsRole})` and ends with one existing persistence request.
- Consumes: existing `TrackRecord::tags`, `LibraryModel::setTags`, and the `library.json` atomic save path; never creates a second track model.

- [ ] **Step 1: Write the failing persistence and delta tests.**

```cpp
void TagModelTest::renamingAndRemovingOnlyTouchAffectedTracks()
{
    LibraryModel library;
    library.replaceAll({track("a", {"电音"}), track("b", {"电音", "人声"}),
                        track("c", {"流行"})});
    TagModel tags;
    tags.setLibraryModel(&library);
    QSignalSpy changed(&library, &QAbstractItemModel::dataChanged);
    QVERIFY(tags.renameTag(QStringLiteral("电音"), QStringLiteral("电子")));
    QCOMPARE(library.trackForId("a").value("tags").toStringList(), {"电子"});
    QCOMPARE(changed.count(), 2);
    QVERIFY(tags.removeTag(QStringLiteral("电子")));
    QCOMPARE(library.trackForId("c").value("tags").toStringList(), {"流行"});
}
```

```cpp
void TagDirectoryStoreTest::corruptDirectoryRebuildsFromTrackTags()
{
    writeFile(storagePath, QByteArrayLiteral("{ invalid json"));
    TagModel tags;
    tags.setStoragePath(storagePath);
    tags.setLibraryModel(&libraryWithTags({"DJ", "流行"}));
    QCOMPARE(tags.rowCount(), 2);
    QVERIFY(tags.recoveredDirectory());
}
```

- [ ] **Step 2: Build and run the two new tests to verify RED.**

Run: `cmake --build --preset windows-msvc-debug --target tag_directory_store_test tag_model_test && ctest --test-dir build/debug --output-on-failure -R "^(tag_directory_store_test|tag_model_test)$"`

Expected: FAIL because the test targets and `TagModel`/`TagDirectoryStore` interfaces do not exist.

- [ ] **Step 3: Implement the smallest complete directory and delta model.**

```cpp
bool LibraryModel::setTags(const QString& trackId, const QStringList& requested)
{
    const int row = indexForTrackId(trackId);
    const QStringList normalized = normalizeTags(requested);
    if (row < 0 || tracks_[row].tags == normalized) return row >= 0;
    const QStringList previous = tracks_[row].tags;
    tracks_[row].tags = normalized;
    emit dataChanged(index(row), index(row), {TagsRole});
    emit tagsChanged(trackId, previous, normalized);
    emit flushRequested();
    return true;
}
```

Implement JSON through `QSaveFile`; on unreadable/corrupt JSON return an empty directory with `recovered=true`, retain all song tags, then let `TagModel` merge them. Normalize comparison with `QString::toCaseFolded()`, preserve the first user display spelling, preserve color across rename, and never call `replaceAll()` or audio probing.

- [ ] **Step 4: Run GREEN checks.**

Run: `ctest --test-dir build/debug --output-on-failure -R "^(tag_directory_store_test|tag_model_test|library_model_test|library_store_test)$"`

Expected: PASS; tests prove empty tags persist, corrupt directory retains song tags, case-insensitive duplicates coalesce, rename preserves color, delete unlinks only affected tracks, and no unrelated row emits `dataChanged`.

- [ ] **Step 5: Refactor without changing behavior.**

Extract shared tag normalization into one private helper owned by `LibraryModel`; keep `TagModel` responsible for catalog ordering/counts only. Re-run the Step 4 command and `git diff --check`.

- [ ] **Step 6: Commit the independently tested change.**

```powershell
git add qt/src/tag_directory_store.hpp qt/src/tag_directory_store.cpp qt/src/tag_model.hpp qt/src/tag_model.cpp qt/src/library_model.hpp qt/src/library_model.cpp qt/CMakeLists.txt tests/CMakeLists.txt tests/qt/tag_directory_store_test.cpp tests/qt/tag_model_test.cpp
git commit -m "feat: add incremental track tag directory model"
```

### Task 2: 标签过滤组合与选择生命周期

**Files:**
- Modify: `qt/src/library_filter_model.hpp`
- Modify: `qt/src/library_filter_model.cpp`
- Modify: `qt/src/tag_model.hpp`
- Modify: `qt/src/tag_model.cpp`
- Modify: `tests/qt/library_filter_model_test.cpp`
- Modify: `tests/qt/tag_model_test.cpp`

**Interfaces:**
- Produces: `Q_PROPERTY(QString tagFilter READ tagFilter WRITE setTagFilter NOTIFY tagFilterChanged)` and `void LibraryFilterModel::setTagFilter(const QString& tag)`; `filterAcceptsRow()` requires all non-empty conditions to match.
- Produces: `TagModel::tagActivated(const QString& canonicalName)` and `TagModel::selectedTag()`; QML binds activation to `filterModel.tagFilter`, not to imports/scans.
- Consumes: Task 1 `TagModel` canonical names and existing `searchText`, `exactRating`, `minBpm`, `maxBpm`, `category`, and `PlaylistModel` ordering.

- [ ] **Step 1: Write failing composition and deletion tests.**

```cpp
void LibraryFilterModelTest::tagFilterCombinesWithoutModelReset()
{
    filter.setSearchText(QStringLiteral("夜"));
    filter.setExactRating(5);
    filter.setMinBpm(120.0);
    filter.setMaxBpm(130.0);
    QSignalSpy reset(&filter, &QAbstractItemModel::modelReset);
    filter.setTagFilter(QStringLiteral("电音"));
    QCOMPARE(filter.rowCount(), 1);
    QCOMPARE(reset.count(), 0);
}

void TagModelTest::deletingSelectedTagClearsOnlyTagFilter()
{
    filter.setSearchText(QStringLiteral("night"));
    filter.setExactRating(4);
    filter.setTagFilter(QStringLiteral("电音"));
    QVERIFY(tags.removeTag(QStringLiteral("电音")));
    QCOMPARE(filter.tagFilter(), QString());
    QCOMPARE(filter.searchText(), QStringLiteral("night"));
    QCOMPARE(filter.exactRating(), 4);
}
```

- [ ] **Step 2: Run RED.**

Run: `cmake --build --preset windows-msvc-debug --target library_filter_model_test tag_model_test && ctest --test-dir build/debug --output-on-failure -R "^(library_filter_model_test|tag_model_test)$"`

Expected: FAIL because `tagFilter` and selected-tag clearing do not exist.

- [ ] **Step 3: Add the single proxy condition and precise invalidation.**

```cpp
bool LibraryFilterModel::rowMatchesTag(int sourceRow) const
{
    if (tagFilter_.isEmpty()) return true;
    const auto tags = sourceModel()->data(sourceModel()->index(sourceRow, 0),
                                          LibraryModel::TagsRole).toStringList();
    return std::any_of(tags.cbegin(), tags.cend(), [this](const QString& tag) {
        return tag.compare(tagFilter_, Qt::CaseInsensitive) == 0;
    });
}
```

Call `invalidateRowsFilter()` only after a tag condition changes; when the selected directory tag disappears, clear `tagFilter` without touching other filter properties. Connect only tag-related source `dataChanged` to this path, preserving playlist order and avoiding full source-model reset.

- [ ] **Step 4: Run GREEN checks.**

Run: `ctest --test-dir build/debug --output-on-failure -R "^(library_filter_model_test|tag_model_test|playlist_model_test)$"`

Expected: PASS for tag + keyword/rating/BPM/category combinations, case-insensitive tags, live count changes, selected-tag deletion, and custom-playlist order.

- [ ] **Step 5: Refactor and guard the cost boundary.**

Centralize case-folded matching in the existing row predicate sequence; assert through a test double that `TagModel::tagActivated` causes no `ImportController` call and no waveform analysis request. Re-run Step 4 and `git diff --check`.

- [ ] **Step 6: Commit.**

```powershell
git add qt/src/library_filter_model.hpp qt/src/library_filter_model.cpp qt/src/tag_model.hpp qt/src/tag_model.cpp tests/qt/library_filter_model_test.cpp tests/qt/tag_model_test.cpp
git commit -m "feat: filter shared track list by tag"
```

### Task 3: 可虚拟化左侧导航与资源文件夹投影

**Files:**
- Create: `qt/src/library_navigation_model.hpp`
- Create: `qt/src/library_navigation_model.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `app/main.cpp`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/qt/library_navigation_model_test.cpp`
- Modify: `tests/qt/library_manager_controller_test.cpp`

**Interfaces:**
- Produces: `class LibraryNavigationModel : public QAbstractListModel` with roles `NodeIdRole`, `NodeTypeRole`, `DepthRole`, `DisplayNameRole`, `CountRole`, `ExpandedRole`, `CanExpandRole`, `OperationRole`; properties `LibraryModel* libraryModel`, `PlaylistModel* playlistModel`, `LibraryManagerController* libraryManager`; invokables `toggleExpanded(const QString& nodeId)`, `activate(const QString& nodeId)`, `removeFolderReference(const QString& rootPath)`.
- Produces: `nodeActivated(const QString& category, const QString& folderPath)` and `folderRemovalRequested(const QString& rootPath)`; `app/main.cpp` owns this model and supplies it to ListWindow context without re-creating it per view.
- Consumes: existing `LibraryManagerController::monitoredFolders()`, `addMonitoredFolderUrl()`, `removeMonitoredFolder()`, `PlaylistModel`, and LibraryModel signals; directories are never recursively scanned by this model.

- [ ] **Step 1: Write failing flat-tree and folder safety tests.**

```cpp
void LibraryNavigationModelTest::expandChangesOnlyVisibleChildRange()
{
    navigation.setMonitoredRoots({rootA, rootB});
    const int before = navigation.rowCount();
    QSignalSpy inserted(&navigation, &QAbstractItemModel::rowsInserted);
    navigation.toggleExpanded(rootNodeId(rootA));
    QCOMPARE(inserted.count(), 1);
    QVERIFY(navigation.rowCount() > before);
    QCOMPARE(navigation.data(navigation.index(1), LibraryNavigationModel::DepthRole), 1);
}

void LibraryManagerControllerTest::removeFolderReferenceNeverDeletesDiskDirectory()
{
    QTemporaryDir folder;
    QVERIFY(manager.addMonitoredFolder(folder.path()));
    QVERIFY(navigation.removeFolderReference(folder.path()));
    QVERIFY(QFileInfo::exists(folder.path()));
    QVERIFY(!manager.monitoredFolders().contains(folder.path()));
}
```

- [ ] **Step 2: Run RED.**

Run: `cmake --build --preset windows-msvc-debug --target library_navigation_model_test library_manager_controller_test && ctest --test-dir build/debug --output-on-failure -R "^(library_navigation_model_test|library_manager_controller_test)$"`

Expected: FAIL because the navigation model/roles and directory removal coordination are absent.

- [ ] **Step 3: Implement the visible-node projection.**

Use `beginInsertRows`/`beginRemoveRows` solely for the one expanded root's visible child interval. Build library, favorite, playlist, tag-management and monitored-root nodes from existing models; create direct child directories only upon `toggleExpanded`, calculate counts from a maintained normalized track-path index, and update only affected node rows on insertion/removal/tag changes. Route a node activation into the existing filter category/folder scope; do not instantiate another `TrackList`.

- [ ] **Step 4: Run GREEN checks.**

Run: `ctest --test-dir build/debug --output-on-failure -R "^(library_navigation_model_test|library_manager_controller_test|library_model_test|playlist_model_test)$"`

Expected: PASS for `＞`/`∨` semantics, section counts, one-level lazy expansion, added/removed track count updates, monitored root persistence, unavailable-root display, and non-destructive remove.

- [ ] **Step 5: Refactor model subscriptions.**

Store and disconnect `QMetaObject::Connection` values when source models change; keep data-row updates separate from structural rows. Re-run Step 4 and `git diff --check`.

- [ ] **Step 6: Commit.**

```powershell
git add qt/src/library_navigation_model.hpp qt/src/library_navigation_model.cpp qt/CMakeLists.txt app/main.cpp tests/CMakeLists.txt tests/qt/library_navigation_model_test.cpp tests/qt/library_manager_controller_test.cpp
git commit -m "feat: add virtualized library navigation model"
```

### Task 4: Cache-only 128-bucket 缩略图数据服务

**Files:**
- Create: `qt/src/track_waveform_thumbnail_provider.hpp`
- Create: `qt/src/track_waveform_thumbnail_provider.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `app/main.cpp`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/qt/track_waveform_thumbnail_provider_test.cpp`

**Interfaces:**
- Produces: `using TrackWaveformThumbnail = QByteArray;` with exactly 128 unsigned amplitude bytes; `class TrackWaveformThumbnailProvider : public QObject` with `setCacheDirectory(const QString&)`, `setEnabled(bool)`, `Q_INVOKABLE quint64 request(const QString& trackId, const QString& sourcePath, quint64 delegateGeneration)`, `Q_INVOKABLE void release(const QString& trackId, quint64 delegateGeneration)`, and `thumbnailReady(const QString& trackId, quint64 delegateGeneration, const QByteArray& peaks)`.
- Produces: pure `static QByteArray quantizeMixPeaks(const std::vector<float>& mix)` and `static QColor color36ForTrackId(const QString& trackId)`; Provider owns a 256-key LRU and one low-priority `QThreadPool` worker.
- Consumes: `agplayer::WaveformCache::key_for()` and `WaveformCache::load_v2()` in existing `core/src/waveform_cache.hpp`; it never calls `WaveformProvider::loadForTrack`, `prefetchTracks`, decoder APIs, or `ag_track_analysis_with_aggregation`.

- [ ] **Step 1: Write failing deterministic/cost-boundary tests.**

```cpp
void TrackWaveformThumbnailProviderTest::cacheMissNeverStartsAnalysis()
{
    TrackWaveformThumbnailProvider provider;
    provider.setCacheDirectory(temp.path());
    QSignalSpy ready(&provider, &TrackWaveformThumbnailProvider::thumbnailReady);
    provider.request(QStringLiteral("track-a"), missingAudioPath, 7);
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(ready.front().at(2).toByteArray().size(), 0);
    QCOMPARE(provider.analysisInvocationCountForTest(), 0);
}

void TrackWaveformThumbnailProviderTest::quantizationKeepsTransientAndIsStable()
{
    const QByteArray peaks = TrackWaveformThumbnailProvider::quantizeMixPeaks(
        {0.0F, 0.01F, 1.0F, std::numeric_limits<float>::quiet_NaN()});
    QCOMPARE(peaks.size(), 128);
    QVERIFY(std::any_of(peaks.cbegin(), peaks.cend(), [](char v) { return v != 0; }));
    QCOMPARE(TrackWaveformThumbnailProvider::color36ForTrackId("same"),
             TrackWaveformThumbnailProvider::color36ForTrackId("same"));
}
```

- [ ] **Step 2: Run RED.**

Run: `cmake --build --preset windows-msvc-debug --target track_waveform_thumbnail_provider_test && ctest --test-dir build/debug --output-on-failure -R "^track_waveform_thumbnail_provider_test$"`

Expected: FAIL because the provider, fixed bucket algorithm, LRU and cache-only instrumentation do not exist.

- [ ] **Step 3: Implement cache-only load, continuous bucket maximum and LRU.**

```cpp
QByteArray TrackWaveformThumbnailProvider::quantizeMixPeaks(
    const std::vector<float>& mix)
{
    QByteArray result(128, '\\0');
    for (int bucket = 0; bucket < result.size(); ++bucket) {
        const auto first = mix.size() * static_cast<std::size_t>(bucket) / 128U;
        const auto last = mix.size() * static_cast<std::size_t>(bucket + 1) / 128U;
        float maximum = 0.0F;
        for (auto i = first; i < last; ++i)
            if (std::isfinite(mix[i])) maximum = std::max(maximum, std::abs(mix[i]));
        result[bucket] = static_cast<char>(std::lround(std::clamp(maximum, 0.0F, 1.0F) * 255.0F));
    }
    return result;
}
```

Resolve `.agwf` from the same cache directory/key/aggregation convention as `WaveformProvider`, call only `load_v2`, return an empty byte array on all misses/errors, coalesce duplicate `(trackId, sourcePath)` loads, enforce 256 LRU keys, and reject results when the supplied generation was released/replaced.

- [ ] **Step 4: Run GREEN checks.**

Run: `ctest --test-dir build/debug --output-on-failure -R "^(track_waveform_thumbnail_provider_test|waveform_cache_test|waveform_provider_test)$"`

Expected: PASS for silent/endpoint/transient/non-finite inputs, 128-byte output, corrupt/missing cache empty state, cache hit result, 256-entry eviction, duplicate request coalescing, stale-generation suppression, zero analysis calls, and sorting/search-independent 36-color mapping.

- [ ] **Step 5: Refactor worker ownership.**

Make the worker capture immutable request data only; marshal results with `QPointer` and queued signals, and keep all LRU mutation on the provider thread. Re-run Step 4 and `git diff --check`.

- [ ] **Step 6: Commit.**

```powershell
git add qt/src/track_waveform_thumbnail_provider.hpp qt/src/track_waveform_thumbnail_provider.cpp qt/CMakeLists.txt app/main.cpp tests/CMakeLists.txt tests/qt/track_waveform_thumbnail_provider_test.cpp
git commit -m "feat: add cache-only track waveform thumbnails"
```

### Task 5: 单节点 Scene Graph 缩略图渲染

**Files:**
- Create: `qt/src/track_waveform_thumbnail_item.hpp`
- Create: `qt/src/track_waveform_thumbnail_item.cpp`
- Modify: `qt/src/qml_registration.cpp`
- Modify: `qt/src/qml_registration.hpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/qt/track_waveform_thumbnail_item_test.cpp`

**Interfaces:**
- Produces: QML type `TrackWaveformThumbnailItem 1.0` with properties `QByteArray peaks`, `QColor color`, and read-only `int geometryRevision`; it rebuilds geometry only when peaks/width/height changes.
- Consumes: Task 4's 128-byte `TrackWaveformThumbnail`; it receives data but cannot request caches, decode audio, animate, or update from playback position.

- [ ] **Step 1: Write failing Scene Graph contract tests.**

```cpp
void TrackWaveformThumbnailItemTest::colorDoesNotRebuildPeakGeometry()
{
    TrackWaveformThumbnailItem item;
    item.setWidth(128); item.setHeight(10);
    item.setPeaks(QByteArray(128, '\\x7f'));
    const int before = item.geometryRevision();
    item.setColor(Qt::cyan);
    QCOMPARE(item.geometryRevision(), before);
    QCOMPARE(item.sceneGraphNodeCountForTest(), 1);
}
```

- [ ] **Step 2: Run RED.**

Run: `cmake --build --preset windows-msvc-debug --target track_waveform_thumbnail_item_test && ctest --test-dir build/debug --output-on-failure -R "^track_waveform_thumbnail_item_test$"`

Expected: FAIL because the QQuickItem type and node-count/geometry test seam are absent.

- [ ] **Step 3: Implement one geometry node.**

In `updatePaintNode`, allocate/reuse exactly one `QSGGeometryNode`, one vertex buffer representing 128 symmetric vertical lines, and one flat-color material. Clamp byte amplitudes and bounds; color changes update material only, peaks/size changes update vertex coordinates, and playback/progress signals have no binding to `update()`.

- [ ] **Step 4: Run GREEN checks.**

Run: `ctest --test-dir build/debug --output-on-failure -R "^(track_waveform_thumbnail_item_test|track_waveform_thumbnail_provider_test|waveform_item_test)$"`

Expected: PASS for empty baseline, 128 lines, invalid input clamp, one node, buffer reuse, color-only material update, and no main-waveform regression.

- [ ] **Step 5: Refactor render ownership.**

Keep rendering math in a private pure helper used by the Qt test seam; retain `QQuickItem` as the only QML-facing class. Re-run Step 4 and `git diff --check`.

- [ ] **Step 6: Commit.**

```powershell
git add qt/src/track_waveform_thumbnail_item.hpp qt/src/track_waveform_thumbnail_item.cpp qt/src/qml_registration.cpp qt/src/qml_registration.hpp qt/CMakeLists.txt tests/CMakeLists.txt tests/qt/track_waveform_thumbnail_item_test.cpp
git commit -m "feat: render static track waveform thumbnails"
```

### Task 6: 设置、主题语义 token 与零实例关闭行为

**Files:**
- Modify: `qt/src/settings_controller.hpp`
- Modify: `qt/src/settings_controller.cpp`
- Modify: `app/qml/AgPlayer/SettingsPage.qml`
- Modify: `app/qml/AgPlayer/theme/Theme.qml`
- Modify: `tests/qt/settings_controller_test.cpp`
- Modify: `tests/qml/tst_main_window.qml`

**Interfaces:**
- Produces: `Q_PROPERTY(bool listWaveformThumbnailEnabled READ listWaveformThumbnailEnabled WRITE setListWaveformThumbnailEnabled NOTIFY listWaveformThumbnailEnabledChanged)` default `true` and `Q_PROPERTY(int listWaveformThumbnailMode READ listWaveformThumbnailMode WRITE setListWaveformThumbnailMode NOTIFY listWaveformThumbnailModeChanged)` default `Color36`; enum values `Color36 = 0`, `Mono = 1`.
- Produces: `Theme.listWorkspaceBackground`, `Theme.listWorkspaceBorder`, `Theme.listWorkspaceDivider`, `Theme.listHeaderText`, `Theme.listWaveformMono`, `Theme.tagPanelSurface`, `Theme.tagPillText`, `Theme.tagPillBorder`, and `Theme.tagSelectedSurface`; no new page-local hex colors for these semantic roles.
- Consumes: existing edit/save/cancel/restore-default settings flow and `SettingsController.cacheDirectory`; setting changes affect visible loaders immediately without re-reading peaks in Mono mode.

- [ ] **Step 1: Write failing persistence/reset and Loader tests.**

```cpp
void SettingsControllerTest::persistsAndRestoresListThumbnailDefaults()
{
    settings.setListWaveformThumbnailEnabled(false);
    settings.setListWaveformThumbnailMode(SettingsController::Mono);
    settings.save();
    SettingsController loaded(settingsPath);
    QVERIFY(!loaded.listWaveformThumbnailEnabled());
    QCOMPARE(loaded.listWaveformThumbnailMode(), SettingsController::Mono);
    loaded.restoreDefaults();
    QVERIFY(loaded.listWaveformThumbnailEnabled());
    QCOMPARE(loaded.listWaveformThumbnailMode(), SettingsController::Color36);
}
```

```qml
function test_list_thumbnail_switch_releases_visible_loaders() {
    SettingsController.listWaveformThumbnailEnabled = false
    tryVerify(function() { return trackList.thumbnailItemCount === 0 })
    compare(trackList.thumbnailRequestCount, 0)
}
```

- [ ] **Step 2: Run RED.**

Run: `cmake --build --preset windows-msvc-debug --target settings_controller_test qml_main_window_test && ctest --test-dir build/debug --output-on-failure -R "^(settings_controller_test|qml_main_window_test)$"`

Expected: FAIL because the properties, settings controls, semantic tokens and zero-instance behavior do not exist.

- [ ] **Step 3: Implement settings and tokens.**

Persist keys `appearance/listWaveformThumbnailEnabled` and `appearance/listWaveformThumbnailMode`, validate mode to `Color36`/`Mono`, include both in edit-session save/cancel/defaults, place the Chinese controls under “外观与波形 → 歌曲列表”, and bind the switch to the ListWindow shared model. Define dark and light semantic colors in `Theme.qml`; verify normal-text pairs rather than hard-coding reference colors into page delegates.

- [ ] **Step 4: Run GREEN checks.**

Run: `ctest --test-dir build/debug --output-on-failure -R "^(settings_controller_test|qml_main_window_test|track_waveform_thumbnail_provider_test)$"`

Expected: PASS for migration/default/save/cancel/reset, immediate Loader destruction/creation, no provider request while disabled, and Mono changing only item color.

- [ ] **Step 5: Refactor property notifications.**

Avoid emitting when a validated value is unchanged; keep loader counters test-only and remove them from release-visible UI. Re-run Step 4 and `git diff --check`.

- [ ] **Step 6: Commit.**

```powershell
git add qt/src/settings_controller.hpp qt/src/settings_controller.cpp app/qml/AgPlayer/SettingsPage.qml app/qml/AgPlayer/theme/Theme.qml tests/qt/settings_controller_test.cpp tests/qml/tst_main_window.qml
git commit -m "feat: add list waveform appearance settings"
```

### Task 7: 连续三栏 QML 工作区、共享表格与三列标签面板

**Files:**
- Create: `app/qml/AgPlayer/components/TagManagementPanel.qml`
- Create: `app/qml/AgPlayer/components/TrackWaveformThumbnail.qml`
- Modify: `app/qml/AgPlayer/ListWindow.qml`
- Modify: `app/qml/AgPlayer/components/SideNavigation.qml`
- Modify: `app/qml/AgPlayer/components/TrackList.qml`
- Modify: `app/qml/AgPlayer/components/SearchFilter.qml`
- Modify: `app/CMakeLists.txt`
- Modify: `tests/qml/tst_main_window.qml`
- Create: `tests/scripts/tag_management_layout_contract_test.ps1`

**Interfaces:**
- Consumes: Task 1 `TagModel`, Task 2 `filterModel.tagFilter`, Task 3 `LibraryNavigationModel`, Task 4 provider, Task 5 item and Task 6 settings/tokens through `app/main.cpp` context properties.
- Produces: `TagManagementPanel` properties `tagModel`, `filterModel`, read-only `gridColumnCount: 3`; signals `addTagRequested(string)`, `renameTagRequested(string,string)`, `removeTagRequested(string)`, `changeTagColorRequested(string,color)`.
- Produces: `TrackWaveformThumbnail` properties `trackId`, `sourcePath`, `delegateGeneration`, `enabled`, `mode`; it owns exactly one conditional Loader for `TrackWaveformThumbnailItem` and calls Task 4 provider only when `enabled && ListView.isCurrentItem`/visible delegate policy permits.
- Produces: `TrackList` property `thumbnailItemCount`, required rows 62/42, fixed header names/order, cover 34×34, waveform 8–10 high, one virtualized list; `SearchFilter` remains the sole bottom filter bar.

- [ ] **Step 1: Write failing QML/layout-contract tests.**

```qml
function test_tag_reference_workspace_uses_one_continuous_three_column_surface() {
    compare(detachedListWorkspace.leftColumnWidth, 256)
    compare(detachedListWorkspace.rightColumnWidth, 328)
    verify(detachedListWorkspace.centerWidth >= 680)
    compare(detachedListWorkspace.dividerWidth, 1)
    compare(findChild(listWindow, "tagManagementPanel").gridColumnCount, 3)
    compare(findChild(listWindow, "trackHeaderIndex").text, "#")
    compare(findChild(listWindow, "trackHeaderDuration").text, "时长")
}
```

```powershell
$required = 'listWorkspace','referenceSideNavigation','sharedTrackList','tagManagementPanel','tagSearchField','addTagButton','tagGrid','librarySearchFilter'
foreach ($name in $required) {
    if ($page -notmatch ('objectName:\s*"' + $name + '"')) { throw "Missing $name" }
}
if ($page -notmatch 'Layout\.preferredWidth:\s*256') { throw 'Left column must be 256' }
if ($page -notmatch 'Layout\.preferredWidth:\s*328') { throw 'Right column must be 328' }
```

- [ ] **Step 2: Run RED.**

Run: `cmake --build --preset windows-msvc-debug --target qml_main_window_test && ctest --test-dir build/debug --output-on-failure -R "^qml_main_window_test$"; powershell -ExecutionPolicy Bypass -File tests/scripts/tag_management_layout_contract_test.ps1 -SourceRoot (Get-Location)`

Expected: FAIL because the panel/components/object names and exact 256/328 continuous workspace contract are absent.

- [ ] **Step 3: Build the reference layout without duplicating data.**

Replace the existing lower `RowLayout` with one rounded `Rectangle` named `listWorkspace`, inside it `SideNavigation` (256), a central column (`TrackList` + existing `SearchFilter`) and `TagManagementPanel` (328), separated only by 1 px tokenized dividers. Set `SideNavigation` to `LibraryNavigationModel`; retain existing playlist actions. Update `TrackList` headers/delegate widths to the fixed required order, make `ListView` reuse delegates, add cover + conditional `TrackWaveformThumbnail`, set 62/42 rows, and retain sort/favorite/rating/right-click behaviors. Make `TagManagementPanel` use a virtualized `GridView` with `cellWidth: width / 3` and `readonly property int gridColumnCount: 3`, a top search/add row, ellipsis plus tooltip, and no per-tag animation.

- [ ] **Step 4: Run GREEN checks.**

Run: `ctest --test-dir build/debug --output-on-failure -R "^qml_main_window_test$"; powershell -ExecutionPolicy Bypass -File tests/scripts/tag_management_layout_contract_test.ps1 -SourceRoot (Get-Location)`

Expected: PASS for continuous one-surface layout, 256/center/328 widths, header order, 62/42 row behavior, cover/waveform dimensions, one shared TrackList, SearchFilter bindings, virtual three-column tags, long-name tooltip, dark/light semantic token use, and 100–200% scale reachability.

- [ ] **Step 5: Refactor view-local calculations.**

Move repeated metric expressions into named readonly QML properties, retain no literal palette colors in new delegates, and make every Loader destruction release its provider generation. Re-run Step 4 and `git diff --check`.

- [ ] **Step 6: Commit.**

```powershell
git add app/qml/AgPlayer/components/TagManagementPanel.qml app/qml/AgPlayer/components/TrackWaveformThumbnail.qml app/qml/AgPlayer/ListWindow.qml app/qml/AgPlayer/components/SideNavigation.qml app/qml/AgPlayer/components/TrackList.qml app/qml/AgPlayer/components/SearchFilter.qml app/CMakeLists.txt tests/qml/tst_main_window.qml tests/scripts/tag_management_layout_contract_test.ps1
git commit -m "feat: add reference tag management list workspace"
```

### Task 8: 标签、歌单、目录右键与一次性拖拽预览

**Files:**
- Modify: `app/qml/AgPlayer/components/TagManagementPanel.qml`
- Modify: `app/qml/AgPlayer/components/SideNavigation.qml`
- Modify: `app/qml/AgPlayer/components/TrackList.qml`
- Modify: `app/qml/AgPlayer/ListWindow.qml`
- Modify: `qt/src/native_drop_router.hpp`
- Modify: `qt/src/native_drop_router.cpp`
- Modify: `app/main.cpp`
- Modify: `tests/qt/native_drop_router_test.cpp`
- Modify: `tests/qml/tst_main_window.qml`
- Modify: `tests/qt/qml_main_window_test_main.cpp`

**Interfaces:**
- Consumes: Task 1 add/rename/remove/color APIs, existing `PlaylistModel::addTracks/moveTracks`, Task 3 folder remove APIs and existing `ImportController::importPaths`.
- Produces: a shared QML `Menu` policy: tags expose rename/delete/change-color on demand; playlists preserve rename/remove/import/export; folders expose add/remove-reference/rescan; every destructive label states the non-destructive disk boundary.
- Produces: `NativeDropRouter::Target::ResourceFolder` only if hit-testing cannot be represented by the existing List target; `pathsDropped(Target, QStringList)` remains compatible. `app/main.cpp` partitions dropped paths by `QFileInfo::isDir()` before calling `addMonitoredFolder` or `ImportController`.
- Produces: `TrackList` drag visual `objectName: "trackDragPreview"`, properties `dragPreviewCreationCount`, `dragTrackIds`; opacity `0.68`; it creates one image/label preview on drag start and only changes coordinates thereafter.

- [ ] **Step 1: Write failing context-menu, folder-drop and drag-preview tests.**

```qml
function test_multi_selection_drag_uses_one_immutable_preview() {
    selectRows([0, 1, 2])
    beginTrackDrag(0)
    var preview = findChild(trackList, "trackDragPreview")
    compare(preview.opacity, 0.68)
    compare(preview.selectedCount, 3)
    compare(trackList.dragPreviewCreationCount, 1)
    moveDragTo(240, 180)
    compare(trackList.dragPreviewCreationCount, 1)
    cancelDrag()
    verify(!preview.visible)
}
```

```cpp
void NativeDropRouterTest::directoryDropAddsReferenceButAudioStillImports()
{
    router.routeLocalPaths(NativeDropRouter::Target::ResourceFolder,
                           {directory.path(), audioFixture});
    QCOMPARE(folderAdds, QStringList({directory.path()}));
    QCOMPARE(importedPaths, QStringList({audioFixture}));
}
```

- [ ] **Step 2: Run RED.**

Run: `cmake --build --preset windows-msvc-debug --target native_drop_router_test qml_main_window_test && ctest --test-dir build/debug --output-on-failure -R "^(native_drop_router_test|qml_main_window_test)$"`

Expected: FAIL because tag/folder right-click actions, directory partitioning and one-time preview behavior are absent.

- [ ] **Step 3: Connect real menus and drag/drop operations.**

For tags, invoke only Task 1 APIs and set/clear Task 2 filter; for playlist and folder menus use existing model/controller methods and confirmation dialogs that state “不删除磁盘文件”. On system drop, canonicalize and partition once: valid directories call `LibraryManagerController::addMonitoredFolder`, audio files retain existing import routing, invalid/mixed cancellation has no side effects. Retain existing `application/x-agplayer-track-ids` MIME data for playlist/tag/folder targets. Build the drag preview once from the first selected delegate’s currently loaded cover/name, write “已选择 N 首” when N>1, set opacity 0.68, and release it on cancel/failure/drop; do not read covers, waveforms or create animations while moving.

- [ ] **Step 4: Run GREEN checks.**

Run: `ctest --test-dir build/debug --output-on-failure -R "^(native_drop_router_test|qml_main_window_test|library_navigation_model_test|tag_model_test|playlist_model_test)$"`

Expected: PASS for real tag/playlist/folder actions, safe folder removal, Windows/Qt directory drop, retained file import, single/multiple/invalid drag paths, MIME drops to allowed targets, one preview construction, opacity 0.60–0.75, and cleanup on cancel/failure.

- [ ] **Step 5: Refactor target routing.**

Keep Windows message decoding inside `NativeDropRouter`; keep App-specific directory/audio dispatch in `app/main.cpp`; keep QML target acceptance declarative. Re-run Step 4 and `git diff --check`.

- [ ] **Step 6: Commit.**

```powershell
git add app/qml/AgPlayer/components/TagManagementPanel.qml app/qml/AgPlayer/components/SideNavigation.qml app/qml/AgPlayer/components/TrackList.qml app/qml/AgPlayer/ListWindow.qml qt/src/native_drop_router.hpp qt/src/native_drop_router.cpp app/main.cpp tests/qt/native_drop_router_test.cpp tests/qml/tst_main_window.qml tests/qt/qml_main_window_test_main.cpp
git commit -m "feat: add tag folder menus and track drag preview"
```

### Task 9: 应用装配、全量回归与性能防回归门禁

**Files:**
- Modify: `app/main.cpp`
- Modify: `qt/src/qml_registration.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `app/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/stress/library_model_stress_test.cpp`
- Modify: `tests/qt/qml_main_window_test_main.cpp`
- Modify: `tests/qml/tst_main_window.qml`

**Interfaces:**
- Consumes: all Task 1–8 interfaces.
- Produces: a single app-lifetime `TagModel`, `LibraryNavigationModel`, and `TrackWaveformThumbnailProvider` configured from existing AppData/settings paths and exposed consistently to both app and QML test harness; no view owns a duplicate model or unbounded worker.
- Produces: test-only observable counters for thumbnail requests, cache reads, scene nodes and drag-preview construction; production builds do not surface these counters to user UI.

- [ ] **Step 1: Write failing integration/stress tests.**

```cpp
void LibraryModelStressTest::tenThousandRowsKeepThumbnailRequestsBounded()
{
    loadTracks(10'000);
    scrollVisibleRange(0, 80);
    QVERIFY(thumbnailProvider.maxResidentEntriesForTest() <= 256);
    QVERIFY(thumbnailProvider.maxConcurrentWorkersForTest() <= 1);
    QCOMPARE(thumbnailProvider.analysisInvocationCountForTest(), 0);
}
```

```qml
function test_switching_tag_playlist_folder_keeps_one_track_list() {
    activateTag("电音"); activatePlaylist("playlist-a"); activateFolder(folderRoot)
    compare(findChildren(listWindow, "detachedTrackList").length, 1)
}
```

- [ ] **Step 2: Run RED.**

Run: `cmake --build --preset windows-msvc-debug --target library_model_stress_test qml_main_window_test && ctest --test-dir build/debug --output-on-failure -R "^(library_model_stress_test|qml_main_window_test)$"`

Expected: FAIL until app/test assembly supplies the same models and evidence counters across main/list paths.

- [ ] **Step 3: Complete composition with explicit ownership.**

Create each model/provider once in `app/main.cpp` after `LibraryModel`, configure tag storage under AppData and thumbnail cache directory from `SettingsController`, connect library/playlist/manager sources, and set context properties before loading QML. Mirror this exact dependency graph in `qml_main_window_test_main.cpp`; update CMake registrations and resource lists. Do not change `WindowController` ownership, replace the list window, start a full-library prefetch, or access the playback engine from thumbnail code.

- [ ] **Step 4: Run GREEN focused regression suite.**

Run: `ctest --test-dir build/debug --output-on-failure -R "^(tag_directory_store_test|tag_model_test|library_filter_model_test|library_navigation_model_test|track_waveform_thumbnail_provider_test|track_waveform_thumbnail_item_test|settings_controller_test|native_drop_router_test|library_model_stress_test|qml_main_window_test|waveform_provider_test|waveform_item_test|window_controller_test)$"`

Expected: PASS, including 10,000-row model data, bounded 256 cache, at most one worker, no list-triggered audio analysis, one TrackList throughout scope switching, and unchanged main-waveform/window-controller behavior.

- [ ] **Step 5: Refactor dependency creation and check Debug/Release.**

Extract only a small app-local setup helper shared by production/test bootstrap if it has no UI ownership; otherwise retain explicit setup. Run `cmake --build --preset windows-msvc-release` and `ctest --preset windows-msvc-release --output-on-failure -R "^(tag_model_test|track_waveform_thumbnail_provider_test|qml_main_window_test|waveform_provider_test|window_controller_test)$"`, then `git diff --check`.

- [ ] **Step 6: Commit.**

```powershell
git add app/main.cpp qt/src/qml_registration.cpp qt/CMakeLists.txt app/CMakeLists.txt tests/CMakeLists.txt tests/stress/library_model_stress_test.cpp tests/qt/qml_main_window_test_main.cpp tests/qml/tst_main_window.qml
git commit -m "test: cover tag workspace integration limits"
```

### Task 10: Windows 真实运行、同尺寸设计 QA、开发记录与集成验收

**Files:**
- Create: `docs/development/2026-08-20-tag-management-reference-ui.md`
- Create: `docs/qa/2026-08-20-tag-management-reference-ui-acceptance.md`
- Create: `design-qa/tag-management-reference-1447x1087.png`
- Create: `design-qa/tag-management-implementation-1447x1087.png`
- Create: `design-qa/tag-management-comparison-1447x1087.png`
- Modify: `design-qa.md`

**Interfaces:**
- Consumes: Release executable, Task 1–9 tests, the exact 1447×1087 reference image and a real AgPlayer window; generated mockups are not evidence.
- Produces: requirements ledger mapping every required design section to source file, test command, screenshot/manual proof, result `PASS`/`BLOCKED`, and owner; `design-qa.md` may say `final result: passed` only after all P0/P1/P2 differences are resolved and re-screenshot evidence exists.

- [ ] **Step 1: Prepare an evidence checklist that initially fails closed.**

```markdown
| Requirement | Evidence | Status |
|---|---|---|
| 256 / adaptive / 328 continuous ListWindow | real 1447×1087 screenshot + layout contract | BLOCKED |
| cache-only 128-byte thumbnails and disabled zero instances | provider/item tests + runtime counters | BLOCKED |
| tag, playlist, resource directory actions | QML/Qt test + desktop operation record | BLOCKED |
| playback isolation and high-DPI behavior | real audio/DPI notes | BLOCKED |
```

- [ ] **Step 2: Run the reproducible automated gate.**

Run: `cmake --build --preset windows-msvc-debug; ctest --preset windows-msvc-debug --output-on-failure; cmake --build --preset windows-msvc-release; ctest --preset windows-msvc-release --output-on-failure; git diff --check`

Expected: every test executable that exists at the implementation baseline passes. If an unrelated pre-existing suite failure occurs, record its target/error verbatim as `BLOCKED`; do not describe the entire suite as passing.

- [ ] **Step 3: Perform the real Windows reference-state walkthrough.**

Launch the Release AgPlayer binary, load a representative library containing tagged/untagged songs, cached and uncached waveforms, at least one playlist, and monitored folders. Set the `ListWindow` to the reference state at 1447×1087; operate add/search/select/rename/color/delete tag, tag+keyword/rating/BPM filter composition, playlist context actions, add/remove monitored folder, Explorer directory/audio drop, single/multi-track drag cancel/drop, thumbnail Color36/Mono/off, sorting/search/scrolling and ListWindow attach/detach. During continuous lossless playback, perform seek, next track and rapid list scroll; record any audible interruption, waveform regression or error state rather than masking it.

- [ ] **Step 4: Capture and compare visual evidence.**

Capture a real 1447×1087 screenshot, crop and scale neither source nor implementation differently, save the supplied reference copy and implementation capture, then build the comparison image. Check title/columns, fixed left/right widths, row heights, cover/waveform quality, three-column tags, typography, spacing, colors, borders, radii, selected state and scroll state. Repeat at 100%, 125%, 150%, 175%, 200% DPI and dark/light mode; fix every P0/P1/P2 discrepancy before replacing the comparison evidence.

- [ ] **Step 5: Record performance and acceptance truthfully.**

For 1,000 and 10,000 tracks measure visible-list scroll, search, sort, playlist/tag/folder switching, CPU, memory, disk I/O and thumbnail node count with thumbnails on/off; verify the LRU remains ≤256 and no cache miss analyzes audio. Mark genuine desktop audio, multi-monitor and hardware results only when performed. Update the development record with changed files, exact test output summaries, failures, remaining `BLOCKED` conditions and no-EXE scope.

- [ ] **Step 6: Commit the acceptance evidence only when it is internally consistent.**

```powershell
git add docs/development/2026-08-20-tag-management-reference-ui.md docs/qa/2026-08-20-tag-management-reference-ui-acceptance.md design-qa/tag-management-reference-1447x1087.png design-qa/tag-management-implementation-1447x1087.png design-qa/tag-management-comparison-1447x1087.png design-qa.md
git commit -m "docs: record tag management reference UI acceptance"
```

If P0/P1/P2 or required real-runtime evidence remains unresolved, do not make this documentation commit as a passing acceptance claim; commit an explicitly `BLOCKED` development record only after the implementation commits above, keeping `design-qa.md` non-passing.

## Plan Self-Review

- **Spec coverage:** Tasks 1–2 cover tag directory truth, persist/recover, counts, rename/delete/color and combined filtering. Task 3 covers virtual left navigation, monitored resources and safe removal. Tasks 4–5 cover cache-only 128 peak reads, LRU, generation safety, 36 colors and one-node rendering. Task 6 covers immediate settings/token behavior. Tasks 7–8 cover 256/adaptive/328 continuous UI, fixed shared list, SearchFilter, virtual tag grid, context menus, system folder drop and one-time drag preview. Tasks 9–10 cover assembly, 1,000/10,000 limits, playback isolation, DPI, screenshots, development records and final integration acceptance.
- **Placeholder scan:** This plan names concrete files, properties, signals, roles, commands, expected RED/GREEN outcomes and commit contents; it contains no deferred implementation markers.
- **Type consistency:** `TagModel` is the sole tag catalog; `LibraryFilterModel::tagFilter` is the sole tag predicate; `LibraryNavigationModel` is the sole navigation projection; `TrackWaveformThumbnailProvider::thumbnailReady(trackId, delegateGeneration, peaks)` is consumed by the QML wrapper; `TrackWaveformThumbnailItem` only renders its `peaks`/`color` properties.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-08-20-tag-management-reference-ui.md`. Two execution options:

1. Subagent-Driven (recommended) - dispatch a fresh subagent per task, review between tasks, fast iteration.
2. Inline Execution - execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?

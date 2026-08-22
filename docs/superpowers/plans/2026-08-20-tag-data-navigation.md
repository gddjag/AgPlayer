# Tag Data and Library Navigation Implementation Plan

> **Supplementary plan:** Execute through `2026-08-20-tag-waveform-ui-master.md`. The master plan owns task boundaries and all shared-QML file ownership.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a persistent tag directory, incremental tag counts, tag/folder filtering, and a single virtualized library-navigation data path that drives the existing shared `TrackList` without changing playback or audio analysis.

**Architecture:** Keep `TrackRecord::tags` in `LibraryModel` as the only track-to-tag relation. Add a small JSON-backed `TagModel` for directory metadata and a flat `LibraryNavigationModel` for visible navigation rows; both subscribe to model signals and make targeted row changes rather than rebuilding `LibraryModel`. Make the application-owned `LibraryManagerController` the persistent resource-root coordinator, wire its existing asynchronous importer/scanner to navigation, then consume these objects from one continuous `ListWindow` surface.

**Tech Stack:** C++17, Qt 6 Core/Concurrent/Quick, QML/Qt Quick Controls, `QSaveFile`, existing `LibraryStore`, `PlaylistModel`, `LibraryManagerController`, CTest/QtTest/Qt Quick Test.

**Spec:** `docs/superpowers/specs/2026-08-20-track-waveform-tag-management-design.md`

## Global Constraints

- Keep the existing data chain `LibraryModel -> LibraryFilterModel -> TrackList`; tag, playlist, and folder views only change filter state and must not create a second track table or track model.
- Preserve `TrackRecord::tags` and `LibraryStore` `library.json` as the track-tag relation truth; persist only tag-directory metadata (including zero-count tags and explicit colors) in a small JSON file written atomically with `QSaveFile`.
- Use case-insensitive tag identity (`QString::toCaseFolded()`), preserve the user-entered display spelling, and never delete an audio file or a `TrackRecord` when deleting a tag or resource-root reference.
- Count changes must be incremental from `rowsInserted`, `rowsAboutToBeRemoved`, `TagsRole` `dataChanged`, and reset handling; do not rescan audio, reset the full library, or reaggregate the whole library for one tag edit.
- Reuse `ImportController`, `LibraryManagerController`, `NativeDropRouter`, Qt Drag & Drop, existing asynchronous folder scanning, and existing `PlaylistModel` persistence; no third-party dependency, database, platform-specific core behavior, or new long-lived worker thread.
- Resource-root add/drag-drop validates a directory, saves its AgPlayer reference, and starts existing background import; removal only removes the persisted reference and filesystem watch.
- The bottom list window remains one continuous left/center/right surface with 1 logical-pixel separators. `TrackList.qml` stays the sole song-list delegate; left, center, and right scroll independently and use virtualized views.
- The fixed table columns are `# | 歌曲 | 收藏 | 艺术家 | 专辑 | 评分 | BPM | 时长`; tag management has only search and add as persistent top controls, three virtualized tag columns, and an on-demand context menu for rename/delete/color.
- Retain current playback, FFmpeg, audio callback, main waveform, seek, hover, BPM, and playback-mode behavior. This plan adds no waveform thumbnail implementation, cache, image, real-time analysis, Canvas, `QQuickPaintedItem`, shader, animation, or per-peak QML items.
- All new C++ uses C++17, RAII, bounded memory, const-correct interfaces, and non-blocking GUI-thread behavior; all changes must build warning-free under the existing warning policy.
- Record requirement-to-evidence mapping in `docs/development/2026-08-20-tag-data-navigation.md`; only claim checks actually run. Visual evidence is a real 1447x1087 AgPlayer capture plus the reference comparison, and `design-qa.md` may be set to `passed` only after all P0/P1/P2 findings are resolved.

---

## File Structure

- `qt/src/library_model.hpp` / `qt/src/library_model.cpp`: expose normalized bulk tag mutations and a precise old/new tag-change signal while retaining the existing per-track `setTags` API.
- `qt/src/tag_store.hpp` / `qt/src/tag_store.cpp`: atomically load and save tag metadata `{ key, displayName, color }` independently of `library.json`.
- `qt/src/tag_model.hpp` / `qt/src/tag_model.cpp`: QAbstractListModel directory of tag records, selected tag, color assignment, incremental counts, and batched rename/remove operations.
- `qt/src/library_filter_model.hpp` / `qt/src/library_filter_model.cpp`: conjunctive `tagKey` and resource-folder filters in the existing proxy, with no source-model reset.
- `qt/src/library_navigation_model.hpp` / `qt/src/library_navigation_model.cpp`: flat, virtualizable visible-node model for fixed library nodes, playlists, and lazily expanded resource-folder nodes.
- `qt/src/library_manager_controller.hpp` / `qt/src/library_manager_controller.cpp`: expose normalized monitored-root records and resource-root removal/update signals used by navigation while retaining existing background scan/import behavior.
- `qt/src/qml_registration.hpp` / `qt/src/qml_registration.cpp`, `app/main.cpp`, `qt/CMakeLists.txt`, `tests/CMakeLists.txt`: construct the persistent objects next to `LibraryModel`, register them as QML singletons/types, add source files, and add the focused tests.
- `app/qml/AgPlayer/components/SideNavigation.qml`: render `LibraryNavigationModel` in one `ListView`, route node actions to the existing filter/playlist/controller objects, and expose folder-add/remove/drop targets.
- `app/qml/AgPlayer/components/TagManagementPanel.qml`: new right-side `GridView` panel with search, create, three columns, tooltip/elision, and a single demand-opened tag context menu.
- `app/qml/AgPlayer/ListWindow.qml`: compose the navigation, shared `TrackList`, and tag panel within the existing continuous row; translate tag/folder navigation events to filter properties and clear only the tag filter when its tag is removed.
- `app/qml/AgPlayer/components/TrackList.qml`: replace the current per-track loop in the tag dialog with the model bulk API and classify tag/folder views as non-playlist views, preserving existing drag/reorder behavior.
- `tests/qt/library_model_test.cpp`, `tests/qt/tag_model_test.cpp`, `tests/qt/library_filter_model_test.cpp`, `tests/qt/library_navigation_model_test.cpp`, `tests/qt/library_manager_controller_test.cpp`: C++ contracts for mutations, persistence, incremental rows/counts, filtering, roots, and imports.
- `tests/qml/tst_main_window.qml`: QML contracts for one shared list, navigation/tag actions, exactly three tag columns, on-demand management menu, and resource-root drop routing.
- `docs/development/2026-08-20-tag-data-navigation.md`: traceability and executed validation/visual evidence log.

### Task 1: Precise and batched library tag mutations

**Files:**
- Modify: `qt/src/library_model.hpp: TrackRecord APIs and signals`
- Modify: `qt/src/library_model.cpp: LibraryModel::setTags and new batch operations`
- Modify: `tests/qt/library_model_test.cpp: LibraryModelTest slots and tag mutation assertions`

**Interfaces:**
- Consumes: `TrackRecord::tags`, `LibraryModel::TagsRole`, `LibraryModel::flushRequested()`.
- Produces: `void tagsChanged(const QString& trackId, const QStringList& oldTags, const QStringList& newTags)`, `Q_INVOKABLE int setTagsForTracks(const QStringList& trackIds, const QStringList& tags)`, `Q_INVOKABLE int renameTag(const QString& oldKey, const QString& displayName)`, and `Q_INVOKABLE int removeTag(const QString& key)`.

- [ ] **Step 1: Write the failing LibraryModel contract tests**

```cpp
void LibraryModelTest::batchesAndSignalsTagChanges()
{
    LibraryModel model;
    model.replaceAll({makeTrack("one", {"Rock"}), makeTrack("two", {"rock", "Night"})});
    QSignalSpy tagChanges(&model, &LibraryModel::tagsChanged);
    QSignalSpy writes(&model, &LibraryModel::flushRequested);

    QCOMPARE(model.setTagsForTracks({"one", "two"}, {"  Road ", "road"}), 2);
    QCOMPARE(tagChanges.count(), 2);
    QCOMPARE(writes.count(), 1);
    QCOMPARE(model.renameTag("road", "Driving"), 2);
    QCOMPARE(model.removeTag("driving"), 2);
    QCOMPARE(model.count(), 2);
}
```

- [ ] **Step 2: Run the focused test to verify it fails**

Run: `cmake --build build/msvc-release --target library_model_test --parallel 4; ctest --test-dir build/msvc-release -R '^library_model_test$' --output-on-failure`

Expected: compilation fails because `LibraryModel::tagsChanged`, `setTagsForTracks`, `renameTag`, and `removeTag` are not declared.

- [ ] **Step 3: Add the minimal normalized mutation API**

```cpp
void LibraryModel::applyTagsAtRow(const int row, const QStringList& next)
{
    const QStringList previous = tracks_.at(row).tags;
    if (previous == next) return;
    tracks_[row].tags = next;
    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed, {TagsRole});
    emit tagsChanged(tracks_.at(row).trackId, previous, next);
}

int LibraryModel::removeTag(const QString& key)
{
    const QString normalizedKey = key.trimmed().toCaseFolded();
    int changed = 0;
    for (int row = 0; row < tracks_.size(); ++row) {
        QStringList next = tracks_.at(row).tags;
        next.erase(std::remove_if(next.begin(), next.end(), [&](const QString& tag) {
            return tag.toCaseFolded() == normalizedKey;
        }), next.end());
        if (next != tracks_.at(row).tags) { applyTagsAtRow(row, next); ++changed; }
    }
    if (changed > 0) emit flushRequested();
    return changed;
}
```

Normalize every public tag input through the current trim/case-fold duplicate rule; make every batch method call `applyTagsAtRow`, emit `flushRequested()` once only when at least one row changed, and never call `beginResetModel()`.

- [ ] **Step 4: Run the focused test to verify it passes**

Run: `cmake --build build/msvc-release --target library_model_test --parallel 4; ctest --test-dir build/msvc-release -R '^library_model_test$' --output-on-failure`

Expected: PASS; existing `updatesTagsAndManualOrder` and `batchesAndSignalsTagChanges` both verify target-only `TagsRole` changes and no track deletion.

- [ ] **Step 5: Refactor the tag-normalization helper and rerun the test**

```cpp
static QStringList normalizeTags(const QStringList& tags);
```

Move the duplicate normalization loop used by `setTags`, `setTagsForTracks`, and `renameTag` into this file-local helper; preserve display spelling of the first accepted input. Re-run the command from Step 4 and require PASS.

- [ ] **Step 6: Commit the independently passing mutation contract**

```bash
git add qt/src/library_model.hpp qt/src/library_model.cpp tests/qt/library_model_test.cpp
git commit -m "feat: add batched library tag mutations"
```

### Task 2: Persistent incremental TagModel directory

**Files:**
- Create: `qt/src/tag_store.hpp`
- Create: `qt/src/tag_store.cpp`
- Create: `qt/src/tag_model.hpp`
- Create: `qt/src/tag_model.cpp`
- Modify: `qt/CMakeLists.txt: agplayer_qt source list`
- Modify: `tests/CMakeLists.txt: qt_test item list`
- Create: `tests/qt/tag_model_test.cpp`

**Interfaces:**
- Consumes: Task 1 `LibraryModel::tagsChanged`, `LibraryModel::TagsRole`, `LibraryModel::tracks()`, and `LibraryModel::removeTag`/`renameTag`.
- Produces: `TagStore::load()`/`TagStore::save(const QList<TagEntry>&)`, `TagModel` roles `key`, `displayName`, `trackCount`, `color`, `selected`; `Q_PROPERTY(QString selectedKey ...)`; and QML calls `createTag(QString)`, `renameTag(QString, QString)`, `removeTag(QString)`, `setTagColor(QString, QString)`.

- [ ] **Step 1: Write the failing persistence and incremental-count tests**

```cpp
void TagModelTest::persistsEmptyTagsAndUpdatesOnlyAffectedCounts()
{
    QTemporaryDir dir;
    LibraryModel library;
    library.replaceAll({makeTrack("a", {"Rock"}), makeTrack("b", {"Jazz"})});
    TagModel tags(&library, dir.filePath("tags.json"));
    QVERIFY(tags.createTag("Driving"));
    QVERIFY(tags.setTagColor("rock", "#AABBCC"));
    QSignalSpy changed(&tags, &QAbstractItemModel::dataChanged);

    QVERIFY(library.setTags("a", {"Jazz"}));
    QCOMPARE(tags.countForKey("rock"), 0);
    QCOMPARE(tags.countForKey("jazz"), 2);
    QCOMPARE(changed.count(), 2);
    QVERIFY(tags.flush());
    TagModel restored(&library, dir.filePath("tags.json"));
    QCOMPARE(restored.colorForKey("rock"), QColor("#AABBCC"));
    QCOMPARE(restored.countForKey("driving"), 0);
}
```

- [ ] **Step 2: Run the new focused test to verify it fails**

Run: `cmake --build build/msvc-release --target tag_model_test --parallel 4; ctest --test-dir build/msvc-release -R '^tag_model_test$' --output-on-failure`

Expected: CMake target configuration or compilation fails because `TagStore`, `TagModel`, `countForKey`, and `flush` do not exist.

- [ ] **Step 3: Implement atomic metadata storage and incremental model wiring**

```cpp
struct TagEntry { QString key; QString displayName; int trackCount = 0; QColor color; };

class TagModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString selectedKey READ selectedKey WRITE setSelectedKey NOTIFY selectedKeyChanged)
public:
    enum Role { KeyRole = Qt::UserRole + 1, DisplayNameRole, TrackCountRole, ColorRole, SelectedRole };
    explicit TagModel(LibraryModel* library, QString storagePath, QObject* parent = nullptr);
    Q_INVOKABLE bool createTag(const QString& displayName);
    Q_INVOKABLE int renameTag(const QString& key, const QString& displayName);
    Q_INVOKABLE int removeTag(const QString& key);
    Q_INVOKABLE bool setTagColor(const QString& key, const QString& color);
    bool flush();
};
```

`TagStore::save` serializes a versioned JSON array through `QSaveFile`; it writes `key`, `displayName`, and canonical `QColor::name(QColor::HexRgb)` only, never derived counts. On load, discard malformed entries, retain `LibraryModel` tags, build missing entries from the first in-memory aggregate, and save neither audio metadata nor `library.json`. Connect to `rowsInserted`, `rowsAboutToBeRemoved`, `dataChanged`, `modelReset`, and Task 1 `tagsChanged`; update only the changed tag rows with `dataChanged` and coalesce metadata saves with a single-shot timer.

- [ ] **Step 4: Run the focused test to verify it passes**

Run: `cmake --build build/msvc-release --target tag_model_test --parallel 4; ctest --test-dir build/msvc-release -R '^tag_model_test$' --output-on-failure`

Expected: PASS; a zero-track tag and explicit color survive restart, a tag edit touches only old/new tag rows, and no library reset signal is observed.

- [ ] **Step 5: Refactor color allocation into a deterministic private helper and rerun**

```cpp
QColor TagModel::nextColorFor(const QString& normalizedKey) const;
```

Choose the first unoccupied value from a fixed tag-only palette and fall back by deterministic key hash; do not share the song waveform palette. Re-run Step 4 and require PASS.

- [ ] **Step 6: Commit the tag directory slice**

```bash
git add qt/src/tag_store.hpp qt/src/tag_store.cpp qt/src/tag_model.hpp qt/src/tag_model.cpp qt/CMakeLists.txt tests/CMakeLists.txt tests/qt/tag_model_test.cpp
git commit -m "feat: add persistent tag directory model"
```

### Task 3: Conjunctive tag and resource-folder filtering

**Files:**
- Modify: `qt/src/library_filter_model.hpp: properties, setters, and matcher declarations`
- Modify: `qt/src/library_filter_model.cpp: filterAcceptsRow and targeted invalidation`
- Modify: `tests/qt/library_filter_model_test.cpp: combined filter slots`

**Interfaces:**
- Consumes: Task 2 normalized tag keys and `LibraryModel::TagsRole`; `PlaylistModel::containsTrack` remains the playlist scope source.
- Produces: `Q_PROPERTY(QString tagKey READ tagKey WRITE setTagKey NOTIFY tagKeyChanged)`, `Q_PROPERTY(QString resourceFolder READ resourceFolder WRITE setResourceFolder NOTIFY resourceFolderChanged)`, `setTagKey(QString)`, and `setResourceFolder(QString)`.

- [ ] **Step 1: Write the failing combined-filter/no-reset test**

```cpp
void LibraryFilterModelTest::intersectsTagFolderAndExistingFiltersWithoutSourceReset()
{
    LibraryModel source;
    source.replaceAll({makeTrack("a", "C:/Music/A/a.mp3", "Night Ride", 5, 128, {"Road"}),
                       makeTrack("b", "C:/Music/B/b.mp3", "Night Jazz", 5, 128, {"Jazz"})});
    LibraryFilterModel filter;
    filter.setSourceModel(&source);
    QSignalSpy reset(&source, &QAbstractItemModel::modelReset);
    filter.setTagKey("road");
    filter.setResourceFolder("C:/Music/A");
    filter.setSearchText("night");
    filter.setExactRating(5);
    filter.setMinBpm(120); filter.setMaxBpm(140);
    QCOMPARE(filter.count(), 1);
    QCOMPARE(reset.count(), 0);
}
```

- [ ] **Step 2: Run the focused test to verify it fails**

Run: `cmake --build build/msvc-release --target library_filter_model_test --parallel 4; ctest --test-dir build/msvc-release -R '^library_filter_model_test$' --output-on-failure`

Expected: compilation fails because `setTagKey` and `setResourceFolder` are not declared.

- [ ] **Step 3: Add independent proxy predicates and precise invalidation**

```cpp
bool LibraryFilterModel::rowMatchesTag(int sourceRow) const
{
    if (tagKey_.isEmpty()) return true;
    const QStringList tags = sourceModel()->data(sourceModel()->index(sourceRow, 0),
                                                  LibraryModel::TagsRole).toStringList();
    return std::any_of(tags.cbegin(), tags.cend(), [this](const QString& tag) {
        return tag.toCaseFolded() == tagKey_;
    });
}

bool LibraryFilterModel::rowMatchesResourceFolder(int sourceRow) const
{
    if (resourceFolder_.isEmpty()) return true;
    const QString path = sourceModel()->data(sourceModel()->index(sourceRow, 0),
                                               LibraryModel::PathRole).toString();
    return QDir::cleanPath(path).startsWith(resourceFolder_ + QLatin1Char('/'),
                                             Qt::CaseInsensitive);
}
```

Make `filterAcceptsRow` return the existing category/search/rating/BPM conjunction plus both predicates. Normalize `resourceFolder` with `QDir::cleanPath(QFileInfo(folder).absoluteFilePath())`; setter changes call `invalidateFilter()` only. A tag click sets `tagKey` then leaves search/rating/BPM/category intact; if Task 2 deletes the selected tag, `ListWindow` sets only `tagKey = ""`.

- [ ] **Step 4: Run the focused test to verify it passes**

Run: `cmake --build build/msvc-release --target library_filter_model_test --parallel 4; ctest --test-dir build/msvc-release -R '^library_filter_model_test$' --output-on-failure`

Expected: PASS; tags compare case-insensitively, folder membership uses canonical paths, and the source model emits no reset.

- [ ] **Step 5: Refactor repeated source-index lookup and rerun**

```cpp
QModelIndex LibraryFilterModel::sourceIndexForRow(int sourceRow) const;
```

Use the helper in the two new matchers and preserve the existing playlist-order `lessThan` path. Re-run Step 4 and require PASS.

- [ ] **Step 6: Commit the proxy-filter contract**

```bash
git add qt/src/library_filter_model.hpp qt/src/library_filter_model.cpp tests/qt/library_filter_model_test.cpp
git commit -m "feat: filter library tracks by tag and folder"
```

### Task 4: Persistent resource roots and flat LibraryNavigationModel

**Files:**
- Create: `qt/src/library_navigation_model.hpp`
- Create: `qt/src/library_navigation_model.cpp`
- Modify: `qt/src/library_manager_controller.hpp: resource-root data and signals`
- Modify: `qt/src/library_manager_controller.cpp: persistence notification and root-scoped scan/import handoff`
- Modify: `qt/src/qml_registration.hpp` and `qt/src/qml_registration.cpp: singleton registration parameters`
- Modify: `app/main.cpp: application-owned manager/navigation/tag construction and shutdown flush`
- Modify: `qt/CMakeLists.txt` and `tests/CMakeLists.txt`
- Modify: `tests/qt/library_manager_controller_test.cpp`
- Create: `tests/qt/library_navigation_model_test.cpp`

**Interfaces:**
- Consumes: Task 2 `TagModel::count`, Task 3 `LibraryFilterModel::setResourceFolder`, existing `PlaylistModel` membership/count roles, and `LibraryManagerController::addMonitoredFolder`/`removeMonitoredFolder`/background `rescan`.
- Produces: `LibraryNavigationModel` roles `nodeId`, `nodeType`, `depth`, `displayName`, `count`, `expanded`, `resourceFolder`; `Q_INVOKABLE bool setExpanded(const QString& nodeId, bool expanded)`; `Q_INVOKABLE bool addResourceFolder(const QUrl& folder)`; `Q_INVOKABLE bool removeResourceFolder(const QString& folder)`; and controller `resourceRootsChanged()`.

- [ ] **Step 1: Write failing resource-root and visible-node tests**

```cpp
void LibraryNavigationModelTest::expandsOnlyTheRequestedFolderRange()
{
    LibraryNavigationModel nav(&library, &playlists, &tags, &manager);
    const int before = nav.rowCount();
    QSignalSpy inserted(&nav, &QAbstractItemModel::rowsInserted);
    QVERIFY(nav.setExpanded("root:C:/Music", true));
    QVERIFY(inserted.count() == 1);
    QVERIFY(nav.rowCount() > before);
    QVERIFY(nav.setExpanded("root:C:/Music", false));
    QCOMPARE(nav.rowCount(), before);
}

void LibraryManagerControllerTest::removesPersistedRootWithoutDeletingFiles()
{
    QVERIFY(manager.addMonitoredFolder(musicRoot));
    QVERIFY(manager.removeMonitoredFolder(musicRoot));
    QVERIFY(QFileInfo::exists(audioPath));
    LibraryManagerController restored;
    restored.setStoragePath(settingsPath);
    QVERIFY(restored.monitoredFolders().isEmpty());
}
```

- [ ] **Step 2: Run the focused tests to verify they fail**

Run: `cmake --build build/msvc-release --target library_navigation_model_test library_manager_controller_test --parallel 4; ctest --test-dir build/msvc-release -R '^(library_navigation_model_test|library_manager_controller_test)$' --output-on-failure`

Expected: CMake target configuration or compilation fails because `LibraryNavigationModel` and controller root-change contract are absent.

- [ ] **Step 3: Implement root persistence, background import handoff, and visible-row mutations**

```cpp
class LibraryNavigationModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { NodeIdRole = Qt::UserRole + 1, NodeTypeRole, DepthRole,
                DisplayNameRole, CountRole, ExpandedRole, ResourceFolderRole };
    Q_INVOKABLE bool setExpanded(const QString& nodeId, bool expanded);
    Q_INVOKABLE bool addResourceFolder(const QUrl& folder);
    Q_INVOKABLE bool removeResourceFolder(const QString& folder);
};

bool LibraryManagerController::addMonitoredFolder(const QString& folder)
{
    const QString path = canonicalDirectory(folder);
    if (!QFileInfo(path).isDir() || containsRoot(path)) return false;
    monitoredRoots_.append(path);
    saveMonitoredFolders(); rebuildDirectoryWatches();
    emit monitoredFoldersChanged(); emit resourceRootsChanged();
    rescan();
    return true;
}
```

Keep the existing `storagePath_` JSON storage but retain its `folders` array schema for backward compatibility. Construct one manager in `app/main.cpp`, call `setStoragePath(QFileInfo(libraryPath).dir().filePath("resource-roots.json"))`, `setLibraryDataPath(libraryPath)`, `setLibraryModel(&library)`, and `setImportController(&importer)`. Construct `TagModel` with sibling `tags.json` and `LibraryNavigationModel` with the four live models; pass/register these existing instances through `register_agplayer_qml_types`. For a node expand, enumerate only immediate directory children on the existing scan worker snapshot, calculate counts from in-memory track paths, then `beginInsertRows`/`endInsertRows` the child range; collapse uses `beginRemoveRows`/`endRemoveRows` only for that node's descendant range. Never recurse synchronously on the GUI thread.

- [ ] **Step 4: Run the focused tests to verify they pass**

Run: `cmake --build build/msvc-release --target library_navigation_model_test library_manager_controller_test --parallel 4; ctest --test-dir build/msvc-release -R '^(library_navigation_model_test|library_manager_controller_test)$' --output-on-failure`

Expected: PASS; roots restore across controller recreation, removal leaves the actual folder/audio present, resource add invokes the existing asynchronous importer, and expand/collapse changes one visible child interval without model reset.

- [ ] **Step 5: Refactor node identity construction and rerun**

```cpp
static QString navigationNodeId(const QString& type, const QString& stableValue);
```

Use this helper for fixed nodes, playlist IDs, and normalized directory paths so rows retain selection/expansion identity after sibling updates. Re-run Step 4 and require PASS.

- [ ] **Step 6: Commit the persistent navigation data slice**

```bash
git add qt/src/library_navigation_model.hpp qt/src/library_navigation_model.cpp qt/src/library_manager_controller.hpp qt/src/library_manager_controller.cpp qt/src/qml_registration.hpp qt/src/qml_registration.cpp app/main.cpp qt/CMakeLists.txt tests/CMakeLists.txt tests/qt/library_manager_controller_test.cpp tests/qt/library_navigation_model_test.cpp
git commit -m "feat: add persistent library navigation model"
```

### Task 5: Unified QML navigation, tag panel, and shared list wiring

**Files:**
- Create: `app/qml/AgPlayer/components/TagManagementPanel.qml`
- Modify: `app/qml/AgPlayer/components/SideNavigation.qml: one virtualized navigation ListView and resource-root controls`
- Modify: `app/qml/AgPlayer/ListWindow.qml: continuous three-column content and filter bindings`
- Modify: `app/qml/AgPlayer/components/TrackList.qml: batch tag editing and non-playlist mode checks`
- Modify: `app/qml/AgPlayer/theme/Theme.qml: semantic navigation/tag-panel tokens only`
- Modify: `app/CMakeLists.txt: qt_add_qml_module(agplayer_app_qml) QML_FILES list`.

**Interfaces:**
- Consumes: Task 2 singleton `TagModel`, Task 3 `LibraryFilterModel.tagKey`/`resourceFolder`, Task 4 singleton `LibraryNavigationModel` and `LibraryManagerController` resource operations, existing `TrackList`/`PlaylistModel`/`ImportController`/`NativeDropRouter` contracts.
- Produces: object names `libraryNavigationView`, `tagManagementPanel`, `tagSearchField`, `tagGrid`, `tagContextMenu`, `resourceFolderDropTarget`, and exactly one `detachedTrackList` for the list window.

- [ ] **Step 1: Write the failing QML structural and interaction contracts**

```qml
function test_tag_navigation_uses_one_shared_track_list_and_three_column_grid() {
    const window = createTemporaryObject(listWindowComponent, testCase)
    verify(window)
    compare(findChildren(window, "detachedTrackList").length, 1)
    verify(findChild(window, "libraryNavigationView"))
    const grid = findChild(window, "tagGrid")
    verify(grid)
    compare(grid.gridColumnCount, 3)
    findChild(window, "tagSearchField").text = "road"
    tryCompare(TagModel, "selectedKey", "road")
    window.destroy()
}
```

- [ ] **Step 2: Run the QML contract test to verify it fails**

Run: `cmake --build build/msvc-release --target qml_main_window_test --parallel 4; ctest --test-dir build/msvc-release -R '^qml_main_window_test$' --output-on-failure`

Expected: FAIL because the navigation/tag-panel object names and `TagModel` singleton are not available to the test scene.

- [ ] **Step 3: Build the continuous three-column QML surface**

```qml
RowLayout {
    Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
    SideNavigation { Layout.preferredWidth: 256; navigationModel: LibraryNavigationModel }
    Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.workspaceDivider }
    ColumnLayout {
        Layout.minimumWidth: 680; Layout.fillWidth: true; Layout.fillHeight: true
        TrackList { objectName: "detachedTrackList"; trackModel: filterModel }
        SearchFilter { /* existing search/rating/BPM bindings remain */ }
    }
    Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.workspaceDivider }
    TagManagementPanel { objectName: "tagManagementPanel"; Layout.preferredWidth: 328 }
}
```

`SideNavigation` replaces the current `Column`/`Repeater` with one `ListView` bound to `LibraryNavigationModel`; its delegate uses `depth`, invokes `setExpanded`, routes playlist nodes to `filterModel.category`, tag-management node to the `tags` surface, and folder nodes to `filterModel.resourceFolder`. Its resource-root `FolderDialog` and `FileDropArea` accept only `QUrl.isLocalFile()` directories and call `LibraryNavigationModel.addResourceFolder`; normal audio drops continue through `ListWindow.beginImport`/native routing. `TagManagementPanel` uses `GridView { cellWidth: width / 3; model: TagModel }` and exposes `readonly property int gridColumnCount: 3`; it filters by a local case-folded display/key predicate without changing song filtering until a tag is clicked, and opens one `Menu` at the pointer for rename/delete/color. On a tag click set `filterModel.tagKey = key` and `TagModel.selectedKey = key`; Task 2 removal clears this property only if it is that key. Change `TrackList` tag-dialog acceptance to `LibraryModel.setTagsForTracks(trackMenu.targetTrackIds, values)`.

- [ ] **Step 4: Run the QML contract test to verify it passes**

Run: `cmake --build build/msvc-release --target qml_main_window_test --parallel 4; ctest --test-dir build/msvc-release -R '^qml_main_window_test$' --output-on-failure`

Expected: PASS; one shared `TrackList` is present, three tag columns are stable, search/add/context-menu controls exist, and navigation/folder selection changes the proxy instead of creating another song view.

- [ ] **Step 5: Refactor visual constants into semantic theme tokens and rerun**

```qml
readonly property color workspaceDivider: dark ? "#39334b" : "#d7dce6"
readonly property color tagSelectedSurface: dark ? "#312a45" : "#e9e3f7"
readonly property int navigationRowHeight: 38
```

Use these tokens for the new panels; leave unrelated existing colors untouched. Re-run Step 4 and require PASS.

- [ ] **Step 6: Commit the QML integration slice**

```bash
git add app/CMakeLists.txt app/qml/AgPlayer/components/TagManagementPanel.qml app/qml/AgPlayer/components/SideNavigation.qml app/qml/AgPlayer/ListWindow.qml app/qml/AgPlayer/components/TrackList.qml app/qml/AgPlayer/theme/Theme.qml tests/qml/tst_main_window.qml
git commit -m "feat: integrate tag navigation in list window"
```

### Task 6: Cross-layer regression evidence and acceptance record

**Files:**
- Modify: `tests/qml/tst_main_window.qml: resource-drop, context-menu, and filter-clear assertions`
- Modify: `tests/qt/tag_model_test.cpp: corrupt metadata fallback and selected-tag deletion assertions`
- Modify: `tests/qt/library_navigation_model_test.cpp: 10K count update and no-reset assertions`
- Create: `docs/development/2026-08-20-tag-data-navigation.md`
- Modify: `design-qa.md: only after the visual acceptance steps have passed`

**Interfaces:**
- Consumes: Tasks 1-5 public contracts and existing Release smoke entry point `scripts/qa-main-smoke.ps1`.
- Produces: a dated traceability matrix from requirement to code/test/evidence and a factual record of automated, visual, scale, playback, and DPI outcomes.

- [ ] **Step 1: Write the remaining failing regression tests before acceptance work**

```qml
function test_removing_active_tag_clears_only_tag_filter() {
    filterModel.searchText = "night"
    filterModel.exactRating = 5
    filterModel.tagKey = "road"
    TagModel.removeTag("road")
    tryCompare(filterModel, "tagKey", "")
    compare(filterModel.searchText, "night")
    compare(filterModel.exactRating, 5)
}
```

```cpp
void TagModelTest::rebuildsDirectoryWhenMetadataIsCorruptWithoutClearingTrackTags()
{
    writeText(storagePath, "{broken");
    TagModel tags(&library, storagePath);
    QCOMPARE(tags.countForKey("road"), 1);
    QCOMPARE(library.trackForId("a").value("tags").toStringList(), QStringList{"Road"});
}
```

- [ ] **Step 2: Run the focused tests to verify they fail**

Run: `cmake --build build/msvc-release --target tag_model_test library_navigation_model_test qml_main_window_test --parallel 4; ctest --test-dir build/msvc-release -R '^(tag_model_test|library_navigation_model_test|qml_main_window_test)$' --output-on-failure`

Expected: FAIL until the active-filter clear handler, corrupt-metadata recovery, and navigation update behavior are complete.

- [ ] **Step 3: Complete the narrow recovery/acceptance behavior**

```cpp
if (!TagStore(storagePath_).load(&storedEntries)) {
    rebuildFromLibrary();
    scheduleSave();
}
```

Connect `TagModel::tagRemoved(QString key)` to the `ListWindow` handler that clears `filterModel.tagKey` only when its case-folded value matches. Add a 10,000-record navigation test that mutates one affected tag/path and asserts targeted `dataChanged`/row-range signals, not `modelReset`; do not add a benchmark loop or persistent instrumentation to production code.

- [ ] **Step 4: Run the focused and suite-level automated checks to verify they pass**

Run: `cmake --build build/msvc-release --target tag_model_test library_navigation_model_test library_model_test library_filter_model_test library_manager_controller_test playlist_model_test qml_main_window_test --parallel 4; ctest --test-dir build/msvc-release -R '^(tag_model_test|library_navigation_model_test|library_model_test|library_filter_model_test|library_manager_controller_test|playlist_model_test|qml_main_window_test)$' --output-on-failure`

Expected: PASS; the source library remains intact after tag/root deletion, corrupt tag metadata rebuilds from tracks, folder import is asynchronous, and one active tag removal retains search/rating/BPM filters.

- [ ] **Step 5: Perform and record real visual, DPI, scale, and playback checks**

Run: `powershell -ExecutionPolicy Bypass -File scripts/qa-main-smoke.ps1 -BuildDirectory build/msvc-release`

Expected: PASS for the existing Release import/play/screenshot/exit smoke. Then run AgPlayer at 1447x1087 in dark and light themes; capture the continuous three-column tag-management state; compare it against the supplied reference for column widths, 1px dividers, row/table layout, selected tag/navigation states, three-column tag grid, and scroll positions. Repeat at 100%, 125%, 150%, 175%, and 200% DPI, and manually exercise add/rename/recolor/delete, folder add/remove/Explorer drop, 1,000 and 10,000 track filter/scroll/switch flows, and playback/seek/track change while scrolling. Record measured outcomes and any unrun platform/hardware checks in the development log; do not mark `design-qa.md` passed if any P0/P1/P2 issue remains.

- [ ] **Step 6: Commit the tested acceptance record**

```bash
git add tests/qml/tst_main_window.qml tests/qt/tag_model_test.cpp tests/qt/library_navigation_model_test.cpp docs/development/2026-08-20-tag-data-navigation.md design-qa.md
git commit -m "test: verify tag navigation acceptance"
```

## Plan Self-Review

### Spec coverage

- Tag directory, display/color metadata, zero-count entries, rename/delete behavior, incremental counts, and case-insensitive identity: Tasks 1-2 and 6.
- Tag, search, rating, BPM, playlist, and folder intersection without source reset: Task 3 and Task 6.
- Flat/virtual navigation, expand/collapse row ranges, playlist/library/favorites/tag counts, resource-root persistence, removal safety, and asynchronous import: Task 4.
- One continuous three-column list-window surface, one `TrackList`, resource drop routing, tag grid/context actions, and theme tokens: Task 5.
- Required C++/QML contracts plus release, visual, scale, playback, DPI, and documentation evidence: Task 6.
- Waveform-thumbnail rendering/settings/drag-preview work is deliberately excluded from this tag-data/navigation plan and remains governed by its separately scoped plan/spec work.

### Placeholder scan

Every change task has explicit files, public interfaces, red command, green command, refactor action, and commit command; no step defers unspecified work or names an undefined API.

### Type consistency

`TagModel` owns only directory metadata and calls the Task 1 `LibraryModel` APIs for relation edits. `LibraryFilterModel.tagKey` and `.resourceFolder` are the only new central-list predicates. `LibraryNavigationModel` owns visible node ranges and delegates root persistence/import to `LibraryManagerController`; QML reads all three application-owned objects through the names registered in Task 4.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-08-20-tag-data-navigation.md`. Two execution options:

1. Subagent-Driven (recommended) - I dispatch a fresh subagent per task, review between tasks, fast iteration

2. Inline Execution - Execute tasks in this session using executing-plans, batch execution with checkpoints

Which approach?

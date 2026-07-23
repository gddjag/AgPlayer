# AgPlayer Phase 2.1: Audio Tools Framework + Info Edit Tool

**Status:** Pending approval
**Date:** 2026-07-23
**Design basis:** `ui/音频工具 信息修改.png`, `ui/音频工具 剪辑.png`, `ui/音频工具 格式转换.png`, `ui/音频工具 调速.png`, `ui/音频工具 升降调.png`, user functional requirements

## 1. Goals

Phase 2.1 establishes the audio tools framework (a separate window with left-sidebar navigation for 5 tools) and implements the first tool: **信息修改 (Info Edit)** — batch metadata editing and batch filename prefix/suffix renaming.

This phase delivers:
- A standalone `AudioToolsWindow` accessible from the TitleBar, with a 240px left navigation sidebar listing all 5 tools.
- The **信息修改** tool fully functional: drag-drop file import, batch tag editing (title/artist/album/year/genre/cover), batch filename renaming (prefix/suffix/auto-number), live preview, and atomic write-back.
- Placeholder pages for the other 4 tools (格式转换/轻度剪辑/调整速度/升调降调) showing a "coming soon" state — no fake buttons.

## 2. Project constraints

- Follow the existing three-layer architecture: pure C++17 core, Qt Bridge, QML UI. The C ABI remains the only cross-layer public boundary.
- No new third-party dependencies in Phase 2.1. Metadata read/write uses FFmpeg (already available via vcpkg) in stream-copy mode (`-c copy` equivalent). SoundTouch is deferred to Phase 2.3.
- All visible controls must have real functionality. Placeholder pages must not contain fake buttons.
- Zero compiler warnings, zero errors. All ctest must pass in Debug and Release.
- No packaging, no deployment, no EXE generation.
- Do not create empty classes, empty pages, or factory interfaces for future tools. Each tool is added when its phase is implemented.

## 3. Scope

### 3.1 Included

- `AudioToolsWindow` — frameless window with left sidebar navigation and right content area.
- `ToolSidebar` — 5 tool entries with icons and labels; current selection highlighted.
- `MetadataEditor` C++ controller — batch metadata read/write via FFmpeg stream copy, batch filename rename with preview.
- `InfoEditPage` QML — two-section layout: metadata batch edit (top) and filename batch rename (bottom).
- `AudioToolsController` — lightweight controller managing `currentTool` property and window visibility.
- TitleBar integration — new "Audio Tools" button opening the window.
- `WindowController` extension — `showAudioTools()` / `hideAudioTools()` methods.
- Unit tests for `MetadataEditor` (metadata round-trip, rename preview, error handling).
- QML tests for `InfoEditPage` (load, preview, apply flow).

### 3.2 Not included (deferred to later phases)

- 格式转换 (Format Conversion) — Phase 2.2
- 升调降调 (Pitch Shifting) — Phase 2.3 (requires SoundTouch)
- 调整速度 (Speed Adjustment) — Phase 2.4 (requires SoundTouch + BPM detection)
- 轻度剪辑 (Light Editing) — Phase 2.5 (multi-track editor)
- Settings page, magnetic window snapping, global hotkeys.

## 4. Architecture

### 4.1 Entry point

TitleBar gets a new `ToolButton` between the logo area and the mini-player button. The button uses the `playlist-2-fill.svg` icon (already in assets) with the label "Audio Tools" tooltip. Clicking calls `WindowController.showAudioTools()`.

### 4.2 AudioToolsWindow

A separate `Window` component following the `MiniPlayerWindow` pattern:

- Size: 1200 x 780, minimum 960 x 600.
- `Qt.FramelessWindowHint`, dark background (`Theme.background`).
- Left sidebar: 240px wide, `Theme.panel` background, contains the app logo at top and 5 tool entries below.
- Right content area: `StackLayout` bound to `audioToolsController.currentTool` index. Phase 2.1 only has `InfoEditPage` at index 4; indices 0-3 show a `ComingSoonPage` placeholder.
- Window controls (minimize/close) in the top-right of the sidebar, using the same `z: -1` drag MouseArea pattern as `TitleBar` and `MiniPlayerWindow`.

### 4.3 ToolSidebar

Each tool entry is a `Button` with:
- Icon (40x40, using existing SVG assets where available, or a generic `music-2-fill.svg` for tools without a dedicated icon)
- Label text below the icon
- Selected state: `Theme.cyan` text/icon + `Theme.border` background
- Unselected state: `Theme.secondaryText` text/icon + transparent background

The 5 tools in order:
1. 格式转换 (icon: `music-2-fill.svg` — no dedicated icon yet)
2. 轻度剪辑 (icon: `music-2-fill.svg`)
3. 调整速度 (icon: `music-2-fill.svg`)
4. 升调降调 (icon: `music-2-fill.svg`)
5. 信息修改 (icon: `music-2-fill.svg`)

Phase 2.1 only tool 5 is clickable with a real page. Tools 1-4 navigate to a `ComingSoonPage` that displays "即将推出" — no fake buttons or controls.

### 4.4 AudioToolsController

```cpp
class AudioToolsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int currentTool READ currentTool WRITE setCurrentTool NOTIFY currentToolChanged)
    Q_PROPERTY(bool visible READ visible NOTIFY visibleChanged)

public:
    explicit AudioToolsController(QObject* parent = nullptr);

    int currentTool() const noexcept;
    bool visible() const noexcept;
    void setCurrentTool(int tool);

    Q_INVOKABLE void show();
    Q_INVOKABLE void hide();
    Q_INVOKABLE void selectTool(int tool);

signals:
    void currentToolChanged();
    void visibleChanged();
    void showRequested();
    void hideRequested();

private:
    int currentTool_ = 4;  // Default to Info Edit (index 4)
    bool visible_ = false;
};
```

Registered as a QML singleton (following the `PlaybackController` pattern in `qml_registration.cpp`).

### 4.5 Core layer C API extension

The existing `MediaMetadata` struct and C API only expose title/artist/album/format/sample_rate/channels/bits_per_sample/bit_rate/duration_ms/cover. Phase 2.1 needs year and genre for the Info Edit tool, plus a metadata write function. These are added to the core layer to maintain the C ABI as the sole cross-layer boundary.

**MediaMetadata struct** (`core/src/decoder.hpp`): add two fields:
```cpp
std::string year;   // e.g. "2024" — from AV_DICT "date"
std::string genre;  // from AV_DICT "genre"
```

**C API additions** (`core/include/agplayer/c_api.h`):
```c
// New getters for existing ag_metadata handle
const char* ag_metadata_year(const ag_metadata* metadata);
const char* ag_metadata_genre(const ag_metadata* metadata);

// New: write metadata to a file using FFmpeg stream copy (no re-encoding).
// Fields set to NULL are preserved from the source. cover_data is applied
// only if non-NULL and cover_size > 0. Writes to a temp file then atomically
// replaces the original. Returns AG_OK on success, or an error code.
ag_result ag_metadata_write(const char* utf8_path,
                             const char* title,
                             const char* artist,
                             const char* album,
                             const char* year,
                             const char* genre,
                             const unsigned char* cover_data,
                             size_t cover_size,
                             const char* cover_mime_type);
```

**Implementation** (`core/src/`): `ag_metadata_year` / `ag_metadata_genre` read from the FFmpeg `AVDictionary` ("date" / "genre" keys). `ag_metadata_write` implements the FFmpeg stream-copy write described in 4.6. The public header `c_api.h` still leaks no FFmpeg types — only C primitives.

### 4.6 MetadataEditor (Qt Bridge)

The backend controller for the Info Edit tool. Handles batch metadata read/write and filename renaming. All FFmpeg interaction stays in the core layer; the Qt bridge calls the C API exclusively.

**Metadata reading:** Uses `ag_metadata_open` + the new `ag_metadata_year` / `ag_metadata_genre` getters. Results are cached in a `QList<MetadataEntry>` internal model.

**Metadata writing:** Calls `ag_metadata_write` for each target file. The FFmpeg stream-copy logic (open input → alloc output → copy streams → set metadata → write header → copy packets → write trailer → atomic replace) lives in the core layer. Fields set to empty string by the user are skipped (NULL passed to `ag_metadata_write`), preserving the source value.

**Filename renaming:** Pure filesystem operation, no FFmpeg needed:
- Apply prefix + suffix to the filename stem (preserving extension).
- If auto-number is enabled, insert a zero-padded sequential number.
- Preview generates the new names without touching disk.
- Apply does `QFile::rename` for each file, with collision avoidance (append `_2`, `_3`, etc. if target exists).

**Thread safety:** All batch operations run on a worker thread via `QtConcurrent::run` + `QFutureWatcher`. Progress is reported via `progressChanged` signal (0.0 to 1.0). The UI shows a progress bar. Cancellation is supported via an `std::atomic<bool> cancelFlag_`.

```cpp
struct MetadataEntry {
    QString path;
    QString fileName;
    QString title;
    QString artist;
    QString album;
    QString year;
    QString genre;
    QString format;
    qint64 durationMs = 0;
    qint64 fileSize = 0;
    QByteArray coverData;  // JPEG/PNG bytes
    bool coverModified = false;
    bool hasError = false;
    QString error;
};

class MetadataEditor : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY fileCountChanged)

public:
    explicit MetadataEditor(QObject* parent = nullptr);

    double progress() const noexcept;
    bool busy() const noexcept;
    int fileCount() const noexcept;

    // Load metadata from a list of file URLs (drag-drop or file dialog)
    Q_INVOKABLE void loadFiles(const QList<QUrl>& urls);

    // Get metadata for a file at index (for QML display)
    Q_INVOKABLE QVariantMap entryAt(int index) const;

    // Batch apply metadata to selected files. Fields that are empty strings
    // are skipped (preserve existing value). coverData is applied only if
    // non-empty.
    Q_INVOKABLE void applyMetadata(const QVariantMap& fields,
                                    const QList<int>& indices);

    // Preview filename rename — returns list of "oldName -> newName" strings
    Q_INVOKABLE QStringList previewRename(const QString& prefix,
                                           const QString& suffix,
                                           bool autoNumber,
                                           int numberStart,
                                           int numberDigits) const;

    // Apply filename rename
    Q_INVOKABLE void applyRename(const QString& prefix,
                                  const QString& suffix,
                                  bool autoNumber,
                                  int numberStart,
                                  int numberDigits);

    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

signals:
    void progressChanged();
    void busyChanged();
    void fileCountChanged();
    void entriesLoaded();
    void metadataApplied(int successCount, int failureCount);
    void renameApplied(int successCount, int failureCount);
    void errorOccurred(const QString& message);

private:
    QList<MetadataEntry> entries_;
    std::atomic<bool> cancelFlag_{false};
    std::atomic<double> progress_{0.0};
    std::atomic<bool> busy_{false};

    static bool writeMetadataFFmpeg(const QString& path,
                                     const MetadataEntry& entry,
                                     QString& error);
};
```

### 4.6 InfoEditPage QML

Two-section layout in a `ColumnLayout`:

**Top section — Metadata Batch Edit (60% height):**

- **File list area** (left, 40% width): `ListView` showing loaded files. Supports drag-drop import (`DropArea`). Each row: filename, format, duration. Checkbox column to select/deselect files for batch apply.
- **Metadata form** (right, 60% width): Input fields for title, artist, album, year, genre. Cover image preview area (click to browse / drag-drop image). "Apply Range" radio buttons: "All Files" / "Selected Files". "Batch Apply" button at bottom.
- Empty state: "拖拽音频文件到左侧列表，或点击添加" with an "Add Files" button that opens a FileDialog.

**Bottom section — Filename Batch Rename (40% height):**

- **Original filenames** (left, 50% width): `ListView` showing current filenames.
- **Rename controls** (right-top, 50% width): Prefix `TextField`, Suffix `TextField`, "Auto Number" `CheckBox`, number start `SpinBox` (default 1), number digits `SpinBox` (default 2, range 1-5).
- **Preview list** (right-bottom, 50% width): `ListView` showing "oldName → newName" pairs, updated live as prefix/suffix/number settings change.
- "Batch Rename" button at the bottom.

**Visual style:**
- Section headers: `Theme.secondaryText`, 13px, uppercase.
- Input fields: `Theme.panel` background, `Theme.border` 1px border, `Theme.radiusSm` corners, `Theme.primaryText` text.
- Buttons: gradient `Theme.cyan` → `Theme.violet` for primary actions, `Theme.panel` with `Theme.border` for secondary.
- Progress bar: `Theme.cyan` fill on `Theme.panel` track, shown when `metadataEditor.busy`.

## 5. Data flow

### 5.1 Load files

1. User drags files onto the file list `DropArea` or clicks "Add Files".
2. `MetadataEditor.loadFiles(urls)` is called.
3. For each URL, `ag_metadata_open` reads metadata on a worker thread.
4. `entriesLoaded` signal fires; QML `ListView` refreshes.
5. Metadata form fields are pre-filled with the first file's values (as defaults).

### 5.2 Apply metadata

1. User edits metadata fields in the form.
2. User selects "All Files" or "Selected Files" (via checkboxes).
3. User clicks "Batch Apply".
4. `MetadataEditor.applyMetadata(fields, indices)` runs on worker thread.
5. For each target file: FFmpeg stream-copy writes new metadata to a temp file, then atomically replaces the original.
6. `metadataApplied(success, failure)` signal fires; QML shows a result toast.
7. `progressChanged` updates the progress bar during processing.

### 5.3 Preview rename

1. User types prefix/suffix, toggles auto-number, adjusts number settings.
2. `MetadataEditor.previewRename(...)` is called (synchronous, fast — just string manipulation).
3. Preview list updates live with "oldName → newName" pairs.
4. No disk I/O during preview.

### 5.4 Apply rename

1. User clicks "Batch Rename".
2. `MetadataEditor.applyRename(...)` runs on worker thread.
3. For each file: `QFile::rename` with collision avoidance.
4. `renameApplied(success, failure)` signal fires.
5. File list refreshes with new filenames.

## 6. Error handling

- **File read failure:** Mark the entry with `hasError = true` and an error message. Skip during apply. Show error icon in the file list.
- **Metadata write failure:** Keep original file intact (temp file is deleted). Record error. Continue with remaining files.
- **Rename collision:** Append `_2`, `_3`, etc. before the extension. If still colliding after 99 attempts, skip and record error.
- **Cover image invalid:** If the provided image data is not valid JPEG/PNG, report error and skip cover update for that file.
- **Cancellation:** `cancel()` sets `cancelFlag_`. The worker thread checks the flag between files and stops. Already-processed files are not rolled back.
- **All errors are collected** and reported in the `metadataApplied` / `renameApplied` signals' failure count, plus an `errorOccurred` signal with a detailed message.

## 7. Testing strategy

### 7.1 Unit tests (C++, in tests/qt/)

- `metadata_editor_test.cpp`:
  - Load single file, verify metadata fields populated.
  - Load multiple files, verify count.
  - Apply metadata to single file, re-read and verify.
  - Apply metadata to multiple files, verify all updated.
  - Preview rename with prefix only, suffix only, both, auto-number.
  - Preview rename collision avoidance.
  - Apply rename, verify files renamed on disk.
  - Cancel mid-operation, verify partial results.
  - Error handling: nonexistent file, read-only file, invalid cover data.

### 7.2 QML tests (in tests/qml/)

- `tst_info_edit_page.qml`:
  - Page loads, shows empty state.
  - Drag-drop simulation adds files.
  - Metadata form pre-fills from first file.
  - Preview updates on prefix/suffix change.
  - Batch apply button calls controller.
  - Progress bar shows during busy state.

### 7.3 Integration

- Reuse the existing `fixture_generator.cpp` to create test WAV/MP3 files with known metadata.
- Metadata round-trip test: write metadata → re-read → verify fields match.
- All tests run in both Debug and Release configurations.

## 8. Dependencies

- **FFmpeg** (already in vcpkg.json): `avformat`, `avcodec` for stream-copy metadata writing.
- **Qt Concurrent** (already in CMakeLists.txt): for background thread execution.
- **No new dependencies** in Phase 2.1.

## 9. File manifest

### New files

| File | Layer | Purpose |
|------|-------|---------|
| `core/src/metadata_writer.hpp` | Core | FFmpeg stream-copy metadata write function |
| `core/src/metadata_writer.cpp` | Core | Implementation (open → copy streams → set tags → write → atomic replace) |
| `app/qml/AgPlayer/AudioToolsWindow.qml` | QML | Window container with sidebar + content area |
| `app/qml/AgPlayer/components/tools/ToolSidebar.qml` | QML | Left navigation with 5 tool entries |
| `app/qml/AgPlayer/components/tools/InfoEditPage.qml` | QML | Info Edit tool page |
| `app/qml/AgPlayer/components/tools/ComingSoonPage.qml` | QML | Placeholder for unimplemented tools |
| `qt/src/audio_tools_controller.hpp` | Qt Bridge | Tool switching + window visibility |
| `qt/src/audio_tools_controller.cpp` | Qt Bridge | Implementation |
| `qt/src/metadata_editor.hpp` | Qt Bridge | Batch metadata + rename backend |
| `qt/src/metadata_editor.cpp` | Qt Bridge | Implementation (calls C API) |
| `tests/qt/metadata_editor_test.cpp` | Tests | Unit tests |
| `tests/qml/tst_info_edit_page.qml` | Tests | QML tests |
| `tests/qt/qml_info_edit_test_main.cpp` | Tests | QML test harness |

### Modified files

| File | Change |
|------|--------|
| `core/include/agplayer/c_api.h` | Add `ag_metadata_year`, `ag_metadata_genre`, `ag_metadata_write` |
| `core/src/c_api.cpp` | Implement new C API functions |
| `core/src/decoder.hpp` | Add `year`, `genre` fields to `MediaMetadata` |
| `core/src/decoder.cpp` | Populate `year` / `genre` from FFmpeg `AVDictionary` |
| `core/CMakeLists.txt` | Add new metadata writer source file |
| `app/qml/AgPlayer/components/TitleBar.qml` | Add "Audio Tools" button |
| `qt/src/window_controller.hpp` | Add `showAudioTools()` / `hideAudioTools()` |
| `qt/src/window_controller.cpp` | Implement audio tools window management |
| `qt/src/qml_registration.cpp` | Register `AudioToolsController` and `MetadataEditor` |
| `qt/CMakeLists.txt` | Add new source files |
| `app/CMakeLists.txt` | Add new QML files to resources |
| `tests/CMakeLists.txt` | Add new test targets |
| `app/main.cpp` | Wire `AudioToolsController` to `WindowController` |

## 10. Future phase hooks

- Phase 2.2 (Format Conversion): Add `FormatConvertPage.qml` + `FormatConverter` controller. Reuses the `AudioToolsWindow` framework.
- Phase 2.3 (Pitch Shifting): Add `SoundTouch` to vcpkg.json. Add `PitchShiftPage.qml` + `PitchShifter` controller.
- Phase 2.4 (Speed Adjustment): Add BPM detection. Add `SpeedAdjustPage.qml` + `SpeedAdjuster` controller.
- Phase 2.5 (Light Editing): Add multi-track editor. Add `LightEditPage.qml` + `MultiTrackEditor` controller.

Each future phase replaces its `ComingSoonPage` with the real implementation. No stubs or interfaces are created in Phase 2.1 for these future tools.

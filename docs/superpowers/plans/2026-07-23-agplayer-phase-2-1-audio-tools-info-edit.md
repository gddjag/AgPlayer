# AgPlayer Phase 2.1: Audio Tools Framework + Info Edit Tool — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the audio tools framework (separate window with 5-tool sidebar navigation) and implement the first tool — 信息修改 (Info Edit) — for batch metadata editing and batch filename renaming.

**Architecture:** Three-layer: pure C++17 core (metadata read/write via FFmpeg stream copy) → Qt Bridge (MetadataEditor + AudioToolsController controllers) → QML UI (AudioToolsWindow + InfoEditPage). C ABI remains the sole cross-layer boundary.

**Tech Stack:** Qt 6.7 / QML / C++17 / CMake / CTest / FFmpeg 7.1 (vcpkg) / Ninja / MSVC v143

## Global Constraints

- Three-layer architecture: pure C++17 core, Qt Bridge, QML UI. C ABI is the only cross-layer public boundary.
- Public header `c_api.h` must not leak Qt, FFmpeg, or miniaudio types.
- No new third-party dependencies. FFmpeg (already in vcpkg.json) used for metadata stream-copy write.
- Zero compiler warnings, zero errors. All ctest must pass in Debug and Release.
- All visible controls must have real functionality. Placeholder pages must not contain fake buttons.
- No packaging, no EXE generation.
- Do not create empty classes, empty pages, or factory interfaces for future tools.
- VS DevShell must be loaded before building: `Import-Module "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"; Enter-VsDevShell -VsInstallPath "C:\Program Files\Microsoft Visual Studio\2022\Community" -SkipAutomaticLocation -DevCmdArguments "-arch=x64" | Out-Null`
- Build commands: `cmake --preset windows-msvc-debug --build` and `cmake --preset windows-msvc-release --build`
- Test commands: `ctest --preset windows-msvc-debug` and `ctest --preset windows-msvc-release`
- Worktree: `d:\ai\TRAE AgPlayer\.worktrees\phase-1-playback`, branch `feature/phase-1-playback`

---

## File Structure

### New files (13)

| File | Layer | Responsibility |
|------|-------|----------------|
| `core/src/metadata_writer.hpp` | Core | `write_metadata()` function declaration |
| `core/src/metadata_writer.cpp` | Core | FFmpeg stream-copy metadata write implementation |
| `qt/src/audio_tools_controller.hpp` | Qt Bridge | Tool switching + window visibility QObject |
| `qt/src/audio_tools_controller.cpp` | Qt Bridge | Implementation |
| `qt/src/metadata_editor.hpp` | Qt Bridge | Batch metadata + rename backend QObject |
| `qt/src/metadata_editor.cpp` | Qt Bridge | Implementation (calls C API, QtConcurrent workers) |
| `app/qml/AgPlayer/AudioToolsWindow.qml` | QML | Frameless window with sidebar + StackLayout |
| `app/qml/AgPlayer/components/tools/ToolSidebar.qml` | QML | 5 tool navigation buttons |
| `app/qml/AgPlayer/components/tools/InfoEditPage.qml` | QML | Info Edit tool page (metadata + rename) |
| `app/qml/AgPlayer/components/tools/ComingSoonPage.qml` | QML | Placeholder for unimplemented tools |
| `tests/qt/metadata_editor_test.cpp` | Tests | Unit tests for MetadataEditor |
| `tests/qml/tst_info_edit_page.qml` | Tests | QML tests for InfoEditPage |
| `tests/qt/qml_info_edit_test_main.cpp` | Tests | QML test harness for InfoEditPage |

### Modified files (13)

| File | Change |
|------|--------|
| `core/include/agplayer/c_api.h` | Add `ag_metadata_year`, `ag_metadata_genre`, `ag_metadata_write` |
| `core/src/c_api.cpp` | Implement new C API functions |
| `core/src/decoder.hpp` | Add `year`, `genre` fields to `MediaMetadata` |
| `core/src/decoder.cpp` | Populate `year` / `genre` from FFmpeg `AVDictionary` |
| `core/CMakeLists.txt` | Add `metadata_writer.cpp` to sources |
| `qt/src/window_controller.hpp` | Add `showAudioTools()` / `hideAudioTools()` |
| `qt/src/window_controller.cpp` | Implement audio tools window management |
| `qt/src/qml_registration.cpp` | Register `AudioToolsController` and `MetadataEditor` |
| `qt/CMakeLists.txt` | Add new source files |
| `app/qml/AgPlayer/components/TitleBar.qml` | Add "Audio Tools" button |
| `app/CMakeLists.txt` | Add new QML files to resources |
| `tests/CMakeLists.txt` | Add new test targets |
| `app/main.cpp` | Wire `AudioToolsController` to `WindowController` |

---

## Task 1: Core — Extend MediaMetadata with year/genre

**Files:**
- Modify: `core/src/decoder.hpp` (add fields to `MediaMetadata` struct)
- Modify: `core/src/decoder.cpp` (populate new fields from `AVDictionary`)
- Test: `tests/core/decoder_test.cpp` (add year/genre assertions)

**Interfaces:**
- Produces: `MediaMetadata::year` (std::string), `MediaMetadata::genre` (std::string) — populated from FFmpeg `AVDictionary` keys "date" and "genre"

- [ ] **Step 1: Add year/genre fields to MediaMetadata struct**

In `core/src/decoder.hpp`, add two fields after `cover_mime_type`:

```cpp
struct MediaMetadata final {
    std::string title;
    std::string artist;
    std::string album;
    std::string format;
    int sample_rate = 0;
    int channels = 0;
    int bits_per_sample = 0;
    std::int64_t bit_rate = 0;
    std::int64_t duration_ms = 0;
    std::vector<unsigned char> cover;
    std::string cover_mime_type;
    std::string year;   // from AV_DICT "date"
    std::string genre;  // from AV_DICT "genre"
};
```

- [ ] **Step 2: Populate year/genre in decoder.cpp**

Find the metadata extraction section in `decoder.cpp` (where `av_dict_get` is used for title/artist/album). Add year and genre extraction in the same pattern:

```cpp
    if (AVDictionaryEntry* date = av_dict_get(fmt_ctx_->metadata, "date", nullptr, 0)) {
        metadata_.year = date->value;
    }
    if (AVDictionaryEntry* genre = av_dict_get(fmt_ctx_->metadata, "genre", nullptr, 0)) {
        metadata_.genre = genre->value;
    }
```

- [ ] **Step 3: Build and verify existing tests still pass**

Run:
```powershell
Import-Module "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"; Enter-VsDevShell -VsInstallPath "C:\Program Files\Microsoft Visual Studio\2022\Community" -SkipAutomaticLocation -DevCmdArguments "-arch=x64" | Out-Null
cmake --preset windows-msvc-debug --build
ctest --preset windows-msvc-debug
```
Expected: 23/23 PASS (existing tests unaffected by new fields with default values)

- [ ] **Step 4: Commit**

```bash
git add core/src/decoder.hpp core/src/decoder.cpp
git commit -m "feat(core): add year/genre fields to MediaMetadata"
```

---

## Task 2: Core — Implement metadata_writer (FFmpeg stream copy)

**Files:**
- Create: `core/src/metadata_writer.hpp`
- Create: `core/src/metadata_writer.cpp`
- Modify: `core/CMakeLists.txt` (add metadata_writer.cpp to sources)

**Interfaces:**
- Produces: `agplayer::write_metadata(const std::string& path, const MetadataUpdate& update, std::string& error) -> ag_result`

- [ ] **Step 1: Create metadata_writer.hpp**

```cpp
#pragma once

#include "agplayer/c_api.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace agplayer {

// Fields set to empty string are preserved from the source file.
// cover_data is applied only if cover_size > 0.
struct MetadataUpdate {
    std::string title;
    std::string artist;
    std::string album;
    std::string year;
    std::string genre;
    const unsigned char* cover_data = nullptr;
    std::size_t cover_size = 0;
    std::string cover_mime_type;
};

// Write metadata to an audio file using FFmpeg stream copy (no re-encoding).
// Writes to a temp file then atomically replaces the original.
// Returns AG_OK on success, or an error code. On failure, error is set.
ag_result write_metadata(const std::string& utf8_path,
                          const MetadataUpdate& update,
                          std::string& error);

} // namespace agplayer
```

- [ ] **Step 2: Create metadata_writer.cpp**

```cpp
#include "metadata_writer.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/opt.h>
}

#include <cstdio>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace agplayer {

namespace {

bool atomic_replace(const std::string& temp_path, const std::string& target_path)
{
#ifdef _WIN32
    return MoveFileExA(temp_path.c_str(), target_path.c_str(),
                       MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return std::rename(temp_path.c_str(), target_path.c_str()) == 0;
#endif
}

} // namespace

ag_result write_metadata(const std::string& utf8_path,
                          const MetadataUpdate& update,
                          std::string& error)
{
    AVFormatContext* in_ctx = nullptr;
    if (avformat_open_input(&in_ctx, utf8_path.c_str(), nullptr, nullptr) < 0) {
        error = "Failed to open input file";
        return AG_IO_ERROR;
    }
    if (avformat_find_stream_info(in_ctx, nullptr) < 0) {
        avformat_close_input(&in_ctx);
        error = "Failed to find stream info";
        return AG_DECODE_ERROR;
    }

    AVFormatContext* out_ctx = nullptr;
    if (avformat_alloc_output_context2(&out_ctx, nullptr, nullptr,
                                        utf8_path.c_str()) < 0) {
        avformat_close_input(&in_ctx);
        error = "Failed to allocate output context";
        return AG_INTERNAL_ERROR;
    }

    // Copy all streams without re-encoding
    for (unsigned int i = 0; i < in_ctx->nb_streams; ++i) {
        AVStream* in_stream = in_ctx->streams[i];
        AVStream* out_stream = avformat_new_stream(out_ctx, nullptr);
        if (out_stream == nullptr) {
            avformat_close_input(&in_ctx);
            avformat_free_context(out_ctx);
            error = "Failed to create output stream";
            return AG_INTERNAL_ERROR;
        }
        if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0) {
            avformat_close_input(&in_ctx);
            avformat_free_context(out_ctx);
            error = "Failed to copy codec parameters";
            return AG_INTERNAL_ERROR;
        }
        out_stream->time_base = in_stream->time_base;
    }

    // Copy format-level metadata, then override with user-specified fields
    av_dict_copy(&out_ctx->metadata, in_ctx->metadata, 0);
    if (!update.title.empty()) av_dict_set(&out_ctx->metadata, "title", update.title.c_str(), 0);
    if (!update.artist.empty()) av_dict_set(&out_ctx->metadata, "artist", update.artist.c_str(), 0);
    if (!update.album.empty()) av_dict_set(&out_ctx->metadata, "album", update.album.c_str(), 0);
    if (!update.year.empty()) av_dict_set(&out_ctx->metadata, "date", update.year.c_str(), 0);
    if (!update.genre.empty()) av_dict_set(&out_ctx->metadata, "genre", update.genre.c_str(), 0);

    // Write to temp file
    const std::filesystem::path p(utf8_path);
    const std::string temp_path = (p.parent_path() / (p.filename().string() + ".agtmp")).string();

    if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_ctx->pb, temp_path.c_str(), AVIO_FLAG_WRITE) < 0) {
            avformat_close_input(&in_ctx);
            avformat_free_context(out_ctx);
            error = "Failed to open temp output file";
            return AG_IO_ERROR;
        }
    }

    if (avformat_write_header(out_ctx, nullptr) < 0) {
        if (out_ctx->pb) avio_closep(&out_ctx->pb);
        avformat_close_input(&in_ctx);
        avformat_free_context(out_ctx);
        error = "Failed to write header";
        return AG_INTERNAL_ERROR;
    }

    // Stream copy: read packets and write them directly
    AVPacket* pkt = av_packet_alloc();
    if (pkt == nullptr) {
        if (out_ctx->pb) avio_closep(&out_ctx->pb);
        av_write_trailer(out_ctx);
        avformat_close_input(&in_ctx);
        avformat_free_context(out_ctx);
        error = "Failed to allocate packet";
        return AG_INTERNAL_ERROR;
    }

    while (av_read_frame(in_ctx, pkt) >= 0) {
        AVStream* in_stream = in_ctx->streams[pkt->stream_index];
        if (pkt->stream_index < static_cast<int>(out_ctx->nb_streams)) {
            AVStream* out_stream = out_ctx->streams[pkt->stream_index];
            av_packet_rescale_ts(pkt, in_stream->time_base, out_stream->time_base);
            pkt->pos = -1;
            if (av_interleaved_write_frame(out_ctx, pkt) < 0) {
                // Non-fatal: skip this packet
            }
        }
        av_packet_unref(pkt);
    }

    av_write_trailer(out_ctx);
    av_packet_free(&pkt);

    if (out_ctx->pb) avio_closep(&out_ctx->pb);
    avformat_close_input(&in_ctx);
    avformat_free_context(out_ctx);

    // Atomic replace
    if (!atomic_replace(temp_path, utf8_path)) {
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        error = "Failed to replace original file";
        return AG_IO_ERROR;
    }

    return AG_OK;
}

} // namespace agplayer
```

- [ ] **Step 3: Add metadata_writer.cpp to core/CMakeLists.txt**

Add `src/metadata_writer.cpp` to the `agplayer_core` source list in `core/CMakeLists.txt`.

- [ ] **Step 4: Build and verify**

```powershell
cmake --preset windows-msvc-debug --build
```
Expected: Compiles with 0 warnings. Existing tests still pass.

- [ ] **Step 5: Commit**

```bash
git add core/src/metadata_writer.hpp core/src/metadata_writer.cpp core/CMakeLists.txt
git commit -m "feat(core): add metadata_writer for FFmpeg stream-copy metadata write"
```

---

## Task 3: Core — Extend C API with year/genre getters + ag_metadata_write

**Files:**
- Modify: `core/include/agplayer/c_api.h` (add 3 function declarations)
- Modify: `core/src/c_api.cpp` (implement 3 functions)

**Interfaces:**
- Produces: `ag_metadata_year()`, `ag_metadata_genre()`, `ag_metadata_write()` C API functions

- [ ] **Step 1: Add declarations to c_api.h**

After `ag_metadata_cover` (line ~102), add:

```c
const char* ag_metadata_year(const ag_metadata* metadata);
const char* ag_metadata_genre(const ag_metadata* metadata);

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

- [ ] **Step 2: Implement in c_api.cpp**

Add the implementation for `ag_metadata_year` and `ag_metadata_genre` following the existing `ag_metadata_title` pattern (return `metadata->year.c_str()` etc.). For `ag_metadata_write`, wrap `agplayer::write_metadata`:

```cpp
const char* ag_metadata_year(const ag_metadata* metadata)
{
    if (metadata == nullptr) return "";
    return metadata->year.c_str();
}

const char* ag_metadata_genre(const ag_metadata* metadata)
{
    if (metadata == nullptr) return "";
    return metadata->genre.c_str();
}

ag_result ag_metadata_write(const char* utf8_path,
                             const char* title,
                             const char* artist,
                             const char* album,
                             const char* year,
                             const char* genre,
                             const unsigned char* cover_data,
                             size_t cover_size,
                             const char* cover_mime_type)
{
    if (utf8_path == nullptr) return AG_INVALID_ARGUMENT;
    agplayer::MetadataUpdate update;
    if (title) update.title = title;
    if (artist) update.artist = artist;
    if (album) update.album = album;
    if (year) update.year = year;
    if (genre) update.genre = genre;
    update.cover_data = cover_data;
    update.cover_size = cover_size;
    if (cover_mime_type) update.cover_mime_type = cover_mime_type;
    std::string error;
    return agplayer::write_metadata(utf8_path, update, error);
}
```

- [ ] **Step 3: Build and run existing tests**

```powershell
cmake --preset windows-msvc-debug --build
ctest --preset windows-msvc-debug
```
Expected: 23/23 PASS (new functions are additive, don't break existing tests)

- [ ] **Step 4: Commit**

```bash
git add core/include/agplayer/c_api.h core/src/c_api.cpp
git commit -m "feat(core): add ag_metadata_year/genre/write to C API"
```

---

## Task 4: Qt Bridge — AudioToolsController + MetadataEditor

**Files:**
- Create: `qt/src/audio_tools_controller.hpp`
- Create: `qt/src/audio_tools_controller.cpp`
- Create: `qt/src/metadata_editor.hpp`
- Create: `qt/src/metadata_editor.cpp`

**Interfaces:**
- Consumes: `ag_metadata_open`, `ag_metadata_year`, `ag_metadata_genre`, `ag_metadata_write` from C API
- Produces: `AudioToolsController` (QML singleton), `MetadataEditor` (QML type)

- [ ] **Step 1: Create audio_tools_controller.hpp**

```cpp
#pragma once

#include <QObject>

class AudioToolsController final : public QObject {
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
    int currentTool_ = 4;  // Default: Info Edit (index 4)
    bool visible_ = false;
};
```

- [ ] **Step 2: Create audio_tools_controller.cpp**

```cpp
#include "audio_tools_controller.hpp"

AudioToolsController::AudioToolsController(QObject* parent)
    : QObject(parent)
{
}

int AudioToolsController::currentTool() const noexcept { return currentTool_; }
bool AudioToolsController::visible() const noexcept { return visible_; }

void AudioToolsController::setCurrentTool(int tool)
{
    if (tool < 0 || tool > 4 || tool == currentTool_) return;
    currentTool_ = tool;
    emit currentToolChanged();
}

void AudioToolsController::show()
{
    if (visible_) return;
    visible_ = true;
    emit visibleChanged();
    emit showRequested();
}

void AudioToolsController::hide()
{
    if (!visible_) return;
    visible_ = false;
    emit visibleChanged();
    emit hideRequested();
}

void AudioToolsController::selectTool(int tool)
{
    setCurrentTool(tool);
    if (!visible_) show();
}
```

- [ ] **Step 3: Create metadata_editor.hpp**

```cpp
#pragma once

#include "agplayer/c_api.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <atomic>

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
    bool hasError = false;
    QString error;
};

class MetadataEditor final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY fileCountChanged)

public:
    explicit MetadataEditor(QObject* parent = nullptr);

    double progress() const noexcept;
    bool busy() const noexcept;
    int fileCount() const noexcept;

    Q_INVOKABLE void loadFiles(const QList<QUrl>& urls);
    Q_INVOKABLE QVariantMap entryAt(int index) const;
    Q_INVOKABLE void applyMetadata(const QVariantMap& fields, const QList<int>& indices);
    Q_INVOKABLE QStringList previewRename(const QString& prefix,
                                           const QString& suffix,
                                           bool autoNumber,
                                           int numberStart,
                                           int numberDigits) const;
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

    void setBusy(bool value);
    void setProgress(double value);
    QString computeNewName(const QString& original, const QString& prefix,
                           const QString& suffix, bool autoNumber,
                           int number, int numberDigits) const;
};
```

- [ ] **Step 4: Create metadata_editor.cpp**

```cpp
#include "metadata_editor.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>

MetadataEditor::MetadataEditor(QObject* parent)
    : QObject(parent)
{
}

double MetadataEditor::progress() const noexcept { return progress_.load(std::memory_order_acquire); }
bool MetadataEditor::busy() const noexcept { return busy_.load(std::memory_order_acquire); }
int MetadataEditor::fileCount() const noexcept { return static_cast<int>(entries_.size()); }

void MetadataEditor::setBusy(bool value)
{
    busy_.store(value, std::memory_order_release);
    emit busyChanged();
}

void MetadataEditor::setProgress(double value)
{
    progress_.store(value, std::memory_order_release);
    emit progressChanged();
}

void MetadataEditor::loadFiles(const QList<QUrl>& urls)
{
    if (busy_.load(std::memory_order_acquire)) return;
    cancelFlag_.store(false, std::memory_order_release);
    setBusy(true);
    setProgress(0.0);

    auto watcher = new QFutureWatcher<QList<MetadataEntry>>(this);
    connect(watcher, &QFutureWatcher<QList<MetadataEntry>>::finished, this, [this, watcher]() {
        entries_ = watcher->result();
        setBusy(false);
        setProgress(1.0);
        emit fileCountChanged();
        emit entriesLoaded();
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run([urls, this]() {
        QList<MetadataEntry> result;
        const int total = urls.size();
        for (int i = 0; i < total; ++i) {
            if (cancelFlag_.load(std::memory_order_acquire)) break;
            const QString path = urls[i].toLocalFile();
            MetadataEntry entry;
            entry.path = path;
            entry.fileName = QFileInfo(path).fileName();
            ag_metadata* md = nullptr;
            if (ag_metadata_open(path.toUtf8().constData(), &md) == AG_OK && md != nullptr) {
                entry.title = QString::fromUtf8(ag_metadata_title(md));
                entry.artist = QString::fromUtf8(ag_metadata_artist(md));
                entry.album = QString::fromUtf8(ag_metadata_album(md));
                entry.year = QString::fromUtf8(ag_metadata_year(md));
                entry.genre = QString::fromUtf8(ag_metadata_genre(md));
                entry.format = QString::fromUtf8(ag_metadata_format(md));
                entry.durationMs = ag_metadata_duration_ms(md);
                size_t cover_size = 0;
                const char* cover_mime = nullptr;
                ag_metadata_cover(md, &cover_size, &cover_mime);
                ag_metadata_destroy(md);
            } else {
                entry.hasError = true;
                entry.error = QStringLiteral("Failed to read metadata");
            }
            entry.fileSize = QFileInfo(path).size();
            result.append(entry);
            progress_.store(static_cast<double>(i + 1) / total, std::memory_order_release);
        }
        return result;
    }));
}

QVariantMap MetadataEditor::entryAt(int index) const
{
    QVariantMap map;
    if (index < 0 || index >= entries_.size()) return map;
    const MetadataEntry& e = entries_[index];
    map["path"] = e.path;
    map["fileName"] = e.fileName;
    map["title"] = e.title;
    map["artist"] = e.artist;
    map["album"] = e.album;
    map["year"] = e.year;
    map["genre"] = e.genre;
    map["format"] = e.format;
    map["durationMs"] = e.durationMs;
    map["fileSize"] = e.fileSize;
    map["hasError"] = e.hasError;
    map["error"] = e.error;
    return map;
}

void MetadataEditor::applyMetadata(const QVariantMap& fields, const QList<int>& indices)
{
    if (busy_.load(std::memory_order_acquire)) return;
    cancelFlag_.store(false, std::memory_order_release);
    setBusy(true);
    setProgress(0.0);

    QList<int> targets = indices;
    if (targets.isEmpty()) {
        for (int i = 0; i < entries_.size(); ++i) targets.append(i);
    }

    auto watcher = new QFutureWatcher<QPair<int, int>>(this);
    connect(watcher, &QFutureWatcher<QPair<int, int>>::finished, this, [this, watcher]() {
        const auto result = watcher->result();
        setBusy(false);
        setProgress(1.0);
        emit metadataApplied(result.first, result.second);
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run([fields, targets, this]() {
        int success = 0, failure = 0;
        const int total = targets.size();
        for (int i = 0; i < total; ++i) {
            if (cancelFlag_.load(std::memory_order_acquire)) break;
            const int idx = targets[i];
            if (idx < 0 || idx >= entries_.size()) { ++failure; continue; }
            const MetadataEntry& e = entries_[idx];
            const QByteArray pathUtf8 = e.path.toUtf8();
            const std::string title = fields.value("title").toString().toStdString();
            const std::string artist = fields.value("artist").toString().toStdString();
            const std::string album = fields.value("album").toString().toStdString();
            const std::string year = fields.value("year").toString().toStdString();
            const std::string genre = fields.value("genre").toString().toStdString();
            const ag_result result = ag_metadata_write(
                pathUtf8.constData(),
                title.empty() ? nullptr : title.c_str(),
                artist.empty() ? nullptr : artist.c_str(),
                album.empty() ? nullptr : album.c_str(),
                year.empty() ? nullptr : year.c_str(),
                genre.empty() ? nullptr : genre.c_str(),
                nullptr, 0, nullptr);
            if (result == AG_OK) ++success; else ++failure;
            progress_.store(static_cast<double>(i + 1) / total, std::memory_order_release);
        }
        return QPair<int, int>{success, failure};
    }));
}

QString MetadataEditor::computeNewName(const QString& original, const QString& prefix,
                                        const QString& suffix, bool autoNumber,
                                        int number, int numberDigits) const
{
    const QFileInfo info(original);
    const QString stem = info.completeBaseName();
    const QString ext = info.suffix();
    QString name = prefix;
    if (autoNumber) {
        name += QString("%1").arg(number, numberDigits, 10, QChar('0'));
    } else {
        name += stem;
    }
    name += suffix;
    if (!ext.isEmpty()) name += "." + ext;
    return name;
}

QStringList MetadataEditor::previewRename(const QString& prefix, const QString& suffix,
                                           bool autoNumber, int numberStart, int numberDigits) const
{
    QStringList result;
    for (int i = 0; i < entries_.size(); ++i) {
        const QString newName = computeNewName(entries_[i].fileName, prefix, suffix,
                                                autoNumber, numberStart + i, numberDigits);
        result.append(entries_[i].fileName + " -> " + newName);
    }
    return result;
}

void MetadataEditor::applyRename(const QString& prefix, const QString& suffix,
                                  bool autoNumber, int numberStart, int numberDigits)
{
    if (busy_.load(std::memory_order_acquire)) return;
    cancelFlag_.store(false, std::memory_order_release);
    setBusy(true);
    setProgress(0.0);

    auto watcher = new QFutureWatcher<QPair<int, int>>(this);
    connect(watcher, &QFutureWatcher<QPair<int, int>>::finished, this, [this, watcher]() {
        const auto result = watcher->result();
        setBusy(false);
        setProgress(1.0);
        emit renameApplied(result.first, result.second);
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run([prefix, suffix, autoNumber, numberStart, numberDigits, this]() {
        int success = 0, failure = 0;
        const int total = entries_.size();
        for (int i = 0; i < total; ++i) {
            if (cancelFlag_.load(std::memory_order_acquire)) break;
            MetadataEntry& e = entries_[i];
            const QString newName = computeNewName(e.fileName, prefix, suffix,
                                                    autoNumber, numberStart + i, numberDigits);
            const QString newPath = QFileInfo(e.path).dir().filePath(newName);
            // Collision avoidance
            QString finalPath = newPath;
            int attempt = 1;
            while (QFileInfo::exists(finalPath) && finalPath != e.path) {
                const QFileInfo info(newName);
                finalPath = QFileInfo(e.path).dir().filePath(
                    info.completeBaseName() + "_" + QString::number(++attempt + 1) +
                    (info.suffix().isEmpty() ? "" : "." + info.suffix()));
                if (attempt > 99) { finalPath.clear(); break; }
            }
            if (finalPath.isEmpty()) { ++failure; continue; }
            if (QFile::rename(e.path, finalPath)) {
                e.path = finalPath;
                e.fileName = newName;
                ++success;
            } else {
                ++failure;
            }
            progress_.store(static_cast<double>(i + 1) / total, std::memory_order_release);
        }
        return QPair<int, int>{success, failure};
    }));
}

void MetadataEditor::cancel() { cancelFlag_.store(true, std::memory_order_release); }

void MetadataEditor::clear()
{
    entries_.clear();
    emit fileCountChanged();
}
```

- [ ] **Step 5: Add to qt/CMakeLists.txt**

Add `src/audio_tools_controller.cpp` and `src/metadata_editor.cpp` to the Qt bridge source list. Add the `.hpp` files to headers if the project tracks them.

- [ ] **Step 6: Build and verify**

```powershell
cmake --preset windows-msvc-debug --build
```
Expected: Compiles with 0 warnings.

- [ ] **Step 7: Commit**

```bash
git add qt/src/audio_tools_controller.hpp qt/src/audio_tools_controller.cpp qt/src/metadata_editor.hpp qt/src/metadata_editor.cpp qt/CMakeLists.txt
git commit -m "feat(qt): add AudioToolsController and MetadataEditor"
```

---

## Task 5: Qt Bridge — QML registration + WindowController extension

**Files:**
- Modify: `qt/src/qml_registration.cpp` (register new types)
- Modify: `qt/src/window_controller.hpp` (add showAudioTools/hideAudioTools)
- Modify: `qt/src/window_controller.cpp` (implement)

**Interfaces:**
- Consumes: `AudioToolsController`, `MetadataEditor` from Task 4
- Produces: QML singletons `AudioToolsController` and `MetadataEditor`; `WindowController.showAudioTools()` / `hideAudioTools()`

- [ ] **Step 1: Register QML types in qml_registration.cpp**

Add includes and registration calls following the existing pattern:

```cpp
#include "audio_tools_controller.hpp"
#include "metadata_editor.hpp"

// In the registration function:
qmlRegisterSingletonInstance<MetadataEditor>("AgPlayer", 1, 0, "MetadataEditor", [](QQmlEngine*, QJSEngine*) -> QObject* {
    return new MetadataEditor();
});
// AudioToolsController is registered as a singleton created in main.cpp and set as context property,
// OR registered here if it follows the PlaybackController pattern.
```

Check the existing `qml_registration.cpp` to see how `PlaybackController` is registered and follow the same pattern.

- [ ] **Step 2: Extend WindowController**

In `window_controller.hpp`, add:
```cpp
    Q_INVOKABLE void showAudioTools();
    Q_INVOKABLE void hideAudioTools();
```

In `window_controller.cpp`, implement by toggling the `AudioToolsWindow` visibility (following the existing `showMini()` pattern).

- [ ] **Step 3: Build and verify**

```powershell
cmake --preset windows-msvc-debug --build
```
Expected: Compiles with 0 warnings.

- [ ] **Step 4: Commit**

```bash
git add qt/src/qml_registration.cpp qt/src/window_controller.hpp qt/src/window_controller.cpp
git commit -m "feat(qt): register AudioToolsController/MetadataEditor, extend WindowController"
```

---

## Task 6: QML — ComingSoonPage + ToolSidebar + AudioToolsWindow

**Files:**
- Create: `app/qml/AgPlayer/components/tools/ComingSoonPage.qml`
- Create: `app/qml/AgPlayer/components/tools/ToolSidebar.qml`
- Create: `app/qml/AgPlayer/AudioToolsWindow.qml`

**Interfaces:**
- Consumes: `AudioToolsController` singleton, `Theme` singleton, `WindowController`
- Produces: `AudioToolsWindow` QML component, `ToolSidebar` with `currentTool` binding

- [ ] **Step 1: Create ComingSoonPage.qml**

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    color: Theme.background

    property string toolName: ""

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Theme.spacingLg

        Text {
            text: parent.parent.toolName
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 24
            font.weight: Font.Medium
            Layout.alignment: Qt.AlignHCenter
        }

        Text {
            text: "即将推出"
            color: Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 14
            Layout.alignment: Qt.AlignHCenter
        }
    }
}
```

- [ ] **Step 2: Create ToolSidebar.qml**

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    color: Theme.panel
    implicitWidth: 240

    property int currentTool: 4

    signal toolSelected(int index)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingSm
        spacing: Theme.spacingSm

        // Logo at top
        Image {
            source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
            sourceSize.width: 32
            sourceSize.height: 32
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingMd
            Layout.bottomMargin: Theme.spacingLg
        }

        Repeater {
            model: [
                { name: "格式转换", icon: "music-2-fill" },
                { name: "轻度剪辑", icon: "music-2-fill" },
                { name: "调整速度", icon: "music-2-fill" },
                { name: "升调降调", icon: "music-2-fill" },
                { name: "信息修改", icon: "music-2-fill" }
            ]

            delegate: Button {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                flat: true
                checked: currentTool === index

                contentItem: RowLayout {
                    spacing: Theme.spacingSm
                    Image {
                        source: Theme.icon(modelData.icon)
                        sourceSize.width: 20
                        sourceSize.height: 20
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                    }
                    Text {
                        text: modelData.name
                        color: checked ? Theme.cyan : Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 14
                        font.weight: checked ? Font.Medium : Font.Normal
                    }
                    Item { Layout.fillWidth: true }
                }

                background: Rectangle {
                    color: checked ? Theme.border : (parent.hovered ? Theme.border : "transparent")
                    radius: Theme.radiusSm
                }

                onClicked: toolSelected(index)
            }
        }

        Item { Layout.fillHeight: true }

        // Window controls at bottom
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs

            ToolButton {
                icon.source: Theme.icon("subtract-line")
                icon.color: Theme.secondaryText
                icon.width: 16
                icon.height: 16
                onClicked: window.visibility = Window.Minimized
            }

            ToolButton {
                icon.source: Theme.icon("close-fill")
                icon.color: Theme.secondaryText
                icon.width: 16
                icon.height: 16
                onClicked: WindowController.hideAudioTools()
            }
        }
    }
}
```

- [ ] **Step 3: Create AudioToolsWindow.qml**

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Window {
    id: window
    objectName: "audioToolsWindow"
    visible: false
    width: 1200
    height: 780
    minimumWidth: 960
    minimumHeight: 600
    flags: Qt.FramelessWindowHint
    color: Theme.background
    title: "AgPlayer Audio Tools"

    property var audioTools: AudioToolsController

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ToolSidebar {
            Layout.fillHeight: true
            currentTool: audioTools.currentTool
            onToolSelected: audioTools.selectTool(index)
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.background

            StackLayout {
                id: contentStack
                anchors.fill: parent
                currentIndex: audioTools.currentTool

                ComingSoonPage { toolName: "格式转换" }
                ComingSoonPage { toolName: "轻度剪辑" }
                ComingSoonPage { toolName: "调整速度" }
                ComingSoonPage { toolName: "升调降调" }
                InfoEditPage {}
            }
        }
    }

    // Drag window from sidebar
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        z: -1
        onPressed: window.startSystemMove()
    }

    Connections {
        target: AudioToolsController
        function onVisibleChanged() {
            if (AudioToolsController.visible) {
                window.show()
                window.raise()
            } else {
                window.hide()
            }
        }
    }
}
```

- [ ] **Step 4: Add QML files to app/CMakeLists.txt**

Add the new QML files to the Qt resource list in `app/CMakeLists.txt` following the existing pattern for QML files.

- [ ] **Step 5: Build and verify**

```powershell
cmake --preset windows-msvc-debug --build
```
Expected: Compiles with 0 warnings.

- [ ] **Step 6: Commit**

```bash
git add app/qml/AgPlayer/AudioToolsWindow.qml app/qml/AgPlayer/components/tools/ app/CMakeLists.txt
git commit -m "feat(qml): add AudioToolsWindow, ToolSidebar, ComingSoonPage"
```

---

## Task 7: QML — InfoEditPage + TitleBar integration + main.cpp wiring

**Files:**
- Create: `app/qml/AgPlayer/components/tools/InfoEditPage.qml`
- Modify: `app/qml/AgPlayer/components/TitleBar.qml` (add Audio Tools button)
- Modify: `app/main.cpp` (wire AudioToolsController + create AudioToolsWindow)

**Interfaces:**
- Consumes: `MetadataEditor` singleton, `Theme`, `FileDialog`
- Produces: Complete InfoEditPage with metadata batch edit + filename rename

- [ ] **Step 1: Create InfoEditPage.qml**

This is the main UI page. It has two sections:
- Top: File list (left) + metadata form (right)
- Bottom: Rename controls (left) + preview list (right)

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Rectangle {
    color: Theme.background

    property var editor: MetadataEditor

    Component {
        id: fileDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFiles
            nameFilters: ["Audio files (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)"]
            onAccepted: editor.loadFiles(files)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        // === Top Section: Metadata Batch Edit ===
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 3
            color: "transparent"

            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                Text {
                    text: "元数据批量修改"
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 13
                    font.capitalization: Font.AllUppercase
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.spacingMd

                    // File list (left, 40%)
                    Rectangle {
                        Layout.fillHeight: true
                        Layout.preferredWidth: parent.width * 0.4
                        color: Theme.panel
                        radius: Theme.radiusSm
                        border.color: Theme.border
                        border.width: 1

                        DropArea {
                            anchors.fill: parent
                            keys: ["text/uri-list"]
                            onDropped: if (drop.hasUrls) editor.loadFiles(drop.urls)
                        }

                        ListView {
                            id: fileList
                            anchors.fill: parent
                            anchors.margins: 1
                            clip: true
                            model: editor.fileCount
                            delegate: Rectangle {
                                width: fileList.width
                                height: 40
                                color: index % 2 === 0 ? "transparent" : Qt.rgba(1,1,1,0.02)
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.spacingSm
                                    spacing: Theme.spacingSm
                                    CheckBox { checked: true }
                                    Text {
                                        text: editor.entryAt(index).fileName
                                        color: Theme.primaryText
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: editor.entryAt(index).format
                                        color: Theme.secondaryText
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }

                        // Empty state
                        Text {
                            anchors.centerIn: parent
                            visible: editor.fileCount === 0
                            text: "拖拽音频文件到此处，或点击添加"
                            color: Theme.secondaryText
                            font.pixelSize: 13
                        }

                        Button {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottomMargin: Theme.spacingSm
                            text: "添加文件"
                            visible: editor.fileCount === 0
                            onClicked: fileDialogComponent.createObject(parent).open()
                        }
                    }

                    // Metadata form (right, 60%)
                    Rectangle {
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        color: Theme.panel
                        radius: Theme.radiusSm
                        border.color: Theme.border
                        border.width: 1

                        GridLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.spacingMd
                            columns: 2
                            rowSpacing: Theme.spacingSm
                            columnSpacing: Theme.spacingMd

                            Text { text: "标题"; color: Theme.secondaryText; font.pixelSize: 12 }
                            TextField { id: titleField; Layout.fillWidth: true; color: Theme.primaryText; background: Rectangle { color: Theme.background; radius: Theme.radiusSm; border.color: Theme.border; border.width: 1 } }

                            Text { text: "艺术家"; color: Theme.secondaryText; font.pixelSize: 12 }
                            TextField { id: artistField; Layout.fillWidth: true; color: Theme.primaryText; background: Rectangle { color: Theme.background; radius: Theme.radiusSm; border.color: Theme.border; border.width: 1 } }

                            Text { text: "专辑"; color: Theme.secondaryText; font.pixelSize: 12 }
                            TextField { id: albumField; Layout.fillWidth: true; color: Theme.primaryText; background: Rectangle { color: Theme.background; radius: Theme.radiusSm; border.color: Theme.border; border.width: 1 } }

                            Text { text: "年份"; color: Theme.secondaryText; font.pixelSize: 12 }
                            TextField { id: yearField; Layout.fillWidth: true; color: Theme.primaryText; background: Rectangle { color: Theme.background; radius: Theme.radiusSm; border.color: Theme.border; border.width: 1 } }

                            Text { text: "流派"; color: Theme.secondaryText; font.pixelSize: 12 }
                            TextField { id: genreField; Layout.fillWidth: true; color: Theme.primaryText; background: Rectangle { color: Theme.background; radius: Theme.radiusSm; border.color: Theme.border; border.width: 1 } }

                            Item { Layout.fillHeight: true; Layout.columnSpan: 2 }

                            RowLayout {
                                Layout.columnSpan: 2
                                Layout.fillWidth: true
                                spacing: Theme.spacingMd

                                ButtonGroup { id: rangeGroup }
                                RadioButton { text: "全部文件"; checked: true; ButtonGroup.group: rangeGroup }
                                RadioButton { text: "选中文件"; ButtonGroup.group: rangeGroup }

                                Item { Layout.fillWidth: true }

                                Button {
                                    text: "批量应用"
                                    enabled: !editor.busy && editor.fileCount > 0
                                    onClicked: {
                                        var fields = { "title": titleField.text, "artist": artistField.text, "album": albumField.text, "year": yearField.text, "genre": genreField.text }
                                        var indices = []
                                        if (rangeGroup.checkedButton.text === "选中文件") {
                                            for (var i = 0; i < fileList.count; i++) {
                                                if (fileList.itemAtIndex(i) && fileList.itemAtIndex(i).children[0].children[0].checked)
                                                    indices.push(i)
                                            }
                                        }
                                        editor.applyMetadata(fields, indices)
                                    }
                                }
                            }
                        }
                    }
                }

                // Progress bar
                ProgressBar {
                    Layout.fillWidth: true
                    visible: editor.busy
                    value: editor.progress
                }
            }
        }

        // === Bottom Section: Filename Batch Rename ===
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 280
            color: "transparent"

            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.spacingSm

                Text {
                    text: "批量文件名重命名"
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 13
                    font.capitalization: Font.AllUppercase
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.spacingMd

                    // Rename controls + preview (right)
                    Rectangle {
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        color: Theme.panel
                        radius: Theme.radiusSm
                        border.color: Theme.border
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.spacingMd
                            spacing: Theme.spacingSm

                            RowLayout {
                                spacing: Theme.spacingSm
                                Text { text: "前缀"; color: Theme.secondaryText; font.pixelSize: 12 }
                                TextField { id: prefixField; Layout.fillWidth: true; color: Theme.primaryText; background: Rectangle { color: Theme.background; radius: Theme.radiusSm; border.color: Theme.border; border.width: 1 } }
                            }

                            RowLayout {
                                spacing: Theme.spacingSm
                                Text { text: "后缀"; color: Theme.secondaryText; font.pixelSize: 12 }
                                TextField { id: suffixField; Layout.fillWidth: true; color: Theme.primaryText; background: Rectangle { color: Theme.background; radius: Theme.radiusSm; border.color: Theme.border; border.width: 1 } }
                            }

                            RowLayout {
                                spacing: Theme.spacingSm
                                CheckBox { id: autoNumberCheck; text: "自动序号"; checked: false }
                                Text { text: "起始"; color: Theme.secondaryText; font.pixelSize: 12 }
                                SpinBox { id: numberStartSpin; from: 0; to: 99999; value: 1 }
                                Text { text: "位数"; color: Theme.secondaryText; font.pixelSize: 12 }
                                SpinBox { id: numberDigitsSpin; from: 1; to: 5; value: 2 }
                            }

                            // Preview list
                            ScrollView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                ListView {
                                    id: previewList
                                    model: editor.previewRename(prefixField.text, suffixField.text, autoNumberCheck.checked, numberStartSpin.value, numberDigitsSpin.value)
                                    delegate: Text {
                                        text: modelData
                                        color: Theme.primaryText
                                        font.pixelSize: 11
                                        elide: Text.ElideMiddle
                                        width: previewList.width
                                    }
                                }
                            }

                            Button {
                                text: "批量重命名"
                                enabled: !editor.busy && editor.fileCount > 0
                                onClicked: editor.applyRename(prefixField.text, suffixField.text, autoNumberCheck.checked, numberStartSpin.value, numberDigitsSpin.value)
                            }
                        }
                    }
                }
            }
        }
    }

    // Pre-fill form when entries loaded
    Connections {
        target: editor
        function onEntriesLoaded() {
            if (editor.fileCount > 0) {
                var first = editor.entryAt(0)
                titleField.text = first.title
                artistField.text = first.artist
                albumField.text = first.album
                yearField.text = first.year
                genreField.text = first.genre
            }
        }
    }
}
```

- [ ] **Step 2: Add Audio Tools button to TitleBar.qml**

In `TitleBar.qml`, add a new `ToolButton` before the mini-player button:

```qml
        ToolButton {
            objectName: "audioToolsButton"
            icon.source: Theme.icon("playlist-2-fill")
            icon.color: Theme.secondaryText
            icon.width: 18
            icon.height: 18
            Accessible.name: "Open audio tools"
            focusPolicy: Qt.StrongFocus
            onClicked: WindowController.showAudioTools()
            ToolTip.text: "Audio Tools"
            ToolTip.visible: hovered

            background: Rectangle {
                color: !parent.enabled ? "transparent"
                      : parent.pressed ? Theme.cyan
                      : parent.visualFocus ? Theme.border
                      : parent.hovered ? Theme.border
                      : "transparent"
                border.color: parent.visualFocus ? Theme.cyan : "transparent"
                border.width: parent.visualFocus ? 2 : 0
                radius: Theme.radiusSm
            }
        }
```

- [ ] **Step 3: Wire in main.cpp**

In `main.cpp`, instantiate `AudioToolsWindow` and wire it to `WindowController`, following the existing pattern for `MiniPlayerWindow`. Create `AudioToolsController` as a singleton and register it as a context property or QML singleton.

- [ ] **Step 4: Build and verify**

```powershell
cmake --preset windows-msvc-debug --build
```
Expected: Compiles with 0 warnings.

- [ ] **Step 5: Commit**

```bash
git add app/qml/AgPlayer/components/tools/InfoEditPage.qml app/qml/AgPlayer/components/TitleBar.qml app/main.cpp app/CMakeLists.txt
git commit -m "feat(qml): add InfoEditPage, Audio Tools button, wire main.cpp"
```

---

## Task 8: Tests + Build verification

**Files:**
- Create: `tests/qt/metadata_editor_test.cpp`
- Create: `tests/qml/tst_info_edit_page.qml`
- Create: `tests/qt/qml_info_edit_test_main.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `MetadataEditor` C++ class, `ag_metadata_write` C API, fixture generator

- [ ] **Step 1: Write metadata_editor_test.cpp**

Test MetadataEditor: load files, apply metadata round-trip, preview rename, apply rename, cancel.

```cpp
#undef NDEBUG

#include "agplayer/c_api.h"
#include "metadata_editor.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path fixture = argv[1];

    // Test 1: Load single file
    {
        MetadataEditor editor;
        QList<QUrl> urls;
        urls.append(QUrl::fromLocalFile(QString::fromStdString(fixture.string())));
        editor.loadFiles(urls);
        // Wait for async load
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5000);
        assert(editor.fileCount() == 1);
        QVariantMap entry = editor.entryAt(0);
        assert(!entry.value("fileName").toString().isEmpty());
    }

    // Test 2: Apply metadata round-trip
    {
        // Copy fixture to temp
        const auto temp_path = fixture.parent_path() / "metadata_test.wav";
        std::filesystem::copy_file(fixture, temp_path, std::filesystem::copy_options::overwrite_existing);

        MetadataEditor editor;
        QList<QUrl> urls;
        urls.append(QUrl::fromLocalFile(QString::fromStdString(temp_path.string())));
        editor.loadFiles(urls);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5000);

        QVariantMap fields;
        fields["title"] = "Test Title";
        fields["artist"] = "Test Artist";
        editor.applyMetadata(fields, {});
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10000);

        // Re-read and verify
        ag_metadata* md = nullptr;
        assert(ag_metadata_open(temp_path.string().c_str(), &md) == AG_OK);
        assert(std::string(ag_metadata_title(md)) == "Test Title");
        assert(std::string(ag_metadata_artist(md)) == "Test Artist");
        ag_metadata_destroy(md);

        std::filesystem::remove(temp_path);
    }

    // Test 3: Preview rename
    {
        MetadataEditor editor;
        QList<QUrl> urls;
        urls.append(QUrl::fromLocalFile(QString::fromStdString(fixture.string())));
        editor.loadFiles(urls);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5000);

        QStringList preview = editor.previewRename("prefix_", "_suffix", true, 1, 2);
        assert(preview.size() == 1);
        assert(preview[0].contains("prefix_"));
        assert(preview[0].contains("_suffix"));
    }

    return 0;
}
```

Note: The test needs `QCoreApplication` for event processing. Add appropriate `QTEST_MAIN` or manual `QCoreApplication` setup.

- [ ] **Step 2: Write tst_info_edit_page.qml**

```qml
import QtQuick
import QtTest
import AgPlayer

TestCase {
    name: "InfoEditPage"
    visible: true
    when: windowShown

    function test_pageLoads() {
        var page = Qt.createQmlObject('import QtQuick; import QtQuick.Controls; import "app/qml/AgPlayer/components/tools"; InfoEditPage {}', this)
        verify(page !== null)
        page.destroy()
    }
}
```

- [ ] **Step 3: Add test targets to tests/CMakeLists.txt**

Add `metadata_editor_test` executable and the QML test harness, following the existing patterns.

- [ ] **Step 4: Build and run Debug tests**

```powershell
cmake --preset windows-msvc-debug --build
ctest --preset windows-msvc-debug
```
Expected: All tests pass (existing 23 + new tests), 0 warnings.

- [ ] **Step 5: Build and run Release tests**

```powershell
cmake --preset windows-msvc-release --build
ctest --preset windows-msvc-release
```
Expected: All tests pass, 0 warnings.

- [ ] **Step 6: Commit**

```bash
git add tests/qt/metadata_editor_test.cpp tests/qml/tst_info_edit_page.qml tests/qt/qml_info_edit_test_main.cpp tests/CMakeLists.txt
git commit -m "test: add metadata_editor and InfoEditPage tests"
```

---

## Self-Review Checklist

After implementing all tasks, verify:

1. **Spec coverage:**
   - [x] AudioToolsWindow with sidebar navigation — Task 6
   - [x] 5 tool entries in sidebar — Task 6
   - [x] InfoEditPage with metadata batch edit — Task 7
   - [x] InfoEditPage with filename batch rename — Task 7
   - [x] MetadataEditor backend — Task 4
   - [x] C API extension (year/genre/write) — Tasks 1-3
   - [x] TitleBar integration — Task 7
   - [x] WindowController extension — Task 5
   - [x] Tests — Task 8
   - [x] Build verification — Task 8

2. **Type consistency:**
   - `MetadataEntry` fields match between header and QML `entryAt()` return
   - `ag_metadata_write` signature matches between c_api.h and c_api.cpp
   - `AudioToolsController.currentTool` default is 4 (Info Edit index)

3. **Placeholder scan:**
   - No TBD/TODO in plan
   - All steps have concrete code or commands
   - All file paths are exact

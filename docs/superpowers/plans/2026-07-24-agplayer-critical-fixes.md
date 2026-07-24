# AgPlayer Critical Fixes 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复代码审查中发现的 Critical 级别问题，消除未定义行为、数据竞争、静默失败和用户数据丢失风险。

**Architecture:** 以最小侵入方式修复 Core 层安全缺陷；Qt Bridge 层补齐对象生命周期与并发保护；保持现有三层架构和 C ABI 不变。

**Tech Stack:** C++17, Qt 6.7, FFmpeg, CMake, ctest.

## Global Constraints

- 不改变 `core/include/agplayer/c_api.h` 的公开函数签名（除非明确移除未实现的封面参数）。
- 所有修改必须在 Windows x64 Debug/Release 下通过 `ctest`。
- 错误处理保持静默原则：UI 不弹出未请求的错误窗，失败写入 `RuntimeLog`。
- 注册表/文件系统操作限定当前用户权限。

---

## Task 1: 修复 decoder.cpp 重叠区间 `std::move` 导致的未定义行为

**Files:**
- Modify: `core/src/decoder.cpp:553-561`
- Test: `tests/core/decoder_test.cpp`（现有 seek 测试已覆盖该路径）

**Interfaces:**
- Consumes: `agplayer::DecodedAudioBlock::samples`, `block.frames`, `block_start_frame_`
- Produces: 修正后的 `trim_to_seek_target()` 行为不变，但不再触发 UB

**问题说明：**
当前代码在源区间与目标区间重叠时调用 `std::move`（`block.samples.begin()` 位于源区间内部），违反 `[alg.move]` 要求。

- [ ] **Step 1: 替换为 `erase`**

将 `core/src/decoder.cpp:553-561` 的 `std::move` + `resize` 改为：

```cpp
            block.samples.erase(
                block.samples.begin(),
                block.samples.begin() + static_cast<std::ptrdiff_t>(samples_to_skip));
            block.frames -= static_cast<std::size_t>(frames_to_skip);
            block.timestamp_ms = seek_target_ms_;
            block_start_frame_ = seek_target_frame_;
```

注意：`erase` 后 `samples.size() == block.frames * channels`，因此删除原来的 `resize` 调用。

- [ ] **Step 2: 构建并运行 decoder 测试**

```powershell
Import-Module "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -InstallPath "C:\Program Files\Microsoft Visual Studio\2022\Community" -SkipAutomaticLocation -Arch amd64
cmake --build build --config Debug --target decoder_test
ctest -C Debug -R "^decoder_test$" --output-on-failure
```

Expected: 测试通过。

- [ ] **Step 3: 提交**

```bash
git add core/src/decoder.cpp
git commit -m "fix(core/decoder): avoid overlapping std::move in seek trim"
```

---

## Task 2: 修复 `audio_engine.cpp` 的 `produced_frames_total_` 数据竞争

**Files:**
- Modify: `core/src/audio_engine.cpp:998-1000`, `core/src/audio_engine.cpp:702`, `core/src/audio_engine.cpp:781`, `core/src/audio_engine.cpp:794`, `core/src/audio_engine.cpp:903`
- Test: `tests/core/audio_engine_test.cpp`

**Interfaces:**
- Consumes: `produced_frames_total_`（仅由解码线程写入，音频回调/快照读取）
- Produces: 线程安全的 `std::atomic<std::int64_t> produced_frames_total_`

**问题说明：**
`produced_frames_total_` 是普通 `std::int64_t`，在解码线程写入，在 `publish_pending_transition` 的边界设置处读取，存在数据竞争。

- [ ] **Step 1: 将成员改为原子类型**

在 `core/src/audio_engine.cpp` 的 Impl 成员区：

```cpp
    std::atomic<std::int64_t> produced_frames_total_{0};
```

替换原来的：

```cpp
    std::int64_t produced_frames_total_ = 0;
```

- [ ] **Step 2: 更新所有读写点**

1. `core/src/audio_engine.cpp:702`：

```cpp
produced_frames_total_.fetch_add(static_cast<std::int64_t>(written),
                                 std::memory_order_relaxed);
```

2. `core/src/audio_engine.cpp:781` 和 `:794`：

```cpp
pending_boundary_frame_.store(
    produced_frames_total_.load(std::memory_order_relaxed),
    std::memory_order_release);
```

3. `core/src/audio_engine.cpp:903`：

```cpp
produced_frames_total_.store(position_frames, std::memory_order_release);
```

- [ ] **Step 3: 构建并运行 audio_engine 测试**

```powershell
cmake --build build --config Debug --target audio_engine_test
ctest -C Debug -R "^audio_engine_test$" --output-on-failure
```

Expected: 测试通过。

- [ ] **Step 4: 提交**

```bash
git add core/src/audio_engine.cpp
git commit -m "fix(core/audio_engine): make produced_frames_total_ atomic"
```

---

## Task 3: 修复 `avcodec_send_packet` 失败被静默吞掉

**Files:**
- Modify: `core/src/transcoder.cpp:345-349`, `core/src/pitch_shifter.cpp:282-285`, `core/src/light_editor.cpp:201-204`
- Test: 现有 `transcoder_test`、`pitch_shifter_test`、`light_editor_test`（或新增断言）

**Interfaces:**
- Consumes: `avcodec_send_packet` 返回值
- Produces: 失败时 `failed = true` 并跳出循环

**问题说明：**
三处音频工具在 `avcodec_send_packet` 失败时直接 `continue`，导致输出文件不完整但返回 `AG_OK`。

- [ ] **Step 1: 修改 transcoder.cpp**

```cpp
        if (avcodec_send_packet(dec.ctx, in_pkt) < 0) {
            error = "Failed to send packet to decoder";
            failed = true;
            av_packet_unref(in_pkt);
            break;
        }
```

- [ ] **Step 2: 修改 pitch_shifter.cpp**

找到对应位置，改为：

```cpp
        if (avcodec_send_packet(dec.ctx, in_pkt) < 0) {
            error = "Failed to send packet to decoder";
            failed = true;
            av_packet_unref(in_pkt);
            break;
        }
```

- [ ] **Step 3: 修改 light_editor.cpp**

找到对应位置，改为：

```cpp
        if (avcodec_send_packet(dec.ctx, in_pkt) < 0) {
            error = "Failed to send packet to decoder";
            failed = true;
            av_packet_unref(in_pkt);
            break;
        }
```

- [ ] **Step 4: 构建并运行相关测试**

```powershell
cmake --build build --config Debug --target transcoder_test pitch_shifter_test light_editor_test
ctest -C Debug -R "^(transcoder_test|pitch_shifter_test|light_editor_test)$" --output-on-failure
```

Expected: 全部通过。

- [ ] **Step 5: 提交**

```bash
git add core/src/transcoder.cpp core/src/pitch_shifter.cpp core/src/light_editor.cpp
git commit -m "fix(core/tools): fail fast on avcodec_send_packet errors"
```

---

## Task 4: 强化 `metadata_writer.cpp` 安全性

**Files:**
- Modify: `core/src/metadata_writer.cpp`
- Test: 新增 `tests/core/metadata_writer_test.cpp`

**Interfaces:**
- Consumes: `MetadataUpdate`, `utf8_path`
- Produces: 安全的写流程；失败时原文件保留

**问题说明：**
1. `av_interleaved_write_frame` 失败被忽略。
2. 使用 `MoveFileExA` 处理中文路径不可靠。
3. 临时文件名固定为 `<file>.agtmp`，并发冲突。
4. 直接覆盖原文件，无备份。
5. `ag_metadata_write` 声明支持封面但 `MetadataUpdate` 封面字段未使用。

- [ ] **Step 1: 修改 `atomic_replace` 使用 Unicode**

```cpp
bool atomic_replace(const std::string& temp_path, const std::string& target_path)
{
#ifdef _WIN32
    const std::filesystem::path temp_w(temp_path);
    const std::filesystem::path target_w(target_path);
    return MoveFileExW(temp_w.wstring().c_str(), target_w.wstring().c_str(),
                       MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return std::rename(temp_path.c_str(), target_path.c_str()) == 0;
#endif
}
```

- [ ] **Step 2: 生成唯一临时文件名**

替换 `core/src/metadata_writer.cpp:111-113`：

```cpp
    const std::filesystem::path p(utf8_path);
    const auto pid = static_cast<std::uint32_t>(
#ifdef _WIN32
        GetCurrentProcessId()
#else
        getpid()
#endif
    );
    const std::string temp_path =
        (p.parent_path()
         / (p.filename().string()
            + ".agtmp-"
            + std::to_string(pid)
            + "-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
            .string();
```

需要添加：

```cpp
#include <chrono>
#include <random>
```

- [ ] **Step 3: 严格处理 `av_interleaved_write_frame` 失败**

替换 `core/src/metadata_writer.cpp:144-155`：

```cpp
    while (av_read_frame(in_ctx, pkt) >= 0) {
        if (pkt->stream_index >= 0
            && pkt->stream_index < static_cast<int>(out_ctx->nb_streams)) {
            AVStream* in_stream = in_ctx->streams[pkt->stream_index];
            AVStream* out_stream = out_ctx->streams[pkt->stream_index];
            av_packet_rescale_ts(pkt, in_stream->time_base, out_stream->time_base);
            pkt->pos = -1;
            if (av_interleaved_write_frame(out_ctx, pkt) < 0) {
                error = "Failed to write packet";
                failed = true;
                av_packet_unref(pkt);
                break;
            }
        }
        av_packet_unref(pkt);
    }
```

- [ ] **Step 4: 失败时回滚，成功时备份原文件**

在 `av_write_trailer` 后、替换前增加：

```cpp
    if (failed) {
        av_packet_free(&pkt);
        avformat_close_input(&in_ctx);
        cleanup_output(out_ctx);
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        if (error.empty()) {
            error = "Metadata write failed";
        }
        return AG_IO_ERROR;
    }
```

替换原来的无条件 `atomic_replace` 为：

```cpp
    // Backup original file before replacing.
    const std::filesystem::path original_path(utf8_path);
    const std::string backup_path = (original_path.parent_path()
                                     / (original_path.filename().string() + ".agbak"))
                                        .string();
    {
        std::error_code ec;
        std::filesystem::remove(backup_path, ec);
        std::filesystem::copy_file(original_path, backup_path,
                                   std::filesystem::copy_options::overwrite_existing, ec);
    }

    if (!atomic_replace(temp_path, utf8_path)) {
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        error = "Failed to replace original file";
        return AG_IO_ERROR;
    }
```

- [ ] **Step 5: 在 C API 层忽略零长度封面（临时处理 ABI 不匹配）**

在 `core/src/c_api.cpp` 的 `ag_metadata_write` 中，若 `cover_size == 0`，不将 `cover_data` 传入 `MetadataUpdate`。

```cpp
    if (cover_size > 0U && cover_data != nullptr) {
        update.cover_data = cover_data;
        update.cover_size = cover_size;
        if (cover_mime_type != nullptr) {
            update.cover_mime_type = cover_mime_type;
        }
    }
```

- [ ] **Step 6: 新增 metadata_writer 测试**

创建 `tests/core/metadata_writer_test.cpp`：

```cpp
#undef NDEBUG
#include <agplayer/c_api.h>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>

int main(const int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path fixture = argv[1];
    const std::filesystem::path work_dir = fixture.parent_path();

    const std::filesystem::path src = work_dir / "meta-write-src.wav";
    std::filesystem::copy_file(fixture, src,
                               std::filesystem::copy_options::overwrite_existing);

    const ag_result result = ag_metadata_write(
        src.u8string().c_str(),
        "New Title", "New Artist", "New Album",
        "2024", "Rock", nullptr,
        nullptr, 0U, nullptr);
    assert(result == AG_OK);

    ag_metadata* metadata = nullptr;
    assert(ag_metadata_open(src.u8string().c_str(), &metadata) == AG_OK);
    assert(metadata != nullptr);
    assert(std::strcmp(ag_metadata_title(metadata), "New Title") == 0);
    assert(std::strcmp(ag_metadata_artist(metadata), "New Artist") == 0);
    ag_metadata_destroy(metadata);

    const std::filesystem::path backup = src.string() + ".agbak";
    assert(std::filesystem::exists(backup));

    std::filesystem::remove(src);
    std::filesystem::remove(backup);
    return 0;
}
```

- [ ] **Step 7: 更新 CMake 注册新测试**

在 `tests/core/CMakeLists.txt` 中参照现有测试添加 `metadata_writer_test`，并设置 `fixture_path` 参数。

- [ ] **Step 8: 构建并测试**

```powershell
cmake -S . -B build
cmake --build build --config Debug --target metadata_writer_test
ctest -C Debug -R "^metadata_writer_test$" --output-on-failure
```

- [ ] **Step 9: 提交**

```bash
git add core/src/metadata_writer.cpp core/src/c_api.cpp tests/core/metadata_writer_test.cpp tests/core/CMakeLists.txt
git commit -m "fix(core/metadata_writer): handle write errors, unicode paths, backup"
```

---

## Task 5: 防止音频工具输入/输出路径冲突

**Files:**
- Modify: `core/src/transcoder.cpp`, `core/src/light_editor.cpp`
- Test: 新增断言到现有测试

**Interfaces:**
- Consumes: `input_path`, `config.output_path` / `output_path`
- Produces: 若输入输出相同返回 `AG_INVALID_ARGUMENT`

- [ ] **Step 1: transcoder 添加路径冲突检查**

在 `core/src/transcoder.cpp` 的配置校验处增加：

```cpp
    if (config.output_path.empty()) {
        error = "Output path is empty";
        return AG_INVALID_ARGUMENT;
    }
    if (std::filesystem::equivalent(input_path, config.output_path, ec) ||
        std::filesystem::canonical(input_path, ec)
            == std::filesystem::canonical(config.output_path, ec)) {
        error = "Input and output path must be different";
        return AG_INVALID_ARGUMENT;
    }
```

- [ ] **Step 2: light_editor 添加路径冲突检查**

在 `core/src/light_editor.cpp` 的编辑入口增加类似检查。

- [ ] **Step 3: 构建并测试**

```powershell
cmake --build build --config Debug --target transcoder_test light_editor_test
ctest -C Debug -R "^(transcoder_test|light_editor_test)$" --output-on-failure
```

- [ ] **Step 4: 提交**

```bash
git add core/src/transcoder.cpp core/src/light_editor.cpp
git commit -m "fix(core/tools): reject input/output path collision"
```

---

## Task 6: 修复 Qt Bridge 生命周期问题

**Files:**
- Modify: `app/main.cpp:194-195`, `app/main.cpp:366-368`, `qt/src/window_controller.cpp`（析构函数）
- Test: `tests/qt/shutdown_test`、`tests/qt/global_hotkey_manager_test`

**Interfaces:**
- Consumes: `GlobalHotkeyManager`, `WindowController`, `QGuiApplication`
- Produces: 安全的析构顺序

- [ ] **Step 1: main.cpp 中显式移除 native event filter**

在 `app/main.cpp` 中，将 `GlobalHotkeyManager hotkeys;` 与 `app.installNativeEventFilter(&hotkeys);` 保持原样，但在 `app.exec()` 返回后、块结束前添加：

```cpp
            app.removeNativeEventFilter(&hotkeys);
            hotkeys.unregisterAll();
```

- [ ] **Step 2: 清空 WindowController 的 shutdown actions**

在 `app/main.cpp` 中 `result = app.exec();` 之后、窗口对象删除之前添加：

```cpp
            windows.setShutdownActions({});
```

- [ ] **Step 3: 在 WindowController 析构函数中清空 shutdown actions**

在 `qt/src/window_controller.cpp` 的析构函数中：

```cpp
WindowController::~WindowController()
{
    shutdownActions_ = {};
}
```

- [ ] **Step 4: 构建并测试**

```powershell
cmake --build build --config Debug --target shutdown_test global_hotkey_manager_test
ctest -C Debug -R "^(shutdown_test|global_hotkey_manager_test)$" --output-on-failure
```

- [ ] **Step 5: 提交**

```bash
git add app/main.cpp qt/src/window_controller.cpp
git commit -m "fix(qt/lifecycle): remove native event filter and clear shutdown actions"
```

---

## Task 7: 修复 `LightEditor` cancel token 竞争

**Files:**
- Modify: `qt/src/light_editor_controller.hpp`, `qt/src/light_editor_controller.cpp`
- Test: 手动验证 + 现有 QML 测试

**Interfaces:**
- Consumes: `ag_cancel_token*`
- Produces: 线程安全的 token 生命周期管理

- [ ] **Step 1: 添加互斥锁并跟踪 watcher**

在 `qt/src/light_editor_controller.hpp` 中：

```cpp
#include <QMutex>
#include <QPointer>
class QFutureWatcher<int>;
```

并在 private 区添加：

```cpp
    QMutex tokenMutex_;
    QPointer<QFutureWatcher<int>> watcher_;
```

- [ ] **Step 2: 修改 `cancel()`**

```cpp
void LightEditor::cancel()
{
    QMutexLocker lock(&tokenMutex_);
    ag_cancel_token* t = token_.load(std::memory_order_acquire);
    if (t != nullptr) {
        ag_cancel_token_cancel(t);
    }
}
```

- [ ] **Step 3: 修改 `start()` 中的 watcher 保存**

```cpp
    auto* watcher = new QFutureWatcher<int>(this);
    watcher_ = watcher;
```

finished lambda 中：

```cpp
            watcher_->deleteLater();
            ag_cancel_token* t = nullptr;
            {
                QMutexLocker lock(&tokenMutex_);
                t = token_.exchange(nullptr, std::memory_order_acq_rel);
            }
            if (t != nullptr) {
                ag_cancel_token_destroy(t);
            }
```

- [ ] **Step 4: 修改析构函数等待 worker 完成**

```cpp
LightEditor::~LightEditor()
{
    cancel();
    if (watcher_ != nullptr) {
        watcher_->waitForFinished();
    }
    QMutexLocker lock(&tokenMutex_);
    ag_cancel_token* t = token_.exchange(nullptr, std::memory_order_acq_rel);
    lock.unlock();
    if (t != nullptr) {
        ag_cancel_token_destroy(t);
    }
}
```

- [ ] **Step 5: 构建并运行相关测试**

```powershell
cmake --build build --config Debug
ctest -C Debug --output-on-failure
```

Expected: 全部 24 项测试通过。

- [ ] **Step 6: 提交**

```bash
git add qt/src/light_editor_controller.hpp qt/src/light_editor_controller.cpp
git commit -m "fix(qt/light_editor): protect cancel token with mutex"
```

---

## Task 8: 替换硬编码的 `D:\Music` 默认路径

**Files:**
- Modify: `qt/src/settings_controller.cpp:935-943`
- Test: `tests/qt/settings_controller_test.cpp`

**Interfaces:**
- Consumes: `QStandardPaths`
- Produces: 跨平台默认缓存/导出目录

- [ ] **Step 1: 修改默认缓存目录**

```cpp
QString SettingsController::defaultCacheDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
           + QStringLiteral("/waveform");
}
```

- [ ] **Step 2: 修改默认导出目录**

```cpp
QString SettingsController::defaultExportDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
           + QStringLiteral("/AgPlayer_Export");
}
```

- [ ] **Step 3: 确保目录存在**

在 `load()` 中首次读取到空缓存/导出目录时，调用 `QDir().mkpath(defaultPath)`。

- [ ] **Step 4: 构建并测试**

```powershell
cmake --build build --config Debug --target settings_controller_test
ctest -C Debug -R "^settings_controller_test$" --output-on-failure
```

- [ ] **Step 5: 提交**

```bash
git add qt/src/settings_controller.cpp
git commit -m "fix(qt/settings): use QStandardPaths instead of hardcoded D:\Music"
```

---

## 全局回归验证

- [ ] **Step 1: 全量构建**

```powershell
cmake --build build --config Debug
```

- [ ] **Step 2: 全量测试**

```powershell
ctest -C Debug --output-on-failure
```

Expected: 所有测试通过。

- [ ] **Step 3: Release 构建验证**

```powershell
cmake --build build --config Release
ctest -C Release --output-on-failure
```

---

## 自评检查

- **Spec coverage:** 每个 Critical 审查项都对应一个 Task（decoder UB、audio_engine 数据竞争、静默 send_packet 失败、metadata_writer 数据丢失、路径冲突、Qt 生命周期、LightEditor 竞争、硬编码路径）。
- **Placeholder scan:** 所有步骤都包含具体文件路径、代码片段和测试命令，无 "TBD"。
- **Type一致性:** `produced_frames_total_` 统一为 `std::atomic<std::int64_t>`；`tokenMutex_` 为 `QMutex`；路径检查使用 `std::filesystem::equivalent/canonical`。

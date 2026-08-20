# 单轨 AudioEvent 编辑器开发记录

## Phase 1 — AudioEvent 数据模型（2026-08-20）

### 需求追踪

- 规格 3.1：未新增运行时依赖；本阶段仅使用 C++ 标准库和既有 Qt Test。
- 规格 3.3：`SampleFrame` 固定为有符号 64 位，所有区间保持半开约定。
- 规格 5.1：`AudioEvent` 只以 `std::shared_ptr<const AudioSource>` 引用不可变源，不持有 PCM；验证源边界、时间线起点、淡入淡出及受限、排序的包络点。
- 规格 17 / 18.1：先 RED、后最小 GREEN，并为 Core 数据模型提供自动化测试。

### 修改文件

- `core/src/audio_editor/audio_event.hpp`：冻结 `SampleFrame`、`EventId`、`AudioSource`、`EnvelopePoint`、`FadeCurve`、`AudioEvent`，并提供 `isValid()` 与 `audibleFrames()`。
- `core/src/audio_editor/edit_command.hpp`：从新的域模型头获得唯一的 `SampleFrame` 定义。
- `core/src/audio_editor/audio_document.hpp`：仅删除已迁移到 `audio_event.hpp` 的重复 `AudioSource` 声明；未改变 `AudioDocument`、`AudioSpan` 或编辑行为。
- `core/CMakeLists.txt`、`tests/CMakeLists.txt`：注册域模型头与 `audio_event_test`。
- `tests/core/audio_event_test.cpp`：覆盖共享不可变 Source、源/时间线边界、淡入淡出、包络限制和超 32 位的两小时 SampleFrame。

### TDD 证据

**RED（预期失败）**

```powershell
cmake --preset windows-msvc-release
cmake --build build/release --target audio_event_test --parallel 4
ctest --test-dir build/release -R "^audio_event_test$" --output-on-failure
```

构建以预期原因失败：`fatal error C1083: cannot open include file: "audio_editor/audio_event.hpp"`。因测试可执行文件尚未生成，随后定向 CTest 报 `audio_event_test` `Not Run`。

**GREEN（通过）**

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build build\release --target audio_event_test --parallel 4 && ctest --test-dir build\release -R "^audio_event_test$" --output-on-failure'
```

输出：`1/1 Test #7: audio_event_test ... Passed`，`100% tests passed`。

### 构建与运行证据

VS x64 开发环境下执行：

```powershell
cmake --build --preset windows-msvc-release --parallel 4
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --parallel 4
ctest --test-dir build/release -R "^audio_event_test$" --output-on-failure
ctest --test-dir build/debug -R "^audio_event_test$" --output-on-failure
```

- Release 全量构建：退出码 0。
- Debug 全量构建：退出码 0。
- Release 聚焦 CTest：`1/1` 通过。
- Debug 聚焦 CTest：`1/1` 通过。
- 本阶段为 header-only 域模型，无独立 UI、真实音频、硬件或应用运行路径可验收。

### 已知失败、未验证项与回退点

- 当前 PowerShell 没有 MSVC `INCLUDE` 环境变量；未载入 VS 开发环境时，首次 GREEN 编译报 `<cmath>` 未找到。使用 `VsDevCmd.bat -arch=x64 -host_arch=x64` 后通过，非产品代码问题。
- 未运行完整 CTest、真实音频、录音、UI、硬件、拖放或性能矩阵；它们不属于 Phase 1 的数据模型门禁。
- 回退点：`9226ad7409d2bdae1ea335a51b22d9eadc25fd65`（Phase 1 提交前 HEAD）。

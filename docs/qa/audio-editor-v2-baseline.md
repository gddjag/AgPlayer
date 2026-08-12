# Audio Editor V2 Baseline

## Scope

- Source tree: `D:/ai/AgPlayer/.worktrees/revised-ui`
- Branch: `codex/revised-ui`
- Pre-production checkpoint: `9526e0a`
- UI reference: `C:/Users/Administrator/Desktop/音视频播放器/AgPlayer音频播放器完整版/音频编辑.png`
- Requirements: `C:/Users/Administrator/Desktop/AG_Player_音频编辑器_最新UI布局对照开发要求.md`
- Navigation override: `音频编辑 / 格式转换 / 元数据修改 / 文件名处理`

## Preserved User Work

The following pre-existing changes are outside Audio Editor V2 commits and must not be reset or overwritten:

- `app/qml/AgPlayer/components/PlayerPane.qml`
- `app/qml/AgPlayer/components/TrackList.qml`
- `tests/qml/tst_main_window.qml`
- `tests/qt/qml_main_window_test_main.cpp`

## Release Baseline

The Release build succeeds after loading the Visual Studio 2022 x64 developer environment. A plain PowerShell build without that environment fails to locate the MSVC standard library header `type_traits`; that is an environment failure, not a source failure.

Baseline CTest result on 2026-08-12:

- Total: 60
- Passed: 56
- Existing failures: 4
  - `translation_catalog_test`: unfinished text in `translations/agplayer_zh.ts`
  - `qml_main_window_test`: 20-second timeout
  - `qml_light_editor_test`: failed in the legacy editor suite
  - `audio_tools_layout_contract_test`: legacy inspector-default contract mismatch

After the Task 1 CMake refresh, `qml_mini_player_test` also exits before
Quick Test emits output. Windows Error Reporting contains earlier pre-change
Qt6Core BEX64 crashes for the same executable, so this is tracked as an
existing intermittent harness/lifecycle failure. It is outside the Feature
Flag change, but must be made stable before the final zero-failure gate.

## Package Baseline

- Directory: `build/package`
- Files: 237
- Bytes: 116,722,890
- MiB: 111.32

Final size comparison must use the same build, deployment, and directory measurement method. No EXE installer is produced in this development round.

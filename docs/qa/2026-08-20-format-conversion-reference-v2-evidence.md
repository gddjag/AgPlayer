# Format Conversion Reference V2 — Verification Evidence

## Scope and reproducibility

- Worktree: `D:\ai\AgPlayer\.worktrees\revised-ui`
- Branch / starting commit: `codex/revised-ui` / `f066a73f50009e90f38424c8b3f6320855b4262d`
- Build directory: `build/release` (this worktree has no `build/release-verify` directory).
- Source visual truth: `C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\格式转换 .png` (`1672×941`).
- Test-only fixture: `tests/qml/FormatConverterVisualFixture.qml`; it is not in `app/CMakeLists.txt` and does not run from production startup. The fixture binds its test-only facade through `FormatConvertPage.converter`. Its facade's `buildPreflight()` explicitly returns a non-ready result and never reports or creates a conversion.
- Screenshot runner: `qml_audio_tools_test` with `QT_QPA_PLATFORM=windows`, `QT_QUICK_CONTROLS_STYLE=Basic`, and the explicit `AGPLAYER_VISUAL_FIXTURE_OUTPUT` output path. Windows QPA was required for readable Chinese glyphs; the offscreen platform rendered missing-glyph boxes and is not visual-QA evidence.

## Real facade conversion

Command (Visual Studio 2022 x64 environment, with Qt and vcpkg runtime DLL paths prepended):

```powershell
build/release/tests/qml_audio_tools_test.exe --format-conversion-evidence build/qa/format-conversion-reference-v2-real
```

The test-only command-line mode generates a two-second 16 kHz mono PCM WAV, then performs the real `FormatConverter` path for each target:

`loadFiles → buildPreflight → confirmPendingPlan → transcodeCompleted → ag_metadata_open`.

The detailed frozen request, resolved plan, task row, file byte size, and reopened probe values are retained at `build/qa/format-conversion-reference-v2-real/conversion-evidence.json`.

| Target | Requested | Resolved encoder / muxer | Published output | Bytes | Final state | Reopened probe |
| --- | --- | --- | --- | ---: | --- | --- |
| FLAC | `flac`, 44.1 kHz, stereo, CBR request, metadata kept, cover off, auto-number | `flac` / `flac`; lossless resolution correctly clears bitrate mode | `build/qa/format-conversion-reference-v2-real/flac/short-reference.flac` | 13,700 | `Done` | 2,000 ms, 44.1 kHz |
| MP3 | `mp3`, 192 kbps CBR, 44.1 kHz, stereo, metadata kept, cover off, auto-number | `libmp3lame` / `mp3`, CBR | `build/qa/format-conversion-reference-v2-real/mp3/short-reference.mp3` | 49,572 | `Done` | 2,000 ms, 44.1 kHz |

Exit result: `0`. Both outputs existed, had non-zero size, and were reopened through the project metadata/probe API after their facade-reported `Done` state. The test command never calls `ffmpeg.exe`.

## Focused build and regression run

```powershell
cmake --build build/release --target AgPlayer format_matrix_test format_conversion_plan_test format_conversion_task_model_test audio_tools_end_to_end_test qml_audio_tools_test
ctest --test-dir build/release -R '^(format_matrix_test|format_conversion_plan_test|format_conversion_task_model_test|audio_tools_end_to_end_test|qml_format_converter_test|qml_format_converter_visual_fixture_test|format_converter_reference_contract_test|audio_tools_layout_contract_test|translation_catalog_test|source_encoding_test)$' --output-on-failure
```

Build result: completed. Final focused CTest result: 10/10 passed after the
layout contract was updated to recognize the settings panel's real expanded
and collapsed width expression.

- Passed: `format_matrix_test`, `audio_tools_end_to_end_test`, `format_conversion_plan_test`, `format_conversion_task_model_test`, `translation_catalog_test`, `source_encoding_test`, `qml_format_converter_test`, `qml_format_converter_visual_fixture_test`, `format_converter_reference_contract_test`, and `audio_tools_layout_contract_test`.

## Visual QA iteration history

- Native test fixture capture: `docs/qa/2026-08-20-format-conversion-reference-v2-fixture-native.png` (`1672×941`, no scaling).
- Full 1:1 side-by-side: `docs/qa/2026-08-20-format-conversion-reference-v2-comparison.png`.
- Task table crop: `docs/qa/2026-08-20-format-conversion-reference-v2-table-comparison.png`.
- Settings crop: `docs/qa/2026-08-20-format-conversion-reference-v2-settings-comparison.png`.
- Footer crop: `docs/qa/2026-08-20-format-conversion-reference-v2-footer-comparison.png`.

The source is on the left and the fixture capture is on the right in every comparison. Neither half is resized; the full comparison is exactly `3344×941`.

### Iteration 1 findings (closed by `9a38dd8`)

- [P2] `FormatTaskTable.qml`: the current column-width sum exceeds the reference-width left panel, clipping the progress percentage at the right edge. The reference exposes the complete `68%`, `42%`, and similar values.
- [P2] `FormatSettingsPanel.qml`: the local-processing hint begins about 25 px above the reference vertical position, leaving excessive blank space below it rather than matching the source's lower alignment.
- [P2] `FormatTaskTable.qml`: the task icon is a bare coloured music/file glyph; the reference uses a white music glyph over a clearly visible coloured rounded-square tile. The missing tile changes row-state visual weight and scanability.

The progress-column clipping, hint placement, and file-icon tile were corrected by the Task 2 production-QML commit `9a38dd8`. The native fixture was rebuilt and captured again at the same viewport before the following second comparison.

### Iteration 2 findings (actionable P2; block acceptance)

- [P2] `FormatSettingsPanel.qml`, A output-format grid: four format buttons are materially wider and the row is less dense than the source (approximately 99 px each in the implementation versus 87 px in the source). Constrain the four columns to the reference rhythm rather than allowing the full inspector width to expand them.
- [P2] `FormatSettingsPanel.qml`, B encoding-parameter grid: its first label column is too narrow. The encoder control begins roughly 38 px too far left; the remaining controls expand correspondingly. Give the label column an explicit reference width and let only the control column fill the remainder.
- [P2] Shared workbench shell / page vertical rhythm: the implementation main panel begins about 3 px lower (`y≈171` versus `y≈168`) and the footer begins about 2 px higher (`y≈823` versus `y≈825`). This leaves the primary main region visibly shorter than the source and exceeds the ≤1 px boundary target.

The second comparison has no remaining P0/P1 from the earlier clipped percentage, hint, or task-icon-tile defects. The P2s above belong to the Task 2 production-QML / shared-shell owner. This task does not modify those files. `design-qa.md` must remain `blocked` until a later same-state capture has no actionable P0/P1/P2 issue.

## Remaining P3 candidates after blocking fixes

- Native Windows font rasterisation may differ subtly from the source bitmap. Re-evaluate only after the three P2s are closed; do not pre-classify any remaining layout drift as P3.

## Final same-state acceptance — `a09e36f`

The final native Windows capture was rebuilt from `a09e36f` at the exact `1672×941` viewport. The full comparison and the table/settings/footer crops were regenerated with the source on the left and the implementation on the right, without scaling.

- [closed P2] A format-grid button rhythm and B label/control alignment (`f84cf53`, `45d1eaa`).
- [closed P2] Task-table filename header alignment and main-workbench top boundary (`c4f52e6`).
- [closed P2] Checked task/settings controls now use the reference blue filled 20 px indicator with a white project check glyph (`a09e36f`).
- Final verdict: no actionable P0/P1/P2. Remaining native font antialiasing and 1–2 px control-rhythm variation are P3 only.

Final focused CTest: all 10 format/audio-tools checks passed, including `qml_format_converter_test`, `qml_format_converter_visual_fixture_test`, `format_converter_reference_contract_test`, and `audio_tools_layout_contract_test`. A complete Release build also succeeded. The subsequent full CTest run passed 74/75 tests; the remaining `qml_main_window_test` reports nine pre-existing main-player interaction/theme assertions outside the format-conversion surface, so a whole-suite pass is not claimed.

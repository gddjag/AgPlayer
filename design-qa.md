## Format Conversion V2 — 2026-08-20

**Source visual truth**: `C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\格式转换 .png` (`1672×941`).

**Evidence**: 1:1 native Windows capture and full/table/settings/footer comparisons in `docs/qa/2026-08-20-format-conversion-reference-v2-*.png`, using the test-only injected 12-row fixture. Real facade conversion evidence is recorded in `docs/qa/2026-08-20-format-conversion-reference-v2-evidence.md`.

**Final review**: no actionable P0/P1/P2 remains. The task-table header, colored file tiles, blue selected checkboxes, A/B settings geometry, main-region boundaries, footer, progress values, and settings options align at the reference viewport. Native Chinese font rasterisation and approximately 1–2 px control-rhythm variation remain P3 only.

**Interactions**: real FLAC and MP3 conversion through `FormatConverter` preflight/confirmation completed with non-empty outputs reopened by the project probe API; all 10 focused QML/format/audio-tools tests passed.

final result: passed

## Filename Processing — 2026-08-20

**Source visual truth**

- `C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\文件名处理.png`

**Implementation evidence**

- `D:\ai\AgPlayer\.worktrees\revised-ui\docs\qa\filename-process-final.png`
- Combined comparison: `D:\ai\AgPlayer\.worktrees\revised-ui\docs\qa\filename-process-comparison-final.png`
- Viewport: 1672 x 941, Chinese, dark theme, 24 real local fixture files.

**Findings**

- Command bar y=110/high 60; file table x=6/y=176/wide 619; rule panel x=631/y=176/high 259; preview starts y=441; summary starts y=801 and ends x=1662/high 135.
- The rule panel matches the four reference regions: prefix, suffix, general rules and automatic numbering. Major dividers and control bounds align at the source viewport.
- Prefix and suffix both expose add/remove modes. Blank add fields perform reverse cleanup; explicit remove fields delete exact case-insensitive leading/trailing text.
- File table, preview, validation summary, three summary cards, and Start/Cancel action order match the source hierarchy, columns, active colors and 1672 x 941 source canvas.
- The implementation screenshot reports 24 ready / 0 warning / 0 error because it uses real executable fixture state; the reference's 19 / 2 / 3 values are illustrative content, not hard-coded UI state.
- Native Qt/Windows text antialiasing differs slightly from the raster reference. P3 only; the same-viewport review found no remaining actionable P0/P1/P2 mismatch.

final result: passed

## Audio Editor V2 — 2026-08-13

**Source visual truth**

- `C:\Users\Administrator\Desktop\音视频播放器\AgPlayer音频播放器完整版\音频编辑.png`

**Implementation evidence**

- `D:\ai\AgPlayer\.worktrees\revised-ui\build\qa-audio-editor\audio-editor-final-v11-1672x942.png`
- Combined comparison: `D:\ai\AgPlayer\.worktrees\revised-ui\build\qa-audio-editor\audio-editor-comparison-v11.png`
- Viewport: 1672 x 942, Chinese, dark theme, real stereo WAV.

**Findings**

- Navigation order, command bar, one-line summary, single-track stereo waveform, selection, playhead, overview, transport and status bar match the reference hierarchy.
- The right inspector contains only Recording and Speed/Pitch, both fully visible without page-level horizontal overflow.
- Command actions use project icons, tooltips and accessible names; visible controls are live or correctly disabled.
- The transport now prioritizes Add/Previous/Next Marker and removes duplicate volume/zoom controls; time readouts remain compact and readable.
- Standard editing shortcuts and Ctrl+wheel waveform zoom are live and covered by QML interaction tests.
- Font rendering and compact control metrics follow native Qt/Windows rather than pixel-copying the source image. P3 only.
- The approved deviations from the reference are limited to removing bottom volume/duplicate zoom controls and adding direct marker controls.
- No actionable P0/P1/P2 mismatch remains in the same-viewport combined comparison.
- Release gates remain tracked separately: no capture endpoint is available for real microphone QA, and two unrelated full-suite tests are not green.

final result: passed

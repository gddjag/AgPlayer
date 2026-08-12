# Metadata Edit Remediation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans or superpowers:subagent-driven-development to implement task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Make Audio Tools > Metadata Edit match the supplied dark two-pane UI and safely apply real batch metadata changes.

**Architecture:** Keep the existing Qt/QML page and FFmpeg packet-copy writer. Introduce a canonical nine-field edit plan at the Qt/core boundary, preflight every target before writing, verify the temporary output by reopening it, then atomically replace only validated originals. QML owns presentation and never submits FFmpeg keys.

**Tech Stack:** Qt 6/QML, C++17, FFmpeg C API, CTest/Qt Quick Test.

## Global Constraints

- Modify only metadata-related code and required shared interfaces in this worktree.
- Metadata-only mode never opens a decoder or encoder and uses same-container packet copy.
- Create temp output beside the source, verify it, then atomically replace the original.
- Keep only title, artist, album, album artist, genre, year, date, composer, BPM and cover edit controls.
- Keep Chinese and English localization keys; do not package an EXE.

---

### Task 1: Canonical plan and preflight

**Files:** `core/src/metadata_writer.hpp`, `core/src/metadata_writer.cpp`, `tests/core/metadata_writer_test.cpp`

- [ ] Add `MetadataAction`, `CanonicalField`, `FieldEdit`, `MetadataEditPlan`, capability and result records.
- [ ] Write failing core tests for Keep/Set/Clear, blank Set rejection, invalid BPM, and year/date conflict.
- [ ] Implement plan validation and container preflight without writing any file.
- [ ] Run `metadata_writer_test` and keep existing metadata tests green.

### Task 2: Safe writer verification

**Files:** `core/src/metadata_writer.cpp`, `tests/core/metadata_writer_test.cpp`

- [ ] Write failing tests asserting metadata-only packet copy preserves codec parameters and reports verified values.
- [ ] Make temp paths retain the real extension, copy safe streams, write a plan, reopen the temp output, verify requested values, then replace atomically.
- [ ] Return structured failures for unsupported, I/O, source-change, verification and replacement failures.
- [ ] Run `metadata_writer_test`.

### Task 3: Qt binding and real batch summaries

**Files:** `qt/src/metadata_editor.hpp`, `qt/src/metadata_editor.cpp`, `tests/qt/audio_tools_end_to_end_test.cpp`

- [ ] Write failing Qt tests for scope selection, mixed values, empty Set validation and preflight summaries.
- [ ] Convert QML descriptors into the canonical plan; expose `previewPlan`, `applyPlan`, target counts and per-file results.
- [ ] Refresh library metadata/cache only after each verified replacement.
- [ ] Run relevant Qt tests.

### Task 4: Screenshot-aligned Metadata UI

**Files:** `app/qml/AgPlayer/components/tools/MetadataEditPage.qml`, `tests/qml/tst_light_editor.qml`, `translations/agplayer_zh.ts`, `translations/agplayer_en.ts`

- [ ] Write failing QML tests for scope/mode controls, nine three-state rows, cover controls, disabled apply state and live summary.
- [ ] Rework the page into the screenshot's file-table/inspector layout using existing Theme controls and translations.
- [ ] Wire all toolbar actions, search, selection, scope, preflight choices, cancellation, export and result status.
- [ ] Run QML layout and metadata interaction tests at offscreen DPI scales.

### Task 5: Regression verification

**Files:** no new production files unless a failing regression demands it.

- [ ] Configure/build the current worktree without new warnings.
- [ ] Run targeted metadata/QML tests, then the complete CTest suite.
- [ ] Record unsupported containers/fields honestly and do not package an EXE.

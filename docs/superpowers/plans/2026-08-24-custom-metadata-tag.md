# Custom Metadata Tag Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the metadata editor's editable year field with a real custom metadata tag field and reproduce the newest reference layout.

**Architecture:** The core metadata writer gains `CanonicalField::CustomTag` backed by stable canonical key `AGPLAYER_TAG`; Qt maps QML `customTag` to that core field and retains existing preflight/write/rollback behavior. The QML page swaps the visible form and table field while retaining its direct-entry batch editing layout.

**Tech Stack:** C++17, Qt 6/QML, FFmpeg metadata dictionaries, CTest, PowerShell layout contracts.

**Spec:** `docs/superpowers/specs/2026-08-24-custom-metadata-tag-design.md`

## Global Constraints

- Preserve the current transactional metadata writer, stream-copy behavior, `.agbak` backup, atomic replacement, and preflight capabilities.
- Do not add dependencies or widen container support.
- Use `customTag` at the QML/Qt boundary and `AGPLAYER_TAG` as the core writer canonical key.
- Match the supplied Chinese reference's existing spacing and field order.

---

### Task 1: Add a verified core custom-tag field

**Files:** `core/src/metadata_writer.hpp`, `core/src/metadata_writer.cpp`, `tests/core/metadata_writer_test.cpp`.

- [x] Add a failing supported-fixture test containing `FieldEdit{CanonicalField::CustomTag, FieldAction::Set, "电子"}` and assert the verified result reports `电子`.
- [x] Run `cmake --build build/metadata-final --target metadata_writer_test --parallel 4`; it must fail because `CustomTag` does not exist. `ctest` runs an already-built executable and is reserved for the green check.
- [x] Add `CustomTag` and the corresponding `MetadataUpdate::custom_tag` slot to every canonical field switch and map it to `AGPLAYER_TAG` without changing other format rules.
- [x] Rebuild `metadata_writer_test`, rerun the test, and require PASS.

### Task 2: Wire customTag through the Qt editor

**Files:** `core/src/decoder.hpp`, `core/src/decoder.cpp`, `core/include/agplayer/c_api.h`, `core/src/c_api.cpp`, `core/src/transcoder.cpp`, `qt/src/metadata_editor.hpp`, `qt/src/metadata_editor.cpp`, `qt/src/format_converter.cpp`, `tests/qt/audio_tools_end_to_end_test.cpp`, `tests/qml/tst_metadata_editor.qml`.

- [x] Add a failing test that `rowForField("customTag")` exists and `rowForField("year")` is null.
- [x] Run `ctest --test-dir build/metadata-final -R '^qml_metadata_editor_test$' --output-on-failure`; it must fail on the old schema.
- [x] Append a C API custom-tag field/accessor without changing existing enum values, expose it from decoder probing, and map it through transcode metadata plans. Replace the metadata-editor-only `year` key with `customTag` in entry data, aggregate fields, payload mapping, writer results, and refreshed metadata. Leave library year data untouched.
- [x] Rerun the QML test and require PASS.

### Task 3: Recreate the revised Chinese UI and contract

**Files:** `app/qml/AgPlayer/components/tools/MetadataEditPage.qml`, `tests/qml/tst_metadata_editor.qml`, `tests/scripts/metadata_editor_layout_contract_test.ps1`.

- [x] Add a failing contract requiring `customTag` and rejecting `year` as an editable field; assert the table header is 标签 and the form label is 自定义标签.
- [x] Run `powershell -NoProfile -ExecutionPolicy Bypass -File tests/scripts/metadata_editor_layout_contract_test.ps1 -SourceRoot D:/ai/AgPlayer`; it must fail against the old UI.
- [x] Replace the table and form bindings with `customTag`, preserving the reference layout and existing summary machinery.
- [x] Rerun the QML and contract tests and require PASS.

### Task 4: Integrate and verify

- [x] Build `AgPlayer`, `metadata_writer_test`, and `qml_audio_tools_test`.
- [x] Run `ctest --test-dir build/metadata-final --output-on-failure -R '^(metadata_writer_test|qml_metadata_editor_test|metadata_editor_layout_contract_test)$'`.
- [x] Run QML lint and `git diff --check`.
- [x] Commit only the writer, Qt, QML, test, spec, and plan files after the preceding commands succeed.

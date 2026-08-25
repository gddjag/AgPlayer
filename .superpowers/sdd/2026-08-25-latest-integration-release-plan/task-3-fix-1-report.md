# Task 3 repair round 1

## Implemented

- Format conversion concurrency is clamped to 1–10, defaults to 5, and is
  controlled beside total progress. The settings panel reads and writes the
  global bitrate, sample-rate, and channel defaults; Settings lists all eight
  output formats.
- Format capability profiles now include Opus 320 kbps and use an explicit
  OGG/Vorbis Q8 quality default (not a fabricated bitrate mapping).
- Cover retention is best effort: unsupported containers and multi-cover
  sources drop the cover rather than failing the audio conversion.
- Metadata apply uses the preflight snapshot for confirm/write/readback and
  refreshes only matched successful paths. Canonical path, size, mtime, and a
  stable snapshot identity are rechecked before writing; changed sources fail
  safely.
- Mixed metadata UI has a visible per-field clear action and consistent text
  explaining untouched versus delete semantics.
- `emitAllChanged` now emits cover, directory-structure, and video-audio
  extraction settings. Cache migration is schema/version plus user-modified
  marker based; an unmarked 1024 MB value is preserved.
- Completed/failed summary filters toggle back to All on a second click.

## Verification

- Passed (3 tests): `transcode_v2_api_test`, `transcode_capability_test`, and
  `settings_controller_test` under `build/verification-release`.
- `git diff --check` passed.
- `qml_format_converter_test`, `qml_metadata_editor_test`, and
  `audio_tools_end_to_end_test` build successfully but their test executables
  currently terminate at process shutdown with Windows heap status
  `0xc0000374` (the former QML test reports no assertion failure before exit).
  This remains a release risk and is not counted as passing.

## Dependency ledger

Task 3 exposes the shared conversion defaults and converter interface. Task 2
remains responsible for consuming those defaults in AudioEditor shared export
and pitch workflows.

## Repair round 2

- The conversion scheduler honors the persisted 1–10 concurrency value,
  independently of logical CPU count. Shared Settings are live-wired into
  AudioEditor export and pitch/formant controls without reverse bindings.
- Schema-less historical 1024 MB defaults migrate once to 10 GB; an explicit
  user-modified marker preserves a deliberate 1 GB choice. Opus/OGG no longer
  advertise cover preservation.
- Passed: `settings_controller_test`, `transcode_capability_test`, and
  `qml_audio_editor_test`.
- Remaining risk: `qml_format_converter_matrix_test` still has one rapid
  capability refresh failure that replaces a VBR click with CBR; not counted
  as passing.

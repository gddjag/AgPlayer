# Task 5 Report: Resumable Verified Package Installation

## Implementation

- Added a strict per-file package manifest. It rejects unknown fields, absolute/traversal paths, duplicate normalized targets, Windows aliases/ADS/device names, invalid hashes, and unsafe download URLs.
- Added an explicit asynchronous state machine: `Idle`, `LicenseRequired`, `Resolving`, `Downloading`, `Paused`, `Verifying`, `Committing`, `Completed`, `Canceled`, and `Failed`.
- Resolves real sizes with HTTP HEAD, reports a percentage only when every total is known, performs disk preflight before GET, persists `.part` resume metadata, and resumes with HTTP Range.
- Verifies every SHA-256, permits only manifest-listed files in staging, rejects reparse points in staging/target trees, and swaps the complete directory while preserving/restoring the previous version.
- Derives the IndexTTS gate from the exact official license identity rather than a manifest boolean. Acceptance records persist `packageId`, `licenseUrl`, `revision`, and UTC `acceptedAt`. Qwen/Cosy license links remain exposed without blocking.
- Added `hasLicenseAcceptance(...)` for the later generation path to enforce the same exact URL/revision gate without coupling this task to UI or Worker code.

## RED / GREEN

Initial RED, VS2022 x64 and `build/msvc-debug`:

```text
voice_clone_package_manager_test.cpp(1): fatal error C1083:
cannot open include file: 'voice_clone_package_manager.hpp'
```

Review-driven RED added five regression cases; all failed before fixes: Windows aliased targets, unlisted staging residue, manifest-boolean license bypass, EOF Range on a complete `.part`, and interrupted-swap rollback recovery.

Focused GREEN:

```text
voice_clone_package_manager_test: Passed
14 test functions plus init/cleanup, 0 failed
```

## Final verification

Environment: Visual Studio 2022 Developer Command Prompt, `cl.exe` x64 (`14.38.33130`), build directory `build/msvc-debug`.

Built targets:

```text
voice_clone_package_manager_test
voice_clone_host_controller_test
voice_clone_manifest_test
voice_clone_capability_schema_test
voice_clone_worker_protocol_test
audio_tools_end_to_end_test
qml_audio_tools_test
```

CTest matrix: 9/9 passed:

```text
audio_tools_end_to_end_test
voice_clone_manifest_test
voice_clone_capability_schema_test
voice_clone_worker_protocol_test
voice_clone_host_controller_test
voice_clone_package_manager_test
source_encoding_test
qml_format_converter_test
qml_filename_process_test
```

## Self-review

- Spec review found staging allowlist, license-policy derivation, complete-partial resume, and interrupted rollback gaps. Each received a failing test and fix.
- Standards review found descendant reparse cleanup and Windows path alias risks. Recursive reparse refusal and Windows component validation were added.
- `git diff --check` is included in final verification.
- No ZIP extraction, UI, Worker, executable payload, or raw archive handling was added.

## Concerns

- Windows cannot atomically exchange two nonempty directories. The implementation preserves the old directory as `.rollback`, restores it on a failed second rename, and recovers an interrupted swap on the next commit attempt; there remains a brief crash window between the two same-volume renames.
- License acceptance uses atomic `QSaveFile`, but concurrent independent writers are not serialized; the current application has one package manager owner. Add an interprocess lock if later tasks allow multiple package-management processes.

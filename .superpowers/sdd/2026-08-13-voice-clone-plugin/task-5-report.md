# Task 5 Report: Resumable Verified Package Installation

## Implementation

- Added a strict per-file package manifest. It rejects unknown fields, absolute/traversal paths, duplicate normalized targets, Windows aliases/ADS/device names, invalid hashes, and unsafe download URLs.
- Added an explicit asynchronous state machine: `Idle`, `LicenseRequired`, `Resolving`, `Downloading`, `Paused`, `Verifying`, `Committing`, `Completed`, `Canceled`, and `Failed`.
- Resolves real sizes with HTTP HEAD, reports a percentage only when every total is known, performs disk preflight before GET, persists `.part` resume metadata, and resumes with HTTP Range.
- Verifies every SHA-256, permits only manifest-listed files in staging, rejects reparse points in staging/target trees, and swaps the complete directory while preserving/restoring the previous version.
- Derives the IndexTTS gate from the exact approved model/adapter identity rather than a manifest boolean or URL guess. Acceptance records persist `modelId`, `adapterId`, `licenseUrl`, `revision`, and UTC `acceptedAt`. Qwen/Cosy license links remain exposed without blocking.
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

## Fix round 1

### Review findings addressed

- Package manifests now carry strict approved `modelId + adapterId` tuples for both Qwen models, IndexTTS 2.5, and CosyVoice 3. Index gating derives only from the exact approved Index tuple, never from a URL prefix or manifest boolean. The acceptance key includes exact model, adapter, license URL, and license revision.
- License storage now requires `schemaVersion: 1` and a `records` array. Every record requires typed nonempty model, adapter, URL, revision, and a valid ISO timestamp. Corrupt stores fail closed; acceptance returns an explicit error and never overwrites the original bytes. `QSaveFile` remains the atomic writer.
- Staging cleanup, verification, and recursive reparse checks include hidden and system entries.
- Each start creates a new operation generation. Every HEAD/GET signal checks both active reply identity and generation before touching shared state or files; pause/cancel invalidate the old generation before aborting.
- A missing install root is created only after its nearest existing ancestor is canonicalized and its whole ancestor chain is proven non-reparse; containment and reparse checks run again after creation.
- `packageId` now uses the Windows component rules and rejects device names, trailing dots/spaces, ADS syntax, `.staging`, `.rollback`, rollback suffixes, and the license-store filename.
- Unknown-total partial downloads restart from byte zero, preventing unverifiable EOF Range requests.
- Deployment operations have a narrow fault-injection seam. Tests prove that a failed staging-to-target rename restores the old target, target descendants are scanned for reparse points before swapping, and backup cleanup failure leaves the new package `Completed` with a retained backup and warning.

### TDD evidence

- Critical RED: the first build failed because `VoiceClonePackageManifest` lacked `modelId`/`adapterId` and the exact license query signature. Corrupt-store and hidden-staging behavior tests were already present before production changes.
- Stale-reply RED: tests requested a constructor accepting a controlled network manager; the build failed because no such API existed. GREEN uses delayed abort-finished replies and proves both pause-then-start and cancel-then-start remain completed after the stale callback arrives.
- Important 5-7 RED: three behavior tests failed independently: unsafe `CON` package ID remained valid, a missing root under a junction created external directories, and unknown-total resume sent a nonzero Range (`24` instead of `0`).
- Important 8 RED: the build failed because `DeploymentOperations` and the injected commit overload did not exist. GREEN proves the real target-to-backup / failed-staging-to-target / backup-to-target sequence, target-subtree reparse rejection, and cleanup-warning success semantics.
- Focused package suite after fixes: 23 test functions plus init/cleanup, 0 failures.

### Fix round 1 verification

Visual Studio 2022 x64 (`cl.exe` 14.38.33130), `build/msvc-debug`:

```text
audio_tools_end_to_end_test .......... Passed
voice_clone_manifest_test ............ Passed
voice_clone_capability_schema_test ... Passed
voice_clone_worker_protocol_test ..... Passed
voice_clone_host_controller_test ..... Passed
voice_clone_package_manager_test ..... Passed
source_encoding_test ................. Passed
qml_format_converter_test ............ Passed
qml_filename_process_test ............ Passed
100% tests passed, 9/9
```

### Remaining concern

- License acceptance remains single-writer within one application process. Atomic replacement prevents torn files; a later multi-process package service would still require an interprocess lock.

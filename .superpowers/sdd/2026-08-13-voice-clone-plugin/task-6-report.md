# Task 6 Report: Native Controller and Worker Client

## Implementation

- Added the `agplayer_voice_clone_plugin` Qt `MODULE` target, implementing the Task 4 native interface without adding final QML, engine assets, or download manifests.
- Added a `QProcess + QLocalServer/QLocalSocket` Worker client with newline-delimited protocol frames, trusted Adapter launcher resolution, handshake-before-command enforcement, identity-triple validation, request correlation, per-request timeouts, crash handling, restart, and bounded shutdown.
- Supports trusted executable launchers and installed `pythonModule` packs via the manifest runtime root's `python.exe`; no user-provided command or free-form argument is accepted.
- Added Controller model selection, load/unload/generate/cancel lifecycle, live capability replacement, dynamic basic/advanced parameter lists, Schema validation, and hidden/unknown/invalid parameter rejection.
- Enforced the Task 5 exact IndexTTS acceptance record at generation time, before any Worker generation request.
- Model refresh merges built-in and valid user models, preserves `ready`/`local-unverified`, and reports invalid manifests as diagnostic model rows rather than discarding the whole refresh.
- Runtime probing is deliberately lazy: refresh never starts every model. The selected model is probed through hello/capabilities/load; a rejected load changes its row to `invalid` with the Adapter diagnostic.
- Output creation uses a Controller-created UUID request directory, Worker-only `.part` output, handle-based source validation, exclusive final creation, and pre/post canonical containment checks. Cancel, crash, OOM failure, timeout, and unsafe output remove temporary trees without traversing reparse points.

## RED

VS 2022 x64, `build/msvc-debug`:

```text
voice_clone_controller_test.cpp(1): fatal error C1083:
cannot open include file: 'voice_clone_controller.hpp'
```

The model-refresh follow-up RED added an invalid local manifest and failed because no `invalid` row was reported. GREEN retained valid models and emitted a diagnostic row.

## GREEN

Focused build and test:

```powershell
cmake --build build/msvc-debug --config Debug --target voice_clone_controller_test agplayer_voice_clone_plugin
ctest --test-dir build/msvc-debug -C Debug -R "^voice_clone_controller_test$" --output-on-failure
```

Result: `voice_clone_controller_test` passed, 1/1; native MODULE linked successfully.

Affected matrix:

```text
audio_tools_end_to_end_test .......... Passed
voice_clone_manifest_test ............ Passed
voice_clone_capability_schema_test ... Passed
voice_clone_worker_protocol_test ..... Passed
voice_clone_host_controller_test ..... Passed
voice_clone_package_manager_test ..... Passed
voice_clone_controller_test .......... Passed
qml_format_converter_test ............ Passed
qml_filename_process_test ............ Passed
100% tests passed, 9/9
```

## Deterministic Worker Coverage

The controller test launches a copied real test executable as a Worker subprocess and communicates over a real local socket. It covers successful hello/capabilities/load/generate/unload, reversed completion order correlation, cancel, structured OOM, wrong Adapter identity, process crash, request timeout, restart, temporary WAV cleanup, and continued host `QCoreApplication` health.

## Self-review

- A response with the wrong Adapter ID/version/protocol now kills only the Worker and fails pending work; it cannot mutate player playback state.
- No durable Worker state is mixed with the player process. All pending requests are idempotently removed by request ID on response, cancel, timeout, or termination.
- Worker commands cannot run before hello plus live capabilities complete.
- Output containment is checked before copying and after exclusive final creation; the source is opened and verified through one OS handle, closing the Task 3 validation-to-open race.
- No production method accepts an arbitrary executable, shell command, output absolute path, or free-form process argument.
- `git diff --check` and the complete matrix are rerun after the final code change before commit.

## Concerns

- The Windows Adapter Pack contract assumes `runtime.root/python.exe` for `pythonModule` launchers. Task 8 must install that exact portable runtime layout and run its own real-engine smoke test.
- Refresh validates manifests and exposes readiness states immediately; the selected model receives the actual Adapter probe through handshake/capabilities/load. Probing every installed model would require a new protocol operation and is intentionally not invented outside Task 3's protocol.
- Exclusive publication is not an OS sandbox for a malicious trusted Worker process. Pack signature/trust and Worker distribution remain part of the installer/runtime supply-chain boundary.

## Fix round 1

### Security and lifecycle changes

- Replaced path-check-then-open output publication. Windows now opens the exact `.part` object with `CreateFileW(..., FILE_FLAG_OPEN_REPARSE_POINT)`, rejects directory/reparse tags through `FileAttributeTagInfo`, records size on the same handle before and after copying, and deletes that opened object by handle. POSIX uses `O_NOFOLLOW` plus same-fd `fstat` checks. The final WAV is created with `QFile::NewOnly`; its parent chain is validated before and after copying.
- Controller construction now fixes canonical `pluginRoot` and `modelsRoot`. Adapter selection accepts only ID/version and resolves `<pluginRoot>/adapters/<id>/<version>/adapter.json`; model selection accepts only a refreshed stable ID whose directory was confirmed by the Registry scanner. External pack/model tests fail closed.
- Added typed `VoiceCloneModel::licenseRevision`. Index manifests and the built-in Registry require `license-2026-08-13`; package manifests must use the same exact model, Adapter, license URL, and license revision. Generation checks that exact tuple. Tests cover unaccepted rejection, wrong-revision rejection, and successful generation after correct acceptance.
- Canceled and superseded request IDs are retired in the Worker client. Late responses are discarded rather than treated as correlation attacks. Selecting another model retires the old load request; only the current load request can change selected-model state.
- Cancel cleanup waits for the ordered cancel acknowledgement. If a Worker-held file prevents deletion, Controller gracefully shuts down then kills if necessary, retries cleanup, retains failed paths in visible pending-cleanup bookkeeping, and retries on termination, restart, shutdown, and destruction.
- IPC buffering is capped at 1 MiB for complete and unterminated frames. Exceeding it emits structured `frame-too-large` failures and kills only the Worker.
- Worker shutdown first sends the protocol `shutdown` operation with a grace period, then terminates/kills. A real Worker marker test proves the protocol path.
- Self-review moved the shutdown guard before the graceful request, registered that request through the normal correlation path, retained cleanup after failed Cancel acknowledgements, and changed frame reads to bounded chunks so an oversized peer cannot force `readAll()` allocation before the 1 MiB check.
- The production MODULE is loaded by the Task 4 Host using a real package manifest and DLL. The test validates compatible discovery, interface/controller/QML URL/protocol checks, shutdown, unload, and post-unload DLL rename.
- Refresh remains static. Runtime probing stays lazy at selected-model handshake/capabilities/load; no batch Worker startup or unapproved protocol operation was introduced.

### RED evidence

- Trusted-root/license RED failed to compile because `VoiceCloneModel::licenseRevision` and stable-ID-only `selectModel` did not exist.
- Exact package identity RED failed because an arbitrary Index license URL remained valid.
- Lifecycle RED failed to compile because pending-cleanup diagnostics did not exist; runtime RED then exposed canceled-output recreation before cancel acknowledgement.
- The real subprocess fixtures covered late cancel responses, stale load responses, locked output, a 1 MiB unterminated frame, symlink replacement with a Windows junction fallback, and graceful shutdown.

### GREEN evidence

Focused VS 2022 x64 runs:

```text
voice_clone_manifest_test .......... Passed
voice_clone_package_manager_test ... Passed
voice_clone_controller_test ........ Passed
```

Final Task 1-6 plus three-baseline matrix after self-review:

```text
audio_tools_end_to_end_test ......... Passed
voice_clone_manifest_test .......... Passed
voice_clone_capability_schema_test . Passed
voice_clone_worker_protocol_test ... Passed
voice_clone_host_controller_test ... Passed
voice_clone_package_manager_test ... Passed
voice_clone_controller_test ........ Passed
qml_format_converter_test .......... Passed
qml_filename_process_test .......... Passed
Result: 9/9, 100%, 21.90 s
```

The Controller binary also reported `11 passed, 0 failed, 0 skipped`; the Windows
symlink/junction substitution case executed rather than being skipped.

### Remaining boundary

- Handle-safe publication prevents filesystem substitution from changing the bytes accepted as generated output. It does not attempt to sandbox a legitimately installed, signed Worker process; that remains outside Task 6 and Task 8.

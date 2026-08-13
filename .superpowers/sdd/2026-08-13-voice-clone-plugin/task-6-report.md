# Task 6 Report: Native Controller and Worker Client

## Implementation

- Added the `agplayer_voice_clone_plugin` Qt `MODULE` target, implementing the Task 4 native interface without adding final QML, engine assets, or download manifests.
- Added a `QProcess + QLocalServer/QLocalSocket` Worker client with newline-delimited protocol frames, trusted Adapter launcher resolution, handshake-before-command enforcement, identity-triple validation, request correlation, per-request timeouts, crash handling, restart, and bounded shutdown.
- Supports trusted executable launchers and installed `pythonModule` packs via the manifest runtime root's `python.exe`; no user-provided command or free-form argument is accepted.
- Added Controller model selection, load/unload/generate/cancel lifecycle, live capability replacement, dynamic basic/advanced parameter lists, Schema validation, and hidden/unknown/invalid parameter rejection.
- Enforced the Task 5 exact IndexTTS acceptance record at generation time, before any Worker generation request.
- Model refresh merges built-in and valid user models, preserves `ready`/`local-unverified`, and reports invalid manifests as diagnostic model rows rather than discarding the whole refresh.
- Runtime probing is deliberately lazy: refresh never starts every model. The selected model is probed through hello/capabilities/load; a rejected load changes its row to `invalid` with the Adapter diagnostic.
- Output creation uses a Controller-created UUID request directory, `QIODevice::NewOnly` reservation, Worker-only `.part` output, pre/post canonical containment and reparse checks, and `QSaveFile` publication. Cancel, crash, OOM failure, timeout, and unsafe output remove temporary trees without traversing reparse points.

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
- Output containment is checked before reservation and after atomic publication, closing the Task 3 validation-to-open race for Controller-owned files.
- No production method accepts an arbitrary executable, shell command, output absolute path, or free-form process argument.
- `git diff --check` and the complete matrix are rerun after the report before commit.

## Concerns

- The Windows Adapter Pack contract assumes `runtime.root/python.exe` for `pythonModule` launchers. Task 8 must install that exact portable runtime layout and run its own real-engine smoke test.
- Refresh validates manifests and exposes readiness states immediately; the selected model receives the actual Adapter probe through handshake/capabilities/load. Probing every installed model would require a new protocol operation and is intentionally not invented outside Task 3's protocol.
- `QSaveFile` gives atomic Controller publication, but it is not an OS sandbox for a malicious trusted Worker process. Pack signature/trust and Worker distribution remain part of the installer/runtime supply-chain boundary.

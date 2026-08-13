# Task 7 Report: Reference-driven QML Plugin UI

## Implementation

- Added stable audio-tool IDs while preserving the legacy indices for the four existing pages (`audio-editor=0`, `format-converter=1`, `metadata-editor=2`, `filename-processor=3`). `voice-clone=4` is visually inserted after format conversion and appended to the legacy StackLayout.
- Added `VoiceCloneHostPage.qml`. The native host owns install/validation state; the Loader remains inactive until `pluginLoaded` is true and then injects the plugin Controller into the external QML workspace.
- Embedded the plugin QML under `qrc:/AgPlayer/VoiceClone/` while keeping the same source files directly loadable by the development/offscreen harness.
- Reproduced the 1672x942 reference composition with an actual model strip, three-column reference/text/output work area, Schema-driven parameter strip, and result panel using existing `Theme`, fonts, and icons.
- Exposed Registry descriptions, capabilities, license metadata, official project/Hugging Face/ModelScope links, refresh, and model-directory actions from the native Controller. QML contains no model-name, Adapter-name, or Worker-type branches.
- Rendered `bool`, `enum`, `int`, `double`, `string`, and `file` controls generically. Advanced settings only appear for a live advanced group; `visibleWhen` is evaluated from live values/defaults.
- Added the legal-authority confirmation entry for models whose Registry metadata requires acceptance. Generation remains tied to real Worker/model readiness, text/reference input, and Controller validation.
- Result actions now use real seams: atomic Controller save to a selected destination, handle-validated deletion limited to Controller-published outputs, and send-to-editor through the existing audio editor when no plugin override is present.
- Empty/running/error/result UI is state-driven. No placeholder waveform, fake progress, hard-coded file size, or fabricated output is painted.

## RED

VS2022 x64, `build/msvc-debug`:

- `qml_voice_clone_test` exited failed before implementation because the Host/Workspace types and stable tool-ID API did not exist.
- The Schema follow-up RED reported the conditional advanced control as visible (`actual true`, `expected false`) before generic `visibleWhen` evaluation.
- The managed-result RED failed to compile because `VoiceCloneController::saveResult` and `deleteResult` did not exist.
- The affected legacy controller test failed because it still treated tool index 4 as invalid, proving the old four-tool contract needed an explicit compatibility update.

## GREEN

Toolchain: Visual Studio 2022 Community 17.8, Hostx64/x64 `cl.exe`; build tree: `build/msvc-debug`.

Focused UI result:

```text
qml_voice_clone_test: 7 passed, 0 failed
qmllint: exit 0
```

Task 1-6 plus three legacy baselines and layout contract:

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
qml_voice_clone_test ................. Passed
audio_tools_layout_contract_test ..... Passed
Result: 11/11, 100%
```

## Reference correspondence

- Top: compact four-model Registry cards plus selected-model provider/status/description/capability/license/official-link strip.
- Middle: numbered reference-audio, clone-text, and output-state panels in three columns.
- Lower: numbered Schema parameter strip with conditional advanced disclosure, followed by a full-width real-result panel.
- Existing dark/light Theme tokens, radii, spacing, typography, keyboard focus, accessible labels, and repository icons are reused; no decorative asset was added.

## Self-review

- Host QML only displays native install state and cannot instantiate the external workspace before native plugin validation/load.
- The production module test opens the exact QRC workspace returned by `mainQmlUrl`; the QML harness separately verifies local-source loading and Controller injection.
- Save/delete accept only Controller-tracked published results. Saving reopens the exact file without following reparse points and commits with `QSaveFile`; deletion uses the opened Windows handle and never recursively traverses user paths.
- Unknown stable tool IDs and legacy index 5 are rejected; the four old pages retain their original integer identities and native-drop routing.
- `git diff --check` and QML lint are clean.

## Deferred acceptance

- Per explicit scope, Task 11 owns final 1672x942 screenshot capture, side-by-side visual comparison, and design-QA acceptance. This task leaves a buildable capture target but makes no screenshot-fidelity claim.
- Real model/runtime installation and engine smoke tests remain Tasks 8-10; the UI surfaces current absent/unverified/ready/error states rather than inventing progress or readiness.

## Fix round 1 — production reachability

### Changes

- Added the single production entry point `activateModel(stableId)`. It selects the Registry model, resolves an installed versioned Adapter Pack, starts the trusted Worker, waits for its real handshake, requests model load, and publishes the phases `selecting`, `starting-worker`, `loading-model`, `ready`, `needs-download`, or `error`.
- Known models without local model files, Adapter Packs, launchers, or runtimes now remain not-ready and report `needs-download`; no install-state label promotes `workerReady` or `modelLoaded`.
- Replaced the local Index checkbox gate with Controller-owned `licenseAcceptanceRequired`, current license metadata, and `acceptSelectedLicense()`. Persistence is restricted to the selected trusted identity tuple: `modelId + adapterId + licenseUrl + revision + acceptedAt`.
- Generation now rebuilds its parameter object from the current live Schema and submits only supported, currently visible controls. Reset omits hidden `visibleWhen` fields. Loader controls use live `Binding`s so capability reset/model switching updates displayed values.
- Removed the fake `sendResultToEditor` shortcut. The result action now selects stable tool ID `audio-editor` and opens the real WAV through `AudioEditorController`.
- Added an actual staged plugin root for QML integration: real host manifest, built DLL, Registry, installed test model, Adapter Pack, and protocol Worker. `VoiceCloneHostController` loads it through `QPluginLoader`; its returned `mainQmlUrl` is instantiated by Loader, bound to the plugin Controller, and exercised by a real model-card click.
- Extended the native module integration to instantiate the returned workspace through `QQmlComponent` + `Loader`, release the workspace/cache, call plugin shutdown/unload, and prove the DLL is no longer locked by renaming it.

### RED evidence

- The first Controller build failed on the intentionally missing `activationChanged`, `activateModel`, activation properties, license properties, and acceptance methods.
- The known-but-uninstalled built-in model test initially returned generic `error`; its RED assertion required `needs-download` and a not-ready message.
- The real dynamic-QML test initially failed with `realVoiceCloneHost is not defined`, proving the old local-source/fake-Host harness did not cross the production plugin boundary.
- The QML round added failing contracts for hidden-parameter payload exclusion, Controller-backed license acceptance, live delegate-value synchronization, and the real audio-editor fallback before their production paths were added.

### GREEN evidence

Toolchain and build tree remained VS2022 Hostx64/x64 and `build/msvc-debug`.

```text
AgPlayer + qml_audio_tools_test + voice_clone_controller_test: build exit 0
qml_voice_clone_test: 8 passed, 0 failed
voice_clone_controller_test: Passed (real Worker + real DLL/QML Loader/unload)
qmllint (all plugin QML): exit 0
git diff --check: exit 0
```

Final requested regression set:

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
qml_voice_clone_test ................. Passed
audio_tools_layout_contract_test ..... Passed
Result: 11/11, 100%
```

### Round 1 self-review

- QML never constructs an Adapter path, runtime path, launcher identity, or model-name branch; those remain inside native trusted-manifest resolution.
- Wrong/stale license identities fail without writing. Successful acceptance is re-read through the exact Task 5 identity before generation unlocks.
- The hidden conditional field is absent both after defaults reset and in the captured generation payload; unsupported field types are likewise omitted.
- Dynamic-QML coverage is no longer satisfied by reading QRC text: one test drives the real plugin/controller/Worker through the visible model card, and the native integration creates the real workspace before shutdown/unload.
- Screenshot comparison remains intentionally deferred to Task 11; this round changes reachability/state behavior, not the reference-visual acceptance scope.

### Round 1 review follow-up

- RED: the QML suite failed when it required a model switch to clear an active generation (`activeRequestId` stayed `request-1`) and when it searched for the actual rendered seed control (the Loader-only assertion could not find it).
- GREEN: model switching now clears the abandoned request state before Controller reactivation; the suite verifies `activeRequestId == ""`, `running == false`, and a subsequent generation can start.
- The real model-card test now clears an `activationChanged` spy and requires at least two new transitions plus the synchronous click state `starting-worker` / `modelLoaded == false` before waiting for the Worker handshake/load. Default auto-activation can no longer satisfy the click assertions.
- Parameter synchronization now assigns stable object names to instantiated Loader controls. The test changes and resets values through the live model, then checks actual `Switch.checked`, `ComboBox.currentIndex`, integer/double `SpinBox.value`, and string/file `TextField.text`; after a Schema/model reset it checks the new seed control's real value.
- Final QML result after review fixes: 9 passed, 0 failed.

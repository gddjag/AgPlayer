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

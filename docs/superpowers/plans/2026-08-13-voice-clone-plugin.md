# AG Player Voice Clone Plugin Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task; use `superpowers:test-driven-development` for every behavior change, `product-design:image-to-code` plus `product-design:audit` for the visual gate, `ponytail:ponytail` to keep the seam minimal, and Open Code Review before final handoff.

**Goal:** Deliver a separately packaged, on-demand Voice Clone plugin for AG Player with four approved official model entries, dynamic model/Adapter discovery, schema-driven basic and advanced parameters, verified downloads, isolated Workers, and the supplied 1672×942 UI.

**Architecture:** Keep only a lightweight plugin host and install shell in the player. Load the signed plugin DLL and its QML from the portable plugin root; the plugin owns Registry, package management, capability validation, Worker IPC, and generation state. Models are data-only manifests, while each distinct inference architecture is supplied by a trusted Adapter Pack with its own Runtime and Worker launcher. UI renders Adapter-provided Capability Schema and never branches on model names or Worker implementations.

**Tech Stack:** C++17, Qt 6.7 Core/Gui/Qml/Quick/QuickControls2/Network, QPluginLoader, QProcess, QLocalSocket/QLocalServer, JSON, QML, CMake/Ninja/MSVC, Qt Test/Quick Test, PowerShell packaging scripts.

**Global Constraints:** Preserve playback core behavior; do not package an AG Player EXE; do not bundle model weights; exclude VibeVoice everywhere; never execute code from a user model directory; use only official model repositories; IndexTTS requires explicit license acceptance; no fake download size/progress/readiness; no release-complete claim before all four real inference E2E runs and visual QA pass.

---

## Task 1: Lock the extensible contracts in tests

**Files:**
- Create: `tests/qt/voice_clone_manifest_test.cpp`
- Create: `tests/qt/voice_clone_capability_schema_test.cpp`
- Create: `tests/fixtures/voice-clone/models/local-qwen/agplayer-model.json`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write a failing Registry contract test**

Test that the built-in Registry has exactly the four approved stable IDs, no VibeVoice text, valid HTTPS official URLs, correct license gates, and valid Adapter/runtime references.

- [ ] **Step 2: Write failing local-model discovery tests**

Cover a valid user Manifest using the Qwen Adapter, duplicate IDs, unknown Adapter, missing required files, absolute paths, `..`, link/junction escape, and missing hashes producing `local-unverified` instead of `ready`.

- [ ] **Step 3: Write failing Capability Schema tests**

Cover `basic` and `advanced` groups, bool/enum/int/double/string/file controls, range/default validation, conditional visibility, protocol mismatch, and rejection of unknown submitted parameters.

- [ ] **Step 4: Run RED**

Run:

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --target voice_clone_manifest_test voice_clone_capability_schema_test
ctest --test-dir build/debug -C Debug -R "voice_clone_(manifest|capability_schema)_test" --output-on-failure
```

Expected: build fails because the production interfaces do not exist.

## Task 2: Implement the data-only Registry and local model scanner

**Files:**
- Create: `qt/src/voice_clone/voice_clone_manifest.hpp`
- Create: `qt/src/voice_clone/voice_clone_manifest.cpp`
- Create: `qt/src/voice_clone/voice_clone_registry.hpp`
- Create: `qt/src/voice_clone/voice_clone_registry.cpp`
- Create: `plugins/voice-clone/registry/models.json`
- Create: `plugins/voice-clone/config/agplayer-model.example.json`
- Modify: `qt/CMakeLists.txt`

- [ ] **Step 1: Define the smallest typed Manifest model**

Use stable IDs and value types for model identity, source links, license, runtime, Adapter, required files, reference-audio rules, and Capability Schema. Keep JSON parsing in one module.

- [ ] **Step 2: Parse the built-in four-model Registry**

Use these official model IDs only:

```text
Qwen/Qwen3-TTS-12Hz-0.6B-Base
Qwen/Qwen3-TTS-12Hz-1.7B-Base
IndexTeam/IndexTTS-2.5
FunAudioLLM/Fun-CosyVoice3-0.5B-2512
```

Keep provider, project/Hugging Face/ModelScope/license links, product description, capability preview, revision field, Adapter ID, and Runtime ID in JSON rather than QML.

- [ ] **Step 3: Implement safe layered discovery**

Scan only `models/voice-clone/*/*/agplayer-model.json`; canonicalize each declared relative path, reject filesystem escape and reparse-point traversal, verify required files, merge user models without replacing built-in IDs, and return structured diagnostics.

- [ ] **Step 4: Run GREEN and refactor**

Run the two focused tests until both pass, then run `git diff --check`.

## Task 3: Add Adapter and Worker protocol contracts

**Files:**
- Create: `qt/src/voice_clone/voice_clone_adapter_manifest.hpp`
- Create: `qt/src/voice_clone/voice_clone_adapter_manifest.cpp`
- Create: `qt/src/voice_clone/voice_clone_worker_protocol.hpp`
- Create: `qt/src/voice_clone/voice_clone_worker_protocol.cpp`
- Create: `tests/qt/voice_clone_worker_protocol_test.cpp`
- Create: `plugins/voice-clone/adapters/qwen/adapter.json`
- Create: `plugins/voice-clone/adapters/indextts25/adapter.json`
- Create: `plugins/voice-clone/adapters/cosyvoice3/adapter.json`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write protocol RED tests**

Test `adapterId + adapterVersion + protocolVersion`, handshake, request IDs, stages, indeterminate progress, capability response, generation request, cancellation, structured errors, and rejection of unlisted launchers/absolute output paths.

- [ ] **Step 2: Implement protocol JSON codecs and validation**

Keep transport-independent parsing in one module. Permit different Adapter launchers and Runtime roots while exposing the same operations: `hello`, `capabilities`, `load`, `generate`, `cancel`, `unload`, `shutdown`.

- [ ] **Step 3: Define each approved Adapter manifest**

Qwen shares a runtime and launcher; IndexTTS and CosyVoice use isolated runtimes and launchers. Declare only relative executable/module paths supplied by the trusted Adapter Pack.

- [ ] **Step 4: Run focused GREEN**

```powershell
cmake --build --preset windows-msvc-debug --target voice_clone_worker_protocol_test
ctest --test-dir build/debug -C Debug -R voice_clone_worker_protocol_test --output-on-failure
```

## Task 4: Build the lightweight host and native plugin seam

**Files:**
- Create: `qt/src/plugins/voice_clone_plugin_interface.hpp`
- Create: `qt/src/voice_clone/voice_clone_host_controller.hpp`
- Create: `qt/src/voice_clone/voice_clone_host_controller.cpp`
- Create: `tests/qt/voice_clone_host_controller_test.cpp`
- Modify: `qt/src/qml_registration.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write host lifecycle RED tests**

Cover absent, invalid, compatible, loaded, update-available, and failed states; verify the absent state does not scan Registry/models, start a Worker, or load Python/runtime libraries.

- [ ] **Step 2: Define a narrow Qt plugin interface**

Expose metadata, controller QObject, main QML URL, protocol version, and shutdown. Do not expose Registry or engine-specific methods through the host.

- [ ] **Step 3: Implement host discovery and QPluginLoader lifecycle**

Resolve the plugin root relative to the portable app root or `AGPLAYER_VOICE_CLONE_ROOT` in tests. Validate manifest/platform/minimum-player version before loading the DLL; unload cleanly when the tools window closes.

- [ ] **Step 4: Register the lightweight singleton without expanding every harness constructor**

Use one application-lifetime host instance registered by `register_agplayer_qml_types`; preserve all existing call signatures.

- [ ] **Step 5: Run host and affected QML harness tests**

```powershell
cmake --build --preset windows-msvc-debug --target voice_clone_host_controller_test qml_audio_tools_test
ctest --test-dir build/debug -C Debug -R "voice_clone_host_controller_test|qml_(format_converter|filename_process)_test" --output-on-failure
```

## Task 5: Implement resumable, verified package installation

**Files:**
- Create: `qt/src/voice_clone/voice_clone_package_manifest.hpp`
- Create: `qt/src/voice_clone/voice_clone_package_manifest.cpp`
- Create: `qt/src/voice_clone/voice_clone_package_manager.hpp`
- Create: `qt/src/voice_clone/voice_clone_package_manager.cpp`
- Create: `tests/qt/voice_clone_package_manager_test.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write package manager RED tests against a local HTTP fixture**

Test metadata resolution, real content length, `.part` resume with HTTP Range, pause, cancel, retry, disk-space preflight, SHA-256 mismatch, path traversal, duplicated targets, atomic commit, and rollback to the previous installed version.

- [ ] **Step 2: Implement the explicit state machine**

Use Qt Network and Qt file APIs. Persist resume metadata; never report a percentage without known transferred and total bytes. Extract only whitelist-declared relative files into a staging directory, then atomically rename.

- [ ] **Step 3: Add license gating**

Block IndexTTS model download until the exact license URL/revision has an acceptance timestamp. Qwen and CosyVoice proceed without the custom-license dialog but still expose their license links.

- [ ] **Step 4: Run focused tests and existing source-safety checks**

```powershell
cmake --build --preset windows-msvc-debug --target voice_clone_package_manager_test
ctest --test-dir build/debug -C Debug -R "voice_clone_package_manager_test|source_encoding_test" --output-on-failure
```

## Task 6: Implement the native plugin Controller and Worker client

**Files:**
- Create: `plugins/voice-clone/CMakeLists.txt`
- Create: `plugins/voice-clone/src/voice_clone_plugin.hpp`
- Create: `plugins/voice-clone/src/voice_clone_plugin.cpp`
- Create: `plugins/voice-clone/src/voice_clone_controller.hpp`
- Create: `plugins/voice-clone/src/voice_clone_controller.cpp`
- Create: `plugins/voice-clone/src/voice_clone_worker_client.hpp`
- Create: `plugins/voice-clone/src/voice_clone_worker_client.cpp`
- Create: `tests/qt/voice_clone_controller_test.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write Controller/Worker lifecycle RED tests**

Use a deterministic test Worker process and cover handshake, runtime capability replacement, model load, generate, cancel, crash, OOM, timeout, restart, unload, request correlation, temporary WAV cleanup, and preserved player process health.

- [ ] **Step 2: Implement QProcess plus QLocalSocket client**

Select the launcher from a validated Adapter manifest, pass only constrained roots and the socket name, require a protocol handshake before any model command, and terminate a failed Worker without touching playback state.

- [ ] **Step 3: Implement dynamic parameter submission**

Expose `basicParameters`, `advancedParameters`, and `advancedSettingsAvailable` from the live Schema. Validate types/ranges/enums and omit hidden/unsupported fields before sending generation requests.

- [ ] **Step 4: Implement local model refresh**

Expose “刷新模型” and “打开模型目录”. Refresh merges built-in and user manifests, asks known Adapters to probe local models, and reports invalid/local-unverified/ready states without changing UI code.

- [ ] **Step 5: Run focused tests**

```powershell
cmake --build --preset windows-msvc-debug --target voice_clone_controller_test
ctest --test-dir build/debug -C Debug -R voice_clone_controller_test --output-on-failure
```

## Task 7: Implement the reference-driven QML plugin UI

**Files:**
- Create: `app/qml/AgPlayer/components/tools/VoiceCloneHostPage.qml`
- Create: `plugins/voice-clone/qml/VoiceCloneWorkspace.qml`
- Create: `plugins/voice-clone/qml/VoiceCloneModelBar.qml`
- Create: `plugins/voice-clone/qml/VoiceCloneReferencePanel.qml`
- Create: `plugins/voice-clone/qml/VoiceCloneTextPanel.qml`
- Create: `plugins/voice-clone/qml/VoiceCloneOutputPanel.qml`
- Create: `plugins/voice-clone/qml/VoiceCloneParameterPanel.qml`
- Create: `plugins/voice-clone/qml/VoiceCloneResultPanel.qml`
- Create: `tests/qml/tst_voice_clone.qml`
- Modify: `app/qml/AgPlayer/components/tools/ToolSidebar.qml`
- Modify: `app/qml/AgPlayer/AudioToolsWindow.qml`
- Modify: `app/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write QML RED tests**

Test stable tool ID selection, absent-plugin install state, loaded workspace, four Registry cards, model info/official links, refresh/open-directory actions, dynamic basic fields, conditional advanced-settings disclosure, IndexTTS license gate, generation enablement, cancel, empty result, save, delete, and send-to-editor.

- [ ] **Step 2: Add the host page and stable tool IDs**

Insert 人声克隆 in the top navigation while preserving existing tools. The host page shows the native install state and loads the external plugin QML only after successful plugin validation.

- [ ] **Step 3: Reproduce the supplied 1672×942 composition**

Implement the model strip, three-column main area, parameter strip, and result panel with existing Theme, fonts, and icons. Use actual data states; do not paint fake waveform, progress, file sizes, or results.

- [ ] **Step 4: Render Schema controls generically**

Choose delegates by the declared control type and group. Show “高级设置” only when the live Schema contains advanced fields; expanding it renders the current Adapter's options with labels and help text.

- [ ] **Step 5: Run QML GREEN**

```powershell
cmake --build --preset windows-msvc-debug --target qml_audio_tools_test
ctest --test-dir build/debug -C Debug -R "qml_voice_clone_test|audio_tools_layout_contract_test" --output-on-failure
```

## Task 8: Create the three real Adapter Workers and Runtime recipes

**Files:**
- Create: `plugins/voice-clone/worker/common/agvoice_protocol.py`
- Create: `plugins/voice-clone/worker/qwen/worker.py`
- Create: `plugins/voice-clone/worker/indextts25/worker.py`
- Create: `plugins/voice-clone/worker/cosyvoice3/worker.py`
- Create: `plugins/voice-clone/runtime/qwen.lock.json`
- Create: `plugins/voice-clone/runtime/indextts25.lock.json`
- Create: `plugins/voice-clone/runtime/cosyvoice3.lock.json`
- Create: `scripts/voice-clone/build-runtime-pack.ps1`
- Create: `tests/scripts/voice_clone_worker_contract_test.ps1`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write Worker contract RED tests**

Start each Worker in a lightweight `--contract-test` mode that imports no model framework, performs the real socket handshake, returns its static capability baseline, validates parameters, and shuts down cleanly.

- [ ] **Step 2: Implement the shared protocol loop**

Share framing, request correlation, cancellation checks, error mapping, path validation, and event encoding only. Keep model imports and generation calls inside each Adapter Worker.

- [ ] **Step 3: Implement engine-specific calls against pinned official revisions**

Qwen supports both approved Base sizes through one Worker; IndexTTS-2.5 and CosyVoice3 use their own official APIs and Runtime locks. No Worker installs packages or downloads missing dependencies during inference.

- [ ] **Step 4: Build reproducible Runtime Pack recipes**

The script requires an explicit output root and official source cache, verifies every locked artifact, emits a complete package Manifest and third-party notices, and fails when a dependency or license is unresolved.

- [ ] **Step 5: Run the protocol contract suite**

```powershell
ctest --test-dir build/debug -C Debug -R voice_clone_worker_contract_test --output-on-failure
```

## Task 9: Add official model download manifests and user-facing documentation

**Files:**
- Create: `plugins/voice-clone/registry/downloads/qwen3-tts-0.6b.json`
- Create: `plugins/voice-clone/registry/downloads/qwen3-tts-1.7b.json`
- Create: `plugins/voice-clone/registry/downloads/indextts-2.5.json`
- Create: `plugins/voice-clone/registry/downloads/fun-cosyvoice3.json`
- Create: `docs/voice-clone/README.zh-CN.md`
- Create: `docs/voice-clone/ADDING_MODELS.zh-CN.md`
- Create: `tests/scripts/voice_clone_registry_contract_test.ps1`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Pin and validate official sources**

Each download manifest points to the model author's official Hugging Face or ModelScope repository, an explicit revision, complete file graph, license URL, and expected hashes. The validator refuses community mirrors and unresolved revisions.

- [ ] **Step 2: Document model descriptions and functions accurately**

Include official project, repository, license, model positioning, supported features, reference-audio expectations, hardware notes limited to measured results, and the IndexTTS custom-license warning.

- [ ] **Step 3: Document no-UI model extension**

Provide a copyable `agplayer-model.json` example, folder layout, refresh flow, local-unverified meaning, supported Adapter IDs, and the separate signed Adapter Pack route for new inference architectures.

- [ ] **Step 4: Run the Registry contract test**

```powershell
ctest --test-dir build/debug -C Debug -R voice_clone_registry_contract_test --output-on-failure
```

## Task 10: Package the standalone plugin for later website upload

**Files:**
- Create: `scripts/voice-clone/package-plugin.ps1`
- Create: `scripts/voice-clone/verify-plugin-package.ps1`
- Create: `plugins/voice-clone/config/plugin-feed.example.json`
- Create: `tests/scripts/voice_clone_package_contract_test.ps1`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write package contract RED tests**

Require the stable ZIP root, platform/architecture/version metadata, SHA-256 list, licenses, QML, plugin DLL, Registry, Adapter manifests, Worker launchers, user Manifest template, and absence of model weights, secrets, absolute development paths, `.git`, caches, and VibeVoice.

- [ ] **Step 2: Implement whitelist-only packaging**

Stage files into a new explicit build directory, hash them, write the package Manifest, create `AGPlayer-VoiceClonePlugin-<version>-windows-x64.zip`, and verify a clean extraction. Do not sign falsely; emit `signatureStatus: unsigned-test` until the release signing key is supplied.

- [ ] **Step 3: Parameterize the future website feed**

The feed example contains package URL, manifest URL, minimum player version, and signature metadata. The publish step requires an explicit real HTTPS base URL and refuses to generate a release feed with an empty or non-HTTPS address.

- [ ] **Step 4: Build and verify the test ZIP**

```powershell
cmake --build --preset windows-msvc-release --target agplayer_voice_clone_plugin
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/voice-clone/package-plugin.ps1 -BuildRoot build/release -OutputRoot build/artifacts/voice-clone -Version 1.0.0
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/voice-clone/verify-plugin-package.ps1 -PackagePath build/artifacts/voice-clone/AGPlayer-VoiceClonePlugin-1.0.0-windows-x64.zip
```

## Task 11: Visual, review, and real inference gates

**Files:**
- Modify: `app/main.cpp`
- Modify: `design-qa.md`
- Create: `docs/qa/2026-08-13-voice-clone-real-inference.md`

- [ ] **Step 1: Add a QA-only navigation selector without changing production defaults**

Extend the existing `--qa-tool` behavior to select the stable voice-clone tool ID and capture the 1672×942 tools window.

- [ ] **Step 2: Build and capture the implementation**

```powershell
cmake --build --preset windows-msvc-debug --target AgPlayer
build/debug/app/AgPlayer.exe --qa-tool 4 --qa-screenshot-tools build/qa/voice-clone-1672x942.png
```

- [ ] **Step 3: Run Product Design comparison**

Open the supplied `C:\Users\Administrator\Desktop\音视频播放器\未开发\人声克隆.png` and `build/qa/voice-clone-1672x942.png` side-by-side. Fix every P0/P1/P2 layout, typography, color, icon, copy, clipping, and state mismatch; repeat at 1280×720, 1920×1080, 4K and Windows scaling targets. Mark `design-qa.md` passed only with screenshot evidence.

- [ ] **Step 4: Run full automated verification**

```powershell
cmake --build --preset windows-msvc-debug
ctest --test-dir build/debug -C Debug --output-on-failure
git diff --check
```

- [ ] **Step 5: Run Open Code Review and Ponytail review**

Review all changes against the approved spec and repo standards, fix high/medium findings, then rerun focused and full tests. Remove speculative abstractions or duplicated mechanisms found by Ponytail without weakening the plugin boundary.

- [ ] **Step 6: Run the four real model E2E cases**

For each approved model, use an authorized short reference WAV and short text inside AG Player. Record official revision, Adapter/runtime version, device, elapsed time, peak RAM/VRAM, output properties, playback/save/send-to-editor results, and any audible issue. A missing model/runtime/hardware run remains explicitly blocked and prevents the formal-completion claim.

## Plan self-review

- Spec coverage: official model links/licenses, VibeVoice exclusion, on-demand plugin install, separate ZIP, model/runtime separation, local user models, Adapter-specific Workers, dynamic advanced settings, Worker IPC, downloads, UI, packaging, visual QA, and real E2E are each mapped to a task.
- Placeholder scan: no `TODO`, `TBD`, fake URL, fake hash, fake size, fake progress, or simulated inference is permitted in production artifacts.
- Type consistency: model `stableId`, `adapterId`, `adapterVersion`, `runtimeId`, `revision`, `protocolVersion`, request ID, parameter keys, and package IDs remain stable strings across JSON, C++, QML, IPC, tests, and packaging.
- Scope discipline: player host is lightweight; engine differences remain in Adapter Workers; model folders are data-only; QML is schema-driven; playback core and player EXE packaging remain out of scope.

# Task 3 Report: Adapter Manifest and Unified Worker Protocol

## Implementation

- Added a strict Adapter Pack manifest parser keyed by `adapterId + adapterVersion + protocolVersion`.
- Added trusted launcher resolution. Only manifest-listed launcher IDs and relative paths inside the supplied Adapter Pack root are accepted.
- Added transport-independent JSON encode/decode and validation for `hello`, `capabilities`, `load`, `generate`, `cancel`, `unload`, and `shutdown`.
- Added request IDs, progress stages, indeterminate progress (`null`), capability responses, generation/cancel payloads, and structured errors.
- Generation requests require an explicit absolute `outputRoot`; their `outputPath` must be relative and resolve inside that root.
- Added Qwen, IndexTTS 2.5, and CosyVoice 3 adapter manifests. Qwen declares its shared runtime; IndexTTS and CosyVoice declare separate isolated runtime and launcher roots.
- Did not add process/socket transport, UI, or package download behavior.

## RED / GREEN

- RED: after adding the protocol test target first, the VS2022 x64 build failed at `voice_clone_adapter_manifest.hpp: No such file or directory`, proving the new contract was absent.
- GREEN: the focused target built and `voice_clone_worker_protocol_test` passed after implementing the codec, validation, and manifests.
- During GREEN, the cancel data-row fixture initially omitted its required `targetRequestId`; the protocol correctly rejected it. The fixture was corrected without weakening validation.

## Files

- `qt/src/voice_clone/voice_clone_adapter_manifest.hpp`
- `qt/src/voice_clone/voice_clone_adapter_manifest.cpp`
- `qt/src/voice_clone/voice_clone_worker_protocol.hpp`
- `qt/src/voice_clone/voice_clone_worker_protocol.cpp`
- `tests/qt/voice_clone_worker_protocol_test.cpp`
- `plugins/voice-clone/adapters/qwen/adapter.json`
- `plugins/voice-clone/adapters/indextts25/adapter.json`
- `plugins/voice-clone/adapters/cosyvoice3/adapter.json`
- `qt/CMakeLists.txt`
- `tests/CMakeLists.txt`

## Verification

VS2022 Community x64 developer environment, `build/msvc-debug`:

- `voice_clone_worker_protocol_test`: passed (1/1)
- `voice_clone_manifest_test`: passed (1/1)
- `voice_clone_capability_schema_test`: passed (1/1)
- `audio_tools_end_to_end_test`: passed (1/1)
- `format_conversion_plan_test`: passed (1/1)
- `format_conversion_task_model_test`: passed (1/1)

## Self-review

- No model-name dispatch exists in production code; routing identity is the adapter/version/protocol triple.
- No `QProcess`, `QLocalSocket`, UI, or downloader code was introduced.
- Unknown launcher IDs, absolute/traversal launcher paths, absent output roots, and absolute/traversal output paths are rejected.
- The codec remains independent of any worker launcher or transport implementation.

## Concerns / Follow-up

- Protocol version support is intentionally limited to version 1; negotiation or migrations need a later contract change.
- Task 6 must re-check the selected manifest identity before launch and preserve output-root safety when it actually creates files, including race/symlink handling at the filesystem boundary.
- The manifests declare pack-relative runtime/worker locations; actual runtime and worker assets remain the responsibility of later Adapter Pack installation work.

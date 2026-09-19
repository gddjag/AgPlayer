# Six-track recording service verification

Date: 2026-09-15. Worktree: `.worktrees/pc-six-track-editor`. No release package or hardware acceptance is claimed.

## Implemented boundary

- `qt/src/audio_editor/editor_recording_service.hpp/.cpp`: QObject recording service with input-device IDs/names, selected and actual device, Idle/Recording/Paused/Stopping/Error, captured frame count, errors, validated completion and recoverable partial paths.
- The existing miniaudio implementation is reused. No QtMultimedia dependency or second miniaudio implementation was introduced. Device/context initialization, enumeration, WAV conversion and file I/O run off the GUI thread.
- Capture callbacks only copy interleaved float PCM into a preallocated two-second single-producer/single-consumer ring and set atomics. They do not allocate, block, call Qt, or write files. Writer-side conversion produces little-endian PCM24 WAV, including odd-sized RIFF padding.
- Pause discards incoming frames and excludes waiting time. Stop quiesces callbacks, drains the ring, finalizes/validates the WAV and emits success only for a non-empty valid file. Overflow, route/device failure and write failure report errors; valid partial audio is retained. Cancelling removes only the exclusively created session file. Existing output paths cannot be overwritten.
- `IEditorCaptureBackend` and guarded `setCaptureFactoryForTesting()` replace only the hardware boundary. Controller integration owns target-track/cursor freezing, playback ownership, insertion/undo and portable media save.

## macOS scope

- Added `NSMicrophoneUsageDescription` and QtCore `QMicrophonePermission` request handling (available in supported Qt 6.7/6.8; no QtMultimedia).
- macOS packaging requires only `permissions/libqdarwinmicrophonepermission.dylib` from that plugin family, excludes the unrelated camera permission plugin, and applies `com.apple.security.device.audio-input` only to the main app entitlement set.
- Primary references: [Qt microphone permission](https://doc.qt.io/qt-6/qmicrophonepermission.html), [Qt permissions](https://doc.qt.io/qt-6/permissions.html), [Apple audio-input entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.device.audio-input).

## Actual verification

1. `editor_recording_service_test`: initial incomplete-service RED was observed. A separate regression then demonstrated that immediate stop could hide an already-reported capture overflow; the stop/drain boundary was fixed before the final run.
2. Final Windows Qt 6.7 Release run: **9 passed, 0 failed** (7 behavior tests plus init/cleanup), recorded in `build/release/tests/recording-green-final.txt`. Real ring, PCM conversion, writer and service states are tested against an injected microphone boundary. Cases cover exact PCM24 bytes, pause exclusion, non-overwrite, disconnect partial preservation, own-file cancellation, empty/open failure, overflow, and stop/overflow race.
3. `tests/scripts/test_package_macos.py`: **30 tests run, 28 passed, 2 skipped** on Windows (symlink-platform restrictions). New permission-plugin and entitlement tests were observed failing before the packaging changes and passing afterwards. These are packaging-logic fixtures, not a macOS package/signing run.
4. Parent-owned controller recording integration separately reported **5 passed, 0 failed**, including real service-to-document insertion/undo/portable save and failure paths. See its test/report for the authoritative integration evidence.

## Remaining platform acceptance

Physical input-device capture/list switching, unplug/permission UX on real hardware, macOS build/TCC first-run authorization, macOS signed package contents, and listening tests remain unverified. No live monitoring or fake live waveform is implemented. This report does not establish full-app or release readiness.

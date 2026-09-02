# AgPlayer Reference Quality Repair Acceptance

Date: 2026-09-03

## Accepted scope

- Shared classic, integrated, rolling, and mini-player actions, icon sizing, theme popup placement, and waveform progress presentation.
- Classic `863 x 266` geometry, shared responsive list profiles, tag/details layouts, navigation icons, and file/folder drop routing.
- Compact settings, color picker, file information, lyrics host behavior, 18-band EQ, and audio-tool navigation/layout.
- Audio editor timeline/selection/export behavior, separation model discovery/download/cancel/preview behavior, and metadata write safety.
- Shared controllers and theme tokens remain authoritative; immersive-only renderer/reactor/shader work is intentionally excluded by the user.

## Fresh verification

- Release build: `cmake --build build/release --config Release --target AgPlayer all_qmllint` passed.
- QML lint: passed; one informational `WaveformSession.qml` import warning remains because the runtime singleton import is required.
- Focused metadata test: `metadata_writer_test` passed after the final Windows CAS/recovery changes.
- Full Release CTest: 160/160 passed in 392.17 seconds using `--repeat until-pass:2` for the repository's known timing-sensitive audio/GPU tests; no retry was required in the final run.
- Formal UI matrix: 33/33 passed across dark, light, and system themes for playback, mini, settings, list, details, and all five audio-tool pages.
- Responsive audio-tool evidence: 27 captures across three themes, three page types, and `1672x941`, `1280x720`, and `880x560`.
- Release WAV smoke: passed with `sine-440hz.wav` at the required `863 x 266` player geometry.
- Independent code review: final metadata partial-replacement/CAS commit passed with no P0-P2 findings.

## Evidence

- UI matrix: `build/qa/final-repair-matrix-v6/`
- UI matrix result table: `build/qa/final-repair-matrix-v6/matrix.csv`
- Audio-tool responsive captures: `build/qa/audio-tools-repair/`
- Release playback smoke: `build/qa/main-smoke-final/main.png`
- Release playback log: `build/qa/main-smoke-final/agplayer.log`
- Approved design: `docs/superpowers/specs/2026-09-02-agplayer-reference-quality-repair-design.md`
- Implementation plan: `docs/superpowers/plans/2026-09-02-agplayer-reference-quality-repair.md`

## Remaining boundaries

- Portable POSIX locks are advisory; the implementation narrows the non-cooperative writer window with identity and SHA checks but cannot provide the Windows `ReplaceFileW` recovery guarantee on every POSIX filesystem.
- Arbitrary user-provided ONNX inference was not exercised with every third-party model; discovery and worker validation use the constrained supported-model contract.
- Free lyric and mirror endpoints remain dependent on external network availability.
- Native Computer Use was unavailable in this environment, so final manual mouse/drag/taskbar/hardware interaction was not performed; repository Release QA, screenshots, playback smoke, and Windows-native automated tests were used instead.
- Packaging was not part of this confirmed repair scope.

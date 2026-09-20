# macOS Universal Windows-test synchronization

- User requests committing the complete source matching the latest Windows test package and producing an internal macOS DMG on Desktop; no online publication.
- Windows package: `AgPlayer-Setup-1.0.4-test-20260920-010149-x64.exe`, 36,616,935 bytes, SHA-256 `7135b9ea55d0817a04002fec41415d84893cd24d61f0df0ba089ec23616616f9`.
- The Desktop installer matches `pc-six-track-editor/build/installer/AgPlayer-Setup-1.0.4-x64.exe`. Its CMake cache identifies that worktree as the source directory. The 47 tracked changes predate the installer and were copied with before/after hashes; unrelated HarmonyOS work was not copied.
- Baseline `eda6f20e` plus complete Windows follow-up snapshot `d6943fc0`; previous mini title fix `a616d3c3` applied as `7d3c7243`.
- Keep version 1.0.4, macOS minimum 13.0, one Universal arm64+x86_64 DMG. Preserve internal ad-hoc signing with no claim of Developer ID signing or notarization.
- Extend cloud checks to six-track document, peak pyramid, controller, recording, waveform and QML workflows, plus mini title centering. Physical microphone permissions, hardware listening and Finder interaction require manual Mac acceptance.
- Final build/package/installed verification results are recorded after completion; synchronization itself is not acceptance.

## Resumed source snapshot

- After the user paused for mainline fixes and requested packaging again, synchronized all 51 tracked Windows worktree changes. Nine files differ from the earlier snapshot, covering tools-window geometry, six-track responsive layout and the violet-heart palette.
- These follow-up fixes postdate the 01:00 Windows installer; this Mac build uses the updated source snapshot, not a claim of byte-exact source provenance for that older installer. The source worktree remains untouched, and copied files were checked using before/after SHA-256 hashes.
- Cloud coverage also includes audio-tools responsive layout and immersive-theme palette tests.

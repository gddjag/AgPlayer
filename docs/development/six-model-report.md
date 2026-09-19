# Six-track model and persistence implementation

Scope: core event/document/timeline commands and Qt project serialization, plus the subsequently delegated controller test adaptation, in `codex/pc-six-track-editor`. No mixer, controller production, QML, or recording backend ownership.

## Contract

- `AudioEvent.trackIndex` is 0..5. `timelineSampleRate` separates project frames from source frames. `audibleFrames` returns project frames; `projectOffsetAt(event, absoluteSourceFrame)` and `sourceOffsetAt(event, projectOffset)` map coordinates using rounded integer rational conversion. Durations use converted absolute source boundaries to avoid cumulative split rounding.
- `TimelineSnapshot` carries six fixed `TrackState { muted, gain }` entries, `sampleRate`, `channels`, and `legacyMasterGain`. New documents default to stereo and fix the rate from their first source; empty recording-first documents can use `setProjectFormat(48000, 2)`.
- Same-track overlap rejects the entire edit; cross-track overlap is valid. Total length is the maximum event end across all tracks. Limits are 4096 events/sources, 128 envelope points per event, 65536 total envelope points, without a one-hour cap.
- Document operations include `moveEvent(id, frame, track)`, `insertSource(source, frame, track)`, `setTrackMuted`, `setTrackGain`, `pasteAt(frame, track)`, `cropEventToSelection(id)`, and `replaceEventWithSource(id, source, start, end)`. Replacement bounds are absolute project frames; replacement PCM must be raw (without baked event/track gain) and have the same mapped duration.
- Track edits and clear are undoable transactions. Clear resets track controls and legacy gain while retaining project format. Copy/paste retains existing PC ripple behavior only on involved tracks; unrelated tracks are neither split nor moved.

## Persistence and generated media

- Saves schema 3; loads schemas 1 and 2 onto track index 0 with their original project rate/channel format. Legacy single-track dB gain is migrated into `legacyMasterGain`, including values above 200%, and legacy controller track fields are reset on migration to avoid applying the gain twice.
- `fromSnapshot` and relink preserve tracks and project format. Callers rebuilding media references must preserve the whole snapshot, not rebuild just `events`.
- `ProjectSaveRequest.generatedMediaPaths` explicitly identifies session-owned generated media. Referenced files are streamed through a temporary file into `<project-name>.media`, with content hashes used for reuse, and are renamed before the project JSON's atomic commit. Failed saves remove only newly created media; existing project/media and live document/history/source references remain intact.
- `ProjectSourceRecord.generatedMedia` and the corresponding schema-3 source flag retain generated-media provenance across reopening, so Save As copies archived generated files into the new project's media directory without granting permission to delete the older archive.
- Save rejects a destination that aliases any source file, checking filesystem identity as well as path spelling. A real hard-link fixture verifies both source and alias remain byte-for-byte unchanged.

## Verification

- Latest completed core run after the integrated rebuild: `audio_document_test` 43 passed; `event_timeline_test` 6 passed; `timeline_edit_command_test` 12 passed. Covers overlap, cross-track undo, mixed rates, overflow, duration, envelope budget, clip replacement/crop, and paste isolation. Logs: `build/release/six-model-test.txt`, `six-timeline-test.txt`, `six-command-test.txt`.
- Final project run: `project_document_test` 35 passed, 0 failed, 1 skipped. Covers schema migration and gain, owned-media survival/reuse, reopened Save As, failed-copy/project-commit rollback, missing generated media, and source-file alias protection. Log: `build/release/six-project-test.txt`.
- The skipped project test requires directory symlink creation, unavailable in the current Windows environment.
- Shared build's MSVC show-includes dependency prefix issue was reported to the parent and repaired. Parent rebuilt the stale ABI-sensitive objects before the project tests above.
- No real-device audio, macOS hardware, GUI, package, or release acceptance is claimed by this model task.

## Controller test adaptation

- Preserved the parent's batch import/gain history test. Added cross-track move/undo and selected-track paste/delete isolation assertions.
- Updated old single-track expectations to explicit clip selection, six-track batch drop, stereo new projects, retained session TimePitch on clear, schema 3, position-preserving clip crop, and generated-media archival on save. Range-based gap fixtures now perform actual split/select/delete, retaining non-ripple and waveform/cache assertions.
- Deferred waveform requests require event-loop-aware semaphore waiting. `QTRY_VERIFY` waits on `available()`, followed by a single `tryAcquire()`, because Qt's QTRY macros evaluate their condition repeatedly.
- Controller suite completed with 133 passed and one outdated offline-source fixture missing its explicit clip selection. After correction and the integrated rebuild, the focused offline-source case passed (3 QtTest passes including setup/cleanup); the entire controller suite was not repeated by this agent after that one fixture correction. Logs: `build/release/six-controller-updated.txt`, `six-controller-offline-focused.txt`. The actual export-action publication regression discovered during this pass was reported to and fixed by the parent in controller production code.
- No files were staged or committed; integration remains with the parent.

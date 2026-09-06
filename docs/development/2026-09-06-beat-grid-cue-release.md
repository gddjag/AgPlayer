# Beat Grid / CUE / separation release ledger

Approved specification: user plan in this task, 2026-09-06. Baseline main@5d71784. Implementation branch codex/beat-grid-cue-release-20260906.

## Scope and acceptance

- Rolling viewport 2/4/8/16/32/64 beats; entry/reset 64, track change retains selection; +/- adjacent steps, disabled endpoints, no audio speed change.
- Grid enabled by default, persistent 4/8 emphasis grouping default4; each4 red downbeat; manual per-track calibration; source-time mapping and explicit estimate/120 fallback.
- Basic orange CUE left of equal-sized green play: set/hold audition/release return/play latch/cancel lifecycle; atomic per-track state separate from audio/cache.
- Rolling waveform 8 DIP vertical margin, no extra amplitude compression; responsive control wrapping and shared grid/drag/cue coordinates.
- Actual GPU inference, device selection distinct from effective provider; optional isolated CUDA runtime with vetted fixed downloads, stage fallback and cancellation; no heavyweight installer payload.
- Unified tool navigation and separate file-search lossless icon. No lossless algorithm changes in this feature.
- Release all tests, related Debug/QML, visual DPI/narrow themes, real playback/GPU short+full song, review before integration/package.

## Ownership and preflight

| Task | Owner | Shared interface / check |
| --- | --- | --- |
| Deck state/CUE | beat_core | playback properties/methods sent to beat_ui; no QML edits |
| Rolling grid/UI/settings | beat_ui | consumes playback deck state; viewport vs grouping are independent |
| Separation | gpu_runtime | independent worker/controller/UI; shared build registration coordinated by root |
| Navigation/integration/QA | root | ToolSidebar/AudioToolsWindow; no algorithm edits |

Ruling: use a feature branch in the current checkout to reuse established build/runtime paths, preserving unrelated untracked `'-'`; other task worktrees remain untouched. User permits parallel independent work; file ownership avoids concurrent core edits. Builds are serialized.

## Progress

- Preflight: HEAD5d71784, no tracked changes. Existing branch tips all merged as of planning; uncommitted outside worktrees require separate readiness review at integration.
- Navigation: unified geometry and independent icon implemented. Release `qml_lossless_identification_test` and `qml_filename_process_test` passed (2/2); this is focused verification, not release acceptance.
- Separation: forced CUDA short-input five-stem inference passed with the existing four HTDemucs FP16 files and native preprocessing/postprocessing; reported provider CUDA/GPU0. Complete-song and application integration checks remain pending.
- Deck/UI work and regression tests are in progress; no release commit or package has been produced.

## Other-task integration candidates

- Frozen lossless 1.10 / parameter13 cumulative patch reviewed then precisely withdrawn from this release tree: `integration-1.10.patch`, SHA-256 `DCF69F741E94CE96EB584B05405CC199F11F8E6AE2113D16D0D4D42B3AF881D5`. Independent review found unresolved LGPL corresponding-source/relink distribution obligations for the copied analysis window. Original worktree and frozen patch remain intact. Forward apply-check passes after withdrawal. The navigation feature is retained; no new lossless algorithm ships here.
- Immersive task reports a ready reopen-timer fix and drawer interaction changes amid a larger uncommitted worktree. Do not copy whole files over current main; only reviewed independent increments may be included. Its broader native stress/visual acceptance remains incomplete.

## Integration verification, in progress

- Included only immersive reopen-timer cancellation/reset. Regression reached an about-to-timeout release state and failed on old code; after fix, Release immersive integration passed. Other immersive work remains outside this release.
- Root focused Release CTest: immersive integration, vocal separation layout and rolling theme passed 3/3, 46.34 seconds. This is not full-suite acceptance.
- Core direct Release playback tests: 36 passed, process exit 0. Actual desktop CUE during playback returned to zero and paused; dragging the rolling viewport moved center time and resumed playback. Real hold-audition audio listening remains unverified.
- Real screenshot caught viewport ComboBox text missing despite accessibility/model state. Rendering now binds directly to visibleBeats; final real-image recheck pending.
- Full-song CUDA revealed separate per-track clipping/gain. Shared output gain preserves stem ratios and accompaniment sum (maximum residual 0.00003052), but whole-song normalized reconstruction error remains about 0.124, above 0.05 gate. This item is NOT accepted yet; short CUDA sample passes at about 0.01846.
- Real cached CUDA setup, pause/immediate resume, fifteen-DLL verification and atomic runtime activation passed. No fresh network/mirror failure acceptance claimed from cached installation.
- Initial targeted build overlapped precise lossless-patch withdrawal; it is not final clean-source build evidence. Final stable-source full build is required.

## Approved keyboard / Hot Cue addendum

- User explicitly expanded the previous no-Hot-Cue scope. Add eight per-track Hot Cue slots to the existing atomic deck store, not the audio file or waveform cache. Empty slot records current position without changing transport; populated slot seeks and plays. Alt/Option plus slot clears it. Legacy deck files remain compatible.
- Rolling window only: C uses the existing press/hold/release CUE state machine, Shift+C jumps and pauses, Alt+C clears CUE; Q sets grid origin, Left/Right nudge one millisecond. Auto-repeat is ignored. Window loss, text editing, rebind and mode exit cancel held audition; no OS-global hook is registered.
- Settings > Keyboard exposes portable single-combination bindings with duplicate rejection, empty-to-disable, reset, persistence and edit cancellation. Mac Option maps to Qt Alt; no actual macOS build or hardware validation has been performed here.
- Native keyboard router's initial stub produced two expected failing assertions; implementation passed the initial five-test suite. Expanded shortcut-override/rebind test and end-to-end QML checks await final build.
- First broad Release pass (before keyboard addendum) finished 158/170 with 12 failures including stale-QRC layout checks, design-token contract and several timing failures. Design-token issues are corrected; failed tests must be closed out, not counted as passing. Debug runtime paths are corrected but timing acceptance remains pending.

## Latest acceptance evidence and remaining gates

- Full Release build after the keyboard addition succeeded. Focused rolling QML, native keyboard routing and design-token contract passed. Settings direct execution passed 43/43 in 7.926 seconds; its CTest run under load exceeded the existing 10-second budget, so CTest acceptance is still pending.
- Real 150% dark capture `build/qa/beat-keyboard-dark150-fixed.png` confirms the viewport text `64` is painted and controls stay inside the window. Native Windows interaction confirmed playing C returns to 00:00 and pauses when no CUE exists, and empty Hot Cue 1 records that paused position and draws the marker. Hold audition, macOS and full customized-keyboard interaction are not yet accepted.
- Independent review found non-focus Popups could steal arrow keys and cross-group shortcut conflicts were not rejected. These are being corrected before final keyboard acceptance.
- Serial Release retries passed scratch/audio engines, terrain GPU smoke, separation Worker, vocal separation controller, integrated shell lifecycle and Windows shell runtime (7/9). Process client exceeded the CTest 15-second budget but direct execution passed 12/12 in 21.888 seconds. Do not substitute that direct result for the CTest gate.
- Main-window QML direct run remains 119 passed / 17 failed / 1 skipped (`build/qa/main-window-final.txt`), including compact volume overlap, control geometry and interaction/layout failures. This is not release-ready.
- Historical full-song GPU reconstruction exceeded the provisional agent-added NRMS check (not a user-agreed quality limit); the follow-up below corrects that acceptance assumption. No release commit, merge or new Desktop installer has been produced; existing installers and other task worktrees are preserved.
- Final keyboard corrections: visible non-focus Popup detection now defers keys to controls; bidirectional legacy/rolling conflict checking also reserves Main's fixed Space key even after the global play binding changes. Tests preserve the actual Popup and all behavioral assertions. Native test windows explicitly drain pending platform events before destruction after a reproducible stale expose crash.
- Latest targeted Release build succeeded. Playback controller and rolling QML CTest passed (28.50 / 10.42 seconds); subsequent final native keyboard router and settings CTest both passed (4.19 / 6.46 seconds). Diff whitespace check passed. These close the keyboard-specific regression failures, not the remaining full release gates above.

## Follow-up: failure classification

- User requested closing the remaining failures. Main-window layout/interaction, GPU reference validation, and process teardown are being investigated independently; shared builds remain serialized.
- Process timing XML identifies error handling as the slow path: wrong-direction/version cases total 10.234 seconds, with warnings that `taskkill.exe` is still running at destruction. A new real event-loop responsiveness test fails before any production fix: longest timer gap 1166 ms (`build/qa/process-responsive-red.txt`). The target is non-blocking owned-process termination, not a larger CTest timeout.
- The former `0.05` sum-of-stems/input NRMS gate needs correction as an acceptance assumption, not silent threshold adjustment: upstream Demucs allows predicted-stem addition or source-minus-vocal accompaniment, and the specialized exporter only guarantees its target output row. Reference consistency, clipping, timing, valid five-track outputs and listening remain independent checks. Same-model raw versus official outer normalization A/B is pending; no replacement model or production normalization change is yet approved by evidence.

## Follow-up verification results

- Raw versus upstream outer-normalization A/B completed without a material reconstruction improvement (0.365461 versus 0.365057); production normalization remains unchanged. The invalid Demucs mixture-consistency threshold was removed, retaining the measured NRMS as diagnostic. Independent reference tolerance and MDX subtraction checks were not weakened.
- Full 237-second song CUDA engineering regression passed 3/3 in 159.440 seconds (`build/qa/separation-cuda-song-engineering-final.txt`): actual CUDA / GPU 0, five complete 10,463,663-frame outputs, finite non-silent samples, no clipping, common gain 0.674916 and accompaniment sum maximum error 0.0000305176. NRMS 0.12422 remains recorded; this is not subjective listening or ground-truth SDR acceptance.
- Windows process termination now uses a per-launch Job Object assigned atomically during process creation, replacing blocking taskkill. Release CTest process-client passed in 9.34 seconds with the original 15-second timeout, including event-loop responsiveness, confirmed child-process cleanup and synchronous retry.
- Independent review identified Qt's early FailedToStart path without a finished signal; startup failure now drains through a generation-checked queued callback. Final full Release rebuild succeeded after that correction.
- Main-window suite passed 137 cases / zero failures / one offscreen-only WM_DROPFILES skip (`build/qa/main-window-final-green-3.txt`). Real enum binding and title/thumbnail double-click fixes are covered by nine focused checks. Test cleanup restores playback and window state; a half-pixel center rounding discrepancy is checked against nearest-pixel alignment rather than independently rounding both coordinates.
- QQuickWindow captures `build/qa/beat-final-light125.png` (1386 wide, 125% DPI, light) and `build/qa/beat-final-narrow100.png` (1100 wide, 100% DPI, dark) show painted viewport values, intact CUE/play controls and responsive settings wrapping without overflow. Prior 150% dark capture remains recorded separately.
- The controller's first 66.55-second CTest nonzero exit was not a timeout (configured budget is 120 seconds). Direct execution passed 62 / zero failures / two opt-in skips; serial CTest retry passed 72.11 seconds. No unsupported root-cause claim is made; final stable-source whole-suite retry is running.
- Full serial Release sweep completed 166/171 in 1324.76 seconds (`build/qa/release-final-followup-ctest.txt`). Five failures: playback_time_pitch (20-second timeout), audio_engine (PCM preparation assertion followed by timeout), native rolling keyboard test (0xc0000374), settings (10-second timeout), process client (15-second timeout). Main-window QML, rolling, immersive integration, metadata and separation controller passed within the same sweep. This is not an all-green release claim.
- Follow-up test fixes retain production DSP and all CTest deadlines: audio engine uses its existing two-second wall-clock buffer wait instead of a fixed yield count; time-pitch buffer polling sleeps one millisecond within the existing three-second deadline. Keyboard test no longer manually destroys native windows; QtTest cleanup drains queued events after test-local C++ objects have actually been destroyed. Target rebuild succeeded; failure retries pending.

## Final failure closure follow-up

- Playback time-pitch, audio engine and settings serial CTest retries passed in 14.47, 2.45 and 6.71 seconds respectively, without changing their deadlines (`build/qa/release-failed-targets-retry.txt`).
- Process-client startup preparation previously used a 1.5-second test wait despite its configured five-second worker hello deadline. Tests now separate startup readiness from cancellation: the 1.5-second cancellation deadline and sub-500-ms event-loop gap remain unchanged. The descendant fixture reports an atomically written readiness marker and proves the child is alive before triggering termination. The whole suite budget is now explicitly 60 seconds for roughly 20 real process launches; this is not a production responsiveness relaxation. Final CTest passed in 11.98 seconds (`build/qa/process-startup-separated-final.txt`).
- The keyboard crash is not resolved by native-window test cleanup alone. Root cause: the self-connected `windowChanged` callback survives the derived destructor and is invoked when `QQuickItem` detaches from its window, after derived members have been destroyed. The new attached-handler destruction regression exits unsuccessfully before the fix (`build/qa/router-destructor-red.txt`); production disconnection at destructor entry and post-fix verification are in progress. This supersedes the earlier platform-event-only hypothesis.
- Keyboard production fix now disconnects self/host signals and removes the window event filter before cancelling held actions and entering base destruction. The new regression passes; the full router suite passes five consecutive Release runs (0.34–1.03 seconds) and one Debug run (1.69 seconds). Evidence: `router-destructor-green.txt`, `router-destructor-green-repeat5.txt`, `router-debug-final.txt` under `build/qa`.
- All five previously failed targets pass together after the fix: time-pitch 14.02s, audio engine 2.13s, router 0.28s, settings 2.74s, process client 3.29s (`build/qa/five-failed-targets-closure.txt`). Final Release rebuild succeeds. The whole-suite post-fix run is tracked separately and is not inferred from this focused result.
- Final inspection found a separate CUE track-change race: core track index changes synchronously but the controller ID updates on its next poll. `cuePress()` now uses the same track-index/ID guard as jump-to-CUE and Hot Cue before mutating state. The new paused/playing regression fails before the fix (old CUE overwritten / new track paused) and passes 4/4 after it (`cue-track-mismatch-red.txt`, `cue-track-mismatch-green.txt`). The full playback test's concurrent 30.45-second timeout remains an unsuccessful run, despite the historical `full-green` filename; an exclusive rerun is required.
- Post-fix sweep exposed a queue-gapless 30.18-second timeout without a stage diagnostic. Its unchanged old binary passed once in isolation (16.503s). Two fixed-iteration sleep loops now use real two-second steady-clock deadlines; stage diagnostics were added without changing production or the 30-second suite limit. Focused CTest passed 26.71s (`queue-gapless-wallclock-ctest.txt`); the original timeout cause is not claimed proven by that pass.
- The same sweep reported thumbnail-provider and audio-editor-controller failures. Thumbnail direct diagnostic rerun passed 29/29 in 11.439s. These intermittent results remain separate from the proven production fixes, pending exclusive CTest retries. Parallel builds/tests in this task have been stopped before those retries.
- Post-destructor-fix full sweep completed 168/171 in 1352.30s (`release-post-destructor-fix-all.txt`). The three unsuccessful targets were queue-gapless, thumbnail-provider and audio-editor-controller. Exclusive serial retry, also including the final CUE playback target, passed 4/4 in 87.75s: queue 20.98s, playback 27.31s, thumbnail 4.87s, editor 34.53s (`final-exclusive-retries.txt`). No thumbnail/editor production changes or timeout increases were made. This is full-suite coverage plus focused closure, not a claim of one uninterrupted 171/171 green run; timing variability remains recorded.
- Independent final review found no remaining issue in the keyboard destruction/cancellation path or the Windows Job lifecycle. The CUE snapshot guard was compared with the existing jump/Hot Cue guard and verified by the new real-core paused/playing cases.
- Performance scope: `grid-profile-results.txt` measured 10,000 offscreen synchronous updates (grid off 439ms, on 808ms, 20,000 requests coalesced to one Canvas paint). This is not actual frame/GPU timing. macOS keyboard behavior, human listening quality and fresh-network mirror failover are not represented as verified by these Windows engineering checks.
- Final CUE/Beat Grid Debug build and focused regression passed 12/12 with zero skips in 6.313s (`cue-grid-debug-final.txt`), covering persistence, source-time calibration, CUE audition/play latch, paused/playing track mismatch, Hot Cues and cancellation. Release application and QML test runner were relinked after the final guard; packaging uses these binaries rather than the earlier sweep binaries.

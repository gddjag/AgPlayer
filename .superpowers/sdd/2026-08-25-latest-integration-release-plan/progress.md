# SDD ledger — plan: D:/ai/AgPlayer/.worktrees/recover-complete-release/docs/development/2026-08-25-latest-integration-release-plan.md

Baseline: branch codex/recover-complete-release at 8291a7346b500bc724adf38700cf4071f6572341; worktree clean before plan document; Release CTest 99/99 passed on build/final-release-msvc.

## Preflight dependency/conflict scan

| Producer task | Consumer/shared task | Shared interface/files | Finding and ruling |
|---|---|---|---|
| Task 1 | Task 4 | WindowController, DockedWindowFrame, window settings | Ruling: Task 1 owns the glass setting and QML bindings; Task 4 owns native Windows backdrop/taskbar behavior. Task 4 must preserve Task 1 bindings. Cost if wrong: duplicated native style handling or docking regression. |
| Task 1 | Task 2 | AudioToolsWindow, shared controls, waveform palette | Ruling: Task 1 owns shell chrome and shared visual primitives; Task 2 owns editor content/behavior and consumes them. Cost if wrong: editor layout can diverge from the shell reference. |
| Task 1 | Task 3 | SettingsController, SettingsPage | Ruling: Task 1 owns theme/waveform/language/native-control fields; Task 3 owns export/transcode/pitch/cache fields. Changes are sequential and reviewed by property group. Cost if wrong: settings migration or signal wiring may overlap. |
| Task 3 | Task 2 | Global export and pitch defaults consumed by AudioEditorController | Ruling: implement Task 3 shared-default contract before finalizing Task 2 export/pitch consumption even though plan numbering lists Task 2 first. Cost if wrong: editor may copy another private default set. |
| Tasks 1-4 | Task 5 | CMake, resources, tests, installer | Ruling: functional tasks add their own resources/tests; Task 5 only integrates, removes obsolete plugin remnants, and packages. Cost if wrong: Task 5 could hide functional changes in a mixed release commit. |
| Unique branch 409b1f2 | Task 2 | editor QML/waveform/tests | Ruling: do not cherry-pick blindly; compare and replay only missing compatible behavior because the integration base contains newer overlapping fixes. Cost if wrong: newer editor fixes could be reverted. |
| Unique branch af93306 | Task 3 | conversion capability matrix/tests | Ruling: do not cherry-pick blindly; use it as a reference and port only missing capability behavior. Cost if wrong: current retry/preserve fixes could be reverted. |

Preflight internal consistency: tasks, exact values, tests and release outputs agree. No destructive operation, external publish or shared-branch push is required.

Task 1: implementation report at commit 89337a6; review Spec ❌, Quality Not Approved.
Task 1: minor (deferred): metadata repair branch lacked a focused regression test; folded into the Critical decoding fix rather than accepted as-is.
Task 1: fix round 1/5 (4 addressed, 1 open — BPM range-slider handle binding; commits 89337a6..2193df3).
Task 1: fix round 2/5 (1 addressed, 0 open — slider binding plus CP936/tag/icon/watcher runtime regressions; commits 2193df3..936dc41).
Task 1: complete (commits 41abe0d..936dc41, review clean; fresh focused CTest 5/5 passed).
Task 3: implementation commit 1f9ce3f; independent review NOT APPROVED (1 Critical, multiple Important; report test count corrected from claimed 7/7 to observed 6/6 plus one matrix failure).
Task 3: fix round 1/5 in progress. AudioEditor consumption of the shared export/pitch contract remains an explicit Task 3 -> Task 2 dependency; Task 3 must finish the provider contract and converter consumer now, Task 2 must consume it before completion.
Task 2: complete (commits 9f1ed1c, a9b24ca; fresh Release focused CTest 5/5 passed; real WASAPI/audio hardware and native Explorer drag remain manual-only).
Task 2: review fix round 1/5 complete (all 1 Critical + 4 Important findings addressed; current Release targets rebuilt, focused CTest 5/5 and QML lint passed; fresh isolated configure was externally blocked by the SoundTouch vcpkg archive hash; hardware/Explorer/visual matrix remain manual-only).
Task 4: complete (implementation commit 8b50950; Release AgPlayer build passed, focused CTest 7/7 passed, multilingual Inno package produced; interactive Explorer/taskbar and installer/uninstaller visuals remain manual-only Shell acceptance).
Task 4: review fix round 1/5 complete (commit 930cb01; stale PE/new source SkipBuild is rejected, matching PE guard path passes, rebuilt About QML test passes, fresh Release focused CTest 9/9 passed; ready for independent re-review).

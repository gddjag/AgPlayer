# Task 5 report — Chinese/English app, Chinese-default installer

- Fixed base: `66e8c8ff6c56fff4bf122e832446c929493211c1`
- Workspace: `D:/ai/AgPlayer/.worktrees/player-polish-lyrics-20260830`
- Scope: application localization/settings, translation catalogs, installer language startup contract, and their tests only

## RED evidence

Tests were changed before production code and then run against the fixed base.
The direct Qt test logs are retained under `build/release/test-results/`:

- `task5-red-translation.txt`: 2 passed / 3 failed. The production list still had four languages, `th` normalized to `th`, and loading `th` left the active language as `th` instead of `zh`.
- `task5-red-settings.txt`: 2 passed / 1 failed. A persisted `th` value loaded as `th` instead of `zh`.
- `task5-red-qml-language.txt`: 2 passed / 1 failed. The visible language model had four entries instead of the required two.
- `task5-red-qml-theme.txt`: 2 passed / 1 failed. The dark theme text still included the default suffix.
- The focused CTest run also failed `phase6_translation_coverage_test` because four application catalogs existed and `installer_contract_test` because `ShowLanguageDialog=yes` remained.

## Implementation

- Application-supported language codes are now exactly `zh` and `en` in both `TranslationManager` and `SettingsController` validation.
- Persisted/requested `th`, `vi`, or unknown codes normalize to `zh` through the existing settings load/set paths.
- Settings shows exactly `中文` and `English`, with no flag/CN/US/Thai/Vietnamese labels.
- The dark theme choice is `深色` / `Dark` without the default suffix.
- CMake translation inputs now contain only Chinese and English; `translations/agplayer_th.ts` and `translations/agplayer_vi.ts` were deleted.
- Normal installer startup now uses `ShowLanguageDialog=no` and the existing `LanguageDetectionMethod=none`. Simplified Chinese remains first and English second.
- Installer Chinese/English/Thai/Vietnamese language resources and all three localized custom-message families remain intact for explicit `/LANG=<name>` usage.

## GREEN evidence

- VS2022 Release configure/build of `translation_manager_test`, `settings_controller_test`, and `qml_main_window_test`: exit 0.
- Translation generation: Chinese and English each generated 851 finished, 0 unfinished entries.
- Direct `translation_manager_test`: 5 passed / 0 failed.
- Direct `settings_controller_test`: 31 passed / 0 failed.
- Direct app-language/theme QML selection: 4 passed / 0 failed.
- Complete `tst_main_window.qml`: 114 passed / 0 failed / 1 expected offscreen `WM_DROPFILES` skip.
- Required focused CTest set (`translation_manager_test`, `settings_controller_test`, `qml_main_window_test`, `phase6_translation_coverage_test`, `installer_contract_test`): 5/5 passed in 23.25 seconds.
- `git diff --check 66e8c8ff6c56fff4bf122e832446c929493211c1 --`: exit 0.

## Self-review and remaining risk

- Fixed-scope diff contains only Task 5-owned localization/settings/installer/tests/CMake files plus this report.
- No player controls, lyrics, waveform, taskbar, audio core, installer language resource files, or package output were changed.
- The installer source contract is automated here; compiling and interactively launching it in a clean Windows VM remains Task 13 acceptance work. This task intentionally did not package an EXE.
- Ninja reported recovery from a prematurely ended build log during incremental builds, but configure, generation, all requested builds, and all tests completed successfully.

## Independent-review correction

The first review found two stale test-only catalog lists that still named the
deleted Thai and Vietnamese application catalogs. Before correction,
`translation_catalog_test` failed because `agplayer_th.ts` was missing and
`audio_tools_layout_contract_test` failed when `Select-String` tried to open
that same deleted file. Both lists now enumerate exactly the supported Chinese
and English application catalogs. The audio-tools contract still scans every
recording-removal source and retains all of its layout and production-wiring
assertions; only the obsolete file inputs changed. The first correction run
then exposed a second stale expectation inside `CheckTranslationCatalogs.cmake`;
its expected locale set and diagnostic now also describe exactly `en_US` and
`zh_CN`, while all catalog message-count, unfinished-text, and encoding checks
remain unchanged.

Correction verification:

- `translation_catalog_test` and `audio_tools_layout_contract_test`: 2/2 passed.
- Original Task 5 focused CTest set: 5/5 passed in 24.97 seconds.
- Direct complete `tst_main_window.qml`: 114 passed / 0 failed / 1 expected offscreen `WM_DROPFILES` skip.
- The correction is test-contract-only; no application, installer, playback, audio, lyrics, waveform, or taskbar production source changed.

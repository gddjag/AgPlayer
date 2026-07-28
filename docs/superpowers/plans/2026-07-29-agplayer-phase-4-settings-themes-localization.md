# AgPlayer Phase 4: Settings, Themes, Localization

## Goal

Make settings transactional and scrollable, make dark/light/system palettes usable
across all windows and icons, and ship only Chinese, English, Thai, and Vietnamese
without missing text or mojibake.

## Tasks

1. Add a settings edit session with commit/cancel/default rollback tests.
2. Wire popup open/save/cancel/close to that transaction and cover it in QML tests.
3. Make the settings content scroll at reduced window sizes.
4. Audit theme tokens and remove fixed dark-only surfaces from production QML.
5. Add QA launch options for theme, language, and settings screenshots.
6. Load the Chinese catalog too; finish all four catalogs and reject unfinished text.
7. Build, run focused tests, run the full CTest suite, capture theme/language/settings
   screenshots, scan logs for QML warnings/mojibake, and update the phase status.

## Acceptance

- Save persists; Cancel, Escape, outside click, and title close restore the snapshot.
- Restore Defaults is previewable and can still be cancelled.
- Settings remain reachable at the supported minimum window size.
- Dark, light, and follow-system change surfaces, text, and SVG icon tint together.
- `zh`, `en`, `th`, and `vi` catalogs compile with no unfinished messages.
- Focused and full automated suites pass with zero runtime warnings.

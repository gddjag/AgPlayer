# AgPlayer Phase 8 — Final Integration and Release Readiness

**Goal:** Trace the latest product requirements to real code and tests, remove
or connect every inert control, validate all supported visual states, and leave
the Windows MVP ready for human audio-device acceptance without packaging.

**Constraints:** Keep Qt 6/QML + C++17, retain the C ABI boundary, add no
dependency, keep all media local, support only Chinese/English/Thai/Vietnamese,
and do not create an installer or deployed EXE bundle.

## Task 1 — Runtime settings integrity

- Trace each settings control to its runtime consumer.
- Add regression tests before wiring missing effects.
- Remove any control that cannot truthfully operate in the Windows MVP rather
  than retaining a fake success path.
- Verify cancel/reset/save semantics and translated labels.

## Task 2 — Requirements traceability

- Map playback, waveform, library/search, windowing, tools, settings, privacy,
  and performance requirements to code and automated evidence.
- Mark platform-future and hardware-audible items explicitly; do not report
  them as complete.
- Resolve all software-only gaps that can be validated locally.

## Task 3 — Visual and interaction matrix

- Capture the production executable in Chinese, English, Thai, and Vietnamese,
  each in dark and light themes.
- Cover empty startup, populated playback, list window, mini player, settings,
  and all five audio-tool pages.
- Reject missing images, clipped critical actions, mojibake, QML warnings, and
  empty/error logs.

## Task 4 — Final technical gate

- Run Debug and Release `/W4 /WX` builds and all tests.
- Re-run production playback smoke, 10,000-row model test, translation
  integrity, privacy scan, and `git diff --check`.
- Measure pre-deployment executable/core dependency size without packaging.
- Obtain independent Critical/Important review and commit the phase.

## Task 5 — Handoff

- Update the phase report with verified passes and explicit remaining manual
  gates.
- Do not package until the user separately requests it.

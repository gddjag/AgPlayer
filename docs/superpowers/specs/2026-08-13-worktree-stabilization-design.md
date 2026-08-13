# Worktree Stabilization Design

**Goal:** Turn the mixed `codex/revised-ui` worktree into independently buildable, testable functional commits without touching any other worktree or producing a package.

## Scope

- Work only in `D:\ai\AgPlayer\.worktrees\revised-ui`.
- Preserve user-facing work already present; delete only obsolete library-manager and light-editor implementation paths that are already being removed by the current diff.
- Do not alter the waveform implementation owned by the separate waveform task. Its files may be committed only after its own focused tests pass.
- Do not package an EXE.

## Commit Boundaries

1. **Build hygiene:** synchronize the rename-journal source, CMake registration, and tests; remove whitespace defects.
2. **Playback scope:** queue scoping, current-list continuation, and list entry points with playback regression tests.
3. **Player UI:** main/mini player layout, contextual list actions, settings and taskbar identity, with QML and installer checks.
4. **Audio editor and metadata:** editor/document lifecycle, filename/metadata tools, and their tests. This stays separate from playback.
5. **Waveform and localization:** only verified waveform/spectrum/settings changes and completed translations.

## Acceptance Gates

Every commit contains only its named files, passes `git diff --check`, builds its CMake targets, and passes its focused tests. A final full build and full CTest run are required before the worktree is called ready; any unrelated failing group remains uncommitted and is reported instead of being masked.

## Safety

No reset, checkout, force operation, packaging, or writes to the four other worktrees. Files that cannot be assigned to a verified group remain unstaged.

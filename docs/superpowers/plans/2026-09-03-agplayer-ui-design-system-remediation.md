# AgPlayer UI Design System Remediation Plan

**Goal:** Standardise the merged application around one restrained native
desktop visual system without changing playback, data models, feature routes,
or C++ interfaces.

## Delivery sequence

1. Freeze the clean merged baseline and record focused Release results.
2. Add executable Theme colour, typography, radius, spacing, and dimension
   contracts before implementation.
3. Replace the old fixed blue/blue-black Theme palette with approved dark
   purple and light blue semantic roles while retaining compatibility aliases.
4. Complete shared Themed controls and test every supported interaction state.
5. Migrate title bars, navigation, search/filter, track lists, and primary
   player controls to shared metrics.
6. Migrate settings, audio tools, dialogs, and utility pages.
7. Align specialised player, editor, equalizer, immersive, mini-player, and
   video shells while preserving domain colours and performance paths.
8. Add static style enforcement, screenshot evidence, dark/light/system QA,
   HiDPI checks, build, lint, focused tests, full Release tests, and final diff
   review.

Each step is independently reviewable. Existing compatibility aliases are kept
until every production consumer has migrated; they are not a second theme.

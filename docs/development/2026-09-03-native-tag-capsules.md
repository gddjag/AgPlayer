# Native tag capsule integration

## Scope

- Recreate the supplied two-part 28 px tag capsule in Qt Quick only.
- Keep tag filtering, selection, context menu, drag-and-drop and scrolling behavior.
- Reuse the shared `TagManagementPanel` so classic, integrated and rolling shells stay synchronized.
- Keep user-selected persisted colours; assign new tags from the supplied ten-colour palette.

## Acceptance

- The name half sizes to its text and uses the tag colour.
- The count half has a 42 px minimum width and neutral surface.
- An 8 px rotated native `Rectangle` creates the central triangular notch.
- Hover, keyboard focus, press, selected and drop states modify the same tag colour.
- Long labels elide inside the available panel width and retain the existing tooltip.
- Screenshot: `.artifacts/acceptance/2026-09-03-tag-capsules/tag-capsules-dark.png`.

No HTML, CSS, WebEngine, rasterized capsule or duplicate theme-specific component is used.

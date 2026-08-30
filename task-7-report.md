# Task 7 Report — Windows Taskbar Window Toggling

## Baseline and scope

- Worktree: `D:\ai\AgPlayer\.worktrees\player-polish-lyrics-20260830`
- Branch: `codex/player-polish-lyrics`
- Starting commit: `0f131244c30f6e3639bbc6fa6ccddc44f1268724`
- No packaging or publishing was performed.

## Confirmed root cause

`WindowController::applyPlatformWindowStyle()` gave the registered main HWND
`WS_EX_APPWINDOW` and gave auxiliary windows `WS_EX_TOOLWINDOW`, but it never
added `WS_SYSMENU | WS_MINIMIZEBOX` to the main window's `GWL_STYLE`. The
existing `WM_SYSCOMMAND` routing already handled `SC_MINIMIZE` and `SC_RESTORE`;
the missing native style contract prevented the frameless main HWND from
presenting the normal Shell taskbar contract.

## Implementation

- Added a private Windows-only `ensureTaskbarWindowStyles(QWindow*)` helper.
- The helper is guarded to the currently registered main window only.
- It preserves every existing `GWL_STYLE` bit, adds only
  `WS_SYSMENU | WS_MINIMIZEBOX`, and calls `SetWindowPos` with
  `SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
  SWP_FRAMECHANGED` only when either bit is missing.
- The helper runs through the existing main platform-style path after the HWND
  is materialized, so re-registration/native recreation reapplies the contract.
- No `WM_ACTIVATE`, `WM_SYSCOMMAND`, Qt frameless flag, geometry, DPI, z-order,
  or shutdown behavior was changed.

## TDD evidence

RED, before the production change, using the Windows QPA backend:

```text
FAIL! WindowControllerTest::nativeTaskbarGroupUsesMainAsOnlyAppWindow()
'mainWindowStyle & WS_SYSMENU' returned FALSE.
Totals: 2 passed, 1 failed
```

GREEN after the minimum implementation, using the Windows QPA backend:

```text
PASS nativeTaskbarGroupUsesMainAsOnlyAppWindow
PASS taskbarCommandsToggleDockedGroupWithoutResizing
PASS taskbarToggleEntryPointMinimizesAndRestoresWindowGroup
PASS taskbarToggleEntryPointActivatesBackgroundGroup
PASS taskbarActivationDoesNotCancelMinimize
Totals: 7 passed, 0 failed, 0 skipped
```

The style test also removes the two bits and re-registers the same main window,
then proves that both required bits return while every other style bit remains
unchanged. Auxiliary list/tools/settings windows remain `WS_EX_TOOLWINDOW`, do
not gain `WS_EX_APPWINDOW`, and do not receive the main taskbar style bits.

## Verification

All builds used the VS2022 Community developer environment (`VsDevCmd.bat`,
x64) and the `windows-msvc-release` preset.

- Built `window_controller_test`: PASS.
- Built Release `AgPlayer` and `shutdown_test`: PASS.
- Direct Windows-QPA focused tests: 7 passed, 0 failed.
- `ctest --test-dir build/release -R
  '^(window_controller_test|windows_shell_runtime_test|shutdown_test|window_mixed_dpi_transition_test|window_taskbar_native_test)$'
  --output-on-failure`: 5/5 passed.
- `windows_shell_runtime_test` verifies main `GWL_STYLE`, auxiliary grouping,
  icon/version identity, native-command minimize/restore, group visibility, and
  unchanged native-pixel geometry.
- `git diff --check`: PASS (only Git's existing LF-to-CRLF checkout warnings).

## Evidence boundary and remaining acceptance

The PowerShell `SendMessage(SC_MINIMIZE/SC_RESTORE)` path is deliberately named
a **native-command regression**, not a real taskbar click. A deterministic
Explorer/UI Automation click cannot be guaranteed in CTest because Windows 11
may group or overflow the taskbar button, localize its accessible name, and
deny foreground activation to the background test process.

Task 13 must therefore use computer-use/manual acceptance for actual Explorer
taskbar clicks in both dual-window and single-window modes, covering foreground
minimize, minimized restore, background activation, auxiliary windows open,
100% and 150% DPI, and unchanged geometry (including the dual-monitor case).

param([Parameter(Mandatory=$true)][string]$AppPath)

$ErrorActionPreference = 'Stop'
$app = (Resolve-Path $AppPath).Path
$sourceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$versionOutput = (& (Join-Path $sourceRoot 'scripts\release-version.ps1') `
    -SourceRoot $sourceRoot | Out-String).Trim()
if ($versionOutput -notmatch '^AgPlayer release version: ([0-9]+\.[0-9]+\.[0-9]+)$') {
    throw "Unable to read expected release version: $versionOutput"
}
$expectedPeVersion = $Matches[1] + '.0'

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class AgPlayerShellProbe
{
    public const int GWL_STYLE = -16;
    public const int GWL_EXSTYLE = -20;
    public const uint GW_OWNER = 4;
    public const long WS_SYSMENU = 0x00080000L;
    public const long WS_MINIMIZEBOX = 0x00020000L;
    public const long WS_EX_APPWINDOW = 0x00040000L;
    public const long WS_EX_TOOLWINDOW = 0x00000080L;
    public const uint WM_GETICON = 0x007F;
    public const uint WM_SYSCOMMAND = 0x0112;
    public static readonly UIntPtr SC_MINIMIZE = new UIntPtr(0xF020);
    public static readonly UIntPtr SC_RESTORE = new UIntPtr(0xF120);
    public static readonly UIntPtr ICON_SMALL = UIntPtr.Zero;
    public static readonly UIntPtr ICON_BIG = new UIntPtr(1);

    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr hwnd);

    [DllImport("user32.dll")]
    public static extern bool IsIconic(IntPtr hwnd);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hwnd);

    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    public static extern IntPtr GetWindow(IntPtr hwnd, uint command);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);

    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    public static extern IntPtr GetWindowLongPtr(IntPtr hwnd, int index);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessage(IntPtr hwnd, uint message,
                                            UIntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    public struct Rect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);

    public static bool WindowVisible(IntPtr hwnd) { return IsWindowVisible(hwnd); }

    public static IntPtr[] VisibleWindowsForProcess(uint processId)
    {
        var windows = new List<IntPtr>();
        EnumWindows((hwnd, unused) => {
            uint owner;
            GetWindowThreadProcessId(hwnd, out owner);
            if (owner == processId && IsWindowVisible(hwnd)) windows.Add(hwnd);
            return true;
        }, IntPtr.Zero);
        return windows.ToArray();
    }

}
'@

$probeRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ('agplayer-shell-probe-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $probeRoot -Force | Out-Null
$logPath = Join-Path $probeRoot 'agplayer.log'
$stderrPath = Join-Path $probeRoot 'stderr.log'
$process = $null
try {
    $priorShellProbe = $env:AGPLAYER_QA_SHELL_PROBE
    try {
        $env:AGPLAYER_QA_SHELL_PROBE = '1'
        $process = Start-Process -FilePath $app -ArgumentList @(
            '--qa-test-mode', '--qa-open-settings', '--qa-log', $logPath) -PassThru `
            -RedirectStandardError $stderrPath
    } finally {
        $env:AGPLAYER_QA_SHELL_PROBE = $priorShellProbe
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $mainWindow = [IntPtr]::Zero
    $identityVerified = $false
    $teardownVerified = $false
    do {
        Start-Sleep -Milliseconds 50
        $visible = [AgPlayerShellProbe]::VisibleWindowsForProcess(
            [uint32]$process.Id)
        $appWindows = @($visible | Where-Object {
            ([AgPlayerShellProbe]::GetWindowLongPtr(
                $_, [AgPlayerShellProbe]::GWL_EXSTYLE).ToInt64() -band
                [AgPlayerShellProbe]::WS_EX_APPWINDOW) -ne 0
        })
        if ($appWindows.Count -eq 1) {
            $mainWindow = $appWindows[0]
        }
        if (Test-Path -LiteralPath $stderrPath) {
            $probeOutput = Get-Content -Raw -LiteralPath $stderrPath
            $identityVerified = $probeOutput -match 'AgPlayer native taskbar identity verified'
            $teardownVerified = $probeOutput -match 'AgPlayer shell teardown probe: passed created=1 destroyed=1 stayedDestroyed=1'
        }
    } while (($mainWindow -eq [IntPtr]::Zero -or -not $identityVerified -or -not $teardownVerified) -and
             [DateTime]::UtcNow -lt $deadline -and
             -not $process.HasExited)

    if ($mainWindow -eq [IntPtr]::Zero -or -not $identityVerified -or -not $teardownVerified) {
        $stderr = if (Test-Path -LiteralPath $stderrPath) {
            Get-Content -Raw -LiteralPath $stderrPath
        } else { '' }
        throw "AgPlayer shell identity/live-window/teardown contract failed:`n$stderr"
    }
    $mainStyle = [AgPlayerShellProbe]::GetWindowLongPtr(
        $mainWindow, [AgPlayerShellProbe]::GWL_STYLE).ToInt64()
    $requiredTaskbarStyles = [AgPlayerShellProbe]::WS_SYSMENU -bor
        [AgPlayerShellProbe]::WS_MINIMIZEBOX
    if (($mainStyle -band $requiredTaskbarStyles) -ne $requiredTaskbarStyles) {
        throw 'The frameless taskbar HWND is missing WS_SYSMENU/WS_MINIMIZEBOX'
    }
    $bigIcon = [AgPlayerShellProbe]::SendMessage(
        $mainWindow, [AgPlayerShellProbe]::WM_GETICON,
        [AgPlayerShellProbe]::ICON_BIG, [IntPtr]::Zero)
    $smallIcon = [AgPlayerShellProbe]::SendMessage(
        $mainWindow, [AgPlayerShellProbe]::WM_GETICON,
        [AgPlayerShellProbe]::ICON_SMALL, [IntPtr]::Zero)
    if ($bigIcon -eq [IntPtr]::Zero -or $smallIcon -eq [IntPtr]::Zero) {
        throw 'WM_GETICON returned an empty brand icon'
    }

    $version = (Get-Item -LiteralPath $app).VersionInfo
    if ($version.FileVersion -ne $expectedPeVersion -or
        $version.ProductVersion -ne $expectedPeVersion) {
        throw "PE version is not synchronized: $($version.FileVersion) / $($version.ProductVersion)"
    }

    $before = [AgPlayerShellProbe+Rect]::new()
    if (-not [AgPlayerShellProbe]::GetWindowRect($mainWindow, [ref]$before)) {
        throw 'Unable to read the taskbar window geometry before activation checks'
    }
    $auxiliaryBefore = @(
        [AgPlayerShellProbe]::VisibleWindowsForProcess([uint32]$process.Id) |
            Where-Object {
                $_ -ne $mainWindow -and
                [AgPlayerShellProbe]::GetWindow(
                    $_, [AgPlayerShellProbe]::GW_OWNER) -eq $mainWindow
            }
    )
    if ($auxiliaryBefore.Count -lt 2) {
        throw "Shell probe did not open both tools/settings owner windows (found $($auxiliaryBefore.Count))"
    }
    foreach ($auxiliary in $auxiliaryBefore) {
        $auxiliaryExStyle = [AgPlayerShellProbe]::GetWindowLongPtr(
            $auxiliary, [AgPlayerShellProbe]::GWL_EXSTYLE).ToInt64()
        if (($auxiliaryExStyle -band [AgPlayerShellProbe]::WS_EX_TOOLWINDOW) -eq 0 -or
            ($auxiliaryExStyle -band [AgPlayerShellProbe]::WS_EX_APPWINDOW) -ne 0) {
            throw 'An auxiliary owner window became an independent taskbar entry'
        }
    }

    # Deterministic native-command regression only. SendMessage does not prove
    # a click from Explorer; real taskbar clicks remain a Task 13 acceptance.
    [void][AgPlayerShellProbe]::SendMessage(
        $mainWindow, [AgPlayerShellProbe]::WM_SYSCOMMAND,
        [AgPlayerShellProbe]::SC_MINIMIZE, [IntPtr]::Zero)
    $minimizeDeadline = [DateTime]::UtcNow.AddSeconds(3)
    while (-not [AgPlayerShellProbe]::IsIconic($mainWindow) -and
           [DateTime]::UtcNow -lt $minimizeDeadline) {
        Start-Sleep -Milliseconds 25
    }
    if (-not [AgPlayerShellProbe]::IsIconic($mainWindow)) {
        throw 'SC_MINIMIZE did not minimize the foreground player window group'
    }
    foreach ($auxiliary in $auxiliaryBefore) {
        if ([AgPlayerShellProbe]::WindowVisible($auxiliary)) {
            throw 'A docked/auxiliary window remained visible after taskbar minimize'
        }
    }

    [void][AgPlayerShellProbe]::SendMessage(
        $mainWindow, [AgPlayerShellProbe]::WM_SYSCOMMAND,
        [AgPlayerShellProbe]::SC_RESTORE, [IntPtr]::Zero)
    $restoreDeadline = [DateTime]::UtcNow.AddSeconds(3)
    $foregroundAccepted = $false
    while (([AgPlayerShellProbe]::IsIconic($mainWindow) -or
            -not $foregroundAccepted) -and
           [DateTime]::UtcNow -lt $restoreDeadline) {
        if (-not [AgPlayerShellProbe]::IsIconic($mainWindow)) {
            # Explorer is allowed to activate a taskbar target; a background
            # CTest process is not always granted that right by Windows'
            # foreground-lock policy. Retry the best-effort probe, but keep the
            # deterministic restore/geometry/window-group contract separate.
            [void][AgPlayerShellProbe]::SetForegroundWindow($mainWindow)
            $foregroundAccepted =
                [AgPlayerShellProbe]::GetForegroundWindow() -eq $mainWindow
        }
        Start-Sleep -Milliseconds 25
    }
    if ([AgPlayerShellProbe]::IsIconic($mainWindow)) {
        throw 'SC_RESTORE did not restore the existing player window'
    }
    if (-not $foregroundAccepted) {
        Write-Warning 'Windows foreground lock denied the synthetic CTest activation; Task 13 must verify real Explorer taskbar clicks'
    }
    $after = [AgPlayerShellProbe+Rect]::new()
    if (-not [AgPlayerShellProbe]::GetWindowRect($mainWindow, [ref]$after) -or
        $after.Left -ne $before.Left -or $after.Top -ne $before.Top -or
        $after.Right -ne $before.Right -or $after.Bottom -ne $before.Bottom) {
        throw 'Taskbar minimize/restore changed the player native-pixel geometry'
    }
    foreach ($auxiliary in $auxiliaryBefore) {
        if (-not [AgPlayerShellProbe]::WindowVisible($auxiliary)) {
            throw 'A previously visible docked/auxiliary window did not restore with the player'
        }
    }
} finally {
    if ($null -ne $process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
    }
    Remove-Item -LiteralPath $probeRoot -Recurse -Force -ErrorAction SilentlyContinue
}

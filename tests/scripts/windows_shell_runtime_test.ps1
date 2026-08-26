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
    public const int GWL_EXSTYLE = -20;
    public const long WS_EX_APPWINDOW = 0x00040000L;
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
            '--qa-test-mode', '--qa-log', $logPath) -PassThru `
            -RedirectStandardError $stderrPath
    } finally {
        $env:AGPLAYER_QA_SHELL_PROBE = $priorShellProbe
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $mainWindow = [IntPtr]::Zero
    $identityVerified = $false
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
            $identityVerified = (Get-Content -Raw -LiteralPath $stderrPath) `
                -match 'AgPlayer native taskbar identity verified'
        }
    } while (($mainWindow -eq [IntPtr]::Zero -or -not $identityVerified) -and
             [DateTime]::UtcNow -lt $deadline -and
             -not $process.HasExited)

    if ($mainWindow -eq [IntPtr]::Zero -or -not $identityVerified) {
        $stderr = if (Test-Path -LiteralPath $stderrPath) {
            Get-Content -Raw -LiteralPath $stderrPath
        } else { '' }
        throw "AgPlayer did not publish one taskbar window with complete shell identity:`n$stderr"
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
            Where-Object { $_ -ne $mainWindow }
    )

    [void][AgPlayerShellProbe]::SendMessage(
        $mainWindow, [AgPlayerShellProbe]::WM_SYSCOMMAND,
        [AgPlayerShellProbe]::SC_MINIMIZE, [IntPtr]::Zero)
    $minimizeDeadline = [DateTime]::UtcNow.AddSeconds(3)
    while (-not [AgPlayerShellProbe]::IsIconic($mainWindow) -and
           [DateTime]::UtcNow -lt $minimizeDeadline) {
        Start-Sleep -Milliseconds 25
    }
    if (-not [AgPlayerShellProbe]::IsIconic($mainWindow)) {
        throw 'A second taskbar activation did not minimize the foreground player'
    }
    foreach ($auxiliary in $auxiliaryBefore) {
        if ([AgPlayerShellProbe]::WindowVisible($auxiliary)) {
            throw 'A docked/auxiliary window remained visible after taskbar minimize'
        }
    }

    [void][AgPlayerShellProbe]::SendMessage(
        $mainWindow, [AgPlayerShellProbe]::WM_SYSCOMMAND,
        [AgPlayerShellProbe]::SC_RESTORE, [IntPtr]::Zero)
    [void][AgPlayerShellProbe]::SetForegroundWindow($mainWindow)
    $restoreDeadline = [DateTime]::UtcNow.AddSeconds(3)
    while (([AgPlayerShellProbe]::IsIconic($mainWindow) -or
            [AgPlayerShellProbe]::GetForegroundWindow() -ne $mainWindow) -and
           [DateTime]::UtcNow -lt $restoreDeadline) {
        Start-Sleep -Milliseconds 25
    }
    if ([AgPlayerShellProbe]::IsIconic($mainWindow) -or
        [AgPlayerShellProbe]::GetForegroundWindow() -ne $mainWindow) {
        throw 'Taskbar restore did not reactivate the existing player window'
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

$ErrorActionPreference = "Stop"
$installer = Get-Content -Raw -Encoding UTF8 -LiteralPath $env:AGPLAYER_INSTALLER_SCRIPT
$installer = $installer -replace "`r`n", "`n"
$repo = Split-Path -Parent (Split-Path -Parent $env:AGPLAYER_INSTALLER_SCRIPT)

if ($installer -notmatch '(?m)^UninstallDisplayName=\{#AppName\}$') {
    throw "Installed Apps must display only AgPlayer"
}

if ($installer -notmatch 'Name:\s*"\{autodesktop\}\\\{#AppName\}";\s*Filename:\s*"\{app\}\\\{#AppExeName\}";[^\r\n]*AppUserModelID:\s*"AgPlayer\.Desktop"') {
    throw "Desktop shortcut must be created unconditionally"
}
if ($installer -match '\[InstallDelete\]' -or
    $installer -match 'RegDeleteKeyIncludingSubkeys') {
    throw "Normal installer must preserve the user's AgPlayer data and settings"
}
if ($installer -notmatch '(?m)^DisableDirPage=no$') {
    throw "Installer must allow choosing an installation directory"
}
if ($installer -notmatch '\[Tasks\]' -or
    $installer -notmatch 'Name: "associateaudio"') {
    throw "Installer must provide an optional audio-file association task"
}
if ($installer -match 'InfoBeforeFile=clean-install-warning\.txt') {
    throw "Normal installer must not show the old clean-install test warning"
}
$launchText = 'Description: "' + [char]0x542F + [char]0x52A8 + ' {#AppName}"'
if (-not $installer.Contains($launchText)) {
    throw "Post-install launch text must remain valid UTF-8 Chinese"
}
if (-not (Test-Path -LiteralPath (Join-Path $repo 'assets\brand\agplayer.ico'))) {
    throw "Windows package must include a multi-size application icon"
}
$appCmake = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $repo 'app\CMakeLists.txt')
if ($appCmake -notmatch 'agplayer\.rc') {
    throw "AgPlayer.exe must embed the icon resource for Explorer and taskbar"
}
if ($appCmake -notmatch 'windeployqt') {
    throw "AgPlayer.exe must deploy the Qt runtime after a release link"
}
$stager = Join-Path $repo 'tools\stage_release.ps1'
if (-not (Test-Path -LiteralPath $stager)) {
    throw "Missing release staging script"
}
$stagerSource = Get-Content -Raw -Encoding UTF8 -LiteralPath $stager
if ($stagerSource -notmatch 'platforms\\qwindows\.dll') {
    throw "Release staging must verify the Windows platform plugin"
}
if ($stagerSource -notmatch "'AgSeparationWorker\.exe'") {
    throw "Release staging must include and verify the on-demand separation Worker"
}
if ($stagerSource -match '\[string\]\$BuildDirectory\s*=\s*\(Join-Path\s+\$PSScriptRoot') {
    throw "Release staging defaults must not evaluate PSScriptRoot inside the parameter block"
}
$packageScriptPath = Join-Path $repo 'scripts\package-windows.ps1'
$packageScript = Get-Content -Raw -Encoding UTF8 -LiteralPath $packageScriptPath
if ($packageScript -notmatch '\$worker\s*=\s*Join-Path\s+\$appDir\s+"AgSeparationWorker\.exe"' -or
    $packageScript -notmatch 'Copy-Item\s+-LiteralPath\s+\$worker\s+-Destination\s+\$stage' -or
    $packageScript -notmatch '"AgSeparationWorker\.exe"') {
    throw "Windows package must stage and validate the on-demand separation Worker"
}
$mainSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $repo 'app\main.cpp')
if ($mainSource -notmatch 'setWindowIcon') {
    throw "QApplication must publish the branded window icon"
}
if ($mainSource -notmatch 'assets/brand/agplayer\.ico') {
    throw "Runtime and taskbar must use the same multi-size Windows icon"
}
if ($mainSource -notmatch 'SetCurrentProcessExplicitAppUserModelID') {
    throw "Windows taskbar identity must use a stable AppUserModelID"
}
if ($installer -notmatch 'AppUserModelID:\s*"AgPlayer\.Desktop"') {
    throw "Installed shortcuts must share the stable taskbar AppUserModelID"
}
$manifestPath = Join-Path $repo 'app\agplayer.manifest'
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Windows executable must declare an explicit DPI/taskbar manifest"
}
$manifest = Get-Content -Raw -Encoding UTF8 -LiteralPath $manifestPath
if ($manifest -notmatch 'PerMonitorV2') {
    throw "Windows manifest must enable per-monitor-v2 DPI handling"
}
foreach ($requiredAssociation in @(
    '[Registry]',
    'Software\Classes\Applications\{#AppExeName}\shell\open\command',
    'Software\Classes\Applications\{#AppExeName}\SupportedTypes',
    'Software\Classes\.mp3\OpenWithProgids'
)) {
    if (-not $installer.Contains($requiredAssociation)) {
        throw "Installer must register AgPlayer in Windows Open With: $requiredAssociation"
    }
}
if ($installer -notmatch 'Tasks:\s*associateaudio') {
    throw "Audio associations must only be written after explicit user opt-in"
}

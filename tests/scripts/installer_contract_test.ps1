$ErrorActionPreference = "Stop"
$installer = Get-Content -Raw -Encoding UTF8 -LiteralPath $env:AGPLAYER_INSTALLER_SCRIPT
$repo = Split-Path -Parent (Split-Path -Parent $env:AGPLAYER_INSTALLER_SCRIPT)
$packageScriptPath = Join-Path $repo 'scripts\package-windows.ps1'
$packageScript = Get-Content -Raw -Encoding UTF8 -LiteralPath $packageScriptPath

if ($packageScript -match 'Visual Studio\\2022\\Community' -or
    $packageScript -notmatch 'vswhere\.exe' -or
    $packageScript -notmatch 'Microsoft\.VisualStudio\.Product\.BuildTools') {
    throw "Release packaging must select the current complete MSVC Build Tools instead of a stale hard-coded IDE"
}

if ($installer -notmatch '(?m)^UninstallDisplayName=\{#AppName\}\r?$') {
    throw "Installed Apps must display only AgPlayer"
}

if ($installer -notmatch 'Name:\s*"\{autodesktop\}\\\{#AppName\}";\s*Filename:\s*"\{app\}\\\{#AppExeName\}";[^\r\n]*WorkingDir:\s*"\{app\}";[^\r\n]*IconFilename:\s*"\{app\}\\\{#AppExeName\}";[^\r\n]*AppUserModelID:\s*"AgPlayer\.Desktop"') {
    throw "Desktop shortcut must be created unconditionally"
}
if ($installer -match '\[InstallDelete\]' -or
    $installer -match 'RegDeleteKeyIncludingSubkeys') {
    throw "Normal installer must preserve the user's AgPlayer data and settings"
}
if ($installer -notmatch '\[Code\]' -or
    $installer -notmatch 'InitializeUninstall' -or
    $installer -notmatch 'MsgBox' -or
    $installer -notmatch 'DelTree\(ExpandConstant\(') {
    throw "Uninstaller must explicitly offer deletion of personal playlists, favorites, and settings"
}
if ($installer -notmatch '(?m)^DisableDirPage=no\r?$') {
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
$iconSource = Join-Path $repo 'assets\brand\agplayer-icon.png'
if (-not (Test-Path -LiteralPath $iconSource)) {
    throw "Windows icon must retain the approved high-resolution source artwork"
}
Add-Type -AssemblyName System.Drawing
$iconSourceImage = [System.Drawing.Image]::FromFile($iconSource)
try {
    if ($iconSourceImage.Width -lt 256 -or $iconSourceImage.Height -lt 256) {
        throw "Windows icon source must remain high-resolution"
    }
} finally {
    $iconSourceImage.Dispose()
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
if ((Get-Content -Raw -Encoding UTF8 -LiteralPath $stager) -notmatch 'platforms\\qwindows\.dll') {
    throw "Release staging must verify the Windows platform plugin"
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
if ($mainSource -notmatch 'SHGetPropertyStoreForWindow' -or
    $mainSource -notmatch 'PKEY_AppUserModel_ID' -or
    $mainSource -notmatch 'PKEY_AppUserModel_RelaunchCommand' -or
    $mainSource -notmatch 'PKEY_AppUserModel_RelaunchDisplayNameResource' -or
    $mainSource -notmatch 'PKEY_AppUserModel_RelaunchIconResource' -or
    $mainSource -notmatch 'window->setIcon') {
    throw "Every native top-level window must publish stable taskbar identity, relaunch metadata, and icon"
}
if ($mainSource -notmatch 'LoadImageW' -or
    $mainSource -notmatch 'MAKEINTRESOURCEW\(kAgPlayerIconResourceId\)' -or
    $mainSource -notmatch 'WM_SETICON' -or
    $mainSource -notmatch 'WM_GETICON') {
    throw "Every native top-level window must publish and verify the PE icon resource"
}
if ($installer -notmatch 'AppUserModelID:\s*"AgPlayer\.Desktop"') {
    throw "Installed shortcuts must share the stable taskbar AppUserModelID"
}
foreach ($identityValue in @(
    'Software\Classes\AppUserModelId\AgPlayer.Desktop',
    'ValueName: "DisplayName"',
    'ValueName: "IconUri"',
    'ValueName: "RelaunchCommand"',
    'ie4uinit.exe -show'
)) {
    if (-not $installer.Contains($identityValue)) {
        throw "Installer must register and refresh the stable taskbar identity: $identityValue"
    }
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

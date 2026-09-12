$ErrorActionPreference = "Stop"
$installer = Get-Content -Raw -Encoding UTF8 -LiteralPath $env:AGPLAYER_INSTALLER_SCRIPT
$repo = Split-Path -Parent (Split-Path -Parent $env:AGPLAYER_INSTALLER_SCRIPT)
$packageScriptPath = Join-Path $repo 'scripts\package-windows.ps1'
$packageScript = Get-Content -Raw -Encoding UTF8 -LiteralPath $packageScriptPath
foreach ($notice in @('lossless-mp3-window-NOTICE.md', 'FFmpeg-LGPL-2.1-or-later.txt')) {
    if (-not (Test-Path -LiteralPath (Join-Path $repo ('LICENSES/' + $notice))) -or
        -not $packageScript.Contains('licenses/' + $notice)) {
        throw "The MP3 analysis-window attribution and license must be shipped: $notice"
    }
}

if ($installer -match '#define AppVersion\s+"[0-9]+\.[0-9]+\.[0-9]+"' -or
    $installer -notmatch '#ifndef AppVersion' -or
    $packageScript -notmatch 'release-version\.ps1' -or
    $packageScript -notmatch '/DAppVersion=') {
    throw "Installer version and output name must derive from the repository release version"
}
if ($packageScript -notmatch '\.VersionInfo' -or
    $packageScript -notmatch '\.FileVersion' -or
    $packageScript -notmatch '\.ProductVersion' -or
    $packageScript -notmatch 'does not match release version') {
    throw "Packaging must reject an executable whose PE version differs from the release version"
}

if ($installer -notmatch '(?m)^ShowLanguageDialog=no\r?$' -or
    $installer -notmatch '(?m)^LanguageDetectionMethod=none\r?$' -or
    $installer -notmatch 'Name:\s*"chinesesimplified"' -or
    $installer -notmatch 'Name:\s*"english"') {
    throw "Installer must start in Chinese without a language dialog"
}
if ($installer -match '(?im)^Name:\s*"(?:thai|vietnamese)"' -or
    $installer -match '(?im)^(?:thai|vietnamese)\.') {
    throw "Installer languages and custom messages must be limited to Simplified Chinese and English"
}
if ($installer.IndexOf('Name: "chinesesimplified"') -gt
    $installer.IndexOf('Name: "english"')) {
    throw "Simplified Chinese must be the first/default installer language"
}
foreach ($localizedContract in @(
    'chinesesimplified.UninstallPersonalDataPrompt=',
    'english.UninstallPersonalDataPrompt=',
    'chinesesimplified.AssociateAudioTask=',
    'english.AssociateAudioTask=',
    'chinesesimplified.LaunchAgPlayer=',
    'english.LaunchAgPlayer=',
    "ExpandConstant('{cm:UninstallPersonalDataPrompt}')"
)) {
    if (-not $installer.Contains($localizedContract)) {
        throw "Installer/uninstaller prompts must follow the selected language: $localizedContract"
    }
}
foreach ($languageFile in @(
    'installer\languages\ChineseSimplified.isl'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $repo $languageFile))) {
        throw "Installer language resource is missing: $languageFile"
    }
}
if ($installer -notmatch '(?m)^WizardImageFile=\.\.\\assets\\brand\\installer-wizard\.png\r?$' -or
    $installer -notmatch '(?m)^WizardSmallImageFile=\.\.\\assets\\brand\\installer-small\.png\r?$') {
    throw "Installer wizard must use dedicated, correctly proportioned AgPlayer artwork"
}
$wizardImagePath = Join-Path $repo 'assets\brand\installer-wizard.png'
$smallImagePath = Join-Path $repo 'assets\brand\installer-small.png'
foreach ($requiredArtwork in @($wizardImagePath, $smallImagePath)) {
    if (-not (Test-Path -LiteralPath $requiredArtwork)) {
        throw "Missing dedicated installer artwork: $requiredArtwork"
    }
}
Add-Type -AssemblyName System.Drawing
$wizardImage = [System.Drawing.Bitmap]::FromFile($wizardImagePath)
$smallImage = [System.Drawing.Bitmap]::FromFile($smallImagePath)
try {
    $wizardAspect = $wizardImage.Width / [double]$wizardImage.Height
    if ($wizardImage.Width -lt 328 -or $wizardImage.Height -lt 628 -or
        $wizardAspect -lt 0.48 -or $wizardAspect -gt 0.56) {
        throw "Wizard artwork must use the modern Inno vertical aspect without stretching"
    }
    if ($smallImage.Width -ne $smallImage.Height -or $smallImage.Width -lt 110 -or
        -not [System.Drawing.Image]::IsAlphaPixelFormat($smallImage.PixelFormat)) {
        throw "Wizard header artwork must be a high-DPI transparent square brand mark"
    }
} finally {
    $wizardImage.Dispose()
    $smallImage.Dispose()
}
if ($installer -match '(?m)^PinToTaskbar=' -or
    $installer -match 'taskbarpin') {
    throw "Installer must not pin AgPlayer to the taskbar"
}

# Follow the compiler that configured this build, not whichever VS installation
# happens to be newest. package_version_guard_test exercises this environment.
if ($packageScript -match 'Visual Studio\\2022\\Community' -or
    $packageScript -match 'vswhere\.exe' -or
    $packageScript -notmatch 'CMAKE_CXX_COMPILER' -or
    $packageScript -notmatch '-vcvars_ver=\{1\}' -or
    $packageScript -notmatch '\$vcvars,\s*\$msvcVersion' -or
    $packageScript -notmatch 'Get-Command cl\.exe' -or
    $packageScript -notmatch 'GetFullPath\(\$activeCompiler\)\s*-ne\s*\$configuredCompiler' -or
    $packageScript -notmatch 'throw "Active compiler') {
    throw "Release packaging must initialize the cached CMake MSVC toolset and reject a mismatched active compiler"
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
if ($installer -notmatch 'if not UninstallSilent then begin') {
    throw "Silent uninstall must preserve personal data without blocking on the interactive deletion prompt"
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
if ($installer -notmatch 'Description:\s*"\{cm:LaunchAgPlayer\}"') {
    throw "Post-install launch text must follow the selected installer language"
}
if (-not (Test-Path -LiteralPath (Join-Path $repo 'assets\brand\agplayer.ico'))) {
    throw "Windows package must include a multi-size application icon"
}
$iconSource = Join-Path $repo 'assets\brand\agplayer-icon.png'
if (-not (Test-Path -LiteralPath $iconSource)) {
    throw "Windows icon must retain the approved high-resolution source artwork"
}
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
$packageScriptPath = Join-Path $repo 'scripts\package-windows.ps1'
$packageScript = Get-Content -Raw -Encoding UTF8 -LiteralPath $packageScriptPath
if ($packageScript -notmatch '\$worker\s*=\s*Join-Path\s+\$appDir\s+"AgSeparationWorker\.exe"' -or
    $packageScript -notmatch 'Copy-Item\s+-LiteralPath\s+\$worker\s+-Destination\s+\$stage' -or
    $packageScript -notmatch '"AgSeparationWorker\.exe"') {
    throw "Windows package must stage and validate the on-demand separation Worker"
}
if ($packageScript -notmatch 'Copy-Item\s+-LiteralPath\s+\$thirdPartyNotices\s+-Destination\s+\$stage' -or
    $packageScript -notmatch 'Copy-Item\s+-LiteralPath\s+\$licenseSource\s+-Destination\s+\$stage\s+-Recurse' -or
    $packageScript -notmatch '(?s)\$requiredRuntime\s*=\s*@\(.*?"THIRD-PARTY-NOTICES\.md".*?"licenses/AgPlayer-Icons-License\.txt".*?"licenses/Lucide-Icons-License\.txt".*?"licenses/RemixIcon-Apache-2\.0\.txt".*?\)') {
    throw "Windows package must stage and validate third-party notices and license files"
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
$manifestPath = Join-Path $repo 'app\agplayer.manifest.in'
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

if ($packageScript -notmatch 'Remove-QmlToolingMetadata\s+-StageDirectory\s+\$stage') {
    throw 'Windows runtime staging must omit QML development type descriptions'
}
& (Join-Path $PSScriptRoot 'package_qml_metadata_test.ps1') -PackageScript $packageScriptPath

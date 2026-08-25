[CmdletBinding()]
param(
    [string]$BuildDirectory = "build/msvc-release",
    [string]$Configuration = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$build = Join-Path $repo $BuildDirectory
$appDir = Join-Path $build "app"
$exe = Join-Path $appDir "AgPlayer.exe"
$stage = Join-Path $repo "build/package/AgPlayer"
$installerOutput = Join-Path $repo "build/installer"
$cmakeCache = Join-Path $build "CMakeCache.txt"
$versionTool = Join-Path $repo 'scripts/release-version.ps1'
$versionOutput = (& $versionTool -SourceRoot $repo | Out-String).Trim()
if ($versionOutput -notmatch '^AgPlayer release version: ([0-9]+\.[0-9]+\.[0-9]+)$') {
    throw "Release version validation failed: $versionOutput"
}
$appVersion = $Matches[1]
if (-not (Test-Path -LiteralPath $cmakeCache)) {
    throw "CMake cache not found: $cmakeCache"
}
$qtDirEntry = Select-String -LiteralPath $cmakeCache `
    -Pattern '^Qt6_DIR:PATH=(.+)$' | Select-Object -First 1
if ($null -eq $qtDirEntry) {
    throw "Qt6_DIR was not found in $cmakeCache"
}
$qtCmakeDir = $qtDirEntry.Matches[0].Groups[1].Value
$qtRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $qtCmakeDir))
$windeployqt = Join-Path $qtRoot "bin/windeployqt.exe"
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio locator was not found: $vswhere"
}
$vsInstall = & $vswhere -latest `
    -products Microsoft.VisualStudio.Product.BuildTools `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vsInstall) {
    $vsInstall = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
}
if (-not $vsInstall) {
    throw "A complete Visual C++ Build Tools installation was not found"
}
$vsShell = Join-Path $vsInstall "Common7\Tools\Launch-VsDevShell.ps1"
$isccCandidates = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe"
)
$iscc = $isccCandidates | Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if (-not $iscc) { throw "Inno Setup compiler was not found" }

foreach ($required in @($build, $windeployqt, $vsShell, $iscc)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required path not found: $required"
    }
}

& $vsShell -Arch amd64 -HostArch amd64
if (-not $SkipBuild) {
    cmake --build $build --config $Configuration --target AgPlayer --parallel
    if ($LASTEXITCODE -ne 0) { throw "Release build failed" }
}
if (-not (Test-Path -LiteralPath $exe)) {
    throw "AgPlayer executable not found: $exe"
}
$expectedPeVersion = "$appVersion.0"
$exeVersionInfo = (Get-Item -LiteralPath $exe).VersionInfo
$actualFileVersion = ([string]$exeVersionInfo.FileVersion).Trim()
$actualProductVersion = ([string]$exeVersionInfo.ProductVersion).Trim()
if ($actualFileVersion -ne $expectedPeVersion -or
    $actualProductVersion -ne $expectedPeVersion) {
    throw "AgPlayer.exe PE version $actualFileVersion / $actualProductVersion does not match release version $expectedPeVersion"
}

if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null
New-Item -ItemType Directory -Path $installerOutput -Force | Out-Null

Copy-Item -LiteralPath $exe -Destination $stage
# CMake's post-build deployment directory may contain transitive Windows
# system DLLs. Copy only the audio libraries that are linked by AgPlayer;
# windeployqt below supplies the Qt runtime itself.
$nativeRuntimeDependencies = @(
    "avcodec-62.dll",
    "avformat-62.dll",
    "avutil-60.dll",
    "swresample-6.dll",
    "libmp3lame.DLL",
    "opus.dll",
    "ogg.dll",
    "vorbis.dll",
    "vorbisenc.dll"
)
foreach ($dependency in $nativeRuntimeDependencies) {
    $source = Join-Path $appDir $dependency
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Native runtime dependency not found: $dependency"
    }
    Copy-Item -LiteralPath $source -Destination $stage
}

& $windeployqt `
    --release `
    --no-compiler-runtime `
    --no-translations `
    --qmldir (Join-Path $repo "app/qml") `
    --dir $stage `
    (Join-Path $stage "AgPlayer.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

# AgPlayer fixes Qt Quick Controls to the Basic style in main.cpp. Remove
# deployment-only debugger modules and the six unused alternative styles.
# They are never loaded at runtime and otherwise add several megabytes.
$unusedDeploymentPaths = @(
    "qmltooling",
    "qml/QtQuick/Controls/FluentWinUI3",
    "qml/QtQuick/Controls/Fusion",
    "qml/QtQuick/Controls/Imagine",
    "qml/QtQuick/Controls/Material",
    "qml/QtQuick/Controls/Universal",
    "qml/QtQuick/Controls/Windows",
    "Qt6QuickControls2FluentWinUI3StyleImpl.dll",
    "Qt6QuickControls2Fusion.dll",
    "Qt6QuickControls2FusionStyleImpl.dll",
    "Qt6QuickControls2Imagine.dll",
    "Qt6QuickControls2ImagineStyleImpl.dll",
    "Qt6QuickControls2Material.dll",
    "Qt6QuickControls2MaterialStyleImpl.dll",
    "Qt6QuickControls2Universal.dll",
    "Qt6QuickControls2UniversalStyleImpl.dll",
    "Qt6QuickControls2WindowsStyleImpl.dll"
)
$stageRoot = [IO.Path]::GetFullPath($stage).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
foreach ($relativePath in $unusedDeploymentPaths) {
    $target = [IO.Path]::GetFullPath((Join-Path $stage $relativePath))
    if (-not $target.StartsWith($stageRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Deployment cleanup escaped the staging directory: $target"
    }
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
}

$crtDirectory = Join-Path $env:VCToolsRedistDir "x64/Microsoft.VC143.CRT"
if (-not (Test-Path -LiteralPath $crtDirectory)) {
    throw "Visual C++ runtime directory not found: $crtDirectory"
}
Get-ChildItem -LiteralPath $crtDirectory -File -Filter "*.dll" |
    Copy-Item -Destination $stage -Force

$requiredRuntime = @(
    "AgPlayer.exe",
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Qml.dll",
    "Qt6Quick.dll",
    "Qt6Widgets.dll",
    "msvcp140.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "platforms/qwindows.dll",
    "avcodec-62.dll",
    "avformat-62.dll",
    "avutil-60.dll",
    "swresample-6.dll"
)
foreach ($relativePath in $requiredRuntime) {
    $fullPath = Join-Path $stage $relativePath
    if (-not (Test-Path -LiteralPath $fullPath)) {
        throw "Deployment validation failed; missing $relativePath"
    }
}

& $iscc "/DAppVersion=$appVersion" (Join-Path $repo "installer/AgPlayer.iss")
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed" }

$installer = Get-Item -LiteralPath (Join-Path $installerOutput `
    "AgPlayer-Setup-$appVersion-x64.exe") -ErrorAction SilentlyContinue
if ($null -eq $installer) {
    throw "Versioned installer was not produced for $appVersion"
}

$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $installer.FullName
[pscustomobject]@{
    Installer = $installer.FullName
    SizeMB = [math]::Round($installer.Length / 1MB, 2)
    SHA256 = $hash.Hash
}

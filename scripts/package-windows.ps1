[CmdletBinding()]
param(
    [string]$BuildDirectory = "build/msvc-release",
    [string]$Configuration = "Release",
    [switch]$SkipBuild,
    [switch]$NewRelease,
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string]$FromVersion,
    [ValidateSet('Patch', 'Minor', 'Major')]
    [string]$Bump = 'Patch'
)

$ErrorActionPreference = "Stop"
if ($NewRelease -and $SkipBuild) { throw '-NewRelease cannot be combined with -SkipBuild' }
if ($NewRelease -and -not $FromVersion) { throw '-NewRelease requires -FromVersion to make retries idempotent' }
if (-not $NewRelease -and ($FromVersion -or $PSBoundParameters.ContainsKey('Bump'))) {
    throw '-FromVersion and -Bump require -NewRelease'
}

function Publish-Installer {
    param(
        [string]$Iscc,
        [string]$InstallerScript,
        [string]$AppVersion,
        [string]$OutputDirectory
    )
    # Keep candidate and destination on one volume for atomic publication.
    $outputRoot = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $transactionId = [guid]::NewGuid().ToString('N')
    $temporary = Join-Path $outputRoot ('.pending-' + $transactionId)
    New-Item -ItemType Directory -Path $temporary | Out-Null
    try {
        & $Iscc "/DAppVersion=$AppVersion" "/O$temporary" $InstallerScript
        if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed' }
        $name = "AgPlayer-Setup-$AppVersion-x64.exe"
        $candidate = Get-Item -LiteralPath (Join-Path $temporary $name)
        if ($candidate.VersionInfo.FileVersion.Trim() -ne "$AppVersion.0") {
            throw 'Installer PE version does not match release version'
        }
        $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $candidate.FullName
        if ($hash.Hash -notmatch '^[0-9A-Fa-f]{64}$') { throw 'Installer SHA256 validation failed' }
        $destination = Join-Path $outputRoot $name
        $previous = if (Test-Path -LiteralPath $destination) {
            Join-Path $outputRoot ("$name.previous-$transactionId.bak")
        } else { $null }
        $result = [pscustomobject]@{
            Installer = $destination
            SizeMB = [math]::Round($candidate.Length / 1MB, 2)
            SHA256 = $hash.Hash
            PreviousInstaller = $previous
        }
        if ($previous) {
            [IO.File]::Replace($candidate.FullName, $destination, $previous)
        } else {
            [IO.File]::Move($candidate.FullName, $destination)
        }
        $result
    } finally {
        # Cleanup cannot turn an already committed publication into a failure.
        if ([IO.Path]::GetFullPath($temporary).StartsWith($outputRoot, [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function Remove-QmlToolingMetadata {
    param([Parameter(Mandatory = $true)][string]$StageDirectory)

    $root = [IO.Path]::GetFullPath($StageDirectory).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $qmlDirectory = Join-Path $root 'qml'
    $removedBytes = 0L
    $removedFiles = 0
    if (Test-Path -LiteralPath $qmlDirectory -PathType Container) {
        # qmltypes describe types to development tools; qmldir, QML and plugins
        # remain intact for runtime imports. Never trim the Qt installation.
        foreach ($file in Get-ChildItem -LiteralPath $qmlDirectory -Recurse -File -Filter '*.qmltypes') {
            $target = [IO.Path]::GetFullPath($file.FullName)
            if (-not $target.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
                throw "QML metadata cleanup escaped the staging directory: $target"
            }
            Remove-Item -LiteralPath $target -Force
            $removedBytes += $file.Length
            ++$removedFiles
        }
    }
    [pscustomobject]@{ Files = $removedFiles; Bytes = $removedBytes }
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$build = Join-Path $repo $BuildDirectory
$appDir = Join-Path $build "app"
$exe = Join-Path $appDir "AgPlayer.exe"
$worker = Join-Path $appDir "AgSeparationWorker.exe"
$stage = Join-Path $repo "build/package/AgPlayer"
$installerOutput = Join-Path $repo "build/installer"
$thirdPartyNotices = Join-Path $repo "THIRD-PARTY-NOTICES.md"
$licenseSource = Join-Path $repo "assets/licenses"
$losslessLicenseSource = Join-Path $repo "LICENSES"
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
    -Pattern '^Qt6_DIR:[^=]+=(.+)$' | Select-Object -First 1
if ($null -eq $qtDirEntry) {
    throw "Qt6_DIR was not found in $cmakeCache"
}
$qtCmakeDir = $qtDirEntry.Matches[0].Groups[1].Value
$qtRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $qtCmakeDir))
$windeployqt = Join-Path $qtRoot "bin/windeployqt.exe"
$compilerEntry = Select-String -LiteralPath $cmakeCache `
    -Pattern '^CMAKE_CXX_COMPILER:[^=]+=(.+)$' | Select-Object -First 1
if ($null -eq $compilerEntry) { throw "CMAKE_CXX_COMPILER was not found in $cmakeCache" }
$configuredCompiler = [IO.Path]::GetFullPath($compilerEntry.Matches[0].Groups[1].Value)
if ($configuredCompiler -notmatch '^(.*)[\\/]VC[\\/]Tools[\\/]MSVC[\\/]([0-9.]+)[\\/]bin[\\/]Hostx64[\\/]x64[\\/]cl\.exe$') {
    throw "The package build requires a cached x64 MSVC compiler: $configuredCompiler"
}
$vsInstall = $Matches[1]
$msvcVersion = $Matches[2]
$vcvars = Join-Path $vsInstall 'VC/Auxiliary/Build/vcvars64.bat'
$isccCandidates = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe"
)
$iscc = $isccCandidates | Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if (-not $iscc) { throw "Inno Setup compiler was not found" }

foreach ($required in @(
    $build, $windeployqt, $vcvars, $configuredCompiler, $iscc,
    $thirdPartyNotices, $licenseSource,
    (Join-Path $losslessLicenseSource 'lossless-mp3-window-NOTICE.md'),
    (Join-Path $losslessLicenseSource 'FFmpeg-LGPL-2.1-or-later.txt')
)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required path not found: $required"
    }
}

$versionTransactionStarted = $false
try {
if ($NewRelease) {
    $versionPath = Join-Path $repo 'cmake/AgPlayerVersion.cmake'
    $manifestPath = Join-Path $repo 'deployment/updates/latest.json'
    $originalVersion = [IO.File]::ReadAllBytes($versionPath)
    $originalManifest = [IO.File]::ReadAllBytes($manifestPath)
    $versionTransactionStarted = $true
    $versionOutput = (& $versionTool -SourceRoot $repo -Bump $Bump -FromVersion $FromVersion | Out-String).Trim()
    if ($versionOutput -notmatch '^AgPlayer release version: ([0-9]+\.[0-9]+\.[0-9]+)$') {
        throw "Release version validation failed: $versionOutput"
    }
    $appVersion = $Matches[1]
}
# Use the installation and exact toolset already selected by CMake. A newer
# Build Tools installation must not silently replace this compiler environment.
$environmentCommand = 'call "{0}" -vcvars_ver={1} >nul && set' -f $vcvars, $msvcVersion
$compilerEnvironment = & $env:ComSpec /d /c $environmentCommand
if ($LASTEXITCODE -ne 0) { throw 'Unable to initialize the configured MSVC environment' }
foreach ($entry in $compilerEnvironment) {
    if ($entry -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}
$activeCompiler = (Get-Command cl.exe -CommandType Application | Select-Object -First 1).Source
if ([IO.Path]::GetFullPath($activeCompiler) -ne $configuredCompiler) {
    throw "Active compiler $activeCompiler does not match CMake compiler $configuredCompiler"
}
if ($NewRelease) {
    # Regenerate the version header, manifest and PE resources from this source,
    # rather than trusting cached project version values from an older build.
    cmake -S $repo -B $build
    if ($LASTEXITCODE -ne 0) { throw 'Release version reconfiguration failed' }
}
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
if (-not (Test-Path -LiteralPath $worker)) {
    throw "Separation Worker executable not found: $worker"
}

if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null
New-Item -ItemType Directory -Path $installerOutput -Force | Out-Null

Copy-Item -LiteralPath $exe -Destination $stage
Copy-Item -LiteralPath $worker -Destination $stage
Copy-Item -LiteralPath $thirdPartyNotices -Destination $stage
Copy-Item -LiteralPath $licenseSource -Destination $stage -Recurse
foreach ($license in @('lossless-mp3-window-NOTICE.md', 'FFmpeg-LGPL-2.1-or-later.txt')) {
    Copy-Item -LiteralPath (Join-Path $losslessLicenseSource $license) -Destination (Join-Path $stage 'licenses')
}
Copy-Item -LiteralPath (Join-Path $losslessLicenseSource 'runtime') `
    -Destination (Join-Path $stage 'licenses/runtime') -Recurse
# CMake's post-build deployment directory may contain transitive Windows
# system DLLs. Copy only the audio libraries that are linked by AgPlayer;
# windeployqt below supplies the Qt runtime itself.
$nativeRuntimeDependencies = @(
    "SoundTouch.dll",
    "avcodec-62.dll",
    "avformat-62.dll",
    "avutil-60.dll",
    "swscale-9.dll",
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

$qmlMetadataSavings = Remove-QmlToolingMetadata -StageDirectory $stage
Write-Verbose ("Removed {0} QML tooling files ({1} bytes) from runtime staging" -f
    $qmlMetadataSavings.Files, $qmlMetadataSavings.Bytes)

$crtDirectory = Join-Path $env:VCToolsRedistDir "x64/Microsoft.VC143.CRT"
if (-not (Test-Path -LiteralPath $crtDirectory)) {
    throw "Visual C++ runtime directory not found: $crtDirectory"
}
Get-ChildItem -LiteralPath $crtDirectory -File -Filter "*.dll" |
    Copy-Item -Destination $stage -Force

$requiredRuntime = @(
    "SoundTouch.dll",
    "AgPlayer.exe",
    "AgSeparationWorker.exe",
    "THIRD-PARTY-NOTICES.md",
    "licenses/AgPlayer-Icons-License.txt",
    "licenses/Lucide-Icons-License.txt",
    "licenses/RemixIcon-Apache-2.0.txt",
    "licenses/lossless-mp3-window-NOTICE.md",
    "licenses/FFmpeg-LGPL-2.1-or-later.txt",
    "licenses/runtime/README.md",
    "licenses/runtime/Qt-LGPL-3.0-only.txt",
    "licenses/runtime/Qt-GPL-3.0-only.txt",
    "licenses/runtime/Qt-6.7-DEPLOYED-ATTRIBUTIONS.md",
    "licenses/runtime/SoundTouch-LGPL-2.1-only.txt",
    "licenses/runtime/FFmpeg-LGPL-2.1-or-later.txt",
    "licenses/runtime/LAME-LGPL-2.0-only.txt",
    "licenses/runtime/Opus-BSD-3-Clause.txt",
    "licenses/runtime/libogg-BSD-3-Clause.txt",
    "licenses/runtime/libvorbis-BSD-3-Clause.txt",
    "licenses/runtime/miniaudio-Unlicense-or-MIT-0.txt",
    "licenses/runtime/ONNX-Runtime-MIT.txt",
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
    "swscale-9.dll",
    "swresample-6.dll"
)
foreach ($relativePath in $requiredRuntime) {
    $fullPath = Join-Path $stage $relativePath
    if (-not (Test-Path -LiteralPath $fullPath)) {
        throw "Deployment validation failed; missing $relativePath"
    }
}

Publish-Installer -Iscc $iscc -InstallerScript (Join-Path $repo 'installer/AgPlayer.iss') `
    -AppVersion $appVersion -OutputDirectory $installerOutput
} catch {
    if ($versionTransactionStarted) {
        # Restore sources only. Keep prior installers and build artifacts; the
        # PE version guard rejects artifacts that no longer match these sources.
        [IO.File]::WriteAllBytes($versionPath, $originalVersion)
        [IO.File]::WriteAllBytes($manifestPath, $originalManifest)
    }
    throw
}

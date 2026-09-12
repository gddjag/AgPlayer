param(
    [Parameter(Mandatory=$true)][string]$SourceRoot,
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$AppPath
)

$ErrorActionPreference = 'Stop'
# CTest may restrict PSModulePath; load the built-in hash implementation from
# this PowerShell installation before the failure-injection wrapper shadows it.
Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1')
$packageScript = Join-Path $SourceRoot 'scripts\package-windows.ps1'
$versionTool = Join-Path $SourceRoot 'scripts\release-version.ps1'
$currentVersionOutput = (& $versionTool -SourceRoot $SourceRoot | Out-String).Trim()
if ($currentVersionOutput -notmatch '^AgPlayer release version: ([0-9]+\.[0-9]+\.[0-9]+)$') {
    throw "Unable to read current release version: $currentVersionOutput"
}
$currentVersion = $Matches[1]

$scratch = Join-Path ([IO.Path]::GetTempPath()) `
    ('agplayer-package-version-' + [guid]::NewGuid().ToString('N'))
try {
    foreach ($directory in @(
        'app', 'assets\licenses', 'LICENSES', 'cmake', 'installer', 'qt\src', 'scripts',
        'build\stale\app', 'deployment\updates'
    )) {
        New-Item -ItemType Directory -Path (Join-Path $scratch $directory) `
            -Force | Out-Null
    }
    foreach ($relativePath in @(
        'CMakeLists.txt',
        'THIRD-PARTY-NOTICES.md',
        'LICENSES\lossless-mp3-window-NOTICE.md',
        'LICENSES\FFmpeg-LGPL-2.1-or-later.txt',
        'app\CMakeLists.txt',
        'app\agplayer.manifest.in',
        'app\agplayer.rc.in',
        'app\main.cpp',
        'assets\licenses\AgPlayer-Icons-License.txt',
        'assets\licenses\Lucide-Icons-License.txt',
        'assets\licenses\RemixIcon-Apache-2.0.txt',
        'cmake\AgPlayerVersion.cmake',
        'deployment\updates\latest.json',
        'installer\AgPlayer.iss',
        'qt\src\settings_controller.cpp',
        'scripts\package-windows.ps1',
        'scripts\release-version.ps1'
    )) {
        Copy-Item -LiteralPath (Join-Path $SourceRoot $relativePath) `
            -Destination (Join-Path $scratch $relativePath)
    }
    Copy-Item -LiteralPath (Join-Path $SourceRoot 'LICENSES/runtime') `
        -Destination (Join-Path $scratch 'LICENSES/runtime') -Recurse
    $sourceBuild = Join-Path $SourceRoot $BuildDirectory
    Copy-Item -LiteralPath (Join-Path $sourceBuild 'CMakeCache.txt') `
        -Destination (Join-Path $scratch 'build\stale\CMakeCache.txt')
    Copy-Item -LiteralPath $AppPath `
        -Destination (Join-Path $scratch 'build\stale\app\AgPlayer.exe')
    Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $AppPath) `
        'AgSeparationWorker.exe') `
        -Destination (Join-Path $scratch 'build\stale\app\AgSeparationWorker.exe')

    $binaryVersion = ((Get-Item -LiteralPath $AppPath).VersionInfo.ProductVersion).Trim() -replace '\.0$', ''
    $staleVersion = if ($binaryVersion -eq '2.3.4') { '2.3.5' } else { '2.3.4' }
    & (Join-Path $scratch 'scripts\release-version.ps1') `
        -SourceRoot $scratch -Version $staleVersion | Out-Null

    $mismatchRejected = $false
    try {
        & (Join-Path $scratch 'scripts\package-windows.ps1') `
            -BuildDirectory 'build\stale' -Configuration Release -SkipBuild `
            | Out-Null
    } catch {
        if ($_.Exception.Message -notmatch 'does not match release version') {
            throw
        }
        $mismatchRejected = $true
    }
    if (-not $mismatchRejected) {
        throw 'SkipBuild accepted a stale executable with a different PE version'
    }

    & (Join-Path $scratch 'scripts\release-version.ps1') `
        -SourceRoot $scratch -Version $binaryVersion | Out-Null
    $matchingVersionPassedGuard = $false
    try {
        & (Join-Path $scratch 'scripts\package-windows.ps1') `
            -BuildDirectory 'build\stale' -Configuration Release -SkipBuild `
            | Out-Null
    } catch {
        if ($_.Exception.Message -notmatch 'Native runtime dependency not found') {
            throw
        }
        $matchingVersionPassedGuard = $true
    }
    if (-not $matchingVersionPassedGuard) {
        throw 'Matching PE version did not proceed past the version guard'
    }

    # Exercise an actual new-release transaction, replacing only the external
    # CMake process so this regression never compiles or produces an installer.
    $versionPath = Join-Path $scratch 'cmake\AgPlayerVersion.cmake'
    $manifestPath = Join-Path $scratch 'deployment\updates\latest.json'
    $versionBefore = [IO.File]::ReadAllBytes($versionPath)
    $manifestBefore = [IO.File]::ReadAllBytes($manifestPath)
    $cmakeProbe = [pscustomobject]@{ Attempted = $false }
    $configuredCompiler = (Select-String -LiteralPath (Join-Path $sourceBuild 'CMakeCache.txt') `
        -Pattern '^CMAKE_CXX_COMPILER:[^=]+=(.+)$').Matches[0].Groups[1].Value
    function cmake {
        $cmakeProbe.Attempted = $true
        $activeCompiler = (Get-Command cl.exe -CommandType Application | Select-Object -First 1).Source
        if ([IO.Path]::GetFullPath($activeCompiler) -ne [IO.Path]::GetFullPath($configuredCompiler)) {
            throw "Active compiler $activeCompiler differs from cached compiler $configuredCompiler"
        }
        $during = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
        $parts = $binaryVersion.Split('.') | ForEach-Object { [int]$_ }
        $parts[2]++
        if ($during.version -ne ($parts -join '.')) {
            throw 'The new version was not prepared before CMake reconfiguration'
        }
        $global:LASTEXITCODE = 1
    }
    $failedBuildRejected = $false
    try {
        & (Join-Path $scratch 'scripts\package-windows.ps1') `
            -BuildDirectory 'build\stale' -NewRelease -FromVersion $binaryVersion | Out-Null
    } catch {
        $failedBuildRejected = $_.Exception.Message -match 'Release version reconfiguration failed'
        $buildFailure = $_.Exception.Message
    }
    if (-not $cmakeProbe.Attempted -or -not $failedBuildRejected -or
        [Convert]::ToBase64String([IO.File]::ReadAllBytes($versionPath)) -ne [Convert]::ToBase64String($versionBefore) -or
        [Convert]::ToBase64String([IO.File]::ReadAllBytes($manifestPath)) -ne [Convert]::ToBase64String($manifestBefore)) {
        throw "A failed new-release build must roll back both version files byte-for-byte (CMake called: $($cmakeProbe.Attempted); failure: $buildFailure)"
    }

    # Run the actual publication transaction, substituting only ISCC and the
    # hash command. No real installer is compiled or installed.
    $packageAst = [System.Management.Automation.Language.Parser]::ParseFile($packageScript, [ref]$null, [ref]$null)
    $publishFunction = $packageAst.Find({ param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Publish-Installer'
    }, $false)
    if (-not $publishFunction) { throw 'Installer output must have an isolated publication transaction' }
    Invoke-Expression $publishFunction.Extent.Text
    $publicationDirectory = Join-Path $scratch 'publication'
    New-Item -ItemType Directory -Path $publicationDirectory | Out-Null
    $finalInstaller = Join-Path $publicationDirectory "AgPlayer-Setup-$binaryVersion-x64.exe"
    [IO.File]::WriteAllText($finalInstaller, 'previous installer bytes')
    $previousBytes = [IO.File]::ReadAllBytes($finalInstaller)
    $injection = [pscustomobject]@{ Mode = 'compile'; Temporary = '' }
    function Invoke-TestIscc {
        $outputArgument = @($args | Where-Object { $_ -like '/O*' })[0]
        if (-not $outputArgument) { throw 'ISCC must receive an isolated output directory' }
        $injection.Temporary = $outputArgument.Substring(2)
        $candidate = Join-Path $injection.Temporary "AgPlayer-Setup-$binaryVersion-x64.exe"
        if ($injection.Mode -eq 'compile') {
            [IO.File]::WriteAllText($candidate, 'partial installer')
            $global:LASTEXITCODE = 1
        } else {
            Copy-Item -LiteralPath $AppPath -Destination $candidate
            $global:LASTEXITCODE = 0
        }
    }
    function Get-FileHash {
        param($Algorithm, $LiteralPath)
        if ($injection.Mode -eq 'hash') { throw 'Injected installer hash failure' }
        Microsoft.PowerShell.Utility\Get-FileHash -Algorithm $Algorithm -LiteralPath $LiteralPath
    }
    foreach ($mode in @('compile', 'hash')) {
        $injection.Mode = $mode
        $rejected = $false
        try {
            Publish-Installer -Iscc Invoke-TestIscc -InstallerScript 'unused.iss' `
                -AppVersion $binaryVersion -OutputDirectory $publicationDirectory | Out-Null
        } catch {
            $rejected = $_.Exception.Message -match 'Inno Setup compilation failed|Injected installer hash failure'
        }
        if (-not $rejected -or
            [Convert]::ToBase64String([IO.File]::ReadAllBytes($finalInstaller)) -ne [Convert]::ToBase64String($previousBytes) -or
            (Test-Path -LiteralPath $injection.Temporary)) {
            throw "$mode failure must preserve the previous installer and remove partial output"
        }
    }
    $injection.Mode = 'success'
    $published = Publish-Installer -Iscc Invoke-TestIscc -InstallerScript 'unused.iss' `
        -AppVersion $binaryVersion -OutputDirectory $publicationDirectory
    if ($published.SHA256 -ne (Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $AppPath).Hash -or
        [Convert]::ToBase64String([IO.File]::ReadAllBytes($published.PreviousInstaller)) -ne [Convert]::ToBase64String($previousBytes) -or
        (Test-Path -LiteralPath $injection.Temporary)) {
        throw 'Successful atomic publication must retain the old installer and report the verified hash'
    }
} finally {
    if (Test-Path -LiteralPath $scratch) {
        Remove-Item -LiteralPath $scratch -Recurse -Force
    }
}

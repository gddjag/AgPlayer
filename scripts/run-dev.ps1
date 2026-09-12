[CmdletBinding()]
param(
    [string]$BuildDirectory = "build/msvc-release"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "qa-runtime.ps1")

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = (Resolve-Path (Join-Path $repoRoot $BuildDirectory)).Path
$appPath = Join-Path $buildRoot "app/AgPlayer.exe"
$cachePath = Join-Path $buildRoot "CMakeCache.txt"

foreach ($requiredPath in @($appPath, $cachePath)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required development runtime input not found: $requiredPath"
    }
}

$process = Start-AgPlayerProcess `
    -BuildRoot $buildRoot `
    -CachePath $cachePath `
    -AppPath $appPath

[pscustomobject]@{
    Executable = $appPath
    ProcessId = $process.Id
    Result = "RUNNING"
}

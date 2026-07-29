[CmdletBinding()]
param(
    [string]$BuildDirectory = "build/msvc-debug",
    [string]$OutputDirectory = "build/qa/audio-tools-matrix",
    [ValidateSet("zh", "en", "th", "vi")]
    [string[]]$Languages = @("zh", "en", "th", "vi"),
    [ValidateSet(0, 1, 2)]
    [int[]]$Themes = @(0, 1, 2)
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "qa-runtime.ps1")
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = (Resolve-Path (Join-Path $repoRoot $BuildDirectory)).Path
$appPath = Join-Path $buildRoot "app/AgPlayer.exe"
$cachePath = Join-Path $buildRoot "CMakeCache.txt"
$outputPath = Join-Path $repoRoot $OutputDirectory

if (-not (Test-Path -LiteralPath $appPath)) {
    throw "AgPlayer executable not found: $appPath"
}
if (-not (Test-Path -LiteralPath $cachePath)) {
    throw "CMake cache not found: $cachePath"
}

$runtimePaths = Get-AgPlayerRuntimePaths `
    -BuildRoot $buildRoot -CachePath $cachePath

$generalKey = "HKCU:\Software\AgPlayer\AgPlayer\general"
$appearanceKey = "HKCU:\Software\AgPlayer\AgPlayer\appearance"

function Get-RegistryValueState {
    param([string]$Path, [string]$Name)

    if (-not (Test-Path -LiteralPath $Path)) {
        return @{ Exists = $false; Value = $null }
    }
    $property = Get-ItemProperty -LiteralPath $Path
    $exists = $property.PSObject.Properties.Name -contains $Name
    return @{
        Exists = $exists
        Value = if ($exists) { $property.$Name } else { $null }
    }
}

function Restore-RegistryValue {
    param(
        [string]$Path,
        [string]$Name,
        [hashtable]$State,
        [ValidateSet("String", "DWord")]
        [string]$PropertyType
    )

    if ($State.Exists) {
        New-ItemProperty -LiteralPath $Path -Name $Name `
            -PropertyType $PropertyType -Value $State.Value -Force | Out-Null
    } else {
        Remove-ItemProperty -LiteralPath $Path -Name $Name `
            -ErrorAction SilentlyContinue
    }
}

$languageState = Get-RegistryValueState $generalKey "language"
$themeState = Get-RegistryValueState $appearanceKey "themeMode"
$originalPath = $env:Path

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
New-Item -ItemType Directory -Force -Path $generalKey | Out-Null
New-Item -ItemType Directory -Force -Path $appearanceKey | Out-Null

try {
    $env:Path = ($runtimePaths -join ";") + ";" + $originalPath

    foreach ($language in $Languages) {
        New-ItemProperty -LiteralPath $generalKey -Name "language" `
            -PropertyType String -Value $language -Force | Out-Null

        foreach ($theme in $Themes) {
            New-ItemProperty -LiteralPath $appearanceKey -Name "themeMode" `
                -PropertyType DWord -Value $theme -Force | Out-Null

            $target = Join-Path $outputPath `
                ("tools-{0}-theme{1}.png" -f $language, $theme)
            $process = Start-Process -FilePath $appPath `
                -ArgumentList @("--qa-screenshot-tools", $target) `
                -Wait -PassThru
            if ($process.ExitCode -ne 0) {
                throw "Screenshot failed for $language theme $theme " +
                    "(exit $($process.ExitCode))"
            }
            if (-not (Test-Path -LiteralPath $target) -or
                (Get-Item -LiteralPath $target).Length -lt 10000) {
                throw "Invalid screenshot: $target"
            }

            Get-Item -LiteralPath $target |
                Select-Object Name, Length, LastWriteTime
        }
    }
}
finally {
    Restore-RegistryValue $generalKey "language" $languageState "String"
    Restore-RegistryValue $appearanceKey "themeMode" $themeState "DWord"
    $env:Path = $originalPath
}

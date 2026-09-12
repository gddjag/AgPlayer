[CmdletBinding()]
param(
    [string]$BuildDirectory = "build/msvc-debug",
    [string]$OutputDirectory = "build/qa/audio-tools-matrix",
    [ValidateSet("zh", "en")]
    [string[]]$Languages = @("zh", "en"),
    [ValidateSet(0, 1, 2)]
    [int[]]$Themes = @(0, 1, 2),
    [ValidateRange(0, 4)]
    [int[]]$Tools = @(0, 4, 1, 2, 3),
    [ValidateSet("1672x941", "1280x720", "880x560")]
    [string[]]$Sizes = @("1672x941", "1280x720", "880x560"),
    [string]$QaImportFile = "",
    [long]$EditorSelectionStartMs = -1,
    [long]$EditorSelectionEndMs = -1,
    [long]$EditorPlayheadMs = -1,
    [switch]$ReferenceEditorState
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "qa-runtime.ps1")
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = (Resolve-Path (Join-Path $repoRoot $BuildDirectory)).Path
$appPath = Join-Path $buildRoot "app/AgPlayer.exe"
$cachePath = Join-Path $buildRoot "CMakeCache.txt"
$outputPath = Join-Path $repoRoot $OutputDirectory
$qaFixture = if ($QaImportFile) {
    (Resolve-Path -LiteralPath $QaImportFile).Path
} else {
    $candidate = Join-Path $buildRoot "tests/fixtures/sine-440hz.wav"
    if (Test-Path -LiteralPath $candidate) { $candidate } else { "" }
}

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
        New-Item -Force -Path $Path | Out-Null
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
New-Item -Force -Path "HKCU:\Software\AgPlayer\AgPlayer" | Out-Null
New-Item -Force -Path $generalKey | Out-Null
New-Item -Force -Path $appearanceKey | Out-Null

try {
    $env:Path = ($runtimePaths -join ";") + ";" + $originalPath

    foreach ($language in $Languages) {
        New-Item -Force -Path $generalKey | Out-Null
        New-ItemProperty -LiteralPath $generalKey -Name "language" `
            -PropertyType String -Value $language -Force | Out-Null

        foreach ($theme in $Themes) {
            $themeName = switch ($theme) {
                0 { "dark" }
                1 { "light" }
                default { "system" }
            }
            New-Item -Force -Path $appearanceKey | Out-Null
            New-ItemProperty -LiteralPath $appearanceKey -Name "themeMode" `
                -PropertyType DWord -Value $theme -Force | Out-Null

            foreach ($tool in $Tools) {
              foreach ($size in $Sizes) {
                $parts = $size -split "x"
                $width = [int]$parts[0]
                $height = [int]$parts[1]
                $target = Join-Path $outputPath `
                    ("tools-{0}-theme{1}-tool{2}-{3}.png" -f `
                        $language, $theme, $tool, $size)
                $arguments = @("--qa-test-mode",
                               "--qa-language", $language,
                               "--qa-theme", $themeName,
                               "--qa-tool", $tool,
                               "--qa-tools-size", $width, $height,
                               "--qa-screenshot-tools", $target)
                if ($qaFixture) {
                    $arguments += @("--qa-import-folder", $qaFixture)
                }
                if ($tool -eq 0 -and $EditorSelectionStartMs -ge 0 -and
                    $EditorSelectionEndMs -gt $EditorSelectionStartMs) {
                    $arguments += @("--qa-editor-selection-ms",
                        $EditorSelectionStartMs, $EditorSelectionEndMs)
                }
                if ($tool -eq 0 -and $EditorPlayheadMs -ge 0) {
                    $arguments += @("--qa-editor-playhead-ms", $EditorPlayheadMs)
                }
                if ($tool -eq 0 -and $ReferenceEditorState) {
                    $arguments += "--qa-editor-reference-state"
                }
                $quotedArguments = $arguments | ForEach-Object {
                    $value = [string]$_
                    if ($value -match '[\s"]') {
                        '"' + ($value -replace '"', '\"') + '"'
                    } else {
                        $value
                    }
                }
                $process = Start-Process -FilePath $appPath `
                    -ArgumentList $quotedArguments `
                    -Wait -PassThru
                if ($process.ExitCode -ne 0) {
                    throw "Screenshot failed for $language theme $theme " +
                        "tool $tool (exit $($process.ExitCode))"
                }
                if (-not (Test-Path -LiteralPath $target) -or
                    (Get-Item -LiteralPath $target).Length -lt 10000) {
                    throw "Invalid screenshot: $target"
                }
                Add-Type -AssemblyName System.Drawing
                $bitmap = [System.Drawing.Bitmap]::new($target)
                try {
                    if ($bitmap.Width -ne $width -or $bitmap.Height -ne $height) {
                        throw "Screenshot $target is $($bitmap.Width)x$($bitmap.Height); expected ${width}x${height}"
                    }
                } finally {
                    $bitmap.Dispose()
                }

                Get-Item -LiteralPath $target |
                    Select-Object Name, Length, LastWriteTime
              }
            }
        }
    }
}
finally {
    Restore-RegistryValue $generalKey "language" $languageState "String"
    Restore-RegistryValue $appearanceKey "themeMode" $themeState "DWord"
    $env:Path = $originalPath
}

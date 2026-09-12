[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$ScreenReference,
    [Parameter(Mandatory)]
    [string]$SettingsReference,
    [Parameter(Mandatory)]
    [string]$EditorReference,
    [string]$FormatReference,
    [string]$MetadataReference,
    [string]$FilenameReference,
    [string]$MiniReference,
    [string]$LibraryReference,
    [string]$DetailsReference,
    [string]$ScreenshotDirectory = "build/qa/final-ui-matrix-v2",
    [string]$AudioToolsScreenshotDirectory,
    [string]$OutputDirectory = "build/qa/comparisons"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$screenshots = (Resolve-Path (Join-Path $repoRoot $ScreenshotDirectory)).Path
$audioToolScreenshots = if ($AudioToolsScreenshotDirectory) {
    (Resolve-Path (Join-Path $repoRoot $AudioToolsScreenshotDirectory)).Path
} else {
    $screenshots
}
$output = Join-Path $repoRoot $OutputDirectory
New-Item -ItemType Directory -Force -Path $output | Out-Null

function New-Comparison {
    param(
        [string]$ReferencePath,
        [string]$CurrentPath,
        [string]$OutputPath,
        [System.Drawing.Rectangle]$ReferenceCrop
    )

    $reference = [System.Drawing.Bitmap]::new($ReferencePath)
    $current = [System.Drawing.Bitmap]::new($CurrentPath)
    $labelHeight = 36
    $canvas = [System.Drawing.Bitmap]::new(
        $current.Width, ($current.Height + $labelHeight) * 2)
    $graphics = [System.Drawing.Graphics]::FromImage($canvas)
    $font = [System.Drawing.Font]::new(
        "Segoe UI", 15, [System.Drawing.FontStyle]::Bold)
    $brush = [System.Drawing.SolidBrush]::new(
        [System.Drawing.Color]::White)
    try {
        $graphics.Clear([System.Drawing.Color]::FromArgb(255, 12, 16, 28))
        $graphics.InterpolationMode =
            [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

        $graphics.DrawString("REFERENCE", $font, $brush, 12, 7)
        $graphics.DrawImage(
            $reference,
            [System.Drawing.Rectangle]::new(
                0, $labelHeight, $current.Width, $current.Height),
            $ReferenceCrop,
            [System.Drawing.GraphicsUnit]::Pixel)

        $secondLabelY = $current.Height + $labelHeight
        $graphics.DrawString(
            "IMPLEMENTATION", $font, $brush, 12, $secondLabelY + 7)
        $graphics.DrawImage(
            $current, 0, $secondLabelY + $labelHeight,
            $current.Width, $current.Height)
        $canvas.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $brush.Dispose()
        $font.Dispose()
        $graphics.Dispose()
        $canvas.Dispose()
        $current.Dispose()
        $reference.Dispose()
    }
}

function Get-FullImageRectangle {
    param([string]$Path)
    $image = [System.Drawing.Image]::FromFile($Path)
    try {
        return [System.Drawing.Rectangle]::new(0, 0, $image.Width, $image.Height)
    }
    finally {
        $image.Dispose()
    }
}

foreach ($required in @(
    $ScreenReference,
    $SettingsReference,
    $EditorReference,
    (Join-Path $screenshots "zh-dark-playback.png"),
    (Join-Path $screenshots "zh-dark-list.png"),
    (Join-Path $screenshots "zh-dark-settings.png"),
    (Join-Path $screenshots "zh-dark-tool-1.png")
)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Comparison input not found: $required"
    }
}

if ($MiniReference -and -not (Test-Path -LiteralPath $MiniReference)) {
    throw "Comparison input not found: $MiniReference"
}
foreach ($optionalReference in @(
    $FormatReference, $MetadataReference, $FilenameReference,
    $MiniReference, $LibraryReference, $DetailsReference
)) {
    if ($optionalReference -and -not (Test-Path -LiteralPath $optionalReference)) {
        throw "Comparison input not found: $optionalReference"
    }
}

$audioToolComparisons = @(
    @{ Reference = $EditorReference; Current = "tools-zh-theme0-tool0.png"; Output = "editor-reference-vs-current.png" },
    @{ Reference = $FormatReference; Current = "tools-zh-theme0-tool1.png"; Output = "format-reference-vs-current.png" },
    @{ Reference = $MetadataReference; Current = "tools-zh-theme0-tool2.png"; Output = "metadata-reference-vs-current.png" },
    @{ Reference = $FilenameReference; Current = "tools-zh-theme0-tool3.png"; Output = "filename-reference-vs-current.png" }
)

foreach ($comparison in $audioToolComparisons) {
    if (-not $comparison.Reference) {
        continue
    }
    $currentPath = Join-Path $audioToolScreenshots $comparison.Current
    if (-not (Test-Path -LiteralPath $currentPath)) {
        throw "Comparison input not found: $currentPath"
    }
    New-Comparison `
        -ReferencePath $comparison.Reference `
        -CurrentPath $currentPath `
        -OutputPath (Join-Path $output $comparison.Output) `
        -ReferenceCrop (Get-FullImageRectangle $comparison.Reference)
}

New-Comparison `
    -ReferencePath $ScreenReference `
    -CurrentPath (Join-Path $screenshots "zh-dark-playback.png") `
    -OutputPath (Join-Path $output "playback-reference-vs-current.png") `
    -ReferenceCrop ([System.Drawing.Rectangle]::new(92, 34, 1240, 410))

New-Comparison `
    -ReferencePath $ScreenReference `
    -CurrentPath (Join-Path $screenshots "zh-dark-list.png") `
    -OutputPath (Join-Path $output "list-reference-vs-current.png") `
    -ReferenceCrop ([System.Drawing.Rectangle]::new(88, 452, 1240, 551))

New-Comparison `
    -ReferencePath $SettingsReference `
    -CurrentPath (Join-Path $screenshots "zh-dark-settings.png") `
    -OutputPath (Join-Path $output "settings-reference-vs-current.png") `
    -ReferenceCrop ([System.Drawing.Rectangle]::new(0, 0, 1122, 822))

if (-not $AudioToolsScreenshotDirectory) {
    New-Comparison `
        -ReferencePath $EditorReference `
        -CurrentPath (Join-Path $screenshots "zh-dark-tool-1.png") `
        -OutputPath (Join-Path $output "editor-reference-vs-current.png") `
        -ReferenceCrop (Get-FullImageRectangle $EditorReference)
}

if ($MiniReference) {
    New-Comparison `
        -ReferencePath $MiniReference `
        -CurrentPath (Join-Path $screenshots "zh-dark-mini.png") `
        -OutputPath (Join-Path $output "mini-reference-vs-current.png") `
        -ReferenceCrop ([System.Drawing.Rectangle]::new(110, 338, 1228, 412))
}

if ($LibraryReference) {
    New-Comparison `
        -ReferencePath $LibraryReference `
        -CurrentPath (Join-Path $screenshots "zh-dark-library.png") `
        -OutputPath (Join-Path $output "library-reference-vs-current.png") `
        -ReferenceCrop (Get-FullImageRectangle $LibraryReference)
}

if ($DetailsReference) {
    New-Comparison `
        -ReferencePath $DetailsReference `
        -CurrentPath (Join-Path $screenshots "zh-dark-details.png") `
        -OutputPath (Join-Path $output "details-reference-vs-current.png") `
        -ReferenceCrop ([System.Drawing.Rectangle]::new(110, 476, 1218, 552))
}

[pscustomobject]@{
    Comparisons = (4 + [int][bool]$FormatReference + [int][bool]$MetadataReference + [int][bool]$FilenameReference + [int][bool]$MiniReference + [int][bool]$LibraryReference + [int][bool]$DetailsReference)
    Output = $output
    Result = "PASS"
}

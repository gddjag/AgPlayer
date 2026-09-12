[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Reference,
    [Parameter(Mandatory = $true)]
    [string]$Candidate,
    [string]$OutputDirectory = "build/qa/phase6/comparisons",
    [string]$Stem = "editor-1672x941"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$referencePath = (Resolve-Path -LiteralPath $Reference).Path
$candidatePath = (Resolve-Path -LiteralPath $Candidate).Path
$outputPath = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory
} else {
    Join-Path $repoRoot $OutputDirectory
}
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

$comparisonPath = Join-Path $outputPath ($Stem + "-comparison.png")
$differenceMaskPath = Join-Path $outputPath ($Stem + "-difference-mask.png")
$metricsPath = Join-Path $outputPath ($Stem + "-difference.txt")

$referenceBitmap = [Drawing.Bitmap]::new($referencePath)
$candidateBitmap = [Drawing.Bitmap]::new($candidatePath)
try {
    if ($referenceBitmap.Width -ne $candidateBitmap.Width -or
        $referenceBitmap.Height -ne $candidateBitmap.Height) {
        throw ("Reference is {0}x{1}; candidate is {2}x{3}. " +
            "Phase 6 comparison requires equal logical dimensions." -f
            $referenceBitmap.Width, $referenceBitmap.Height,
            $candidateBitmap.Width, $candidateBitmap.Height)
    }

    $labelHeight = 32
    $comparison = [Drawing.Bitmap]::new(
        $candidateBitmap.Width,
        ($candidateBitmap.Height + $labelHeight) * 2)
    $graphics = [Drawing.Graphics]::FromImage($comparison)
    $font = [Drawing.Font]::new("Segoe UI", 13, [Drawing.FontStyle]::Bold)
    $brush = [Drawing.SolidBrush]::new([Drawing.Color]::White)
    try {
        $graphics.Clear([Drawing.Color]::FromArgb(255, 3, 20, 38))
        $graphics.DrawString("SOURCE REFERENCE", $font, $brush, 10, 6)
        $graphics.DrawImageUnscaled($referenceBitmap, 0, $labelHeight)
        $candidateLabelY = $labelHeight + $candidateBitmap.Height
        $graphics.DrawString("PHASE 6 CANDIDATE", $font, $brush,
            10, $candidateLabelY + 6)
        $graphics.DrawImageUnscaled(
            $candidateBitmap, 0, $candidateLabelY + $labelHeight)
        $comparison.Save($comparisonPath, [Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $brush.Dispose()
        $font.Dispose()
        $graphics.Dispose()
        $comparison.Dispose()
    }

    $rectangle = [Drawing.Rectangle]::new(
        0, 0, $candidateBitmap.Width, $candidateBitmap.Height)
    $pixelFormat = [Drawing.Imaging.PixelFormat]::Format32bppArgb
    $referenceData = $referenceBitmap.LockBits(
        $rectangle, [Drawing.Imaging.ImageLockMode]::ReadOnly, $pixelFormat)
    $candidateData = $candidateBitmap.LockBits(
        $rectangle, [Drawing.Imaging.ImageLockMode]::ReadOnly, $pixelFormat)
    try {
        $byteCount = [Math]::Abs($referenceData.Stride) * $referenceData.Height
        $referenceBytes = [byte[]]::new($byteCount)
        $candidateBytes = [byte[]]::new($byteCount)
        [Runtime.InteropServices.Marshal]::Copy(
            $referenceData.Scan0, $referenceBytes, 0, $byteCount)
        [Runtime.InteropServices.Marshal]::Copy(
            $candidateData.Scan0, $candidateBytes, 0, $byteCount)
    } finally {
        $referenceBitmap.UnlockBits($referenceData)
        $candidateBitmap.UnlockBits($candidateData)
    }

    $maskBytes = [byte[]]::new($byteCount)
    [long]$changedPixels = 0
    [long]$differenceTotal = 0
    for ($offset = 0; $offset -lt $byteCount; $offset += 4) {
        $blue = [Math]::Abs(
            [int]$referenceBytes[$offset] - [int]$candidateBytes[$offset])
        $green = [Math]::Abs(
            [int]$referenceBytes[$offset + 1] - [int]$candidateBytes[$offset + 1])
        $red = [Math]::Abs(
            [int]$referenceBytes[$offset + 2] - [int]$candidateBytes[$offset + 2])
        $difference = [Math]::Max($red, [Math]::Max($green, $blue))
        $differenceTotal += $difference
        if ($difference -gt 12) {
            $changedPixels++
            $intensity = [Math]::Min(255, $difference * 3)
            $maskBytes[$offset] = [byte]$intensity
            $maskBytes[$offset + 1] = 0
            $maskBytes[$offset + 2] = [byte]$intensity
        }
        $maskBytes[$offset + 3] = 255
    }

    $differenceMask = [Drawing.Bitmap]::new(
        $candidateBitmap.Width, $candidateBitmap.Height, $pixelFormat)
    try {
        $maskData = $differenceMask.LockBits(
            $rectangle, [Drawing.Imaging.ImageLockMode]::WriteOnly,
            $pixelFormat)
        try {
            [Runtime.InteropServices.Marshal]::Copy(
                $maskBytes, 0, $maskData.Scan0, $byteCount)
        } finally {
            $differenceMask.UnlockBits($maskData)
        }
        $differenceMask.Save(
            $differenceMaskPath, [Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $differenceMask.Dispose()
    }

    $pixelCount = [long]$candidateBitmap.Width * $candidateBitmap.Height
    $changedRatio = if ($pixelCount -gt 0) {
        [double]$changedPixels / $pixelCount
    } else { 0.0 }
    $meanDifference = if ($pixelCount -gt 0) {
        [double]$differenceTotal / $pixelCount
    } else { 0.0 }
    @(
        "reference=$referencePath"
        "candidate=$candidatePath"
        "size=$($candidateBitmap.Width)x$($candidateBitmap.Height)"
        "threshold=12"
        "changed_pixels=$changedPixels"
        ("changed_ratio={0:N6}" -f $changedRatio)
        ("mean_max_channel_difference={0:N3}" -f $meanDifference)
    ) | Set-Content -LiteralPath $metricsPath -Encoding UTF8

    [pscustomobject]@{
        Comparison = $comparisonPath
        DifferenceMask = $differenceMaskPath
        Metrics = $metricsPath
        ChangedRatio = $changedRatio
    }
} finally {
    $candidateBitmap.Dispose()
    $referenceBitmap.Dispose()
}

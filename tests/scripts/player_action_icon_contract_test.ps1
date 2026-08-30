param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'

$icons = @{
    'assets/icons/lyrics.svg' = '9491E7F3E9472772A7E81AE623D2E621BCA170EC2C88D8109CCD7263C572F2CC'
    'assets/icons/immersive-visual-mode.svg' = 'F36070248533B512678299426887A721ECDD17EFFFB8756C9B335B0AFC739EC9'
}

foreach ($entry in $icons.GetEnumerator()) {
    $path = Join-Path $SourceRoot $entry.Key
    $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actualHash -ne $entry.Value) {
        throw "$($entry.Key) does not match the approved canonical SVG."
    }

    [xml]$document = Get-Content -Raw -Encoding UTF8 -LiteralPath $path
    if ($document.DocumentElement.LocalName -ne 'svg' -or
            [string]::IsNullOrWhiteSpace($document.DocumentElement.GetAttribute('viewBox'))) {
        throw "$($entry.Key) must be a scalable SVG with a viewBox."
    }
}

Write-Output 'Player action icon contract passed.'

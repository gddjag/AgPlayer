param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'

$icons = @{
    'assets/icons/lyrics.svg' = '7358680EB87056916DE5C869752417E6BDDE11833E8D11659EDF0774860AF6CC'
    'assets/icons/immersive-visual-mode.svg' = '916EDD52CACAFD27F598A60AB0C3747F93C37E8E0FAB271E1EBA280AAA013C18'
}

foreach ($entry in $icons.GetEnumerator()) {
    $path = Join-Path $SourceRoot $entry.Key
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $actualHash = ([BitConverter]::ToString(
            $sha256.ComputeHash([IO.File]::ReadAllBytes($path)))).Replace('-', '')
    } finally {
        $sha256.Dispose()
    }
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

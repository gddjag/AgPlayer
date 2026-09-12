param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'

$icons = @{
    'assets/icons/lyrics.svg' = 'AA5C1B97E7690B0A8930B183A9C54660B9BD652BDD62817169F5215AAAB87DE5'
}

foreach ($entry in $icons.GetEnumerator()) {
    $path = Join-Path $SourceRoot $entry.Key
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        # Git may materialize the same tracked SVG with CRLF in the primary
        # Windows checkout and LF in a worktree.  Verify canonical SVG content,
        # not the checkout-specific line-ending representation.
        $canonicalText = [IO.File]::ReadAllText($path).Replace("`r`n", "`n")
        $canonicalBytes = [Text.Encoding]::UTF8.GetBytes($canonicalText)
        $actualHash = ([BitConverter]::ToString(
            $sha256.ComputeHash($canonicalBytes))).Replace('-', '')
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

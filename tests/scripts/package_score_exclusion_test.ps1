param([string]$SourceRoot = (Join-Path $PSScriptRoot '../..'))
$ErrorActionPreference = 'Stop'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $SourceRoot 'scripts/package-windows.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw $errors[0] }
$guard = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Assert-NoScoreTools' }, $true)
if (-not $guard) { throw 'Package exclusion guard missing' }
Invoke-Expression $guard.Extent.Text
$directory = Join-Path ([IO.Path]::GetTempPath()) ('agplayer-score-exclusion-' + [guid]::NewGuid())
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/')
if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($directory)) -ne $temporaryRoot) { throw 'Test directory escaped temp root' }
New-Item -ItemType Directory -Path $directory | Out-Null
try {
    Assert-NoScoreTools $directory
    foreach ($name in @('AgTranscriptionWorker.exe','verovio-data','yourmt3-model','basic_pitch.json','transcription-capabilities.json','AgPlayer.exe')) {
        $file = Join-Path $directory $name
        [IO.File]::WriteAllText($file, 'TranscriptionController')
        $rejected = $false
        try { Assert-NoScoreTools $directory } catch { $rejected = $_.Exception.Message -match 'Removed audio-to-MIDI/score' }
        if (-not $rejected) { throw "Old score component accepted: $name" }
        Remove-Item -LiteralPath $file
    }
    [IO.File]::WriteAllText((Join-Path $directory 'AgPlayer.exe'), 'ordinary player')
    [IO.File]::WriteAllText((Join-Path $directory 'AgSeparationWorker.exe'), 'ordinary separation')
    Assert-NoScoreTools $directory
    Write-Output 'PASS: removed score artifacts/controllers rejected; player and separation retained'
} finally { Remove-Item -LiteralPath $directory -Recurse }

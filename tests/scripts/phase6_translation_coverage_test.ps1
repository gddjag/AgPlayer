param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
$phase6Qml = @(
    'app/qml/AgPlayer/AudioToolsWindow.qml',
    'app/qml/AgPlayer/components/tools/AudioEditorPage.qml',
    'app/qml/AgPlayer/components/audioeditor/EditorCommandBar.qml',
    'app/qml/AgPlayer/components/audioeditor/EditorWaveformCanvas.qml',
    'app/qml/AgPlayer/components/audioeditor/FileSummaryBar.qml',
    'app/qml/AgPlayer/components/audioeditor/EditorStatusBar.qml',
    'app/qml/AgPlayer/components/tools/ToolSidebar.qml'
)
$expected = @{}
$extractedSourceCount = 0
foreach ($relativePath in $phase6Qml) {
    $path = Join-Path $SourceRoot $relativePath
    $context = [IO.Path]::GetFileNameWithoutExtension($path)
    $text = Get-Content -Raw -Encoding UTF8 -LiteralPath $path
    $sources = @([regex]::Matches(
        $text, 'qsTr\("((?:\\.|[^"\\])*)"\)') | ForEach-Object {
            $_.Groups[1].Value
        } | Select-Object -Unique)
    if ($sources.Count -eq 0) {
        throw "No qsTr sources extracted from $relativePath."
    }
    $expected[$context] = $sources
    $extractedSourceCount += $sources.Count
}
if ($extractedSourceCount -ne 134) {
    throw "Expected exactly 134 context-scoped Phase 6 sources, got $extractedSourceCount."
}

foreach ($locale in @('zh', 'en')) {
    $catalogPath = Join-Path $SourceRoot "translations/agplayer_$locale.ts"
    [xml]$catalog = Get-Content -Raw -Encoding UTF8 -LiteralPath $catalogPath
    foreach ($context in $expected.Keys) {
        $catalogMessages = @($catalog.TS.context |
            Where-Object { [string]$_.name -eq $context } |
            ForEach-Object { @($_.message) })
        foreach ($source in $expected[$context]) {
            $message = @($catalogMessages |
                Where-Object { [string]$_.source -eq $source }) | Select-Object -Last 1
            if ($null -eq $message) {
                throw "$locale catalog misses [$context] $source"
            }
            $translationNode = $message.SelectSingleNode('translation')
            $translation = [string]$translationNode.InnerText
            if ($translationNode.GetAttribute('type') -eq 'unfinished' -or
                [string]::IsNullOrWhiteSpace($translation)) {
                throw "$locale catalog has unfinished [$context] $source"
            }
            if ($locale -ne 'zh' -and $source -match '[\p{IsCJKUnifiedIdeographs}]' -and
                $translation -match '[\p{IsCJKUnifiedIdeographs}]') {
                throw "$locale catalog falls back to Chinese for [$context] $source"
            }
            $sourcePlaceholders = @([regex]::Matches($source, '%\d+') |
                ForEach-Object { $_.Value } | Sort-Object)
            $translationPlaceholders = @([regex]::Matches($translation, '%\d+') |
                ForEach-Object { $_.Value } | Sort-Object)
            if (@(Compare-Object $sourcePlaceholders $translationPlaceholders).Count -ne 0) {
                throw "$locale catalog changes placeholders for [$context] $source"
            }
        }
    }
}

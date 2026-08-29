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
}

$settingsSources = @(
    '5q2M5puy5YiX6KGo',
    '5pi+56S65q2M5puy5YiX6KGo5rOi5b2i57yp55Wl5Zu+',
    '57yp55Wl5rOi5b2i6aKc6Imy',
    'MzYg6Imy',
    '5rOi5b2i5LiO6aKR6LCx6aKc6Imy',
    '6Ieq5a6a5LmJ5rOi5b2i',
    '5bqV6ImyIC8gUkdCIOa4kOWPmA==',
    'UkdCIOaYvuekuuWMuuWfnw==',
    '5bey5pKt5pS+5Yy65Z+f5Li6IFJHQg==',
    '5pyq5pKt5pS+5Yy65Z+f5Li6IFJHQg=='
) | ForEach-Object {
    [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($_))
}
$settingsText = Get-Content -Raw -Encoding UTF8 -LiteralPath (
    Join-Path $SourceRoot 'app/qml/AgPlayer/SettingsPage.qml')
foreach ($source in $settingsSources) {
    if ($settingsText -notmatch [regex]::Escape("qsTr(`"$source`")")) {
        throw "SettingsPage no longer exposes the required translated source: $source"
    }
}
$expected['SettingsPage'] = $settingsSources

$expected['LibraryNavigationModel'] = @(
    '5oiR55qE6Z+z5LmQ5bqT',
    '5oiR55qE5pS26JeP',
    '5qCH562+566h55CG',
    '6LWE5rqQ5paH5Lu25aS5'
) | ForEach-Object {
    [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($_))
}
$expected['SideNavigation'] = @(
    '6LWE5rqQ5paH5Lu25aS5'
) | ForEach-Object {
    [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($_))
}
$expected['SearchFilter'] = @(
    '5q2M5puyIMK3IOiJuuacr+WutiDCtyDkuJPovpEgwrcg5qCH562+'
) | ForEach-Object {
    [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($_))
}

foreach ($locale in @('zh', 'en', 'th', 'vi')) {
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

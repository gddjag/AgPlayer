param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
function ConvertFrom-Utf8Base64([string]$Value) {
    [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Value))
}
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
$requiredSources = @{
    AudioEditorPage = @(
        '5qOA5rWL5Lit4oCm', '5Y6f5aeLICUxIEJQTQ==',
        '4oCUIC8g5LiN6YCC55So', 'JTEg5aOw6YGT'
    ) | ForEach-Object { ConvertFrom-Utf8Base64 $_ }
    EditorWaveformCanvas = @(
        '57q/5oCn', '5bmz5ruR', '5oyH5pWw'
    ) | ForEach-Object { ConvertFrom-Utf8Base64 $_ }
    EditorStatusBar = @(
        '5a+85Ye65a6M5oiQ77yaJTE=', '5q2j5Zyo5aSE55CG4oCmICUxJQ=='
    ) | ForEach-Object { ConvertFrom-Utf8Base64 $_ }
}
foreach ($context in $requiredSources.Keys) {
    foreach ($source in $requiredSources[$context]) {
        if ($expected[$context] -notcontains $source) {
            throw "Phase 6 source manifest misses intended [$context] $source"
        }
    }
}
$obsoleteFadeSource = ConvertFrom-Utf8Base64 '5reh5Ye65o6n5Yi254K5'
if ($expected['EditorWaveformCanvas'] -contains $obsoleteFadeSource) {
    throw 'The obsolete top fade handle translation source remains in the waveform canvas.'
}

$settingsSources = @(
    '5q2M5puy5YiX6KGo',
    '5pi+56S65q2M5puy5YiX6KGo5rOi5b2i57yp55Wl5Zu+',
    '57yp55Wl5rOi5b2i6aKc6Imy',
    'MzYg6Imy',
    '5rOi5b2i5LiO6aKR6LCx6aKc6Imy',
    '6buY6K6k5rOi5b2i5qih5byP',
    '5bqV6ImyIC8gUkdCIOa4kOWPmA==',
    'UkdCIOaYvuekuuWMuuWfnw==',
    '5bey5pKt5pS+5Yy65Z+f5Li6IFJHQg==',
    '5pyq5pKt5pS+5Yy65Z+f5Li6IFJHQg==',
    '56qX5Y+j5Li76aKY',
    '5Y+M56qX5Y+j5Li76aKY',
    '5Y2V56qX5Y+j5Li76aKY'
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

$expected['EqualizerController'] = @(
    'Bass',
    'Classical',
    'Pop',
    'Vocal',
    'EDM',
    'Jazz'
)
$expected['EqualizerWindow'] = @(
    '5L+d5oqk5Lit',
    '5YWz6Zet',
    '5Z2H6KGh5Zmo6aKE6K6+',
    '6aKE6K6+77ya',
    '6Ieq5a6a5LmJ',
    '5L+d5a2Y6aKE6K6+',
    '566h55CG6aKE6K6+',
    '6YeN572u',
    '6IyD5Zu077ya',
    'wrE2IGRC',
    'wrExMiBkQg==',
    'wrExOCBkQg==',
    '57K+5bqm77ya',
    '6auY',
    '5Lit',
    '5L2O',
    '6L6T5Ye655S15bmz77ya',
    '5Y+W5raI',
    '6auY57qn',
    '5peB6Lev5Z2H6KGh5Zmo',
    '5ZCv55So5Z2H6KGh5Zmo',
    '5YWo6YOo5b2S6Zu2',
    'MTgg5q615Zu+5b2i5Z2H6KGh5Zmo',
    '5Y+M5Ye75ruR5p2G5b2S6Zu2IMK3IOa7mui9ruaIluaWueWQkemUruW+ruiwgw==',
    '5L2Z6YeP',
    '5pyA5aSn5YyW',
    '5pyA5bCP5YyW'
) | ForEach-Object {
    [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($_))
}
$expected['EqualizerWindow'] += 'Custom'
$expected['PlayerControls'] = @(
    '5Y2B5YWr5q615Zu+5b2i5Z2H6KGh5Zmo'
) | ForEach-Object {
    [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($_))
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

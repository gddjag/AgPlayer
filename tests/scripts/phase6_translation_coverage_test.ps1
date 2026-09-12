param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
function ConvertFrom-Utf8Base64([string]$Value) {
    [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Value))
}
function Test-IsProjectPath([string]$Path, [string]$Root) {
    $normalizedRoot = [IO.Path]::GetFullPath($Root).TrimEnd([char[]]@(92, 47)) +
        [IO.Path]::DirectorySeparatorChar
    $normalizedPath = [IO.Path]::GetFullPath($Path)
    return $normalizedPath.StartsWith($normalizedRoot,
        [StringComparison]::OrdinalIgnoreCase)
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
    '6aKR5b2p6LCD6Imy5p2/',
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

# Extract the same source graph consumed by Qt's AgPlayer_lupdate target into a
# disposable catalog.  This catches new tr()/qsTr() strings even when the
# checked-in release catalogs have not been regenerated.
$lupdateProject = if ($env:AGPLAYER_LUPDATE_PROJECT) {
    $env:AGPLAYER_LUPDATE_PROJECT
} else {
    $null
}
if (-not $lupdateProject) {
    $probe = [IO.Path]::GetFullPath((Get-Location).Path)
    while ($probe) {
        $candidate = Join-Path $probe '.lupdate\AgPlayer_lupdate_project.cmake'
        if (Test-Path -LiteralPath $candidate) {
            $lupdateProject = $candidate
            break
        }
        $parent = Split-Path -Parent $probe
        if (-not $parent -or $parent -eq $probe) { break }
        $probe = $parent
    }
}
if (-not $lupdateProject) {
    $candidate = Join-Path $SourceRoot 'build\release\.lupdate\AgPlayer_lupdate_project.cmake'
    if (Test-Path -LiteralPath $candidate) { $lupdateProject = $candidate }
}
if (-not $lupdateProject -or -not (Test-Path -LiteralPath $lupdateProject)) {
    throw 'Unable to locate the generated AgPlayer lupdate project.'
}
$buildRoot = Split-Path -Parent (Split-Path -Parent $lupdateProject)
$cmakeCache = Join-Path $buildRoot 'CMakeCache.txt'
$qtDirEntry = Select-String -LiteralPath $cmakeCache -Pattern '^Qt6_DIR:[^=]+=(.+)$' |
    Select-Object -First 1
if ($null -eq $qtDirEntry) { throw "Qt6_DIR was not found in $cmakeCache" }
$qtCmakeDir = $qtDirEntry.Matches[0].Groups[1].Value
$qtRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $qtCmakeDir))
$lupdate = Join-Path $qtRoot 'bin\lupdate.exe'
if (-not (Test-Path -LiteralPath $lupdate)) { throw "lupdate was not found: $lupdate" }

$extractionDirectory = Join-Path ([IO.Path]::GetTempPath()) (
    'agplayer-source-translations-' + [guid]::NewGuid().ToString('N'))
$sourceCatalogPath = Join-Path $extractionDirectory 'current-source.ts'
New-Item -ItemType Directory -Path $extractionDirectory -Force | Out-Null
try {
    if ([IO.Path]::GetExtension($lupdateProject) -eq '.cmake') {
        # The normal build generates the CMake input, not the JSON. Generate
        # only the disposable project here; never run the catalog-update target.
        $temporaryProject = Join-Path $extractionDirectory 'project.json'
        $generator = Join-Path $qtRoot 'lib\cmake\Qt6LinguistTools\GenerateLUpdateProject.cmake'
        & cmake "-DIN_FILE=$lupdateProject" "-DOUT_FILE=$temporaryProject" -P $generator
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $temporaryProject)) {
            throw 'Unable to generate the temporary AgPlayer lupdate project.'
        }
        $generatedProject = Get-Content -Raw -Encoding UTF8 -LiteralPath $temporaryProject |
            ConvertFrom-Json
        # CMake propagates Qt SDK paths/metatypes and build-generated files
        # into this project. They are not AgPlayer translation sources, and
        # letting lupdate traverse them makes this coverage check timing-dependent.
        foreach ($subproject in @($generatedProject.subProjects)) {
            $subproject.includePaths = @($subproject.includePaths | Where-Object {
                (Test-IsProjectPath $_ $SourceRoot) -and
                    -not (Test-IsProjectPath $_ $buildRoot)
            })
            $subproject.sources = @($subproject.sources | Where-Object {
                (Test-IsProjectPath $_ $SourceRoot) -and
                    -not (Test-IsProjectPath $_ $buildRoot)
            })
        }
        $externalPaths = @(
            foreach ($subproject in @($generatedProject.subProjects)) {
                foreach ($path in @($subproject.includePaths) + @($subproject.sources)) {
                    if (-not (Test-IsProjectPath $path $SourceRoot) -or
                        (Test-IsProjectPath $path $buildRoot)) { $path }
                }
            }
        )
        if ($externalPaths.Count -gt 0) {
            throw "Temporary lupdate project must exclude non-source paths: $($externalPaths[0])"
        }
        [IO.File]::WriteAllText($temporaryProject,
            ($generatedProject | ConvertTo-Json -Depth 8),
            [Text.UTF8Encoding]::new($false))
        $lupdateProject = $temporaryProject
    }
    & $lupdate -project $lupdateProject -no-obsolete -locations none -silent `
        -ts $sourceCatalogPath
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $sourceCatalogPath)) {
        throw 'lupdate failed to extract the current application source catalog.'
    }
    [xml]$sourceCatalog = Get-Content -Raw -Encoding UTF8 -LiteralPath $sourceCatalogPath
} finally {
    if (Test-Path -LiteralPath $extractionDirectory) {
        Remove-Item -LiteralPath $extractionDirectory -Recurse -Force
    }
}
foreach ($contextNode in @($sourceCatalog.TS.context)) {
    $context = [string]$contextNode.name
    $currentSources = @($contextNode.message | ForEach-Object { [string]$_.source })
    if ($expected.ContainsKey($context)) {
        $expected[$context] = @(@($expected[$context]) + @($currentSources) |
            Select-Object -Unique)
    } else {
        $expected[$context] = @($currentSources | Select-Object -Unique)
    }
}

$catalogDirectory = Join-Path $SourceRoot 'translations'
$actualCatalogNames = @(Get-ChildItem -LiteralPath $catalogDirectory -Filter 'agplayer_*.ts' |
    ForEach-Object Name | Sort-Object)
$expectedCatalogNames = @('agplayer_en.ts', 'agplayer_zh.ts')
if (@(Compare-Object $expectedCatalogNames $actualCatalogNames).Count -ne 0) {
    throw "Application translation catalogs must be exactly agplayer_zh.ts and agplayer_en.ts"
}

foreach ($locale in @('zh', 'en')) {
    $catalogPath = Join-Path $SourceRoot "translations/agplayer_$locale.ts"
    [xml]$catalog = Get-Content -Raw -Encoding UTF8 -LiteralPath $catalogPath
    # Index once instead of rescanning every message for every source. Keep
    # the previous last-message-wins rule for duplicate catalog entries.
    $messagesByContext = [Collections.Generic.Dictionary[string,object]]::new(
        [StringComparer]::Ordinal)
    foreach ($contextNode in @($catalog.TS.context)) {
        $contextName = [string]$contextNode.name
        if (-not $messagesByContext.ContainsKey($contextName)) {
            $messagesByContext[$contextName] =
                [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
        }
        foreach ($catalogMessage in @($contextNode.message)) {
            $messagesByContext[$contextName][[string]$catalogMessage.source] = $catalogMessage
        }
    }
    foreach ($context in $expected.Keys) {
        foreach ($source in $expected[$context]) {
            $message = $null
            if ($messagesByContext.ContainsKey($context)) {
                [void]$messagesByContext[$context].TryGetValue($source, [ref]$message)
            }
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
            # A non-empty translation can still be untranslated English. These
            # contexts contain user-facing messages, not model or codec names.
            if ($locale -eq 'zh' -and $context -in @(
                'MetadataEditor', 'FileAssociationController', 'Main',
                'PlayerPane', 'ResourceFolderController'
            ) -and $source -match '[A-Za-z]{3}' -and
                $translation -notmatch '[\p{IsCJKUnifiedIdeographs}]') {
                throw "$locale catalog falls back to English for [$context] $source"
            }
            $sourcePlaceholders = @([regex]::Matches($source, '%(?:[1-9][0-9]?|n)') |
                ForEach-Object { $_.Value } | Sort-Object)
            $translationPlaceholders = @([regex]::Matches($translation, '%(?:[1-9][0-9]?|n)') |
                ForEach-Object { $_.Value } | Sort-Object)
            if (@(Compare-Object $sourcePlaceholders $translationPlaceholders).Count -ne 0) {
                throw "$locale catalog changes placeholders for [$context] $source"
            }
        }
    }
}

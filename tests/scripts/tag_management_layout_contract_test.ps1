param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'

function Read-RequiredFile([string]$RelativePath) {
    $path = Join-Path $SourceRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing required file: $RelativePath"
    }
    return Get-Content -LiteralPath $path -Raw
}

function Assert-Matches(
    [string]$Text,
    [string]$Pattern,
    [string]$Message
) {
    if ($Text -notmatch $Pattern) {
        throw $Message
    }
}

$window = Read-RequiredFile 'app/qml/AgPlayer/ListWindow.qml'
$trackList = Read-RequiredFile 'app/qml/AgPlayer/components/TrackList.qml'
$tagPanel = Read-RequiredFile 'app/qml/AgPlayer/components/TagManagementPanel.qml'
$theme = Read-RequiredFile 'app/qml/AgPlayer/theme/Theme.qml'
$main = Read-RequiredFile 'app/main.cpp'
$qaMatrix = Read-RequiredFile 'scripts/qa-final-ui-matrix.ps1'

$trackListCount = ([regex]::Matches($window, '\bTrackList\s*\{')).Count
if ($trackListCount -ne 1) {
    throw "ListWindow must contain exactly one shared TrackList; found $trackListCount"
}

Assert-Matches $window 'readonly property int leftColumnWidth:\s*208' `
    'Left workspace column must be 208 px'
Assert-Matches $window 'readonly property int rightColumnWidth:\s*248' `
    'Right tag column must be 248 px'
Assert-Matches $window 'readonly property int dividerWidth:\s*1' `
    'Workspace dividers must remain 1 px'
Assert-Matches $window '(?s)objectName:\s*"librarySearchFilter".*Layout\.preferredHeight:\s*listWindow\.filterBarHeight' `
    'Search/filter bar must be 54 px'

Assert-Matches $tagPanel '(?s)Flickable\s*\{.*id:\s*tagFlickable' `
    'Tag panel must scroll with a Flickable'
Assert-Matches $tagPanel '(?s)Flow\s*\{.*id:\s*tagFlow' `
    'Tag panel must lay capsules out with Flow'
Assert-Matches $tagPanel 'contentHeight:\s*tagFlow\.height' `
    'Tag Flickable content height must follow the natural Flow height'
Assert-Matches $tagPanel '(?s)id:\s*tagPill.*implicitWidth:.*height:\s*28.*radius:\s*14' `
    'Tag capsules must preserve natural width in compact rounded pills'
Assert-Matches $tagPanel 'selectedVisual|hoveredVisual|tagDropTarget\.containsDrag' `
    'Tag capsules must expose selected, hover and drop visual states'
if ($tagPanel -match 'Keys\.onSpacePressed') {
    throw 'Tag pills must leave Space for global playback shortcuts'
}
if ($tagPanel -match '\bfocus\s*:\s*true') {
    throw 'Tag delegates must not claim initial keyboard focus'
}
Assert-Matches $tagPanel 'property int pillHorizontalPadding' `
    'Tag pill geometry must use explicit horizontal padding'
Assert-Matches $tagPanel 'property int pillContentSpacing' `
    'Tag pill geometry must use explicit content spacing'
Assert-Matches $tagPanel 'anchors\.top:\s*tagPill\.bottom' `
    'Tag pill shadow must be offset below the capsule'
Assert-Matches $tagPanel '(?s)TagFilterModel\s*\{.*sourceModel:\s*root\.tagModel.*query:\s*root\.searchText' `
    'Tag search must use the incremental C++ proxy model'
if ($tagPanel -match '\bGridView\s*\{') {
    throw 'Tag capsules must not use a fixed GridView'
}

Assert-Matches $theme 'tagPillSurface' `
    'Theme must expose a base translucent tag surface'
Assert-Matches $theme 'tagPillHoverSurface' `
    'Theme must expose a tag hover surface'
Assert-Matches $theme 'tagPillSelectedSurface' `
    'Theme must expose a low-saturation selected tag surface'
Assert-Matches $theme 'tagPillDropSurface' `
    'Theme must expose a tag drop surface'

Assert-Matches $main 'QStringList qaSeedTags' `
    'QA screenshots must support temporary tag seeds'
Assert-Matches $main 'arg == QStringLiteral\("--qa-tag"\)' `
    'QA tag seeds must be supplied through an explicit CLI argument'
Assert-Matches $main '(?s)if \(qaTestMode\).*tagModel\.createTag' `
    'QA tag seeds must stay behind the test-mode boundary'
Assert-Matches $main '(?s)!qaLibraryPath\.isEmpty\(\).*?!qaSeedTags\.isEmpty\(\).*?QTemporaryDir' `
    'QA tag seeds combined with an explicit library must use isolated temporary storage'
Assert-Matches $main 'qaTagStorageDirectory->filePath\(QStringLiteral\("tags\.json"\)\)' `
    'Isolated QA tag seeds must never write the tags file beside an explicit library'
Assert-Matches $qaMatrix '(?s)function New-QALibraryPath.*Join-Path \$StateRoot \$Surface' `
    'Each QA surface must isolate its adjacent tags.json state'

Assert-Matches $trackList 'reuseItems:\s*true' `
    'TrackList must reuse delegates'
Assert-Matches $trackList 'cacheBuffer:\s*0' `
    'TrackList must not retain off-screen waveform delegates'
Assert-Matches $trackList 'listWaveformThumbnailEnabled\s*\?\s*62\s*:\s*42' `
    'Track rows must switch directly between 62 px and 42 px'
Assert-Matches $trackList '(?s)trackHeaderIndex.*trackHeaderTitle.*trackHeaderFavorite.*trackHeaderArtist.*trackHeaderAlbum.*trackHeaderRating.*trackHeaderBpm.*trackHeaderDuration' `
    'Track header order must match the reference table'
Assert-Matches $trackList '(?s)active:\s*SettingsController\.listWaveformThumbnailEnabled\s*&&\s*root\s*&&\s*root\.thumbnailHostVisible\s*&&\s*rowItem\.inViewport' `
    'Waveform wrapper Loader must require enabled and effective host visibility'
Assert-Matches $trackList '(?s)ListView\.onPooled:\s*\{.*pooled\s*=\s*true.*ListView\.onReused:\s*\{.*waveformGeneration.*pooled\s*=\s*false' `
    'Pooled delegates must deactivate and reused delegates must get a new generation'

Write-Output 'Tag management layout contract passed.'

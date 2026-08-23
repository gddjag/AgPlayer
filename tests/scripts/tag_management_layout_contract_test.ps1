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

Assert-Matches $tagPanel 'readonly property int gridColumnCount:\s*3' `
    'Tag panel must expose a fixed three-column contract'
Assert-Matches $tagPanel 'cellWidth:\s*width\s*/\s*3' `
    'Tag GridView must compute cellWidth from width / 3'
Assert-Matches $tagPanel 'cellHeight:\s*32' `
    'Tag capsule grid must use compact 32 px rows'
Assert-Matches $tagPanel '(?s)id:\s*tagPill.*height:\s*24.*radius:\s*12' `
    'Tag capsules must use compact 24 px linear pills'
Assert-Matches $tagPanel '(?s)TagFilterModel\s*\{.*sourceModel:\s*root\.tagModel.*query:\s*root\.searchText' `
    'Tag search must use the incremental C++ proxy model'
if ($tagPanel -match '\bcolumns\s*:') {
    throw 'Do not use the nonexistent GridView.columns property'
}

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

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
$integratedShell = Read-RequiredFile 'app/qml/AgPlayer/components/IntegratedPlayerShell.qml'
$rollingShell = Read-RequiredFile 'app/qml/AgPlayer/components/RollingPlayerShell.qml'
$tagPanel = Read-RequiredFile 'app/qml/AgPlayer/components/TagManagementPanel.qml'
$theme = Read-RequiredFile 'app/qml/AgPlayer/theme/Theme.qml'
$main = Read-RequiredFile 'app/main.cpp'
$qaMatrix = Read-RequiredFile 'scripts/qa-final-ui-matrix.ps1'

$trackListCount = ([regex]::Matches($window, '\bTrackList\s*\{')).Count
if ($trackListCount -ne 1) {
    throw "ListWindow must contain exactly one shared TrackList; found $trackListCount"
}

Assert-Matches $window 'readonly property int leftColumnWidth:\s*Theme\.navigationWidth' `
    'Left workspace column must be 208 px'
Assert-Matches $window 'readonly property int rightColumnWidth:\s*Theme\.navigationWidthExpanded' `
    'Right tag column must be 248 px'
Assert-Matches $window 'readonly property int dividerWidth:\s*1' `
    'Workspace dividers must remain 1 px'
Assert-Matches $window '(?s)objectName:\s*"centerTrackFooter".*Layout\.preferredHeight:\s*listWindow\.filterBarHeight' `
    'Search/filter bar must follow the compact shared footer height'

Assert-Matches $tagPanel '(?s)Flickable\s*\{.*id:\s*tagFlickable' `
    'Tag panel must scroll with a Flickable'
Assert-Matches $tagPanel '(?s)Flow\s*\{.*id:\s*tagFlow' `
    'Tag panel must lay capsules out with Flow'
Assert-Matches $tagPanel 'contentHeight:\s*tagFlow\.height' `
    'Tag Flickable content height must follow the natural Flow height'
Assert-Matches $tagPanel '(?s)id:\s*tagPill.*countSectionWidth:.*tagCount\.implicitWidth.*nameSectionWidth:.*tagNameMeasure\.implicitWidth.*implicitWidth:\s*Math\.min\(tagFlow\.width,.*implicitHeight:\s*Theme\.tagCapsuleHeight.*height:\s*implicitHeight.*radius:\s*Theme\.tagCapsuleRadius' `
    'Tag capsules must preserve compact natural width in 24 px capsules'
Assert-Matches $tagPanel 'selectedVisual|hoveredVisual|pressedVisual|tagDropTarget\.containsDrag' `
    'Tag capsules must expose selected, hover and drop visual states'
Assert-Matches $tagPanel '(?s)id:\s*tagName.*objectName:\s*"tagCapsuleName-".*text:\s*tagCell\.displayName' `
    'Tag capsules must display the tag name'
Assert-Matches $tagPanel '(?s)id:\s*tagCount.*objectName:\s*"tagCapsuleCount-".*text:\s*tagCell\.trackCount' `
    'Tag capsules must display the live track count'
Assert-Matches $tagPanel 'filledVisual:\s*selectedVisual\s*\|\|\s*hoveredVisual\s*\|\|\s*pressedVisual\s*[\r\n]' `
    'Selected tags retain their solid color after the pointer leaves'
Assert-Matches $tagPanel 'color:\s*filledVisual\s*\?\s*baseAccent\s*:\s*"transparent"' `
    'Tag capsules must switch between a transparent interior and their own solid color'
Assert-Matches $tagPanel '(?s)border\.color:\s*baseAccent.*border\.width:\s*Theme\.tagCapsuleBorderWidth\s*\+\s*\(selectedVisual\s*\?\s*0\.8\s*:\s*0\)' `
    'Selected tags must use a stronger same-color outline'
Assert-Matches $tagPanel '(?s)contentColor:\s*filledVisual\s*\?\s*Theme\.tagCapsuleFilledText\(baseAccent\)\s*:\s*Theme\.primaryText' `
    'Tag text must use readable theme foreground when hollow and contrasting text when filled'
if ($tagPanel -match 'tagCapsule(?:Left|Right|Notch)-|tagCapsuleCountSurface') {
    throw 'Tag capsules must not restore split color blocks, a white count half, or an arrow notch'
}
Assert-Matches $tagPanel '(?s)Flickable\s*\{.*contentHeight:\s*tagFlow\.height.*Flow\s*\{.*width:\s*tagFlickable\.width.*height:\s*childrenRect\.height' `
    'Tag capsules must wrap naturally and expose their full height to scrolling'
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
if ($tagPanel -match 'anchors\.top:\s*tagPill\.bottom') {
    throw 'Tag pills must not render a drop shadow below the capsule'
}
Assert-Matches $tagPanel '(?s)TagFilterModel\s*\{.*sourceModel:\s*root\.tagModel.*query:\s*root\.searchText' `
    'Tag search must use the incremental C++ proxy model'
if ($tagPanel -match '\bGridView\s*\{') {
    throw 'Tag capsules must not use a fixed GridView'
}

Assert-Matches $theme 'readonly property int tagCapsuleHeight:\s*24\b' `
    'Shared tag capsules must use the compact 24 px height'
Assert-Matches $theme 'readonly property int tagCapsuleRadius:\s*10\b' `
    'Shared tag capsules must use the compact 10 px radius'
Assert-Matches $theme 'readonly property real tagCapsuleBorderWidth:\s*1\.4\b' `
    'Shared tag capsules must reduce the previous outline by one DIP'
Assert-Matches $theme 'function tagCapsuleFilledText\(fill\)' `
    'Theme must supply contrasting foreground for arbitrary filled tag colors'
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
Assert-Matches $trackList 'property bool thumbnailVisibilityFollowsSetting:\s*true' `
    'Classic TrackList instances must follow the thumbnail visibility setting by default'
Assert-Matches $trackList '(?s)readonly property bool waveformThumbnailsVisible:\s*!thumbnailVisibilityFollowsSetting\s*\|\|\s*SettingsController\.listWaveformThumbnailEnabled' `
    'TrackList must expose the effective thumbnail visibility policy'
Assert-Matches $trackList '(?s)rowHeight:\s*singleWindowLayout\s*\?\s*Theme\.mediaListRowHeight[\s\S]*waveformThumbnailsVisible[\s\S]*Theme\.mediaListRowHeight\s*:\s*Theme\.listRowHeight' `
    'Single-window rows stay 50 px while classic rows follow effective thumbnail visibility'
Assert-Matches $integratedShell '(?s)objectName:\s*"integratedTrackList".*thumbnailVisibilityFollowsSetting:\s*false' `
    'Integrated list thumbnails must remain visible when the classic switch is off'
Assert-Matches $rollingShell '(?s)objectName:\s*"rollingTrackList".*thumbnailVisibilityFollowsSetting:\s*false' `
    'Rolling list thumbnails must remain visible when the classic switch is off'
Assert-Matches $window '(?s)objectName:\s*"sharedTrackList".*layoutProfile:\s*"classic".*tagManagementLayout:\s*listWindow\.tagManagementMode' `
    'The classic list must opt into the relaxed reference layout and its tag variant'
Assert-Matches $trackList '(?s)trackHeaderTitle.*Layout\.column:\s*1.*trackHeaderFavorite.*root\.singleWindowLayout \? 5 : \(root\.relaxedClassicColumns \? 4 : 2\).*trackHeaderArtist.*visible:\s*root\.showArtistColumn.*trackHeaderAlbum.*visible:\s*root\.showAlbumColumn.*trackHeaderRating.*root\.singleWindowLayout \? 4 : \(root\.relaxedClassicColumns \? 3 : 8\).*trackHeaderBpm.*Layout\.column:\s*root\.relaxedClassicColumns \? 5 : 9.*trackHeaderDuration.*root\.singleWindowLayout \? 3 : \(root\.relaxedClassicColumns \? 2 : 10\)' `
    'Classic headers must render title, duration, rating, favorite, and BPM in reference order'
Assert-Matches $trackList 'readonly property int ratingIconSize:\s*relaxedClassicColumns \? 15' `
    'Classic and tag list stars must use the smaller reference size'
Assert-Matches $trackList 'readonly property int favoriteIconSize:\s*relaxedClassicColumns \? 20' `
    'Classic and tag favorites must use the requested compact 20 px size, above 15 px row rating stars'
Assert-Matches $trackList '(?s)id:\s*titleCell.*Layout\.fillWidth:\s*!root\.singleWindowLayout.*Layout\.maximumWidth:\s*root\.singleWindowLayout.*Number\.POSITIVE_INFINITY' `
    'Classic title and waveform column must grow while trailing metadata columns keep fixed widths'
Assert-Matches $window 'objectName:\s*"leftWorkspaceDivider"[\s\S]*opacity:\s*0\.3' `
    'The classic workspace divider must use 30 percent emphasis'
Assert-Matches $window 'objectName:\s*"tagPanelDivider"[\s\S]*Layout\.bottomMargin:\s*listWindow\.filterBarHeight' `
    'The tag divider must stop above the shared filter footer'
Assert-Matches $window 'objectName:\s*"tagPanelBottomDivider"' `
    'The tag pane must have a horizontal bottom boundary above the filter footer'
Assert-Matches $trackList '(?s)active:\s*root\s*&&\s*root\.waveformThumbnailsVisible\s*&&\s*!root\.singleWindowLayout\s*&&\s*root\.thumbnailHostVisible\s*&&\s*rowItem\.inViewport' `
    'Waveform wrapper Loader must require effective visibility and a visible host'
Assert-Matches $trackList '(?s)ListView\.onPooled:\s*\{.*pooled\s*=\s*true.*ListView\.onReused:\s*\{.*waveformGeneration.*pooled\s*=\s*false' `
    'Pooled delegates must deactivate and reused delegates must get a new generation'

Write-Output 'Tag management layout contract passed.'

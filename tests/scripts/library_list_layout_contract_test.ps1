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

function Assert-Matches([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) {
        throw $Message
    }
}

$window = Read-RequiredFile 'app/qml/AgPlayer/ListWindow.qml'
$trackList = Read-RequiredFile 'app/qml/AgPlayer/components/TrackList.qml'
$navigation = Read-RequiredFile 'app/qml/AgPlayer/components/SideNavigation.qml'
$tagPanel = Read-RequiredFile 'app/qml/AgPlayer/components/TagManagementPanel.qml'
$theme = Read-RequiredFile 'app/qml/AgPlayer/theme/Theme.qml'
$mini = Read-RequiredFile 'app/qml/AgPlayer/components/MiniPlayerControls.qml'
$libraryIcon = Read-RequiredFile 'assets/icons/user-library.svg'

Assert-Matches $window '(?s)id:\s*centerColumn.*id:\s*centerTrackFooter.*objectName:\s*"centerTrackFooter".*Layout\.fillWidth:\s*true' `
    'Search/rating/BPM footer must remain inside and fill the center track column'
if ($window -match '(?s)id:\s*centerTrackFooter.*Layout\.(minimumWidth|maximumWidth):\s*centerColumn\.width') {
    throw 'The center footer must not introduce a circular width binding to its parent column'
}

Assert-Matches $trackList '(?s)id:\s*trackTitleMarquee.*fontWeight:\s*Font\.DemiBold.*id:\s*waveformWrapperLoader.*anchors\.top:\s*trackTitleMarquee\.bottom.*anchors\.topMargin:\s*[2-4].*height:\s*(15|16)' `
    'Thumbnail waveform must sit 2-4 px below the bold title'
Assert-Matches $trackList '(?s)property string layoutProfile:\s*"classic".*readonly property bool singleWindowLayout:\s*layoutProfile === "integrated" \|\| layoutProfile === "rolling"' `
    'TrackList must expose one explicit shared presentation profile for classic, single-window, and rolling layouts'
Assert-Matches $trackList '(?s)objectName:\s*"singleWindowTrackSubtitle".*artist:\s*rowItem\.artist.*album:\s*rowItem\.album.*tags:\s*rowItem\.rowTags' `
    'Single-window rows must show artist, album, and optional tags beneath the title'
Assert-Matches $trackList '(?s)objectName:\s*"singleWindowWaveformThumbnailLoader".*Layout\.fillWidth:\s*true.*Layout\.preferredHeight:\s*root\.singleWindowWaveformHeight.*Layout\.alignment:\s*Qt\.AlignVCenter' `
    'Single-window and rolling waveform thumbnails must occupy their own shorter, vertically centered column'
Assert-Matches $trackList '(?s)readonly property int titleMinimumWidth:\s*singleWindowLayout \? 180 : \(compactColumns \? 150 : 180\).*readonly property int singleWindowFlexibleWidth:\s*Math\.max\(.*readonly property int singleWindowTitleWidth:\s*Math\.max\(\s*titleMinimumWidth,\s*Math\.floor\(singleWindowFlexibleWidth \* 0\.56\)\)' `
    'Shared rows must allocate 56% of flexible space to the title without changing the ordinary/tag floor'
Assert-Matches $trackList '(?s)readonly property bool showBpmColumn:\s*singleWindowLayout \? false.*relaxedClassicColumns \? !tagManagementLayout : !tagFilterActive.*readonly property bool showDurationColumn:\s*singleWindowLayout \|\| relaxedClassicColumns \|\| !tagFilterActive' `
    'Single-window lists must show duration without BPM while classic tag mode keeps its existing policy'

Assert-Matches $navigation '(?s)readonly property int navigationIconVisualSize:\s*Theme\.navigationIconVisualSize.*readonly property int navigationActionExtent:\s*Theme\.navigationActionExtent.*objectName:\s*"navigationExpandButton".*nodeRow\.nodeType === "library".*Layout\.preferredWidth:\s*visible\s*\? root\.navigationActionExtent : 0.*Layout\.preferredHeight:\s*root\.navigationActionExtent.*icon\.width:\s*root\.navigationIconVisualSize.*icon\.height:\s*root\.navigationIconVisualSize' `
    'Library chevron must remain visible with a 28 px hit target and 18 px icon'
Assert-Matches $navigation 'anchors\.leftMargin:\s*6 \+ nodeRow\.depth \* 12' `
    'Sidebar indentation must remain compact'

Assert-Matches $tagPanel '(?s)id:\s*tagPill.*implicitHeight:\s*Theme\.tagCapsuleHeight.*height:\s*implicitHeight.*radius:\s*Theme\.tagCapsuleRadius' `
    'Tag capsules must use the shared compact height and rounded outline'
Assert-Matches $theme 'readonly property int tagCapsuleHeight:\s*24\b' `
    'Shared tag capsules must use the compact 24 px height'
Assert-Matches $theme 'readonly property int tagCapsuleRadius:\s*10\b' `
    'Shared tag capsules must use the compact 10 px radius'
Assert-Matches $tagPanel '(?s)TagFilterModel\s*\{.*sourceModel:\s*root\.tagModel.*query:\s*root\.searchText' `
    'Tag panel must retain the quantity-descending stable-name proxy sorting'

Assert-Matches $mini '(?s)objectName:\s*"miniTrackTitle".*objectName:\s*"miniFavoriteButton".*width:\s*16.*height:\s*16.*anchors\.left:\s*miniTrackTitle\.right.*objectName:\s*"miniMetadataRow".*Layout\.preferredHeight:\s*26.*objectName:\s*"miniArtist".*wrapMode:\s*Text\.NoWrap.*objectName:\s*"miniAlbum".*wrapMode:\s*Text\.NoWrap.*objectName:\s*"miniTagSeparator".*visible:\s*miniTags\.visible.*objectName:\s*"miniRating"' `
    'Mini favorite follows the title; metadata remains one non-wrapping artist/album/tag/rating row'

Assert-Matches $libraryIcon 'viewBox="-125 -125 250 250"' `
    'The library entry must use the supplied blob icon geometry'
Assert-Matches $libraryIcon '#f08a24' `
    'The supplied orange library icon accent must be preserved'
if ($libraryIcon -match '<style|@keyframes') {
    throw 'The Qt navigation icon must not retain unsupported browser CSS animation'
}

Write-Output 'Library/list layout contract passed.'

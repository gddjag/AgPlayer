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
$mini = Read-RequiredFile 'app/qml/AgPlayer/components/MiniPlayerControls.qml'

Assert-Matches $window '(?s)id:\s*centerColumn.*id:\s*centerTrackFooter.*objectName:\s*"centerTrackFooter".*Layout\.fillWidth:\s*true' `
    'Search/rating/BPM footer must remain inside and fill the center track column'
if ($window -match '(?s)id:\s*centerTrackFooter.*Layout\.(minimumWidth|maximumWidth):\s*centerColumn\.width') {
    throw 'The center footer must not introduce a circular width binding to its parent column'
}

Assert-Matches $trackList '(?s)id:\s*trackTitleMarquee.*fontWeight:\s*Font\.DemiBold.*id:\s*waveformWrapperLoader.*anchors\.top:\s*trackTitleMarquee\.bottom.*anchors\.topMargin:\s*[2-4].*height:\s*(15|16)' `
    'Thumbnail waveform must sit 2-4 px below the bold title'
Assert-Matches $trackList 'readonly property int titleMinimumWidth:\s*compactColumns \? 150 : 180' `
    'Ordinary and tag lists must keep the same title-width floor'
Assert-Matches $trackList 'readonly property bool showBpmColumn:\s*!tagFilterActive' `
    'Tag mode must crop late columns before shrinking the title target'

Assert-Matches $navigation '(?s)objectName:\s*"navigationExpandButton".*nodeRow\.nodeType === "library".*Layout\.preferredWidth:\s*visible \? 28 : 0.*Layout\.preferredHeight:\s*28.*icon\.width:\s*18.*icon\.height:\s*18' `
    'Library chevron must remain visible with a 28 px hit target and 18 px icon'
Assert-Matches $navigation 'anchors\.leftMargin:\s*6 \+ nodeRow\.depth \* 12' `
    'Sidebar indentation must remain compact'

Assert-Matches $tagPanel '(?s)id:\s*tagPill.*implicitHeight:\s*26.*height:\s*implicitHeight.*radius:\s*13' `
    'Tag capsules must be 26 px high and vertically centered'
Assert-Matches $tagPanel '(?s)TagFilterModel\s*\{.*sourceModel:\s*root\.tagModel.*query:\s*root\.searchText' `
    'Tag panel must retain the quantity-descending stable-name proxy sorting'

Assert-Matches $mini '(?s)objectName:\s*"miniMetadataRow".*Layout\.preferredHeight:\s*26.*objectName:\s*"miniArtist".*wrapMode:\s*Text\.NoWrap.*objectName:\s*"miniAlbum".*wrapMode:\s*Text\.NoWrap.*objectName:\s*"miniTagSeparator".*visible:\s*miniTags\.visible.*objectName:\s*"miniRating".*objectName:\s*"miniFavoriteButton".*Layout\.preferredWidth:\s*16.*Layout\.preferredHeight:\s*16' `
    'Mini metadata must be one non-wrapping artist/album/tag/rating/favorite row'

Write-Output 'Library/list layout contract passed.'

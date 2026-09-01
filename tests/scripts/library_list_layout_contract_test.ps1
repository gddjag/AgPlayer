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
$managerPage = Read-RequiredFile 'app/qml/AgPlayer/components/LibraryManagerPage.qml'
$libraryIcon = Read-RequiredFile 'assets/icons/user-library.svg'

Assert-Matches $window '(?s)id:\s*centerColumn.*id:\s*centerTrackFooter.*objectName:\s*"centerTrackFooter".*Layout\.fillWidth:\s*true' `
    'Search/rating/BPM footer must remain inside and fill the center track column'
if ($window -match '(?s)id:\s*centerTrackFooter.*Layout\.(minimumWidth|maximumWidth):\s*centerColumn\.width') {
    throw 'The center footer must not introduce a circular width binding to its parent column'
}

Assert-Matches $trackList '(?s)id:\s*trackTitleMarquee.*fontWeight:\s*Font\.DemiBold.*id:\s*waveformWrapperLoader.*anchors\.top:\s*trackTitleMarquee\.bottom.*anchors\.topMargin:\s*[2-4].*height:\s*(15|16)' `
    'Thumbnail waveform must sit 2-4 px below the bold title'
Assert-Matches $trackList 'property bool singleWindowLayout:\s*false' `
    'TrackList must expose an explicit single-window/rolling presentation profile'
Assert-Matches $trackList '(?s)objectName:\s*"singleWindowTrackSubtitle".*artist:\s*rowItem\.artist.*album:\s*rowItem\.album.*tags:\s*rowItem\.rowTags' `
    'Single-window rows must show artist, album, and optional tags beneath the title'
Assert-Matches $trackList '(?s)objectName:\s*"singleWindowWaveformThumbnailLoader".*Layout\.fillWidth:\s*true.*Layout\.preferredHeight:\s*root\.singleWindowMediaHeight' `
    'Single-window and rolling waveform thumbnails must occupy their own column and match the cover height'
Assert-Matches $trackList '(?s)readonly property int titleMinimumWidth:\s*singleWindowLayout \? 220 : \(compactColumns \? 150 : 180\)' `
    'Single-window rows need a wider metadata title region without changing the ordinary/tag floor'
Assert-Matches $trackList '(?s)readonly property bool showBpmColumn:\s*singleWindowLayout \? false.*relaxedClassicColumns \? !tagManagementLayout : !tagFilterActive.*readonly property bool showDurationColumn:\s*singleWindowLayout \|\| relaxedClassicColumns \|\| !tagFilterActive' `
    'Single-window lists must show duration without BPM while classic tag mode keeps its existing policy'

Assert-Matches $navigation '(?s)objectName:\s*"navigationExpandButton".*nodeRow\.nodeType === "library".*Layout\.preferredWidth:\s*visible \? 28 : 0.*Layout\.preferredHeight:\s*28.*icon\.width:\s*18.*icon\.height:\s*18' `
    'Library chevron must remain visible with a 28 px hit target and 18 px icon'
Assert-Matches $navigation 'anchors\.leftMargin:\s*6 \+ nodeRow\.depth \* 12' `
    'Sidebar indentation must remain compact'

Assert-Matches $tagPanel '(?s)id:\s*tagPill.*implicitHeight:\s*24.*height:\s*implicitHeight.*radius:\s*12' `
    'Tag capsules must be 24 px high and vertically centered'
Assert-Matches $tagPanel '(?s)TagFilterModel\s*\{.*sourceModel:\s*root\.tagModel.*query:\s*root\.searchText' `
    'Tag panel must retain the quantity-descending stable-name proxy sorting'

Assert-Matches $mini '(?s)objectName:\s*"miniMetadataRow".*Layout\.preferredHeight:\s*26.*objectName:\s*"miniArtist".*wrapMode:\s*Text\.NoWrap.*objectName:\s*"miniAlbum".*wrapMode:\s*Text\.NoWrap.*objectName:\s*"miniTagSeparator".*visible:\s*miniTags\.visible.*objectName:\s*"miniRating".*objectName:\s*"miniFavoriteButton".*Layout\.preferredWidth:\s*16.*Layout\.preferredHeight:\s*16' `
    'Mini metadata must be one non-wrapping artist/album/tag/rating/favorite row'

Assert-Matches $managerPage '(?s)function\s+removeTracksFromLibrary\(trackIds\).*manager\.removeTrackFromLibrary.*objectName:\s*"libraryTrackRemove".*removeTracksFromLibrary' `
    'Library manager menu removal must persist a controller tombstone'
Assert-Matches $managerPage '(?s)Keys\.onDeletePressed:.*removeTracksFromLibrary' `
    'Library manager Delete key removal must persist a controller tombstone'
if ($managerPage -match 'libraryTrackRename|renameTrackDialog|libraryTrackRelocate|relocateTrackDialog') {
    throw 'Library manager must not expose rename or relocate actions'
}
Assert-Matches $managerPage '(?s)function\s+openFileDetails\(trackId\).*fileOps\.trackDetails.*objectName:\s*"libraryTrackDetails".*text:\s*qsTr\("查看音频文件信息"\).*openFileDetails' `
    'Library manager menu must end with the shared audio file details action'
Assert-Matches $managerPage 'lastPersistenceError' `
    'Library manager must surface tombstone persistence errors'

Assert-Matches $libraryIcon 'viewBox="-125 -125 250 250"' `
    'The library entry must use the supplied blob icon geometry'
Assert-Matches $libraryIcon '#f08a24' `
    'The supplied orange library icon accent must be preserved'
if ($libraryIcon -match '<style|@keyframes') {
    throw 'The Qt navigation icon must not retain unsupported browser CSS animation'
}

Write-Output 'Library/list layout contract passed.'

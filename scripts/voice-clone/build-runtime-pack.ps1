param(
    [Parameter(Mandatory = $true)]
    [string]$LockFile,
    [Parameter(Mandatory = $true)]
    [string]$OfficialCacheRoot,
    [Parameter(Mandatory = $true)]
    [string]$OutputRoot
)

$ErrorActionPreference = 'Stop'

function Assert-NoReparseAncestors([string]$Path, [string]$Label) {
    $full = [System.IO.Path]::GetFullPath($Path)
    $current = $full
    while ($true) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
            if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
                throw "$Label contains a link or reparse-point ancestor"
            }
        }
        $parent = Split-Path -Parent $current
        if ([string]::IsNullOrEmpty($parent) -or $parent -eq $current) { break }
        $current = $parent
    }
}

function Remove-TreeSafely([string]$Path, [string]$ExpectedParent, [string]$Label) {
    $full = [System.IO.Path]::GetFullPath($Path)
    $parentPrefix = [System.IO.Path]::GetFullPath($ExpectedParent).TrimEnd('\') + '\'
    if (-not $full.StartsWith($parentPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label cleanup escaped its canonical parent"
    }
    Assert-NoReparseAncestors $full $Label
    $directories = [System.Collections.Generic.Stack[string]]::new()
    $directories.Push($full)
    while ($directories.Count -gt 0) {
        $directory = $directories.Pop()
        foreach ($child in @(Get-ChildItem -LiteralPath $directory -Force)) {
            if ($child.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
                throw "$Label cleanup refused a reparse point: $($child.FullName)"
            }
            if ($child.PSIsContainer) { $directories.Push($child.FullName) }
        }
    }
    Remove-Item -LiteralPath $full -Recurse -Force
}

function Resolve-ExistingDirectory([string]$Path, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($Path) -or -not [System.IO.Path]::IsPathRooted($Path)) {
        throw "$Label must be an absolute path"
    }
    $full = [System.IO.Path]::GetFullPath($Path)
    Assert-NoReparseAncestors $full $Label
    $item = Get-Item -LiteralPath $full -ErrorAction Stop
    if (-not $item.PSIsContainer -or ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
        throw "$Label must be a real directory, not a link or reparse point"
    }
    return $item.FullName
}

function Resolve-RelativePath([string]$Root, [string]$RelativePath, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($RelativePath) -or [System.IO.Path]::IsPathRooted($RelativePath)) {
        throw "$Label must be a relative path"
    }
    $normalized = $RelativePath.Replace('/', [System.IO.Path]::DirectorySeparatorChar)
    $parts = $normalized.Split([System.IO.Path]::DirectorySeparatorChar, [System.StringSplitOptions]::RemoveEmptyEntries)
    if ($parts -contains '..' -or $normalized.Contains(':')) {
        throw "$Label escapes its root"
    }
    $candidate = [System.IO.Path]::GetFullPath((Join-Path $Root $normalized))
    $rootPrefix = $Root.TrimEnd('\') + '\'
    if (-not $candidate.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label escapes its root"
    }
    Assert-NoReparseAncestors $candidate $Label
    return $candidate
}

function Resolve-ExistingRelativeFile([string]$Root, [string]$RelativePath, [string]$Label) {
    $path = Resolve-RelativePath $Root $RelativePath $Label
    $item = Get-Item -LiteralPath $path -ErrorAction Stop
    if ($item.PSIsContainer -or ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
        throw "$Label must be a regular file"
    }
    return $item.FullName
}

function Assert-Sha256([string]$Path, [string]$Expected, [string]$Label) {
    if ($Expected -notmatch '^[0-9a-f]{64}$') {
        throw "$Label does not have a locked SHA-256"
    }
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Expected) {
        throw "$Label SHA-256 mismatch"
    }
    return $actual
}

function Expand-LockedArchive([string]$PythonExecutable, [string]$Helper,
                              [string]$ArchivePath, [object]$Descriptor,
                              [string]$Destination, [string]$Label) {
    if ($Descriptor.format -notin @('zip', 'tar.gz') -or
        $Descriptor.archiveRoot -notmatch '^[A-Za-z0-9_.-]+$' -or
        $Descriptor.fileCount -lt 1 -or $Descriptor.expandedBytes -lt 1 -or
        $Descriptor.treeSha256 -notmatch '^[0-9a-f]{64}$') {
        throw "$Label extraction contract is invalid"
    }
    & $PythonExecutable -I -s $Helper `
        --archive $ArchivePath --format $Descriptor.format `
        --archive-root $Descriptor.archiveRoot `
        --file-count $Descriptor.fileCount `
        --expanded-bytes $Descriptor.expandedBytes `
        --tree-sha256 $Descriptor.treeSha256 `
        --destination $Destination
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $Destination -PathType Container)) {
        throw "$Label extraction failed"
    }
}

function Invoke-GitValue([string]$Repository, [string[]]$Arguments, [string]$Label) {
    $value = & git -C $Repository @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed: $value"
    }
    return ($value | Out-String).Trim()
}

function Normalize-RepositoryUrl([string]$Url) {
    return $Url.Trim().TrimEnd('/').Replace('.git', '').ToLowerInvariant()
}

function Get-RelativePath([string]$Root, [string]$Path) {
    $rootUri = [System.Uri]::new($Root.TrimEnd('\') + '\')
    $pathUri = [System.Uri]::new($Path)
    return [System.Uri]::UnescapeDataString($rootUri.MakeRelativeUri($pathUri).ToString()).Replace('/', '\')
}

function Normalize-DistributionName([string]$Name) {
    return ($Name.ToLowerInvariant() -replace '[-_.]+', '-')
}

function Get-CompatibleWheel([string]$Wheelhouse, [string]$Name, [string]$Version,
                             [string]$PythonVersion, [string]$RequirementsText) {
    $normalizedName = Normalize-DistributionName $Name
    $versionParts = $PythonVersion.Split('.')
    $pythonTag = "$($versionParts[0])$($versionParts[1])"
    $pythonMinor = [int]$versionParts[1]
    $candidates = @()
    foreach ($wheel in @(Get-ChildItem -LiteralPath $Wheelhouse -Filter '*.whl' -File)) {
        $stem = $wheel.Name.Substring(0, $wheel.Name.Length - 4)
        $parts = $stem.Split('-')
        if ($parts.Count -lt 5) { throw "Invalid wheel filename: $($wheel.Name)" }
        $distribution = Normalize-DistributionName $parts[0]
        $wheelVersion = $parts[1].Replace('_', '-')
        $pyTags = $parts[$parts.Count - 3].Split('.')
        $abiTags = $parts[$parts.Count - 2].Split('.')
        $platformTags = $parts[$parts.Count - 1].Split('.')
        $pythonCompatible = $false
        foreach ($tag in $pyTags) {
            if ($tag -eq 'py3' -or $tag -eq "py$pythonTag" -or $tag -eq "cp$pythonTag") {
                $pythonCompatible = $true
                break
            }
            $stableAbiMatch = [regex]::Match($tag, '^cp3(\d+)$')
            if ($abiTags -contains 'abi3' -and $stableAbiMatch.Success -and
                [int]$stableAbiMatch.Groups[1].Value -le $pythonMinor) {
                $pythonCompatible = $true
                break
            }
        }
        $abiCompatible = @($abiTags | Where-Object {
            $_ -eq 'none' -or $_ -eq 'abi3' -or $_ -eq "cp$pythonTag"
        }).Count -gt 0
        $platformCompatible = @($platformTags | Where-Object {
            $_ -eq 'any' -or $_ -eq 'win_amd64'
        }).Count -gt 0
        if ($distribution -eq $normalizedName -and $wheelVersion -eq $Version -and
            $pythonCompatible -and $abiCompatible -and $platformCompatible) {
            $candidates += $wheel
        }
    }
    if ($candidates.Count -ne 1) {
        throw "RUNTIME_CACHE_AMBIGUOUS: $Name $Version has $($candidates.Count) compatible wheel candidates"
    }
    $wheelName = $candidates[0].Name
    $hash = (Get-FileHash -LiteralPath $candidates[0].FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($RequirementsText -notmatch [regex]::Escape("--hash=sha256:$hash")) {
        throw "RUNTIME_CACHE_UNTRUSTED: $wheelName hash is absent from the dependency lock"
    }
    return [ordered]@{ file = $wheelName; sha256 = $hash; name = $Name; version = $Version }
}

function Export-Source([object]$Descriptor, [string]$CacheRoot, [string]$StagingRoot, [string]$ToolRoot, [string]$Label) {
    if ($Descriptor.repository -notmatch '^https://' -or
        $Descriptor.revision -notmatch '^[0-9a-f]{40}$' -or
        $Descriptor.tree -notmatch '^[0-9a-f]{40}$') {
        throw "$Label must pin an HTTPS repository, commit and tree"
    }
    $sourcePath = Resolve-RelativePath $CacheRoot $Descriptor.cachePath "$Label cachePath"
    $sourcePath = Resolve-ExistingDirectory $sourcePath "$Label cache"
    $origin = Invoke-GitValue $sourcePath @('remote', 'get-url', 'origin') "$Label origin"
    if ((Normalize-RepositoryUrl $origin) -ne (Normalize-RepositoryUrl $Descriptor.repository)) {
        throw "$Label cache origin does not match the lock"
    }
    $commit = Invoke-GitValue $sourcePath @('rev-parse', "$($Descriptor.revision)^{commit}") "$Label commit"
    $tree = Invoke-GitValue $sourcePath @('rev-parse', "$($Descriptor.revision)^{tree}") "$Label tree"
    if ($commit -ne $Descriptor.revision -or $tree -ne $Descriptor.tree) {
        throw "$Label Git object does not match the locked commit/tree"
    }

    $destination = Resolve-RelativePath $StagingRoot $Descriptor.installPath "$Label installPath"
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    $archive = Join-Path $ToolRoot ("source-" + [guid]::NewGuid().ToString('N') + '.zip')
    & git -C $sourcePath archive --format=zip --output=$archive $Descriptor.revision
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        throw "$Label could not be exported from the locked commit"
    }
    Expand-Archive -LiteralPath $archive -DestinationPath $destination -Force
    return [pscustomobject]@{
        descriptor = $Descriptor
        sourcePath = $sourcePath
        installPath = $destination
        revision = $commit
        tree = $tree
    }
}

$lockPath = (Get-Item -LiteralPath $LockFile -ErrorAction Stop).FullName
$lockDirectory = Split-Path -Parent $lockPath
$cacheRoot = Resolve-ExistingDirectory $OfficialCacheRoot 'OfficialCacheRoot'
$lock = Get-Content -Raw -LiteralPath $lockPath | ConvertFrom-Json
if ($lock.schemaVersion -ne 1 -or [string]::IsNullOrWhiteSpace($lock.runtimeId)) {
    throw 'Runtime lock schema or identity is invalid'
}
if ($lock.ready -ne $true -or $lock.unresolved.Count -ne 0) {
    throw "RUNTIME_LOCK_NOT_READY: $($lock.runtimeId) has unresolved inputs"
}
if ($lock.artifacts.Count -lt 4 -or $lock.notices.Count -lt 1) {
    throw 'Runtime lock requires Python, resolver, dependency, source and notice inputs'
}

$pythonArchive = Resolve-ExistingRelativeFile $cacheRoot $lock.python.archive.cachePath 'Python archive'
$pythonHash = Assert-Sha256 $pythonArchive $lock.python.archive.sha256 'Python archive'
$resolverArchive = Resolve-ExistingRelativeFile $cacheRoot $lock.resolver.archive.cachePath 'Resolver archive'
$resolverHash = Assert-Sha256 $resolverArchive $lock.resolver.archive.sha256 'Resolver archive'
$requirementsPath = Resolve-ExistingRelativeFile $lockDirectory $lock.requirements.path 'Requirements lock'
$requirementsHash = Assert-Sha256 $requirementsPath $lock.requirements.sha256 'Requirements lock'
$archiveExtractor = Resolve-ExistingRelativeFile $PSScriptRoot 'extract-locked-archive.py' 'Locked archive extractor'

$pythonArtifact = @($lock.artifacts | Where-Object { $_.kind -eq 'python-archive' })
$resolverArtifact = @($lock.artifacts | Where-Object { $_.kind -eq 'resolver-archive' })
$requirementsArtifact = @($lock.artifacts | Where-Object { $_.kind -eq 'dependency-lock' })
if ($pythonArtifact.Count -ne 1 -or $pythonArtifact[0].cachePath -ne $lock.python.archive.cachePath -or $pythonArtifact[0].sha256 -ne $pythonHash -or
    $resolverArtifact.Count -ne 1 -or $resolverArtifact[0].cachePath -ne $lock.resolver.archive.cachePath -or $resolverArtifact[0].sha256 -ne $resolverHash -or
    $requirementsArtifact.Count -ne 1 -or $requirementsArtifact[0].path -ne $lock.requirements.path -or $requirementsArtifact[0].sha256 -ne $requirementsHash) {
    throw 'Runtime artifact inventory does not match its authoritative inputs'
}

$requirementsText = Get-Content -Raw -LiteralPath $requirementsPath
$sourceOverlays = @($lock.sourceOverlays | Where-Object { $null -ne $_ })
$overlayInputs = @()
foreach ($overlay in $sourceOverlays) {
    if ($overlay.name -notmatch '^[A-Za-z0-9_.-]+$' -or
        [string]::IsNullOrWhiteSpace($overlay.version) -or
        $overlay.packagePath -notmatch '^[A-Za-z0-9_.-]+$' -or
        $overlay.licensePath -notmatch '^[A-Za-z0-9_.\/-]+$' -or
        $overlay.archive.url -notmatch '^https://' -or
        $overlay.archive.sha256 -notmatch '^[0-9a-f]{64}$') {
        throw 'Runtime source overlay metadata is invalid'
    }
    $archivePath = Resolve-ExistingRelativeFile $cacheRoot $overlay.archive.cachePath "Source overlay $($overlay.name)"
    $archiveHash = Assert-Sha256 $archivePath $overlay.archive.sha256 "Source overlay $($overlay.name)"
    $inventoryMatch = @($lock.artifacts | Where-Object {
        $_.kind -eq 'source-overlay' -and $_.cachePath -eq $overlay.archive.cachePath -and
        $_.sha256 -eq $archiveHash -and $_.treeSha256 -eq $overlay.treeSha256
    })
    if ($inventoryMatch.Count -ne 1) {
        throw "Runtime source overlay is absent from the artifact inventory: $($overlay.name)"
    }
    $requirementPattern = '(?ms)^' + [regex]::Escape($overlay.name) + '==' +
        [regex]::Escape([string]$overlay.version) + '\s+\\(.*?)(?=^[A-Za-z0-9_.-]+==|\z)'
    $requirementBlock = [regex]::Matches($requirementsText, $requirementPattern)
    if ($requirementBlock.Count -ne 1 -or
        $requirementBlock[0].Value -notmatch [regex]::Escape("--hash=sha256:$archiveHash")) {
        throw "Runtime source overlay is not bound to exactly one dependency lock entry: $($overlay.name)"
    }
    $overlayInputs += [pscustomobject]@{
        descriptor = $overlay
        archivePath = $archivePath
        requirementBlock = $requirementBlock[0].Value
    }
}

$nativeTools = @($lock.nativeTools | Where-Object { $null -ne $_ })
$nativeToolInputs = @()
foreach ($nativeTool in $nativeTools) {
    if ($nativeTool.name -notmatch '^[A-Za-z0-9_. -]+$' -or
        $nativeTool.architecture -notin @('x86', 'x64') -or
        $nativeTool.archive.url -notmatch '^https://' -or
        $nativeTool.archive.sha256 -notmatch '^[0-9a-f]{64}$' -or
        $nativeTool.executable.sha256 -notmatch '^[0-9a-f]{64}$' -or
        @($nativeTool.installFiles).Count -lt 1 -or
        $nativeTool.correspondingSource.url -notmatch '^https://' -or
        $nativeTool.correspondingSource.sha256 -notmatch '^[0-9a-f]{64}$') {
        throw 'Runtime native tool metadata is invalid'
    }
    $archivePath = Resolve-ExistingRelativeFile $cacheRoot $nativeTool.archive.cachePath "Native tool $($nativeTool.name)"
    $archiveHash = Assert-Sha256 $archivePath $nativeTool.archive.sha256 "Native tool $($nativeTool.name)"
    $sourceArchivePath = Resolve-ExistingRelativeFile $cacheRoot $nativeTool.correspondingSource.cachePath `
        "Native tool $($nativeTool.name) corresponding source"
    $sourceArchiveHash = Assert-Sha256 $sourceArchivePath $nativeTool.correspondingSource.sha256 `
        "Native tool $($nativeTool.name) corresponding source"
    $toolInventory = @($lock.artifacts | Where-Object {
        $_.kind -eq 'native-tool-archive' -and $_.cachePath -eq $nativeTool.archive.cachePath -and
        $_.sha256 -eq $archiveHash -and $_.treeSha256 -eq $nativeTool.treeSha256
    })
    $sourceInventory = @($lock.artifacts | Where-Object {
        $_.kind -eq 'corresponding-source-archive' -and
        $_.cachePath -eq $nativeTool.correspondingSource.cachePath -and $_.sha256 -eq $sourceArchiveHash
    })
    if ($toolInventory.Count -ne 1 -or $sourceInventory.Count -ne 1) {
        throw "Runtime native tool inventory is incomplete: $($nativeTool.name)"
    }
    $nativeToolInputs += [pscustomobject]@{
        descriptor = $nativeTool
        archivePath = $archivePath
        correspondingSourcePath = $sourceArchivePath
    }
}

$dependencyLicenseFiles = @($lock.dependencyLicenseFiles | Where-Object { $null -ne $_ })
$dependencyLicenseInputs = @()
foreach ($supplement in $dependencyLicenseFiles) {
    $normalizedSupplementName = Normalize-DistributionName ([string]$supplement.name)
    $sourceMatch = [regex]::Match([string]$supplement.sourceUrl,
        '^https://github\.com/[^/]+/[^/]+/tree/([0-9a-f]{40})$')
    if ($normalizedSupplementName -notmatch '^[a-z0-9-]+$' -or
        [string]::IsNullOrWhiteSpace($supplement.version) -or
        [string]::IsNullOrWhiteSpace($supplement.license) -or
        -not $sourceMatch.Success -or @($supplement.files).Count -lt 1) {
        throw 'Runtime dependency license supplement metadata is invalid'
    }
    $sourceRevision = $sourceMatch.Groups[1].Value
    $resolvedFiles = @()
    foreach ($licenseFile in @($supplement.files)) {
        if ($licenseFile.fileName -notmatch '^[A-Za-z0-9_.-]+$' -or
            $licenseFile.url -notmatch ('^https://raw\.githubusercontent\.com/[^/]+/[^/]+/' +
                [regex]::Escape($sourceRevision) + '/.+$') -or
            $licenseFile.sha256 -notmatch '^[0-9a-f]{64}$' -or $licenseFile.bytes -lt 1) {
            throw "Runtime dependency license file metadata is invalid: $($supplement.name)"
        }
        $licensePath = Resolve-ExistingRelativeFile $cacheRoot $licenseFile.cachePath `
            "Dependency license $($supplement.name) $($licenseFile.fileName)"
        $licenseItem = Get-Item -LiteralPath $licensePath -ErrorAction Stop
        if ($licenseItem.Length -ne [long]$licenseFile.bytes) {
            throw "Runtime dependency license size mismatch: $($supplement.name) $($licenseFile.fileName)"
        }
        $licenseHash = Assert-Sha256 $licensePath $licenseFile.sha256 `
            "Dependency license $($supplement.name) $($licenseFile.fileName)"
        $inventoryMatch = @($lock.artifacts | Where-Object {
            $_.kind -eq 'dependency-license-file' -and $_.cachePath -eq $licenseFile.cachePath -and
            $_.sha256 -eq $licenseHash
        })
        if ($inventoryMatch.Count -ne 1) {
            throw "Runtime dependency license is absent from the artifact inventory: $($supplement.name) $($licenseFile.fileName)"
        }
        $resolvedFiles += [pscustomobject]@{
            descriptor = $licenseFile
            path = $licensePath
            hash = $licenseHash
        }
    }
    $dependencyLicenseInputs += [pscustomobject]@{
        descriptor = $supplement
        normalizedName = $normalizedSupplementName
        files = $resolvedFiles
    }
}

$outputIsAbsolute = -not [string]::IsNullOrWhiteSpace($OutputRoot) -and [System.IO.Path]::IsPathRooted($OutputRoot)
$outputFull = if ($outputIsAbsolute) { [System.IO.Path]::GetFullPath($OutputRoot) } else { '' }
if (-not $outputIsAbsolute -or $outputFull -eq [System.IO.Path]::GetPathRoot($outputFull) -or (Test-Path -LiteralPath $outputFull)) {
    throw 'OutputRoot must be an absolute, non-root path that does not already exist'
}
Assert-NoReparseAncestors $outputFull 'OutputRoot'
$outputParentPath = Split-Path -Parent $outputFull
$outputParent = Resolve-ExistingDirectory $outputParentPath 'Output parent'
$staging = Join-Path $outputParent ('.agplayer-runtime-' + [guid]::NewGuid().ToString('N'))
$toolRoot = Join-Path $staging '.build-tools'
New-Item -ItemType Directory -Path $staging,$toolRoot | Out-Null

try {
    Expand-Archive -LiteralPath $pythonArchive -DestinationPath $staging -Force
    $pythonExecutable = Join-Path $staging 'python.exe'
    if (-not (Test-Path -LiteralPath $pythonExecutable -PathType Leaf)) {
        throw 'Python archive does not provide runtimeRoot/python.exe'
    }
    $pythonVersion = & $pythonExecutable --version 2>&1
    if ($LASTEXITCODE -ne 0 -or $pythonVersion -ne "Python $($lock.python.version)") {
        throw "Python runtime version mismatch: $pythonVersion"
    }

    $exportedSource = Export-Source $lock.source $cacheRoot $staging $toolRoot 'Runtime source'
    $exportedSubmodules = @()
    foreach ($submodule in @($lock.source.submodules | Where-Object { $null -ne $_ })) {
        $exportedSubmodules += Export-Source $submodule $cacheRoot $staging $toolRoot "Runtime submodule $($submodule.repository)"
    }
    $sourceArtifacts = @($lock.artifacts | Where-Object { $_.kind -eq 'source-tree' })
    $exportedTrees = @($exportedSource) + @($exportedSubmodules)
    if ($sourceArtifacts.Count -ne $exportedTrees.Count) {
        throw 'Runtime source artifact inventory is incomplete'
    }
    foreach ($source in $exportedTrees) {
        $inventoryMatch = @($sourceArtifacts | Where-Object {
            $_.cachePath -eq $source.descriptor.cachePath -and
            $_.revision -eq $source.revision -and
            $_.tree -eq $source.tree
        })
        if ($inventoryMatch.Count -ne 1) {
            throw "Runtime source artifact is absent from the inventory: $($source.descriptor.cachePath)"
        }
    }

    $sitePackages = Join-Path $staging 'Lib/site-packages'
    New-Item -ItemType Directory -Path $sitePackages -Force | Out-Null
    $externalNoticeSections = @()
    $overlayManifest = @()
    $overlayIndex = 0
    foreach ($overlayInput in $overlayInputs) {
        $overlay = $overlayInput.descriptor
        $extractedRoot = Join-Path $toolRoot "overlay-$overlayIndex"
        Expand-LockedArchive $pythonExecutable $archiveExtractor $overlayInput.archivePath `
            ([pscustomobject]@{
                format = $overlay.archive.format
                archiveRoot = $overlay.archive.archiveRoot
                fileCount = $overlay.fileCount
                expandedBytes = $overlay.expandedBytes
                treeSha256 = $overlay.treeSha256
            }) $extractedRoot "Source overlay $($overlay.name)"
        $packageSource = Resolve-RelativePath $extractedRoot $overlay.packagePath `
            "Source overlay $($overlay.name) packagePath"
        $packageItem = Get-Item -LiteralPath $packageSource -ErrorAction Stop
        if (-not $packageItem.PSIsContainer -or
            ($packageItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
            throw "Source overlay package is not a real directory: $($overlay.name)"
        }
        $packageDestination = Join-Path $sitePackages $packageItem.Name
        if (Test-Path -LiteralPath $packageDestination) {
            throw "Source overlay collides with an installed package: $($overlay.name)"
        }
        Copy-Item -LiteralPath $packageItem.FullName -Destination $packageDestination -Recurse
        $licenseSource = Resolve-ExistingRelativeFile $extractedRoot $overlay.licensePath `
            "Source overlay $($overlay.name) license"
        $licenseDestination = Resolve-RelativePath $staging `
            "third-party/licenses/$($overlay.name)-$($overlay.version)-LICENSE.txt" `
            "Source overlay $($overlay.name) installed license"
        New-Item -ItemType Directory -Path (Split-Path -Parent $licenseDestination) -Force | Out-Null
        Copy-Item -LiteralPath $licenseSource -Destination $licenseDestination
        $licenseText = Get-Content -Raw -LiteralPath $licenseSource
        $externalNoticeSections += "$($overlay.name) $($overlay.version)`r`nLicense: $($overlay.license)`r`nSource: $($overlay.sourceUrl)`r`nArchive SHA-256: $($overlay.archive.sha256)`r`nTree SHA-256: $($overlay.treeSha256)`r`n`r`n$licenseText"
        $overlayManifest += [ordered]@{
            name = $overlay.name
            version = $overlay.version
            archiveSha256 = $overlay.archive.sha256
            treeSha256 = $overlay.treeSha256
            packagePath = "Lib/site-packages/$($packageItem.Name)"
        }
        $overlayIndex++
    }

    $nativeToolManifest = @()
    $nativeIndex = 0
    foreach ($nativeInput in $nativeToolInputs) {
        $nativeTool = $nativeInput.descriptor
        $extractedRoot = Join-Path $toolRoot "native-$nativeIndex"
        Expand-LockedArchive $pythonExecutable $archiveExtractor $nativeInput.archivePath `
            ([pscustomobject]@{
                format = $nativeTool.archive.format
                archiveRoot = $nativeTool.archive.archiveRoot
                fileCount = $nativeTool.fileCount
                expandedBytes = $nativeTool.expandedBytes
                treeSha256 = $nativeTool.treeSha256
            }) $extractedRoot "Native tool $($nativeTool.name)"
        $installedNativeFiles = @()
        foreach ($installFile in @($nativeTool.installFiles)) {
            $sourceFile = Resolve-ExistingRelativeFile $extractedRoot $installFile `
                "Native tool $($nativeTool.name) install file"
            $destinationRelative = if ($nativeTool.installPath -eq '.') {
                $installFile
            } else {
                "$($nativeTool.installPath)/$installFile"
            }
            $destinationFile = Resolve-RelativePath $staging $destinationRelative `
                "Native tool $($nativeTool.name) destination"
            New-Item -ItemType Directory -Path (Split-Path -Parent $destinationFile) -Force | Out-Null
            if (Test-Path -LiteralPath $destinationFile) {
                throw "Native tool install collision: $destinationRelative"
            }
            Copy-Item -LiteralPath $sourceFile -Destination $destinationFile
            $installedNativeFiles += $destinationRelative.Replace('\', '/')
        }
        $installedExecutableRelative = if ($nativeTool.installPath -eq '.') {
            $nativeTool.executable.path
        } else {
            "$($nativeTool.installPath)/$($nativeTool.executable.path)"
        }
        $installedExecutable = Resolve-ExistingRelativeFile $staging $installedExecutableRelative `
            "Native tool $($nativeTool.name) executable"
        Assert-Sha256 $installedExecutable $nativeTool.executable.sha256 `
            "Native tool $($nativeTool.name) executable" | Out-Null
        $versionOutput = (& $installedExecutable @($nativeTool.executable.versionArguments) 2>&1 | Out-String).Trim()
        if ($LASTEXITCODE -ne 0 -or $versionOutput -notmatch $nativeTool.executable.versionPattern) {
            throw "Native tool version probe failed: $($nativeTool.name): $versionOutput"
        }
        $licenseSource = Resolve-ExistingRelativeFile $extractedRoot $nativeTool.licensePath `
            "Native tool $($nativeTool.name) license"
        $licenseDestination = Resolve-RelativePath $staging `
            "third-party/licenses/$($nativeTool.name)-$($nativeTool.version)-LICENSE.txt" `
            "Native tool $($nativeTool.name) installed license"
        New-Item -ItemType Directory -Path (Split-Path -Parent $licenseDestination) -Force | Out-Null
        Copy-Item -LiteralPath $licenseSource -Destination $licenseDestination
        $correspondingSourceDestination = Resolve-RelativePath $staging `
            $nativeTool.correspondingSource.installPath `
            "Native tool $($nativeTool.name) corresponding source destination"
        New-Item -ItemType Directory -Path (Split-Path -Parent $correspondingSourceDestination) -Force | Out-Null
        Copy-Item -LiteralPath $nativeInput.correspondingSourcePath -Destination $correspondingSourceDestination
        $licenseText = Get-Content -Raw -LiteralPath $licenseSource
        $externalNoticeSections += "$($nativeTool.name) $($nativeTool.version) ($($nativeTool.architecture))`r`nLicense: $($nativeTool.license)`r`nSource: $($nativeTool.sourceUrl)`r`nBinary archive SHA-256: $($nativeTool.archive.sha256)`r`nCorresponding source SHA-256: $($nativeTool.correspondingSource.sha256)`r`n`r`n$licenseText"
        $nativeToolManifest += [ordered]@{
            name = $nativeTool.name
            version = $nativeTool.version
            architecture = $nativeTool.architecture
            archiveSha256 = $nativeTool.archive.sha256
            treeSha256 = $nativeTool.treeSha256
            correspondingSourceSha256 = $nativeTool.correspondingSource.sha256
            executable = $installedExecutableRelative.Replace('\', '/')
            executableSha256 = $nativeTool.executable.sha256
            files = $installedNativeFiles
        }
        $nativeIndex++
    }
    $pthFile = Get-ChildItem -LiteralPath $staging -Filter 'python*._pth' -File | Select-Object -First 1
    if ($pthFile) {
        $pthLines = @(Get-Content -LiteralPath $pthFile.FullName | Where-Object {
            $_.Trim() -notin @('#import site', 'import site', '.', 'Lib\site-packages', $lock.source.pythonPath)
        })
        $pthLines += '.'
        $pthLines += 'Lib\site-packages'
        if (-not [string]::IsNullOrWhiteSpace($lock.source.pythonPath)) {
            $pthLines += $lock.source.pythonPath
        }
        $pthLines += 'import site'
        [System.IO.File]::WriteAllLines($pthFile.FullName, $pthLines, [System.Text.UTF8Encoding]::new($false))
    }
    if (-not [string]::IsNullOrWhiteSpace($lock.source.pythonPath)) {
        [System.IO.File]::WriteAllText(
            (Join-Path $sitePackages 'agplayer-runtime-source.pth'),
            "import sys; sys.path.insert(0, sys.prefix + r'\$($lock.source.pythonPath.Replace('/', '\'))')`n",
            [System.Text.UTF8Encoding]::new($false))
    }
    if ($nativeToolInputs.Count -gt 0) {
        [System.IO.File]::WriteAllText(
            (Join-Path $sitePackages 'agplayer-runtime-native-tools.pth'),
            'import os,sys; os.environ["PATH"]=sys.prefix+os.pathsep+os.environ.get("PATH","")' + "`n",
            [System.Text.UTF8Encoding]::new($false))
    }
    $baselineMetadata = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($distInfo in @(Get-ChildItem -LiteralPath $sitePackages -Filter '*.dist-info' -Directory)) {
        $metadataPath = Join-Path $distInfo.FullName 'METADATA'
        if (Test-Path -LiteralPath $metadataPath -PathType Leaf) {
            $null = $baselineMetadata.Add([System.IO.Path]::GetFullPath($metadataPath))
        }
    }
    $effectiveRequirementsText = $requirementsText
    foreach ($overlayInput in $overlayInputs) {
        $effectiveRequirementsText = $effectiveRequirementsText.Replace(
            [string]$overlayInput.requirementBlock, '')
    }
    $effectiveRequirementsPath = Join-Path $toolRoot 'effective.requirements.txt'
    [System.IO.File]::WriteAllText($effectiveRequirementsPath, $effectiveRequirementsText,
        [System.Text.UTF8Encoding]::new($false))
    $dependencyLines = @($effectiveRequirementsText -split "`r?`n" | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and -not $_.TrimStart().StartsWith('#')
    })
    if ($dependencyLines.Count -gt 0) {
        $wheelhousePath = Resolve-RelativePath $cacheRoot 'wheelhouse' 'Wheelhouse'
        $wheelhousePath = Resolve-ExistingDirectory $wheelhousePath 'Wheelhouse'
        if (@(Get-ChildItem -LiteralPath $wheelhousePath -File).Count -eq 0) {
            throw 'RUNTIME_CACHE_INCOMPLETE: wheelhouse is empty'
        }
        $nonWheels = @(Get-ChildItem -LiteralPath $wheelhousePath -File | Where-Object {
            $_.Extension -ne '.whl'
        })
        if ($nonWheels.Count -gt 0) {
            throw "RUNTIME_CACHE_UNTRUSTED: source distributions are forbidden: $($nonWheels[0].Name)"
        }
        $resolverRoot = Join-Path $toolRoot 'resolver'
        Expand-Archive -LiteralPath $resolverArchive -DestinationPath $resolverRoot -Force
        $resolverExecutable = Get-ChildItem -LiteralPath $resolverRoot -Filter 'uv.exe' -File -Recurse | Select-Object -First 1
        if (-not $resolverExecutable) {
            throw 'Resolver archive does not contain uv.exe'
        }
        & $resolverExecutable.FullName pip install --python $pythonExecutable --offline --no-index `
            --find-links $wheelhousePath --require-hashes --no-deps --only-binary ':all:' `
            --requirements $effectiveRequirementsPath
        if ($LASTEXITCODE -ne 0) {
            throw 'RUNTIME_CACHE_INCOMPLETE: locked dependencies could not be installed offline'
        }
    }

    $noticeSections = @($externalNoticeSections)
    if (Test-Path -LiteralPath (Join-Path $staging 'LICENSE.txt') -PathType Leaf) {
        $pythonLicense = Get-Content -Raw -LiteralPath (Join-Path $staging 'LICENSE.txt')
        $noticeSections += "CPython $($lock.python.version)`r`nSource: https://www.python.org/`r`n`r`n$pythonLicense"
    }
    foreach ($notice in $lock.notices) {
        if ([string]::IsNullOrWhiteSpace($notice.name) -or [string]::IsNullOrWhiteSpace($notice.license) -or
            $notice.sourceUrl -notmatch '^https://' -or [string]::IsNullOrWhiteSpace($notice.sourceLicensePath)) {
            throw 'Runtime notice metadata is incomplete'
        }
        $noticeSource = $exportedSource
        if (-not [string]::IsNullOrWhiteSpace($notice.sourceRef)) {
            if ($notice.sourceRef -notmatch '^submodules\[(\d+)\]$') {
                throw "Unsupported notice sourceRef: $($notice.sourceRef)"
            }
            $sourceIndex = [int]$Matches[1]
            if ($sourceIndex -ge $exportedSubmodules.Count) {
                throw "Notice sourceRef is out of range: $($notice.sourceRef)"
            }
            $noticeSource = $exportedSubmodules[$sourceIndex]
        }
        $licensePath = Resolve-ExistingRelativeFile $noticeSource.installPath $notice.sourceLicensePath 'Source license'
        $licenseText = Get-Content -Raw -LiteralPath $licensePath
        $noticeSections += "$($notice.name)`r`nLicense: $($notice.license)`r`nSource: $($notice.sourceUrl)`r`n`r`n$licenseText"
    }

    $distributions = @()
    $wheels = @()
    $usedDependencyLicenses = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($metadataFile in @(Get-ChildItem -LiteralPath $sitePackages -Filter '*.dist-info' -Directory |
        ForEach-Object { Get-Item -LiteralPath (Join-Path $_.FullName 'METADATA') -ErrorAction Stop } |
        Where-Object { -not $baselineMetadata.Contains($_.FullName) })) {
        $metadata = Get-Content -LiteralPath $metadataFile.FullName
        $nameLine = $metadata | Where-Object { $_ -like 'Name: *' } | Select-Object -First 1
        $versionLine = $metadata | Where-Object { $_ -like 'Version: *' } | Select-Object -First 1
        $distributionName = if ($nameLine) { $nameLine.Substring(6).Trim() } else { $metadataFile.Directory.BaseName }
        $distributionVersion = if ($versionLine) { $versionLine.Substring(9).Trim() } else { 'unknown' }
        $wheels += Get-CompatibleWheel $wheelhousePath $distributionName $distributionVersion `
            $lock.python.version $requirementsText
        $licenseFiles = @(Get-ChildItem -LiteralPath $metadataFile.Directory.FullName -File -Recurse | Where-Object {
            $_.Name -match '^(LICENSE|LICENCE|COPYING|NOTICE)' -or $_.Directory.Name -eq 'licenses'
        })
        $supplements = @($dependencyLicenseInputs | Where-Object {
            $_.normalizedName -eq (Normalize-DistributionName $distributionName) -and
            $_.descriptor.version -eq $distributionVersion
        })
        if ($supplements.Count -gt 1) {
            throw "Runtime dependency license supplement is ambiguous: $distributionName $distributionVersion"
        }
        if ($licenseFiles.Count -eq 0 -and $supplements.Count -eq 0) {
            throw "RUNTIME_LICENSE_UNRESOLVED: $distributionName $distributionVersion has no packaged license text"
        }
        $licenseInventory = @()
        foreach ($licenseFile in $licenseFiles) {
            $relativeLicense = (Get-RelativePath $staging $licenseFile.FullName).Replace('\', '/')
            $licenseHash = (Get-FileHash -LiteralPath $licenseFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            $licenseInventory += [ordered]@{ path = $relativeLicense; sha256 = $licenseHash }
            $noticeSections += "$distributionName $distributionVersion`r`nInstalled license: $relativeLicense`r`n`r`n$(Get-Content -Raw -LiteralPath $licenseFile.FullName)"
        }
        if ($licenseFiles.Count -eq 0) {
            $supplement = $supplements[0]
            $supplementKey = "$($supplement.normalizedName)==$distributionVersion"
            $supplementDirectory = "$($supplement.normalizedName)-$distributionVersion"
            $null = $usedDependencyLicenses.Add($supplementKey)
            foreach ($licenseInput in @($supplement.files)) {
                $destinationRelative = "third-party/licenses/dependencies/$supplementDirectory/$($licenseInput.descriptor.fileName)"
                $destination = Resolve-RelativePath $staging $destinationRelative `
                    "Dependency license $distributionName destination"
                New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
                if (Test-Path -LiteralPath $destination) {
                    throw "Runtime dependency license destination collides: $destinationRelative"
                }
                Copy-Item -LiteralPath $licenseInput.path -Destination $destination
                $relativeLicense = $destinationRelative.Replace('\', '/')
                $licenseInventory += [ordered]@{ path = $relativeLicense; sha256 = $licenseInput.hash }
                $noticeSections += "$distributionName $distributionVersion`r`nLicense: $($supplement.descriptor.license)`r`nOfficial source: $($supplement.descriptor.sourceUrl)`r`nInstalled supplemental license: $relativeLicense`r`n`r`n$(Get-Content -Raw -LiteralPath $licenseInput.path)"
            }
        }
        $distributions += [ordered]@{ name = $distributionName; version = $distributionVersion; licenses = $licenseInventory }
    }
    if ($usedDependencyLicenses.Count -ne $dependencyLicenseInputs.Count) {
        $unused = @($dependencyLicenseInputs | Where-Object {
            -not $usedDependencyLicenses.Contains("$($_.normalizedName)==$($_.descriptor.version)")
        } | ForEach-Object { "$($_.normalizedName)==$($_.descriptor.version)" })
        throw "Runtime dependency license supplements do not match installed distributions: $($unused -join ', ')"
    }

    foreach ($overlayInput in $overlayInputs) {
        $overlay = $overlayInput.descriptor
        if ([string]::IsNullOrWhiteSpace($overlay.importProbe.module) -or
            [string]::IsNullOrWhiteSpace($overlay.importProbe.expectedRoot)) {
            throw "Source overlay import probe is incomplete: $($overlay.name)"
        }
        $originMarker = 'AGPLAYER_IMPORT_ORIGIN='
        $overlayCommand = "import $($overlay.importProbe.module); print('$originMarker' + $($overlay.importProbe.module).__file__)"
        $oldPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $overlayOutput = (& $pythonExecutable -I -s -c $overlayCommand 2>&1 | Out-String).Trim()
        $overlayExitCode = $LASTEXITCODE
        $ErrorActionPreference = $oldPreference
        $overlayOrigins = @($overlayOutput -split "`r?`n" | Where-Object {
            $_.StartsWith($originMarker, [System.StringComparison]::Ordinal)
        })
        $overlayOrigin = if ($overlayOrigins.Count -eq 1) {
            $overlayOrigins[0].Substring($originMarker.Length)
        } else { '' }
        $expectedOverlayRoot = (Resolve-RelativePath $staging $overlay.importProbe.expectedRoot `
            "Source overlay $($overlay.name) importProbe expectedRoot").TrimEnd('\')
        if ($overlayExitCode -ne 0 -or $overlayOrigins.Count -ne 1 -or
            -not [System.IO.Path]::GetFullPath($overlayOrigin).StartsWith(
                $expectedOverlayRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Source overlay import did not resolve from the locked archive: $($overlay.name): $overlayOutput"
        }
    }

    foreach ($nativeInput in $nativeToolInputs) {
        $nativeTool = $nativeInput.descriptor
        if ($nativeTool.smokeTest -eq 'sox-norm') {
            $smokeCommand = "import numpy as np, sox; x=np.sin(2*np.pi*440*np.arange(3200)/16000).astype(np.float32)*0.1; t=sox.Transformer(); t.norm(db_level=-6); y=t.build_array(input_array=x, sample_rate_in=16000); assert len(y)>0 and 0.49<float(np.max(np.abs(y)))<0.52; print(len(y), float(np.max(np.abs(y))))"
            $oldPreference = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            $smokeOutput = (& $pythonExecutable -I -s -c $smokeCommand 2>&1 | Out-String).Trim()
            $smokeExitCode = $LASTEXITCODE
            $ErrorActionPreference = $oldPreference
            if ($smokeExitCode -ne 0 -or $smokeOutput -notmatch '^\d+\s+0\.5') {
                throw "Native tool short-WAV normalization failed: $($nativeTool.name): $smokeOutput"
            }
        } elseif (-not [string]::IsNullOrWhiteSpace($nativeTool.smokeTest)) {
            throw "Unsupported native tool smoke test: $($nativeTool.smokeTest)"
        }
    }

    $probe = $lock.source.importProbe
    if ([string]::IsNullOrWhiteSpace($probe.module) -or [string]::IsNullOrWhiteSpace($probe.expectedRoot)) {
        throw 'Runtime source import probe is incomplete'
    }
    $probeOriginMarker = 'AGPLAYER_SOURCE_ORIGIN='
    $probeCommand = "import $($probe.module); print('$probeOriginMarker' + $($probe.module).__file__)"
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $probeOutput = (& $pythonExecutable -I -s -c $probeCommand 2>&1 | Out-String).Trim()
    $probeExitCode = $LASTEXITCODE
    $ErrorActionPreference = $oldPreference
    $importOrigins = @($probeOutput -split "`r?`n" | Where-Object {
        $_.StartsWith($probeOriginMarker, [System.StringComparison]::Ordinal)
    })
    $importOrigin = if ($importOrigins.Count -eq 1) {
        $importOrigins[0].Substring($probeOriginMarker.Length)
    } else { '' }
    $expectedImportRoot = (Resolve-RelativePath $staging $probe.expectedRoot 'Import probe expectedRoot').TrimEnd('\') + '\'
    if ($probeExitCode -ne 0 -or $importOrigins.Count -ne 1 -or
        -not [System.IO.Path]::GetFullPath($importOrigin).StartsWith($expectedImportRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Runtime source import did not resolve from the locked Git tree: $probeOutput"
    }

    [System.IO.File]::WriteAllText((Join-Path $staging 'THIRD_PARTY_NOTICES.txt'),
        ($noticeSections -join "`r`n`r`n---`r`n`r`n"), [System.Text.UTF8Encoding]::new($false))

    $manifest = [ordered]@{
        schemaVersion = 1
        runtimeId = $lock.runtimeId
        python = [ordered]@{ version = $lock.python.version; sha256 = $pythonHash }
        resolver = [ordered]@{ version = $lock.resolver.version; sha256 = $resolverHash }
        source = [ordered]@{ repository = $lock.source.repository; revision = $exportedSource.revision; tree = $exportedSource.tree }
        requirements = [ordered]@{ path = $lock.requirements.path; sha256 = $requirementsHash }
        artifacts = @($lock.artifacts)
        models = @($lock.models)
        distributions = $distributions
        wheels = $wheels
        sourceOverlays = $overlayManifest
        nativeTools = $nativeToolManifest
    }
    [System.IO.File]::WriteAllText((Join-Path $staging 'runtime-manifest.json'),
        ($manifest | ConvertTo-Json -Depth 12), [System.Text.UTF8Encoding]::new($false))

    $toolItem = Get-Item -LiteralPath $toolRoot -ErrorAction Stop
    $stagingPrefix = $staging.TrimEnd('\') + '\'
    if (-not $toolItem.FullName.StartsWith($stagingPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
        ($toolItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
        throw 'Build tool cleanup target escaped staging'
    }
    Remove-TreeSafely $toolItem.FullName $staging 'Build tool directory'

    $finalVersion = & $pythonExecutable --version 2>&1
    if ($LASTEXITCODE -ne 0 -or $finalVersion -ne "Python $($lock.python.version)") {
        throw "Packaged python.exe is not executable: $finalVersion"
    }
    Move-Item -LiteralPath $staging -Destination $outputFull
    Write-Output "Runtime Pack created: $outputFull"
}
catch {
    $stagingItem = Get-Item -LiteralPath $staging -ErrorAction SilentlyContinue
    if ($stagingItem -and
        $stagingItem.FullName.StartsWith($outputParent.TrimEnd('\') + '\', [System.StringComparison]::OrdinalIgnoreCase) -and
        -not ($stagingItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
        Remove-TreeSafely $stagingItem.FullName $outputParent 'Runtime staging directory'
    }
    throw
}

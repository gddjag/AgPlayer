param(
    [Parameter(Mandatory = $true)]
    [string]$LockFile,
    [Parameter(Mandatory = $true)]
    [string]$OfficialCacheRoot,
    [Parameter(Mandatory = $true)]
    [string]$OutputRoot
)

$ErrorActionPreference = 'Stop'

function Resolve-ExistingDirectory([string]$Path, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($Path) -or -not [System.IO.Path]::IsPathRooted($Path)) {
        throw "$Label must be an absolute path"
    }
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
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

$pythonArtifact = @($lock.artifacts | Where-Object { $_.kind -eq 'python-archive' })
$resolverArtifact = @($lock.artifacts | Where-Object { $_.kind -eq 'resolver-archive' })
$requirementsArtifact = @($lock.artifacts | Where-Object { $_.kind -eq 'dependency-lock' })
if ($pythonArtifact.Count -ne 1 -or $pythonArtifact[0].cachePath -ne $lock.python.archive.cachePath -or $pythonArtifact[0].sha256 -ne $pythonHash -or
    $resolverArtifact.Count -ne 1 -or $resolverArtifact[0].cachePath -ne $lock.resolver.archive.cachePath -or $resolverArtifact[0].sha256 -ne $resolverHash -or
    $requirementsArtifact.Count -ne 1 -or $requirementsArtifact[0].path -ne $lock.requirements.path -or $requirementsArtifact[0].sha256 -ne $requirementsHash) {
    throw 'Runtime artifact inventory does not match its authoritative inputs'
}

$outputIsAbsolute = -not [string]::IsNullOrWhiteSpace($OutputRoot) -and [System.IO.Path]::IsPathRooted($OutputRoot)
$outputFull = if ($outputIsAbsolute) { [System.IO.Path]::GetFullPath($OutputRoot) } else { '' }
if (-not $outputIsAbsolute -or $outputFull -eq [System.IO.Path]::GetPathRoot($outputFull) -or (Test-Path -LiteralPath $outputFull)) {
    throw 'OutputRoot must be an absolute, non-root path that does not already exist'
}
$outputParentPath = Split-Path -Parent $outputFull
if (-not (Test-Path -LiteralPath $outputParentPath -PathType Container)) {
    New-Item -ItemType Directory -Path $outputParentPath -Force | Out-Null
}
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

    $dependencyLines = @(Get-Content -LiteralPath $requirementsPath | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and -not $_.TrimStart().StartsWith('#')
    })
    if ($dependencyLines.Count -gt 0) {
        $wheelhousePath = Resolve-RelativePath $cacheRoot 'wheelhouse' 'Wheelhouse'
        $wheelhousePath = Resolve-ExistingDirectory $wheelhousePath 'Wheelhouse'
        if (@(Get-ChildItem -LiteralPath $wheelhousePath -File).Count -eq 0) {
            throw 'RUNTIME_CACHE_INCOMPLETE: wheelhouse is empty'
        }
        $resolverRoot = Join-Path $toolRoot 'resolver'
        Expand-Archive -LiteralPath $resolverArchive -DestinationPath $resolverRoot -Force
        $resolverExecutable = Get-ChildItem -LiteralPath $resolverRoot -Filter 'uv.exe' -File -Recurse | Select-Object -First 1
        if (-not $resolverExecutable) {
            throw 'Resolver archive does not contain uv.exe'
        }
        & $resolverExecutable.FullName pip install --python $pythonExecutable --offline --no-index `
            --find-links $wheelhousePath --require-hashes --no-deps --requirements $requirementsPath
        if ($LASTEXITCODE -ne 0) {
            throw 'RUNTIME_CACHE_INCOMPLETE: locked dependencies could not be installed offline'
        }
    }

    $noticeSections = @()
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
    foreach ($metadataFile in @(Get-ChildItem -LiteralPath $sitePackages -Filter 'METADATA' -File -Recurse | Where-Object {
        $_.Directory.Name.EndsWith('.dist-info', [System.StringComparison]::OrdinalIgnoreCase)
    })) {
        $metadata = Get-Content -LiteralPath $metadataFile.FullName
        $nameLine = $metadata | Where-Object { $_ -like 'Name: *' } | Select-Object -First 1
        $versionLine = $metadata | Where-Object { $_ -like 'Version: *' } | Select-Object -First 1
        $distributionName = if ($nameLine) { $nameLine.Substring(6).Trim() } else { $metadataFile.Directory.BaseName }
        $distributionVersion = if ($versionLine) { $versionLine.Substring(9).Trim() } else { 'unknown' }
        $licenseFiles = @(Get-ChildItem -LiteralPath $metadataFile.Directory.FullName -File -Recurse | Where-Object {
            $_.Name -match '^(LICENSE|LICENCE|COPYING|NOTICE)' -or $_.Directory.Name -eq 'licenses'
        })
        if ($licenseFiles.Count -eq 0) {
            throw "RUNTIME_LICENSE_UNRESOLVED: $distributionName $distributionVersion has no packaged license text"
        }
        $licenseInventory = @()
        foreach ($licenseFile in $licenseFiles) {
            $relativeLicense = (Get-RelativePath $staging $licenseFile.FullName).Replace('\', '/')
            $licenseHash = (Get-FileHash -LiteralPath $licenseFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            $licenseInventory += [ordered]@{ path = $relativeLicense; sha256 = $licenseHash }
            $noticeSections += "$distributionName $distributionVersion`r`nInstalled license: $relativeLicense`r`n`r`n$(Get-Content -Raw -LiteralPath $licenseFile.FullName)"
        }
        $distributions += [ordered]@{ name = $distributionName; version = $distributionVersion; licenses = $licenseInventory }
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
    }
    [System.IO.File]::WriteAllText((Join-Path $staging 'runtime-manifest.json'),
        ($manifest | ConvertTo-Json -Depth 12), [System.Text.UTF8Encoding]::new($false))

    $toolItem = Get-Item -LiteralPath $toolRoot -ErrorAction Stop
    $stagingPrefix = $staging.TrimEnd('\') + '\'
    if (-not $toolItem.FullName.StartsWith($stagingPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
        ($toolItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
        throw 'Build tool cleanup target escaped staging'
    }
    Remove-Item -LiteralPath $toolItem.FullName -Recurse -Force

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
        Remove-Item -LiteralPath $stagingItem.FullName -Recurse -Force
    }
    throw
}

param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('qwen', 'indextts25', 'cosyvoice3')]
    [string]$Runtime,
    [Parameter(Mandatory = $true)]
    [string]$OfficialCacheRoot,
    [switch]$Download
)

$ErrorActionPreference = 'Stop'

if (-not [System.IO.Path]::IsPathRooted($OfficialCacheRoot)) {
    throw 'OfficialCacheRoot must be absolute'
}
$cache = Get-Item -LiteralPath ([System.IO.Path]::GetFullPath($OfficialCacheRoot)) -Force -ErrorAction Stop
if (-not $cache.PSIsContainer -or
    ($cache.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
    throw 'OfficialCacheRoot must be a real directory'
}
$current = $cache
while ($null -ne $current) {
    if ($current.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
        throw 'OfficialCacheRoot has a reparse-point ancestor'
    }
    $current = $current.Parent
}

$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runtimeDirectory = Join-Path $sourceRoot 'plugins/voice-clone/runtime'
$lockPath = Join-Path $runtimeDirectory "$Runtime.lock.json"
$lock = Get-Content -Raw -LiteralPath $lockPath | ConvertFrom-Json
if ($lock.schemaVersion -ne 1 -or $lock.ready -ne $true -or $lock.unresolved.Count -ne 0) {
    throw 'Runtime lock is not ready'
}

function Resolve-CachePath([string]$Relative, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($Relative) -or [System.IO.Path]::IsPathRooted($Relative) -or
        $Relative.Contains(':') -or $Relative.Replace('\','/').Split('/') -contains '..') {
        throw "$Label cache path is unsafe"
    }
    $candidate = [System.IO.Path]::GetFullPath((Join-Path $cache.FullName $Relative))
    $prefix = $cache.FullName.TrimEnd('\') + '\'
    if (-not $candidate.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label cache path escaped the cache root"
    }
    return $candidate
}

function Test-LockedFile([object]$Descriptor, [string]$Label) {
    $path = Resolve-CachePath $Descriptor.cachePath $Label
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $false }
    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -eq
           $Descriptor.sha256
}

function Fetch-LockedFile([object]$Descriptor, [string]$Label) {
    if ($Descriptor.url -notmatch '^https://') { throw "$Label URL is not HTTPS" }
    $path = Resolve-CachePath $Descriptor.cachePath $Label
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        $partial = "$path.part"
        if (Test-Path -LiteralPath $partial) { throw "$Label has a stale .part file" }
        Invoke-WebRequest -UseBasicParsing -Uri $Descriptor.url -OutFile $partial
        if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLowerInvariant() -ne
            $Descriptor.sha256) {
            throw "$Label download SHA-256 mismatch"
        }
        Move-Item -LiteralPath $partial -Destination $path
    }
}

function Test-LockedSource([object]$Descriptor, [string]$Label) {
    $path = Resolve-CachePath $Descriptor.cachePath $Label
    if (-not (Test-Path -LiteralPath $path -PathType Container)) { return $false }
    $origin = (& git -C $path remote get-url origin 2>$null | Out-String).Trim().TrimEnd('/').Replace('.git','')
    $expected = $Descriptor.repository.Trim().TrimEnd('/').Replace('.git','')
    if ($LASTEXITCODE -ne 0 -or $origin -ne $expected) { return $false }
    $revision = (& git -C $path rev-parse "$($Descriptor.revision)^{commit}" 2>$null | Out-String).Trim()
    $tree = (& git -C $path rev-parse "$($Descriptor.revision)^{tree}" 2>$null | Out-String).Trim()
    return $LASTEXITCODE -eq 0 -and $revision -eq $Descriptor.revision -and $tree -eq $Descriptor.tree
}

function Fetch-LockedSource([object]$Descriptor, [string]$Label) {
    if ($Descriptor.repository -notmatch '^https://github\.com/' -or
        $Descriptor.revision -notmatch '^[0-9a-f]{40}$') { throw "$Label source lock is invalid" }
    $path = Resolve-CachePath $Descriptor.cachePath $Label
    if (Test-Path -LiteralPath $path) { throw "$Label cache exists but is not the locked source" }
    New-Item -ItemType Directory -Path $path -Force | Out-Null
    & git -C $path init --quiet
    & git -C $path remote add origin $Descriptor.repository
    & git -C $path fetch --quiet --depth=1 origin $Descriptor.revision
    & git -C $path checkout --quiet --detach $Descriptor.revision
    if ($LASTEXITCODE -ne 0 -or -not (Test-LockedSource $Descriptor $Label)) {
        throw "$Label could not be fetched at its locked commit/tree"
    }
}

function Test-CompatibleWheel([System.IO.FileInfo]$Wheel, [string]$Name,
                              [string]$Version, [string]$PythonVersion) {
    $parts = $Wheel.BaseName.Split('-')
    if ($parts.Count -lt 5) { return $false }
    $pythonParts = $PythonVersion.Split('.')
    $pythonTag = "$($pythonParts[0])$($pythonParts[1])"
    $minor = [int]$pythonParts[1]
    $pyTags = $parts[$parts.Count - 3].Split('.')
    $abiTags = $parts[$parts.Count - 2].Split('.')
    $platformTags = $parts[$parts.Count - 1].Split('.')
    $pythonCompatible = @($pyTags | Where-Object {
        $_ -eq 'py3' -or $_ -eq "py$pythonTag" -or $_ -eq "cp$pythonTag" -or
        (($_ -match '^cp3(\d+)$') -and $abiTags -contains 'abi3' -and
         [int]$Matches[1] -le $minor)
    }).Count -gt 0
    $abiCompatible = @($abiTags | Where-Object {
        $_ -eq 'none' -or $_ -eq 'abi3' -or $_ -eq "cp$pythonTag"
    }).Count -gt 0
    $platformCompatible = @($platformTags | Where-Object {
        $_ -eq 'any' -or $_ -eq 'win_amd64'
    }).Count -gt 0
    $distribution = ($parts[0].ToLowerInvariant() -replace '[-_.]+', '-')
    $expected = ($Name.ToLowerInvariant() -replace '[-_.]+', '-')
    return $distribution -eq $expected -and $parts[1].Replace('_','-') -eq $Version -and
           $pythonCompatible -and $abiCompatible -and $platformCompatible
}

$missing = [System.Collections.Generic.List[string]]::new()
foreach ($artifact in @(
    [pscustomobject]@{ descriptor = $lock.python.archive; label = 'python-archive' },
    [pscustomobject]@{ descriptor = $lock.resolver.archive; label = 'resolver-archive' })) {
    if (-not (Test-LockedFile $artifact.descriptor $artifact.label)) {
        if ($Download) { Fetch-LockedFile $artifact.descriptor $artifact.label }
        if (-not (Test-LockedFile $artifact.descriptor $artifact.label)) {
            $missing.Add("$($artifact.label):$($artifact.descriptor.cachePath)")
        }
    }
}

$additionalFiles = @()
$overlayIndex = 0
foreach ($overlay in @($lock.sourceOverlays | Where-Object { $null -ne $_ })) {
    $additionalFiles += [pscustomobject]@{
        descriptor = $overlay.archive
        label = "source-overlay-$overlayIndex-$($overlay.name)"
    }
    $overlayIndex++
}
$nativeIndex = 0
foreach ($nativeTool in @($lock.nativeTools | Where-Object { $null -ne $_ })) {
    $additionalFiles += [pscustomobject]@{
        descriptor = $nativeTool.archive
        label = "native-tool-$nativeIndex-$($nativeTool.name)"
    }
    $additionalFiles += [pscustomobject]@{
        descriptor = $nativeTool.correspondingSource
        label = "corresponding-source-$nativeIndex-$($nativeTool.name)"
    }
    $nativeIndex++
}
$licenseIndex = 0
foreach ($supplement in @($lock.dependencyLicenseFiles | Where-Object { $null -ne $_ })) {
    $fileIndex = 0
    foreach ($licenseFile in @($supplement.files | Where-Object { $null -ne $_ })) {
        $additionalFiles += [pscustomobject]@{
            descriptor = $licenseFile
            label = "dependency-license-$licenseIndex-$fileIndex-$($supplement.name)"
        }
        $fileIndex++
    }
    $licenseIndex++
}
foreach ($artifact in $additionalFiles) {
    if (-not (Test-LockedFile $artifact.descriptor $artifact.label)) {
        if ($Download) { Fetch-LockedFile $artifact.descriptor $artifact.label }
        if (-not (Test-LockedFile $artifact.descriptor $artifact.label)) {
            $missing.Add("$($artifact.label):$($artifact.descriptor.cachePath)")
        }
    }
}

$sources = @([pscustomobject]@{ descriptor = $lock.source; label = 'source' })
$index = 0
foreach ($submodule in @($lock.source.submodules | Where-Object { $null -ne $_ })) {
    $sources += [pscustomobject]@{ descriptor = $submodule; label = "submodule-$index" }
    $index++
}
foreach ($source in $sources) {
    if (-not (Test-LockedSource $source.descriptor $source.label)) {
        if ($Download) { Fetch-LockedSource $source.descriptor $source.label }
        if (-not (Test-LockedSource $source.descriptor $source.label)) {
            $missing.Add("$($source.label):$($source.descriptor.cachePath)@$($source.descriptor.revision)")
        }
    }
}

$requirementsPath = Join-Path $runtimeDirectory $lock.requirements.path
$requirementsText = Get-Content -Raw -LiteralPath $requirementsPath
$blocks = [regex]::Matches($requirementsText, '(?ms)^([A-Za-z0-9_.-]+)==([^\s\\]+)\s+\\(.*?)(?=^[A-Za-z0-9_.-]+==|\z)')
$wheelhouse = Resolve-CachePath 'wheelhouse' 'wheelhouse'
$wheels = if (Test-Path -LiteralPath $wheelhouse -PathType Container) {
    @(Get-ChildItem -LiteralPath $wheelhouse -Filter '*.whl' -File)
} else { @() }
foreach ($block in $blocks) {
    $name = $block.Groups[1].Value
    $version = $block.Groups[2].Value
    $sourceOverlay = @($lock.sourceOverlays | Where-Object {
        $_.name -eq $name -and $_.version -eq $version
    })
    if ($sourceOverlay.Count -gt 0) {
        if ($sourceOverlay.Count -ne 1 -or
            $block.Value -notmatch [regex]::Escape("--hash=sha256:$($sourceOverlay[0].archive.sha256)")) {
            $missing.Add("source-overlay:$name==$version")
        }
        continue
    }
    $candidates = @($wheels | Where-Object {
        Test-CompatibleWheel $_ $name $version $lock.python.version
    })
    $trusted = @($candidates | Where-Object {
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $block.Value -match [regex]::Escape("--hash=sha256:$hash")
    })
    if ($trusted.Count -ne 1) { $missing.Add("wheel:$name==$version") }
}

if ($missing.Count -gt 0) {
    Write-Output "RUNTIME_CACHE_INCOMPLETE:$($lock.runtimeId)"
    foreach ($item in $missing) { Write-Output "MISSING $item" }
    exit 2
}
Write-Output "RUNTIME_CACHE_READY:$($lock.runtimeId)"

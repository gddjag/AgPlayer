# tools/memory_probe.ps1
# Starts the AgPlayer app with --qa-play on the supplied audio file, waits for
# the working set to stabilise, and reports the stable working set in MB.
# Exits nonzero when the stable working set is >= 60 MB.
param([Parameter(Mandatory)][string]$Executable,
      [Parameter(Mandatory)][string]$AudioFile)
$process = Start-Process -PassThru -FilePath $Executable -ArgumentList @('--qa-play', $AudioFile)
try {
  Start-Sleep -Seconds 15
  $process.Refresh()
  $mb = [math]::Round($process.WorkingSet64 / 1MB, 2)
  [pscustomobject]@{ StableWorkingSetMB = $mb; LimitMB = 60; Passed = ($mb -lt 60) } |
    ConvertTo-Json
  if($mb -ge 60){ exit 1 }
} finally {
  if(!$process.HasExited){ $process.CloseMainWindow() | Out-Null; $process.WaitForExit(5000) | Out-Null }
}

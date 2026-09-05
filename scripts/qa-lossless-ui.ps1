param(
    [string]$BuildDirectory='build/release',
    [string]$OutputDirectory='build/qa/lossless-ui',
    [string]$InputDirectory='',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name='empty',
    [int]$Width=1672,
    [int]$Height=941,
    [ValidateSet('1','1.25','1.5','2')][string]$Scale='1',
    [ValidateSet('zh','en')][string]$Language='zh',
    [switch]$Running,
    [switch]$Idle
)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot=[IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
$outRoot=[IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDirectory))
[IO.Directory]::CreateDirectory($outRoot) | Out-Null
$image=Join-Path $outRoot "$Name.png"
$log=Join-Path $outRoot "$Name.log"
Remove-Item -LiteralPath $image,$log -Force -ErrorAction SilentlyContinue
$appArguments=@('--qa-test-mode','--qa-instance-key',"lossless-$Name",'--qa-tool','5',
    '--qa-theme','dark','--qa-language',$Language,'--qa-tools-size',"$Width","$Height",
    '--qa-screenshot-tools',$image,'--qa-log',$log,'--qa-exit-after-ms','180000')
if($InputDirectory){$appArguments+=@('--qa-import-folder',[IO.Path]::GetFullPath($InputDirectory))}
if($Running){$appArguments+='--qa-lossless-running'}
if($Idle){$appArguments+='--qa-lossless-idle'}
$previousPath=$env:PATH;$previousScale=$env:QT_SCALE_FACTOR
$cache=Get-Content -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt')
$debugBuild=($cache -match '^CMAKE_BUILD_TYPE:STRING=Debug$').Count -gt 0
$dependencyBin=if($debugBuild){'debug/bin'}else{'bin'}
$env:PATH="D:/Qt/6.7.0/msvc2019_64/bin;D:/ai/AgPlayer/build/release/vcpkg_installed/x64-windows/$dependencyBin;"+$env:PATH
$env:QT_SCALE_FACTOR=$Scale
try {
    # Arguments contain paths only; quote for Windows CreateProcess, never a shell.
    $quoted=$appArguments | ForEach-Object {'"'+$_.Replace('"','\"')+'"'}
    $process=Start-Process -FilePath (Join-Path $buildRoot 'app/AgPlayer.exe') -ArgumentList $quoted -WindowStyle Hidden -PassThru
    $timer=[Diagnostics.Stopwatch]::StartNew();$peak=0L;$cpu=0.0
    $idleCpuStart=$null;$idleTimeStart=0L;$idleCpuEnd=$null;$idleTimeEnd=0L
    while(!$process.WaitForExit(100)) {
        $process.Refresh();$peak=[Math]::Max($peak,$process.PrivateMemorySize64);$cpu=$process.TotalProcessorTime.TotalMilliseconds
        if($Idle -and $timer.ElapsedMilliseconds -gt 4000 -and $null -eq $idleCpuStart){$idleCpuStart=$cpu;$idleTimeStart=$timer.ElapsedMilliseconds}
        # QA captures at 12 seconds. Measure static time before PNG encoding/exit.
        if($Idle -and $timer.ElapsedMilliseconds -gt 10000 -and $null -eq $idleCpuEnd){$idleCpuEnd=$cpu;$idleTimeEnd=$timer.ElapsedMilliseconds}
        if($timer.Elapsed.TotalSeconds -gt 190){$process.Kill();throw 'Lossless UI QA process timed out'}
    }
    $process.Refresh()
    $metrics=[ordered]@{name=$Name;logicalWidth=$Width;logicalHeight=$Height;scale=$Scale;
        input=$InputDirectory;running=[bool]$Running;exitCode=$process.ExitCode;elapsedMs=$timer.ElapsedMilliseconds;
        sampledPeakPrivateBytes=$peak;sampledCpuMs=$cpu;screenshot=$image}
    if($null -ne $idleCpuStart -and $null -ne $idleCpuEnd){
        $metrics.idleCpuMs=$idleCpuEnd-$idleCpuStart
        $metrics.idleMeasuredMs=$idleTimeEnd-$idleTimeStart
        $metrics.idleOneCorePercent=100*$metrics.idleCpuMs/$metrics.idleMeasuredMs
    }
    $metrics | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outRoot "$Name.metrics.json") -Encoding utf8
    if($process.ExitCode -ne 0){throw "QA exit code $($process.ExitCode); inspect $log"}
    if(!(Test-Path -LiteralPath $image)){throw "No screenshot produced; inspect $log"}
    $metrics
} finally {$env:PATH=$previousPath;$env:QT_SCALE_FACTOR=$previousScale}

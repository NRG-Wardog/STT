param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [Parameter(Mandatory=$true)][string]$Model,
    [Parameter(Mandatory=$true)][string]$Audio,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [int]$Repeats=3,
    [switch]$Gpu,
    [switch]$HoldAfterFree,
    [switch]$ResetBeforeHold,
    [switch]$TestReload,
    [switch]$TestUnload
)
$ErrorActionPreference='Stop'
$exePath=(Resolve-Path -LiteralPath $Exe).Path
$modelPath=(Resolve-Path -LiteralPath $Model).Path
$audioPath=(Resolve-Path -LiteralPath $Audio).Path
$out=[IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $out) { throw "Refusing to overwrite $out" }
New-Item -ItemType Directory -Path $out | Out-Null
$argsList=@('"'+$modelPath+'"','"'+$audioPath+'"',[string]$Repeats)
if(($HoldAfterFree -or $ResetBeforeHold -or $TestReload -or $TestUnload) -and -not $Gpu) { throw 'Hold/reset/reload tests require -Gpu.' }
if($Gpu) { $argsList+= $(if($TestUnload){'gpu-unload-reload-hold'}elseif($TestReload){'gpu-reset-reload-hold'}elseif($ResetBeforeHold){'gpu-reset-hold'}elseif($HoldAfterFree){'gpu-hold'}else{'gpu'}) }
$p=Start-Process -FilePath $exePath -ArgumentList $argsList -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $out 'inference.jsonl') -RedirectStandardError (Join-Path $out 'backend.log')
$null = $p.Handle  # Keep a handle so exit code remains available after fast jobs exit.
$rows=[Collections.Generic.List[object]]::new()
$processRows=[Collections.Generic.List[object]]::new()
while(-not $p.HasExited) {
    $utc=[DateTime]::UtcNow.ToString('o')
    $p.Refresh()
    if (-not $p.HasExited) {
        $processRows.Add([pscustomobject]@{utc=$utc;pid=$p.Id;cpu_seconds=$p.TotalProcessorTime.TotalSeconds;working_set_bytes=$p.WorkingSet64;private_bytes=$p.PrivateMemorySize64;handles=$p.HandleCount;threads=$p.Threads.Count})
    }
    foreach($counter in @('\GPU Engine(*)\Utilization Percentage','\GPU Process Memory(*)\Dedicated Usage','\GPU Process Memory(*)\Shared Usage')) {
      try {
        $set=Get-Counter -Counter $counter -ErrorAction SilentlyContinue
        foreach($s in $set.CounterSamples) {
            if($s.InstanceName -match ('(^|_)pid_'+$p.Id+'(_|$)')) {
                $rows.Add([pscustomobject]@{utc=$utc;pid=$p.Id;path=$s.Path;instance=$s.InstanceName;value=$s.CookedValue;status=$s.Status})
            }
        }
      } catch { $rows.Add([pscustomobject]@{utc=$utc;pid=$p.Id;path='COUNTER_ERROR';instance=$counter;value='';status=$_.Exception.Message}) }
    }
    $p.Refresh()
}
if($rows.Count) { $rows | Export-Csv -LiteralPath (Join-Path $out 'gpu-process-counters.csv') -NoTypeInformation -Encoding UTF8 }
else { 'No matching GPU process counters observed; this is not a measurement of zero.' | Set-Content (Join-Path $out 'gpu-counter-status.txt') }
if($processRows.Count) { $processRows | Export-Csv -LiteralPath (Join-Path $out 'process-counters.csv') -NoTypeInformation -Encoding UTF8 }
$p.WaitForExit()
[pscustomobject]@{pid=$p.Id;exit_code=$p.ExitCode;gpu_requested=[bool]$Gpu;counter_rows=$rows.Count;exe=$exePath;model=$modelPath;audio=$audioPath;note='Read backend.log to verify actual backend. GPU counters are per process, sampled approximately once per Get-Counter call; very short jobs can be missed.'} | ConvertTo-Json | Set-Content (Join-Path $out 'metadata.json') -Encoding UTF8
if($p.ExitCode -ne 0) { throw "Inference failed with exit code $($p.ExitCode); see $out" }

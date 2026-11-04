param([string]$Package='build\recovery-candidate')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$packagePath=(Resolve-Path -LiteralPath (Join-Path $root $Package)).Path
$exe=Join-Path $packagePath 'HebENDictation.exe'
if(-not (Test-Path -LiteralPath $exe)){throw 'Recovery executable not found.'}
if(Get-Process HebENDictation -ErrorAction SilentlyContinue){throw 'HebEN is already running. Exit it from the tray before starting the diagnostic build; no process was stopped.'}
$evidence=Join-Path $root 'build\evidence\supervised-live'
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
$trace=Join-Path $evidence ('trace-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')+'-'+[Guid]::NewGuid().ToString('N')+'.jsonl')
$old=$env:HEBEN_DIAGNOSTICS_PATH
try {
    $env:HEBEN_DIAGNOSTICS_PATH=$trace
    $p=Start-Process -FilePath $exe -WorkingDirectory $packagePath -WindowStyle Hidden -PassThru
} finally { $env:HEBEN_DIAGNOSTICS_PATH=$old }
[pscustomobject]@{Pid=$p.Id;Trace=$trace;Note='Metadata and stage outcomes only: no microphone audio or transcription text is written to this trace.'} | Format-List
Write-Host 'Focus a dedicated blank test field, press Ctrl+Shift+R, speak a short sentence, pause, and press the shortcut again to stop.'
Write-Host 'For ChatGPT, use the draft input only and do not submit; record whether text appears.'

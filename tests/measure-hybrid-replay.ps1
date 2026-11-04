param(
    [Parameter(Mandatory=$true)][string]$FixtureExe,
    [Parameter(Mandatory=$true)][string]$Audio,
    [Parameter(Mandatory=$true)][string]$Reference,
    [Parameter(Mandatory=$true)][string]$Model,
    [string]$HebrewModel,
    [Parameter(Mandatory=$true)][string]$Vad,
    [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference='Stop'
$outPath=[IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $outPath) { throw 'Output already exists' }
$script=Join-Path $PSScriptRoot 'replay-pipeline.py'
function Quoted([string]$path) { return '"'+[IO.Path]::GetFullPath($path)+'"' }
$argsList=@((Quoted $script),'--exe',(Quoted $FixtureExe),'--wav',(Quoted $Audio),'--reference-json',(Quoted $Reference),
    '--model',(Quoted $Model),'--vad',(Quoted $Vad),'--backend','cuda',
    '--out',(Quoted $outPath),'--observe-idle-seconds','18')
if ($HebrewModel) { $argsList+=@('--hebrew-model',(Quoted $HebrewModel)) }
$python=(Get-Command python -ErrorAction Stop).Source
$runner=Start-Process -FilePath $python -ArgumentList $argsList -WindowStyle Hidden -PassThru -RedirectStandardOutput ($outPath+'.stdout.txt') -RedirectStandardError ($outPath+'.stderr.txt')
$null=$runner.Handle # Retain an exit handle even if the child finishes before polling.
$processFile=Join-Path $outPath 'fixture-process.json'
$deadline=[DateTime]::UtcNow.AddSeconds(15)
while (-not (Test-Path -LiteralPath $processFile) -and -not $runner.HasExited -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 50 }
if (-not (Test-Path -LiteralPath $processFile)) { throw 'Fixture process identity did not appear; see runner logs' }
& (Join-Path $PSScriptRoot 'sample-process-gpu.ps1') -FixtureProcessJson $processFile -Output ($outPath+'.gpu.csv') -Seconds 22
$runner.WaitForExit()
if ($runner.ExitCode -ne 0) { throw "Replay failed with $($runner.ExitCode); see $outPath" }
Get-Content -LiteralPath (Join-Path $outPath 'result.json') -Raw -Encoding UTF8

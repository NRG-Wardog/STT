param([Parameter(Mandatory=$true)][string]$Output,[switch]$SkipWarm,[switch]$WarmOnVoice)
$ErrorActionPreference='Stop'
$out=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $out){throw 'Refusing to overwrite evidence'}
$script=(Resolve-Path 'tests/replay-pipeline.py').Path
$exe=(Resolve-Path 'build/cuda-ninja/HebENReplayFixture.exe').Path
$wav=(Resolve-Path 'build/evidence/streaming-attribution-baseline/silence-7s.wav').Path
$model=(Resolve-Path 'dist/models/ggml-small-q5_1.bin').Path
$hebrew=(Resolve-Path 'build/models/ivrit-whisper-large-v3-turbo-q5_0.bin').Path
$vad=(Resolve-Path 'dist/models/ggml-silero-v6.2.0.bin').Path
$runtime=(Resolve-Path 'build/recovery-input-live-v2').Path
$python=(Get-Command python -ErrorAction Stop).Source
function Q([string]$s){return '"'+$s+'"'}
$arguments=@((Q $script),'--exe',(Q $exe),'--wav',(Q $wav),'--model',(Q $model),'--hebrew-model',(Q $hebrew),
    '--vad',(Q $vad),'--out',(Q $out),'--backend','cuda','--language','auto','--pace-ms','32','--auto-silence',
    '--streaming-prototype','--streaming-agreement','2','--streaming-hold-words','1','--model-idle-seconds','10',
    '--startup-wait-seconds','3','--observe-idle-seconds','16')
if($SkipWarm){$arguments+='--skip-model-warm-ablation'}
if($WarmOnVoice){$arguments+='--warm-on-voice'}
$oldPath=$env:PATH
try {
    $env:PATH=$runtime+';'+$env:PATH
    $runner=Start-Process -FilePath $python -ArgumentList $arguments -WindowStyle Hidden -PassThru `
      -RedirectStandardOutput ($out+'.stdout.txt') -RedirectStandardError ($out+'.stderr.txt')
    $null=$runner.Handle
} finally {$env:PATH=$oldPath}
$processFile=Join-Path $out 'fixture-process.json';$until=[DateTime]::UtcNow.AddSeconds(20)
while(-not (Test-Path -LiteralPath $processFile) -and -not $runner.HasExited -and [DateTime]::UtcNow -lt $until){Start-Sleep -Milliseconds 50}
if(-not (Test-Path -LiteralPath $processFile)){throw 'Fixture did not publish its process identity'}
& 'tests/sample-process-gpu.ps1' -FixtureProcessJson $processFile -Output ($out+'.gpu.csv') -Seconds 28
$runner.WaitForExit()
if($runner.ExitCode -ne 0){throw "Fixture failed with exit code $($runner.ExitCode); inspect $out"}
Get-Content -LiteralPath (Join-Path $out 'result.json') -Encoding UTF8 -Raw

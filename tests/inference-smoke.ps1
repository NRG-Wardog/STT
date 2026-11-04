param(
    [Parameter(Mandatory=$true)][string]$BenchExe,
    [Parameter(Mandatory=$true)][string]$Model,
    [Parameter(Mandatory=$true)][string]$EnglishWav,
    [Parameter(Mandatory=$true)][string]$SilenceWav,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference='Stop'
$exe=(Resolve-Path -LiteralPath $BenchExe).Path
$modelPath=(Resolve-Path -LiteralPath $Model).Path
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $out) { throw "Refusing to overwrite $out" }
New-Item -ItemType Directory -Path $out | Out-Null
function Run-Case([string]$name,[string]$audio,[string]$mode) {
    $wave=(Resolve-Path -LiteralPath $audio).Path
    $argsList=@('"'+$modelPath+'"','"'+$wave+'"','1')
    if($mode) { $argsList+=$mode }
    $p=Start-Process -FilePath $exe -ArgumentList $argsList -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $out "$name.jsonl") -RedirectStandardError (Join-Path $out "$name.stderr.txt")
    if($p.ExitCode -ne 0) { throw "$name failed with exit code $($p.ExitCode)" }
    return [IO.File]::ReadAllText((Join-Path $out "$name.jsonl"),[Text.Encoding]::UTF8) | ConvertFrom-Json
}
$speech=Run-Case 'english' $EnglishWav ''
$silent=Run-Case 'silence' $SilenceWav 'vad:0.013:1200:0.35'
$results=[pscustomobject]@{speech_contains_hello_world=($speech.text -match '(?i)hello\s+world');speech_contains_dictation=($speech.text -match '(?i)dictation');silence_no_phrases=($silent.phrases -eq 0);silence_no_text=([string]::IsNullOrWhiteSpace($silent.text));note='Synthetic voice and generated silence only; not a microphone or full UI test.'}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'summary.json') -Encoding UTF8
if(-not ($results.speech_contains_hello_world -and $results.speech_contains_dictation -and $results.silence_no_phrases -and $results.silence_no_text)) { throw "Smoke test failed; see $out" }
Write-Host 'Offline inference smoke test passed.'

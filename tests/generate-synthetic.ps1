param([string]$Output = 'build\synthetic', [string]$Corpus = (Join-Path $PSScriptRoot 'synthetic-corpus.json'))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
[void][Windows.Media.SpeechSynthesis.SpeechSynthesizer, Windows.Media.SpeechSynthesis, ContentType=WindowsRuntime]
[void][Windows.Storage.Streams.DataReader, Windows.Storage.Streams, ContentType=WindowsRuntime]
$asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.IsGenericMethodDefinition -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
} | Select-Object -First 1
function Await-Operation($operation, [Type]$type) {
    $task = $asTask.MakeGenericMethod($type).Invoke($null, @($operation))
    $task.Wait()
    return $task.Result
}
$directory = [IO.Path]::GetFullPath($Output)
[IO.Directory]::CreateDirectory($directory) | Out-Null
$samples = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $Corpus).Path, [Text.Encoding]::UTF8) | ConvertFrom-Json
foreach ($sample in $samples) {
    $synth = [Windows.Media.SpeechSynthesis.SpeechSynthesizer]::new()
    $synth.Voice = [Windows.Media.SpeechSynthesis.SpeechSynthesizer]::AllVoices | Where-Object { $_.Language -eq $sample.voice } | Select-Object -First 1
    if (-not $synth.Voice) { throw "Voice $($sample.voice) unavailable" }
    $stream = Await-Operation ($synth.SynthesizeTextToStreamAsync($sample.text)) ([Windows.Media.SpeechSynthesis.SpeechSynthesisStream])
    $reader = [Windows.Storage.Streams.DataReader]::new($stream.GetInputStreamAt(0))
    $count = Await-Operation ($reader.LoadAsync([uint32]$stream.Size)) ([uint32])
    $bytes = [byte[]]::new($count)
    $reader.ReadBytes($bytes)
    [IO.File]::WriteAllBytes((Join-Path $directory ($sample.id + '-original.wav')), $bytes)
    $reader.Dispose(); $stream.Dispose(); $synth.Dispose()
    Write-Host "Generated $($sample.id): $count bytes"
}
Write-Host 'Synthetic audio is an engineering fixture, not a verified human speech corpus. Check WAV format before benchmarking.'

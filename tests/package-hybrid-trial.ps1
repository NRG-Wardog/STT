param(
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$Exe='build\cuda-ninja\HebENDictation.exe',
    [string]$PrimaryModel='dist\models\ggml-small-q5_1.bin',
    [string]$HebrewModel='build\models\ivrit-whisper-large-v3-turbo-q5_0.bin',
    [string]$VadModel='dist\models\ggml-silero-v6.2.0.bin',
    [string]$CudaRoot='C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1',
    [ValidateRange(0,20)][int]$UnicodeDelayMs=0,
    [switch]$TrustedTerminal,
    [switch]$StreamingPrototype,
    [ValidateRange(2,8)][int]$StreamingAgreement=6,
    [ValidateRange(1,5)][int]$StreamingHoldWords=1
)
$ErrorActionPreference='Stop'
$dest=[IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $dest) { throw 'Refusing to overwrite an existing package.' }
$required=@($Exe,$PrimaryModel,$HebrewModel,$VadModel,
    (Join-Path $CudaRoot 'bin\x64\cublas64_13.dll'),(Join-Path $CudaRoot 'bin\x64\cublasLt64_13.dll'),(Join-Path $CudaRoot 'EULA.txt'),
    'licenses\LICENSE-Ivrit-Model-Apache-2.0.txt','dist\LICENSE-HebEN.txt','dist\LICENSE-miniaudio.txt',
    'dist\LICENSE-OpenAI-Whisper.txt','dist\LICENSE-Silero-VAD.txt','dist\LICENSE-whisper.cpp.txt')
foreach ($file in $required) { if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing required file: $file" } }
New-Item -ItemType Directory -Path $dest | Out-Null
New-Item -ItemType Directory -Path (Join-Path $dest 'models') | Out-Null
Copy-Item -LiteralPath $Exe -Destination (Join-Path $dest 'HebENDictation.exe')
Copy-Item -LiteralPath $PrimaryModel -Destination (Join-Path $dest 'models\ggml-small-q5_1.bin')
Copy-Item -LiteralPath $HebrewModel -Destination (Join-Path $dest 'models\ivrit-whisper-large-v3-turbo-q5_0.bin')
Copy-Item -LiteralPath $VadModel -Destination (Join-Path $dest 'models\ggml-silero-v6.2.0.bin')
Copy-Item -LiteralPath (Join-Path $CudaRoot 'bin\x64\cublas64_13.dll') -Destination $dest
Copy-Item -LiteralPath (Join-Path $CudaRoot 'bin\x64\cublasLt64_13.dll') -Destination $dest
Copy-Item -LiteralPath (Join-Path $CudaRoot 'EULA.txt') -Destination (Join-Path $dest 'LICENSE-NVIDIA-CUDA.txt')
foreach ($file in @('LICENSE-HebEN.txt','LICENSE-miniaudio.txt','LICENSE-OpenAI-Whisper.txt','LICENSE-Silero-VAD.txt','LICENSE-whisper.cpp.txt')) {
    Copy-Item -LiteralPath (Join-Path 'dist' $file) -Destination $dest
}
Copy-Item -LiteralPath 'licenses\LICENSE-Ivrit-Model-Apache-2.0.txt' -Destination $dest
@'
[General]
hotkey=CTRL+SHIFT+R
language=auto
backend=cuda
model=models\ggml-small-q5_1.bin
hybrid=1
hebrew_model=models\ivrit-whisper-large-v3-turbo-q5_0.bin
auto_hebrew_fallback=0
vad_backend=silero
vad_model=models\ggml-silero-v6.2.0.bin
silence_ms=1200
preroll_ms=600
max_phrase_seconds=15
threads=0
model_idle_seconds=10
'@ | Set-Content -LiteralPath (Join-Path $dest 'config.ini') -Encoding ASCII
@(('unicode_delay_ms='+$UnicodeDelayMs),('trusted_terminal_input='+[int][bool]$TrustedTerminal),'input_method=unicode',
  ('streaming_prototype='+[int][bool]$StreamingPrototype),'streaming_interval_ms=1000',
  ('streaming_agreement='+$StreamingAgreement),('streaming_hold_words='+$StreamingHoldWords)) |
    Add-Content -LiteralPath (Join-Path $dest 'config.ini') -Encoding ASCII
@'
HebEN Dictation - experimental GPU trial

This candidate is NOT production-ready. Live microphone-to-editor operation,
ChatGPT, Chrome and VS Code have not been verified with this package. Mixed
Hebrew/English speech may lose words. Check the output before relying on it.

Requires a compatible NVIDIA GPU and driver. Native cuBLAS libraries are
included; no Python or CUDA toolkit installation is needed for the program.
It processes speech locally and does not upload recordings or transcripts.

Run HebENDictation.exe and focus a normal editable field. Press and release
Ctrl+Shift+R to start; press it again to stop, or wait five seconds of silence.
The dot appears only when the caret location can be determined exactly. The
microphone stops if the foreground window changes. Right-click the tray icon
and choose Keyboard shortcut... to type or record a shortcut. Save checks
common reserved combinations and global registration conflicts. App-specific
local shortcuts can still conflict. The previous shortcut remains active if
the new one cannot be saved. The tray also offers language selection and
editing config.ini. Do not use the tool in password
fields or elevated applications. Windows Terminal input requires explicit
trusted mode; the app cannot determine whether an unknown prompt is sensitive.
Some VS Code terminal surfaces may appear as ordinary editable controls, so
the app cannot always identify a terminal or an undisclosed password prompt.
Do not enable clipboard paste: real preservation tests have failed. The
clipboard option remains experimental and is not safe to rely on.

The two-model profile prioritizes Hebrew after a first language decision. It
can misclassify short phrases, and mixed-language accuracy is not established.
If streaming_prototype=1, repeated hypotheses are experimental; later results
can contradict already typed words. Check all text. The full final result is
retained for manual review on conflict. Real microphone streaming has not been
validated. Set streaming_prototype=0 to use the baseline utterance path.
The Hebrew model is ivrit-ai/whisper-large-v3-turbo, quantized locally to Q5_0:
https://huggingface.co/ivrit-ai/whisper-large-v3-turbo-ggml
The bundled NVIDIA libraries are subject to LICENSE-NVIDIA-CUDA.txt.

This ZIP does not change Windows startup. If an earlier HebEN process is
running, exit it before starting this candidate. The original startup shortcut
continues to point to its previous installation.
'@ | Set-Content -LiteralPath (Join-Path $dest 'EXPERIMENTAL-QUICKSTART.txt') -Encoding ASCII
$base=$dest.TrimEnd('\')+'\'
Get-ChildItem -LiteralPath $dest -File -Recurse | Sort-Object FullName | ForEach-Object {
    $relative=$_.FullName.Substring($base.Length).Replace('\','/')
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(),$relative
} | Set-Content -LiteralPath (Join-Path $dest 'SHA256SUMS.txt') -Encoding ASCII
Get-ChildItem -LiteralPath $dest -File -Recurse | Measure-Object -Property Length -Sum | Select-Object Count,Sum

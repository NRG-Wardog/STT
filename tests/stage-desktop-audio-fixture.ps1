param([Parameter(Mandatory=$true)][string]$Output,[ValidateRange(2,8)][int]$StreamingAgreement=6,[ValidateRange(1,5)][int]$HoldWords=1,[switch]$PhysicalMicrophone,[string]$Hotkey='CTRL+ALT+SHIFT+F12')
$ErrorActionPreference='Stop'
$dest=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $dest){throw 'Refusing to overwrite fixture directory'}
$name=if($PhysicalMicrophone){'HebENDesktopMicFixture.exe'}else{'HebENDesktopAudioFixture.exe'}
$exe=(Resolve-Path (Join-Path 'build/cuda-ninja' $name)).Path
$model=(Resolve-Path 'dist/models/ggml-small-q5_1.bin').Path
$hebrew=(Resolve-Path 'build/models/ivrit-whisper-large-v3-turbo-q5_0.bin').Path
$vad=(Resolve-Path 'dist/models/ggml-silero-v6.2.0.bin').Path
New-Item -ItemType Directory -Path $dest | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $dest $name)
@('[General]',('hotkey='+$Hotkey),'backend=cuda','language=auto',('model='+$model),
  ('hebrew_model='+$hebrew),'hybrid=1',('vad_model='+$vad),'vad_backend=silero',
  'silence_ms=1200','preroll_ms=600','max_phrase_seconds=15','model_idle_seconds=10',
  'unicode_delay_ms=5','trusted_terminal_input=0','input_method=unicode',
  'streaming_prototype=1','streaming_interval_ms=1000',('streaming_agreement='+$StreamingAgreement),('streaming_hold_words='+$HoldWords)) |
  Set-Content -LiteralPath (Join-Path $dest 'config.ini') -Encoding ASCII
$hash=(Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $dest $name)).Hash.ToLowerInvariant()
$mode=if($PhysicalMicrophone){'test-only physical microphone, actual hotkey, VAD, ASR and Unicode editor input'}else{'test-only synthetic PCM; actual hotkey, VAD, ASR and Unicode editor input; no microphone'}
[pscustomobject]@{sha256=$hash;executable=$name;mode=$mode;hotkey=$Hotkey} |
  ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dest 'metadata.json') -Encoding UTF8
Write-Output "Staged test-only desktop audio fixture: $dest"

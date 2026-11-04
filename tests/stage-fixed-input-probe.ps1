param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$Hotkey='CTRL+ALT+SHIFT+F12',
    [ValidateRange(0,20)][int]$DelayMs=0,
    [switch]$TrustedTerminal,
    [switch]$ClipboardPaste
)
$ErrorActionPreference='Stop'
$source=(Resolve-Path -LiteralPath $Exe).Path
$dest=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $dest){throw 'Refusing to overwrite probe directory'}
$parts=$Hotkey.ToUpperInvariant().Split('+')
if($parts.Count -lt 2 -or @($parts[0..($parts.Count-2)] | Where-Object { $_ -notin @('CTRL','ALT','SHIFT','WIN') }).Count -ne 0 -or
   $parts[-1] -notmatch '^(?:[A-Z0-9]|F(?:[1-9]|1[0-2])|SPACE)$') { throw 'Use modifiers followed by an A-Z, 0-9, F1-F12 or SPACE key.' }
New-Item -ItemType Directory -Path $dest | Out-Null
Copy-Item -LiteralPath $source -Destination (Join-Path $dest 'HebENFixedInputProbe.exe')
$inputMethod=if($ClipboardPaste){'clipboard'}else{'unicode'}
@('[General]',('hotkey='+$Hotkey),('unicode_delay_ms='+$DelayMs),('trusted_terminal_input='+[int][bool]$TrustedTerminal),('input_method='+$inputMethod)) | Set-Content -LiteralPath (Join-Path $dest 'config.ini') -Encoding ASCII
$hash=(Get-FileHash -LiteralPath (Join-Path $dest 'HebENFixedInputProbe.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
[pscustomobject]@{exe=(Join-Path $dest 'HebENFixedInputProbe.exe');sha256=$hash;hotkey=$Hotkey;unicode_delay_ms=$DelayMs;trusted_terminal=[bool]$TrustedTerminal;input_method=$inputMethod;
    note='Test-only fixed-string probe; no microphone or ASR. It is not the installed app.'} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dest 'metadata.json') -Encoding UTF8
Write-Output "Staged fixed input probe: $dest"

param(
 [Parameter(Mandatory=$true)][string]$SourceRoot,
 [Parameter(Mandatory=$true)][string]$Exe,
 [Parameter(Mandatory=$true)][string]$DependencyRoot,
 [Parameter(Mandatory=$true)][string]$ModelDirectory,
 [Parameter(Mandatory=$true)][string]$Output,
 [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{40}$')][string]$Commit
)
$ErrorActionPreference='Stop'
$source=(Resolve-Path -LiteralPath $SourceRoot).Path
$exePath=(Resolve-Path -LiteralPath $Exe).Path
$deps=(Resolve-Path -LiteralPath $DependencyRoot).Path
$models=(Resolve-Path -LiteralPath $ModelDirectory).Path
$out=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $out){throw 'Refusing to overwrite an existing package.'}
$checks=@{
 'ggml-small-q5_1.bin'='ae85e4a935d7a567bd102fe55afc16bb595bdb618e11b2fc7591bc08120411bb'
 'ggml-silero-v6.2.0.bin'='2aa269b785eeb53a82983a20501ddf7c1d9c48e33ab63a41391ac6c9f7fb6987'
}
foreach($name in $checks.Keys){
 $file=Join-Path $models $name
 if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $checks[$name]){throw "Model hash mismatch: $name"}
}
New-Item -ItemType Directory -Path $out,(Join-Path $out 'models'),(Join-Path $out 'licenses'),(Join-Path $out 'docs'),(Join-Path $out 'docs/assets'),(Join-Path $out 'assets') | Out-Null
Copy-Item -LiteralPath $exePath -Destination (Join-Path $out 'HebENDictation.exe')
foreach($file in @('config.ini','README.md','README.he.md','LICENSE')){Copy-Item -LiteralPath (Join-Path $source $file) -Destination $out}
foreach($name in $checks.Keys){Copy-Item -LiteralPath (Join-Path $models $name) -Destination (Join-Path $out 'models')}
foreach($file in @('BENCHMARKS.md','benchmark-summary.json','LICENSING.md','DEVELOPMENT.md','RELEASE-NOTES.md','QUICKSTART.txt')){Copy-Item -LiteralPath (Join-Path $source ('docs/'+$file)) -Destination (Join-Path $out 'docs')}
Copy-Item -LiteralPath (Join-Path $source 'docs/assets/shortcut-settings.png') -Destination (Join-Path $out 'docs/assets')
Copy-Item -LiteralPath (Join-Path $source 'assets/heben-logo.png') -Destination (Join-Path $out 'assets')
Copy-Item -LiteralPath (Join-Path $source 'licenses/LICENSE-OpenAI-Whisper.txt') -Destination (Join-Path $out 'licenses')
Copy-Item -LiteralPath (Join-Path $source 'licenses/LICENSE-Silero-VAD.txt') -Destination (Join-Path $out 'licenses')
Copy-Item -LiteralPath (Join-Path $deps 'whispercpp-src/LICENSE') -Destination (Join-Path $out 'licenses/LICENSE-whisper.cpp.txt')
Copy-Item -LiteralPath (Join-Path $deps 'miniaudio-src/LICENSE') -Destination (Join-Path $out 'licenses/LICENSE-miniaudio.txt')
[IO.File]::WriteAllText((Join-Path $out 'SOURCE-COMMIT.txt'),$Commit+"`n",[Text.Encoding]::ASCII)
$prefix=$out.TrimEnd('\')+'\'
Get-ChildItem -LiteralPath $out -File -Recurse | Sort-Object FullName | ForEach-Object {
 $relative=$_.FullName.Substring($prefix.Length).Replace('\','/')
 '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(),$relative
} | Set-Content -LiteralPath (Join-Path $out 'SHA256SUMS.txt') -Encoding ASCII
[pscustomobject]@{directory=$out;source_commit=$Commit;files=(Get-ChildItem $out -Recurse -File).Count;bytes=(Get-ChildItem $out -Recurse -File | Measure-Object -Sum Length).Sum} | ConvertTo-Json

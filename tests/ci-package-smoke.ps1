param([Parameter(Mandatory=$true)][string]$Exe,[Parameter(Mandatory=$true)][string]$WorkDirectory)
$ErrorActionPreference='Stop'
$path=(Resolve-Path -LiteralPath $Exe).Path
$work=[IO.Path]::GetFullPath($WorkDirectory)
if(Test-Path -LiteralPath $work){throw 'Refusing to overwrite package-test directory.'}
New-Item -ItemType Directory -Path $work,(Join-Path $work 'contents') | Out-Null
Copy-Item -LiteralPath $path -Destination (Join-Path $work 'contents/HebENDictation.exe')
Copy-Item -LiteralPath 'config.ini','LICENSE' -Destination (Join-Path $work 'contents')
Compress-Archive -Path (Join-Path $work 'contents/*') -DestinationPath (Join-Path $work 'smoke.zip')
Expand-Archive -LiteralPath (Join-Path $work 'smoke.zip') -DestinationPath (Join-Path $work 'extracted')
$extracted=Join-Path $work 'extracted/HebENDictation.exe'
if((Get-FileHash $path -Algorithm SHA256).Hash -ne (Get-FileHash $extracted -Algorithm SHA256).Hash){throw 'Extracted executable hash mismatch.'}
& (Join-Path $PSScriptRoot 'check-icon-resource.ps1') -Exe $extracted
Write-Output 'PASS: model-free CI packaging smoke test. This is not a release package or live dictation test.'

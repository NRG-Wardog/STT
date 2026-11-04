param(
    [ValidateSet('tiny','base','small')][string]$Model = 'small',
    [switch]$SkipModelDownload
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root 'build'
$dist = Join-Path $root 'dist'

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($install) {
            $cmakeBin = Join-Path $install 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
            if (Test-Path -LiteralPath (Join-Path $cmakeBin 'cmake.exe')) { $env:PATH = "$cmakeBin;$env:PATH" }
        }
    }
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw 'CMake not found. Install Visual Studio 2022+ with Desktop development with C++ and CMake.'
    }
}
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw 'Git not found. Install Git for Windows first.'
}

Write-Host 'Configuring native Windows application and fetching pinned dependencies...'
cmake -S $root -B $build -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
cmake --build $build --config Release --target HebENDictation HebENDetectorTest HebENUnicodeInputTest HebENClipboardMockTest HebENStablePrefixTest HebENHotkeyPolicyTest HebENHotkeyRegistryTest --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'CMake build failed.' }
ctest --test-dir $build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Native unit tests failed.' }

New-Item -ItemType Directory -Path $dist -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $dist 'models') -Force | Out-Null
$exe = Join-Path $build 'Release\HebENDictation.exe'
if (-not (Test-Path $exe)) { throw "Expected EXE not found: $exe" }
try {
    Copy-Item -LiteralPath $exe -Destination (Join-Path $dist 'HebENDictation.exe') -Force
} catch {
    throw "Could not update the distribution executable. If HebEN is running from dist, exit it from the tray and rerun the build. The running process was not stopped. $($_.Exception.Message)"
}
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $dist 'LICENSE-HebEN.txt') -Force
Copy-Item -LiteralPath (Join-Path $root 'docs\QUICKSTART.txt') -Destination (Join-Path $dist 'QUICKSTART.txt') -Force
Copy-Item -LiteralPath (Join-Path $root 'licenses\LICENSE-OpenAI-Whisper.txt') -Destination (Join-Path $dist 'LICENSE-OpenAI-Whisper.txt') -Force
Copy-Item -LiteralPath (Join-Path $root 'licenses\LICENSE-Silero-VAD.txt') -Destination (Join-Path $dist 'LICENSE-Silero-VAD.txt') -Force
$whisperLicense = Join-Path $build '_deps\whispercpp-src\LICENSE'
$miniaudioLicense = Join-Path $build '_deps\miniaudio-src\LICENSE'
if (Test-Path -LiteralPath $whisperLicense) { Copy-Item -LiteralPath $whisperLicense -Destination (Join-Path $dist 'LICENSE-whisper.cpp.txt') -Force }
if (Test-Path -LiteralPath $miniaudioLicense) { Copy-Item -LiteralPath $miniaudioLicense -Destination (Join-Path $dist 'LICENSE-miniaudio.txt') -Force }
if (-not (Test-Path (Join-Path $dist 'config.ini'))) {
    Copy-Item (Join-Path $root 'config.ini') -Destination $dist
}

if (-not $SkipModelDownload) {
    $asset = "ggml-$Model-q5_1.bin"
    $dest = Join-Path (Join-Path $dist 'models') $asset
    # SHA-256 values are the Hugging Face LFS object identifiers, checked
    # against the upstream repository. Refuse incomplete or replaced weights.
    $expectedHashes = @{
        tiny = '818710568da3ca15689e31a743197b520007872ff9576237bda97bd1b469c3d7'
        base = '422f1ae452ade6f30a004d7e5c6a43195e4433bc370bf23fac9cc591f01a8898'
        small = 'ae85e4a935d7a567bd102fe55afc16bb595bdb618e11b2fc7591bc08120411bb'
    }
    if (-not (Test-Path $dest)) {
        $uri = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$asset"
        Write-Host "Downloading multilingual model $asset..."
        $temporary = "$dest.$([Guid]::NewGuid().ToString('N')).download"
        Invoke-WebRequest -Uri $uri -OutFile $temporary
        if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $expectedHashes[$Model]) {
            throw "Downloaded model hash mismatch; retained for inspection: $temporary"
        }
        Move-Item -LiteralPath $temporary -Destination $dest
    }
    if ((Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash -ne $expectedHashes[$Model]) {
        throw "Existing model hash mismatch; file left untouched: $dest"
    }
    $vadDest = Join-Path (Join-Path $dist 'models') 'ggml-silero-v6.2.0.bin'
    $vadHash = '2aa269b785eeb53a82983a20501ddf7c1d9c48e33ab63a41391ac6c9f7fb6987'
    if (-not (Test-Path -LiteralPath $vadDest)) {
        $vadTemp = "$vadDest.$([Guid]::NewGuid().ToString('N')).download"
        Invoke-WebRequest -Uri 'https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v6.2.0.bin' -OutFile $vadTemp
        if ((Get-FileHash -LiteralPath $vadTemp -Algorithm SHA256).Hash -ne $vadHash) { throw "VAD model hash mismatch; retained for inspection: $vadTemp" }
        Move-Item -LiteralPath $vadTemp -Destination $vadDest
    }
    if ((Get-FileHash -LiteralPath $vadDest -Algorithm SHA256).Hash -ne $vadHash) { throw "Existing VAD model hash mismatch; file left untouched: $vadDest" }
    $ini = Join-Path $dist 'config.ini'
    $settings = Get-Content -LiteralPath $ini -Raw
    # Change only the model= line; preserve all other user settings.
    $settings = $settings -replace '(?m)^model=.*$', "model=models\$asset"
    [System.IO.File]::WriteAllText($ini, $settings, [System.Text.UTF8Encoding]::new($false))
}

Write-Host "Portable application: $dist"
Write-Host "Run: $dist\HebENDictation.exe"
Write-Host 'Default hotkey: CTRL+SHIFT+R'

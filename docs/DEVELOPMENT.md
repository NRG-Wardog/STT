# Developer guide: build, run, test and package HebEN

[README](../README.md) · [עברית](../README.he.md) · [Benchmarks](BENCHMARKS.md) · [Licenses](LICENSING.md)

This guide describes the **actual Windows build scripts and CMake targets in this repository**, not generic whisper.cpp instructions. HebEN is a native Windows 10/11 x64 C++17/Win32 application. A local speech model and Silero VAD weights are required to **run** it, but weights are **not** stored in Git.

> **Important:** source compilation, a runnable portable folder and a tested GitHub Release are three separate deliverables. A successful CMake build or hash-checked ZIP is not proof that microphone-to-editor dictation works.

## Prerequisites and environment

Use a supported Windows 10/11 x64 machine with:

| Tool | Requirement | Why |
|---|---|---|
| Visual Studio | **2022** Community or Build Tools; **Desktop development with C++**, MSVC x64, Windows SDK | Native Win32/MSVC compiler and resource compiler |
| CMake | **3.21+** on `PATH`, with Visual Studio generator support | Project configuration and fetching pinned sources |
| Git | Git for Windows | Clone, commit and tag source |
| PowerShell | Windows PowerShell or PowerShell 7 | `build.ps1` and packaging scripts |
| Python | Optional for research, data preparation and archive verification | Not needed to compile or run the standard application |
| CUDA (optional) | Compatible NVIDIA GPU, driver and CUDA Toolkit | Only when compiling a separate GPU backend |

Open PowerShell or **Developer PowerShell for VS 2022**:

```powershell
git --version
cmake --version
```

If `cmake` is missing, install it or use VS's CMake command-line tools. If CMake finds no MSVC compiler, ensure the workload and Windows SDK are installed. All examples below use the Visual Studio 2022 generator; do not add `-A x64` to a Ninja configuration.

### Start from a clean checkout

```powershell
git clone https://github.com/NRG-Wardog/STT.git
cd STT
git status --short
```

The standard CMake configuration fetches **SHA-256-verified, commit-pinned source archives** for whisper.cpp and miniaudio. It needs network access at least on a fresh dependency cache:

- whisper.cpp v1.9.5 snapshot: commit `d1be6fde11ac6e0407606b4e42fe72d34add8037`.
- miniaudio 0.11.25 snapshot: commit `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`.

See `CMakeLists.txt` for URL/hash details and the exact compiler settings (C++17, static MSVC runtime, Unicode and resource-backed icon).

## Recommended CPU build: one script

The shortest reproducible way to get a **runnable application** is:

```powershell
.\build.ps1 -Model small
.\dist\HebENDictation.exe
```

`build.ps1` executes the following steps in order:

1. Check CMake and Git.
2. Configure native x64 CMake in `build\` (CPU backend by default).
3. Compile HebEN and its six registered deterministic unit tests.
4. Run `ctest` and stop if it fails.
5. Copy the Release EXE and license notices to `dist\`.
6. Download the selected **multilingual** GGML Whisper model and Silero GGML VAD model if missing, and verify both SHA-256 hashes.
7. Create `dist\config.ini` if absent and point its `model=` entry at the selected weight file.

Typical runtime layout:

```text
dist/
  HebENDictation.exe
  config.ini
  models/
    ggml-small-q5_1.bin
    ggml-silero-v6.2.0.bin
  LICENSE-HebEN.txt
  LICENSE-whisper.cpp.txt
  LICENSE-miniaudio.txt
  LICENSE-OpenAI-Whisper.txt
  LICENSE-Silero-VAD.txt
  QUICKSTART.txt
```

**Do not run this script on top of your current installation.** It uses fixed `build\` and `dist\` paths, updates `dist\HebENDictation.exe`, and may rewrite the model line in an existing `dist\config.ini`. It will not automatically stop a running app, and Windows may refuse to replace its EXE. For experimentation, use a **separate checkout** and keep the installed application untouched.

Other options:

```powershell
.\build.ps1 -Model base
.\build.ps1 -Model tiny
.\build.ps1 -Model small -SkipModelDownload
```

`base` and `tiny` reduce model storage but can substantially reduce accuracy, particularly in Hebrew. `-SkipModelDownload` means no weights will be downloaded or verified by the script: only use it when you have already staged and independently validated the files.

If execution policy blocks the script, **read the source first**, then use a process-scoped bypass if you trust it:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Model small
```

## Manual build and runtime staging

Use this workflow when iterating on C++, applying different CMake options, or building a test executable. Choose a **fresh build folder** instead of mixing generators or modifying an installed `dist\`.

### 1. Configure and compile CPU sources

```powershell
cmake -S . -B build-dev -G "Visual Studio 17 2022" -A x64 -DGGML_CUDA=OFF
cmake --build build-dev --config Release --parallel 4 --target HebENDictation HebENInferenceBench HebENDetectorTest HebENUnicodeInputTest HebENClipboardMockTest HebENStablePrefixTest HebENHotkeyPolicyTest HebENHotkeyRegistryTest
ctest --test-dir build-dev -C Release --output-on-failure
```

Expected compiled binary:

```text
build-dev\Release\HebENDictation.exe
```

CMake's `-G "Visual Studio 17 2022"` chooses the required generator. When using an already-configured build folder, a generator mismatch cannot be repaired by simply rerunning CMake with a different generator; create a new folder instead.

### 2. Stage a runnable folder

An EXE sitting alone in `build-dev\Release\` cannot locate models unless configured accordingly. Use a **separate portable directory**, not the live installation:

```powershell
New-Item -ItemType Directory -Force .\portable-dev\models | Out-Null
Copy-Item .\build-dev\Release\HebENDictation.exe .\portable-dev\
Copy-Item .\config.ini .\portable-dev\
```

Download the **small multilingual Whisper Q5_1** and Silero VAD weights, validate their hashes and leave them under `portable-dev\models\`:

```powershell
$assets = @(
    @{
        Name = 'ggml-small-q5_1.bin'
        Url = 'https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small-q5_1.bin'
        SHA256 = 'ae85e4a935d7a567bd102fe55afc16bb595bdb618e11b2fc7591bc08120411bb'
    },
    @{
        Name = 'ggml-silero-v6.2.0.bin'
        Url = 'https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v6.2.0.bin'
        SHA256 = '2aa269b785eeb53a82983a20501ddf7c1d9c48e33ab63a41391ac6c9f7fb6987'
    }
)
foreach ($asset in $assets) {
    $destination = Join-Path '.\portable-dev\models' $asset.Name
    if (-not (Test-Path -LiteralPath $destination)) {
        Invoke-WebRequest -Uri $asset.Url -OutFile $destination
    }
    $actual = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $asset.SHA256) {
        throw "Model integrity failure: $destination"
    }
}
.\portable-dev\HebENDictation.exe
```

The checked-in `config.ini` already points to the filenames shown above:

```ini
backend=cpu
model=models\ggml-small-q5_1.bin
vad_backend=silero
vad_model=models\ggml-silero-v6.2.0.bin
```

**Never use an English-only `*.en.bin` Whisper model for Hebrew.** Use the configured multilingual model; other weights require matching configuration and validation.

### 3. Verify the icon and application

```powershell
powershell.exe -NoProfile -File .\tests\check-icon-resource.ps1 -Exe .\build-dev\Release\HebENDictation.exe
```

This tests the EXE resource; it does **not** visually verify the tray icon. For a live smoke test, run `portable-dev\HebENDictation.exe`, verify the tray microphone, configure a hotkey, and dictate into a safe Notepad field. Never use password prompts or auto-submit messages. The application uses a single-instance lock; close any previously running HebEN instance before testing the main binary.

## CUDA development build

CUDA is a **distinct build** and is not activated by changing `backend=cpu` to `backend=cuda` on the published CPU executable.

Install an NVIDIA driver and a CUDA Toolkit version compatible with your GPU, compiler and the selected whisper.cpp/ggml snapshot. Verify your driver/toolkit independently (`nvidia-smi`, `nvcc --version` where installed).

Configure into a **different** directory:

```powershell
cmake -S . -B build-cuda -G "Visual Studio 17 2022" -A x64 -DGGML_CUDA=ON
cmake --build build-cuda --config Release --parallel 4 --target HebENDictation
```

The build uses `CUDA::cudart` and delay-loads cuBLAS when supported; check the actual executable and CUDA backend dependencies and install only the legally redistributable DLLs needed on your target system. It is not enough to copy `HebENDictation.exe` alone.

Use a **separate** runtime folder/configuration with `backend=cuda`, appropriate weights and dependent runtime libraries. Do not overwrite the CPU install. GPU memory, model load time and correctness must be measured on the packaged binary. See [Licensing](LICENSING.md) before redistributing NVIDIA components.

The optional Hebrew-tuned ivrit.ai workflow is **not in the CPU MVP ZIP**. `tests/fetch-ivrit-whisper.ps1` retrieves a pinned model for research; follow its compatible conversion/quantization steps and validate hashes. For this model, evaluate language routing carefully (especially Hebrew-only vs code-switched input).

## Opt-in 200 ms silence endpoint for local development builds

The public v1.0.0 binary clamps `silence_ms` to **at least 250 ms** even when `config.ini` requests 200. A newer development source revision supports **200 ms when explicitly configured**, while the checked-in default remains **1200 ms**. The existing v1.0.0 release binary is unchanged; recompile the later source to use the new minimum.

In a separate test installation, back up `config.ini` and change only this value:

```ini
silence_ms=200
```

Use **Reload settings** from the tray or restart the new development binary, then validate actual microphone-to-editor dictation against your normal setting. In the detector's 20 ms frames, the configured threshold triggers an utterance flush after ten consecutive unvoiced frames. Silero VAD classification can vary near word boundaries, so short mid-sentence pauses may cause premature segmentation, repeated inference or dropped words. Compare 1200, 900, 500 and 200 ms on identical permitted audio, with actual editor WER/CER, premature splits, end-of-speech to final-text p50/p95, CPU and memory. Revert if accuracy or usability degrades. **200 ms of silence is not 200 ms total text latency**; inference and text insertion still take time. Never silently change another user's default or overwrite the currently running installation.

## Automated and interactive tests

### Registered deterministic tests (CI-safe)

```powershell
ctest --test-dir build-dev -C Release --output-on-failure
python .\tests\test-rootcause-errors.py
python .\tests\check-doc-links.py
```

The Python commands are optional and require Python on `PATH`. Six native CTest tests cover the detector, Unicode-event construction, clipboard mock, prefix agreement and hotkey policy/registration. GitHub Actions runs these on `windows-2022`. These tests **do not** measure live dictation accuracy or exercise a physical keyboard, microphone or third-party editor.

### Optional integration and replay fixtures

To compile extra **test-only** executables for input/editor integration and prerecorded audio:

```powershell
cmake -S . -B build-fixtures -G "Visual Studio 17 2022" -A x64 -DGGML_CUDA=OFF -DHEBEN_BUILD_INTEGRATION_FIXTURE=ON
cmake --build build-fixtures --config Release --parallel 4 --target HebENReplayFixture HebENDesktopAudioFixture HebENDesktopMicFixture HebENFixedInputProbe
```

The replay fixture bypasses the physical microphone. The desktop-audio fixture can use a real hotkey and editor with prerecorded PCM. The desktop-microphone fixture exercises the microphone path. **Never label a replay/audio fixture PASS as a physical microphone-to-editor PASS.**

See `python .\tests\replay-hybrid-corpus.py --help`, `python .\tests\run-inference-bench.py --help` and [Benchmarks](BENCHMARKS.md) for inputs and reproducibility. These fixtures require the appropriate models and authorized, nonsensitive target windows.

### Benchmarks and evidence hygiene

Record executable SHA-256, source commit, model and quantization, backend, Windows build, CPU/GPU/driver, input-device details, dataset and number of samples. Measure private bytes, working set, CPU, GPU, VRAM, startup/model-load time, latency and WER/CER **on the same named binary** when comparing results.

Do not commit personal audio, confidential transcripts, browser state or desktop diagnostic dumps. The [benchmark report](BENCHMARKS.md) clearly separates historical CUDA and CPU data.

## Release packaging

A reproducible **public CPU archive** is more than the contents of `build-dev\Release`. The repository contains two packaging helpers:

- `tests/package-public-release.ps1`: stages EXE, configuration, checked models, notices, docs/assets, a source commit ID and a per-file SHA-256 manifest.
- `tests/archive-package.py`: verifies the manifest, archives the staged directory, reads **every file back** from the ZIP and emits an archive-level checksum.

For a release build made from an intended commit in your local checkout:

1. Ensure your working tree is clean and matches that commit; verify `git rev-parse HEAD`.
2. Generate a Release EXE and fetch/verify the models using `.\build.ps1 -Model small` in a safe checkout.
3. Run `ctest` and icon-resource checks.
4. Stage the package into a **new, nonexistent output directory**, then archive into a **new ZIP path**:

```powershell
$commit = (git rev-parse HEAD).Trim()
.\tests\package-public-release.ps1 -SourceRoot . -Exe .\dist\HebENDictation.exe -DependencyRoot .\build\_deps -ModelDirectory .\dist\models -Output .\build\release-staging -Commit $commit
python .\tests\archive-package.py --package .\build\release-staging --out .\build\HebEN-Windows-CPU-x64.zip
Get-FileHash .\build\HebEN-Windows-CPU-x64.zip -Algorithm SHA256
```

**Python is required for `archive-package.py`, not for ordinary app builds.** Both packaging scripts refuse to overwrite existing outputs; use new paths per release attempt. The staging helper depends on licenses under the fetched `build\_deps` directories. Do not delete or bypass them.

5. Extract the archive into a fresh, noninstalled test folder. Validate model paths and execute a smoke test from the **extracted** package. For a public release, also record real user-interaction testing separately from fixture tests.
6. Publish only redistributable assets and exact SHA-256 checksums. Tag the **actual source commit** used to build the ZIP. Do not modify a released tag silently.

The current public v1.0.0 package and its historical test scope are described in [Release notes](RELEASE-NOTES.md). Subsequent documentation-only commits on `main` do not retroactively change the tagged v1.0.0 binaries.

## Common failures

| Problem | Resolution |
|---|---|
| CMake fails to find MSVC or Windows SDK | Install Visual Studio C++ workload and SDK; open Developer PowerShell. |
| Generator mismatch or `-A` unsupported | Start with a new build directory and the explicit VS 2022 generator. |
| Missing / corrupted model | Check `config.ini` relative to the EXE and validate model SHA-256; never skip verification to silence an error. |
| Build script cannot copy EXE | Exit HebEN in the tray before replacing a running executable; build in a separate checkout. |
| App starts but fails to capture speech | Check Windows microphone permission, input device, voice activity detection and model initialization diagnostics. |
| Shortcut not registered | Check conflicts, single-instance lock and the shortcut settings menu. |
| No text in Chrome/Codex | Validate focus with a controlled field; Windows integrity boundaries and unknown terminal prompts may prevent safe injection. |
| CUDA build runs on CPU or fails loading DLLs | Verify CUDA was enabled at CMake configure time, set `backend=cuda` in the **GPU** folder, and inspect DLL dependencies. |

For detailed third-party terms, see [Licensing](LICENSING.md). Report reproducible defects in [GitHub Issues](https://github.com/NRG-Wardog/STT/issues), with sanitized logs and exact binary/model identifiers.

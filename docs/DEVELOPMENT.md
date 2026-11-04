# Building, testing and model acquisition

## Requirements

Use Windows 10/11 x64, Git, CMake 3.21+ and Visual Studio 2022 with the **Desktop development with C++** workload. Python is optional and used only for development and benchmark scripts. The application itself is native.

From a Visual Studio developer PowerShell or a normal PowerShell with CMake on PATH:

```powershell
cmake -S . -B build -A x64 -DGGML_CUDA=OFF
cmake --build build --config Release --parallel 4 --target HebENDictation HebENInferenceBench HebENDetectorTest HebENUnicodeInputTest HebENClipboardMockTest HebENStablePrefixTest HebENHotkeyPolicyTest HebENHotkeyRegistryTest
ctest --test-dir build -C Release --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File tests/check-icon-resource.ps1 -Exe build/Release/HebENDictation.exe
python tests/test-rootcause-errors.py
```

The CMake dependencies are fetched from immutable commit archives with SHA-256 verification: whisper.cpp v1.9.5 commit `d1be6fde11ac6e0407606b4e42fe72d34add8037` and miniaudio 0.11.25 commit `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`. The standard CPU build does not need CUDA.

## Models

The CPU MVP package uses the multilingual [Whisper small Q5_1](https://huggingface.co/ggerganov/whisper.cpp) model and [Silero GGML VAD](https://huggingface.co/ggml-org/whisper-vad). Model files are not committed to Git. `build.ps1 -Model small` downloads them and verifies these SHA-256 values:

| File | SHA-256 |
|---|---|
| `ggml-small-q5_1.bin` | `ae85e4a935d7a567bd102fe55afc16bb595bdb618e11b2fc7591bc08120411bb` |
| `ggml-silero-v6.2.0.bin` | `2aa269b785eeb53a82983a20501ddf7c1d9c48e33ab63a41391ac6c9f7fb6987` |

The build script creates or updates `dist`; do not run it over an installation you need to preserve. It does not stop a running process. The script also offers multilingual tiny and base models with pinned hashes; these may reduce accuracy.

For the optional Hebrew-tuned hybrid, `tests/fetch-ivrit-whisper.ps1` downloads a pinned ivrit.ai model revision and checks its hash; a compatible whisper.cpp quantizer is required. Review [Licensing](LICENSING.md). No model weights are committed.

## CUDA

Install a compatible NVIDIA driver and CUDA toolkit, then configure a separate build, for example:

```powershell
cmake -S . -B build-cuda -A x64 -DGGML_CUDA=ON
cmake --build build-cuda --config Release --target HebENDictation
```

Set `backend=cuda` only for a CUDA-enabled executable and provide required runtime DLLs under the applicable NVIDIA license. The CPU release does not include NVIDIA libraries. GPU support and memory tradeoffs vary by hardware; it is not automatically faster overall.

## Tests and measurement

The native tests exercise segmentation, Unicode-event construction, retained text, clipboard policy, prefix agreement and shortcut registration. The Python normalization fixture is deterministic. These checks do **not** prove live microphone dictation, actual editor insertion or accuracy on a population.

Optional `HEBEN_BUILD_INTEGRATION_FIXTURE=ON` builds separate test-only fixtures, including a replay worker. `tests/replay-hybrid-corpus.py --help` and `tests/run-inference-bench.py --help` show invocation parameters; audio and models must be provided separately. Tests that interact with a real desktop or microphone require explicit user awareness, a safe field and an idle desktop. Never run them against sensitive prompts or submit messages automatically.

See [Benchmarks](BENCHMARKS.md) for historical measurements, dataset limitations and reproduction methodology. Keep raw microphone audio, user transcripts, credentials and desktop captures out of Git.

[English](README.md) · [עברית](README.he.md)

<p align="center"><img src="assets/heben-logo.png" width="120" alt="HebEN microphone mascot"></p>

# HebEN Dictation

**Offline Hebrew and English voice dictation for Windows.** Focus a text field, press a global shortcut, and speak. HebEN runs in the system tray and sends recognized Unicode text to the focused application.

**v1.0.0 MVP:** the core workflow is available for use; accuracy, editor compatibility, resource usage and optional streaming continue to improve. See [known limitations](#known-limitations) before using it for important text.

## Features

- Local Whisper-based speech recognition and Silero voice activity detection; no cloud speech service is used at runtime.
- Hebrew, English and mixed-language dictation, with automatic or manually selected language.
- Configurable global shortcut, system-tray controls and a small caret indicator when an exact position is available.
- Unicode keyboard insertion without using the clipboard by default.
- Native C++/Win32 executable; Python is not needed to run the application.
- Optional experimental progressive text while speaking. It is **off by default** in the public package.

![Shortcut settings window](docs/assets/shortcut-settings.png)

## Install and use

1. Download the Windows CPU ZIP from [Releases](https://github.com/NRG-Wardog/STT/releases), extract it to a writable folder, and run `HebENDictation.exe` from that folder. Keep the `models` folder alongside it.
2. Allow Windows microphone access and select your preferred input device. Network access is not required for dictation after installation.
3. Click a normal, non-sensitive text field. Press and release **Ctrl+Shift+R**, then speak. Press it again to stop, or stay silent for approximately five seconds. Keep the target focused while text is inserted.
4. Look for the microphone in the system tray, including the hidden-icons area. A dot appears at the caret when Windows exposes an exact position; some editors do not expose one.

The portable package does not automatically add itself to Windows startup. To start it on sign-in, place a shortcut to the executable in your Windows Startup folder; remove that shortcut to undo it.

### Change the shortcut

Right-click the tray icon → **Keyboard shortcut…**. Type a combination or choose **Record shortcut**, then select **Save**. The app rejects common combinations such as Ctrl+C and checks whether Windows can register the global shortcut. A shortcut can still conflict with an individual application's local shortcuts.

### Language and streaming

Choose **Language** from the tray menu: automatic detection, Hebrew or English. Automatic detection can mistake short Hebrew utterances for another language; code-switching within one sentence can be inaccurate. Always review the result.

By default, text is inserted after a phrase or pause. To try progressive insertion, set `streaming_prototype=1` in `config.ini`, then choose **Reload settings** in the tray menu. Earlier text is not automatically rewritten: a later hypothesis can conflict with it. Streaming is experimental and has not established sub-second first-word latency or general accuracy.

### Terminals and sensitive fields

`trusted_terminal_input=1` explicitly permits certain terminal controls even when Windows cannot identify the prompt type. Enable it only for known non-sensitive prompts. The app cannot reliably identify every password prompt in a terminal. Explicitly identified password controls and elevated targets are refused; do not bypass Windows integrity boundaries. Terminal newlines are sanitized and HebEN does not submit messages or commands automatically.

Clipboard paste is an experimental opt-in, not the default. Preservation failed in prior tests, so it should not be relied upon. Normal Unicode insertion leaves the clipboard alone.

## Requirements

| | Requirement |
|---|---|
| OS | 64-bit Windows 10 or 11; a microphone and microphone permission |
| CPU package | x64 processor with AVX2, FMA, F16C and BMI2; memory and latency depend strongly on model and speech length |
| GPU build | NVIDIA GPU, compatible driver and CUDA build/runtime; built separately from source |
| Network | Needed for downloads and dependency fetching, not for local dictation |

The CPU release uses multilingual Whisper **small Q5_1** and Silero VAD. The separately configurable Hebrew-tuned hybrid and CUDA builds require additional models/libraries and are **not included in the CPU ZIP**. Model files dominate the package size. Do not use an English-only `.en` model for Hebrew.

## Compatibility and performance

The Unicode insertion path has been observed in controlled or supervised tests with Notepad, Chrome/ChatGPT, VS Code and a Codex terminal. Those checks used different trial builds and a limited number of utterances; they do not establish universal compatibility or verify every field. Focus changes, editor accessibility, integrity levels and terminal behavior can block insertion.

| Historical measurement | Result | Scope |
|---|---:|---|
| Idle private / working set | 4.00 / 13.56 MiB median | CUDA candidate `c5ff8e…`, 59 one-second samples |
| Mic active, no detected voice: private / working set | 1,118 / 158.9 MiB median | Same CUDA candidate, 20 samples; no inference |
| First partial text | 3.28 s median | Different replay fixture `ed7e43…`, same recording repeated five times |
| Hebrew / English / mixed WER | 20.0% / 7.2% / 28.1% | Different hybrid replay fixture `517ca1…`; 30 clips each, references unverified, mixed clips artificially stitched |

These rows are **not measurements of the CPU release ZIP** and should not be compared as one configuration. Detailed hardware, model, CER, sampling, limitations and redacted evidence are in [Benchmarks](docs/BENCHMARKS.md). CPU release runtime, first-word and final-text latency are not yet measured.

## Build and test

Install Visual Studio 2022 with **Desktop development with C++**, CMake and Git. From a fresh checkout:

```powershell
cmake -S . -B build -A x64 -DGGML_CUDA=OFF
cmake --build build --config Release --parallel 4 --target HebENDictation HebENDetectorTest HebENUnicodeInputTest HebENClipboardMockTest HebENStablePrefixTest HebENHotkeyPolicyTest HebENHotkeyRegistryTest
ctest --test-dir build -C Release --output-on-failure
```

CMake fetches hash-pinned source archives. The executable also needs a compatible multilingual GGML model and Silero VAD model configured in `config.ini`; see [Development](docs/DEVELOPMENT.md) for acquisition, checksums, CUDA, fixture tests and benchmarking. `build.ps1 -Model small` can create a local `dist` folder and download checked models; it can overwrite that folder's executable.

## Known limitations

- Hebrew and mixed-language accuracy vary by speaker, microphone, model and context. Short utterances may be misclassified.
- The current microphone-to-editor pipeline has limited real-world validation. A speaker-playback test did not produce detectable speech at the microphone; it did not prove a recognition defect.
- The caret dot may be absent in editors that do not expose a precise caret location. Focus changes can cause a transcript to be retained rather than inserted.
- Optional streaming may omit or conflict with later words, and can use substantially more resources. It is not a guarantee of word-by-word output.
- Some applications, elevated windows, password controls and terminal prompts may refuse or be unsafe for insertion. Do not dictate into sensitive fields.
- Models may be slow on CPU and use considerable memory while active. Windows startup behavior after an actual reboot has not been independently verified on every system.

## License and credits

HebEN source is [MIT licensed](LICENSE). It uses [whisper.cpp](https://github.com/ggml-org/whisper.cpp), [miniaudio](https://miniaud.io/), [OpenAI Whisper](https://github.com/openai/whisper), and [Silero VAD](https://github.com/snakers4/silero-vad). Optional ivrit.ai models and NVIDIA components have their own terms. See [Licensing](docs/LICENSING.md) and [release notes](docs/RELEASE-NOTES.md).

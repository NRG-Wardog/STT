# HebEN Dictation v1.0.0: MVP

The first public MVP provides local Hebrew/English dictation for 64-bit Windows, a tray icon and caret feedback where available, configurable global shortcut (including Ctrl+? on supported keyboard layouts), Unicode text insertion, local Silero voice detection and optional experimental progressive output. The CPU ZIP requires x64 with AVX2, FMA, F16C and BMI2, and includes the multilingual Whisper small Q5_1 model and Silero VAD; it does not require Python or an internet connection for normal operation. Windows does not distinguish left and right Ctrl for global shortcuts, and Ctrl+? can conflict with application shortcuts.

## Installation

Extract the CPU ZIP into a writable directory and run `HebENDictation.exe`. Allow microphone access, focus a safe text field and press Ctrl+Shift+R. Press it again to stop. See the [README](../README.md) for shortcut settings, startup and troubleshooting.

## Verified scope

Native unit tests and icon-resource checks are separate from real dictation. Earlier supervised trial builds inserted selected Hebrew, English and mixed phrases in several editors. A later controlled Chrome test exercised a real hotkey, VAD, ASR and insertion using prerecorded PCM through a test fixture. An exact CUDA candidate opened a physical microphone, but speaker playback was not detected as speech. These observations do not establish broad accuracy or verify the final CPU package's live microphone-to-editor path.

## Post-release field-evaluation addendum (not the original CPU benchmark)

A later user-supervised evaluation compared 19 dictated editor outputs with text approved by one speaker: **Hebrew (10 examples): WER 4.69%, CER 2.57%; English (9 examples): WER 7.25%, CER 5.15%**. Some audio was incomplete and the audio/derived segments were deleted after review. There were no completed natural mixed-language examples. The exact executable hash/model configuration was not included with these aggregates. **These figures are not verified performance metrics for the published v1.0.0 CPU ZIP.** See [Benchmarks](BENCHMARKS.md). This addendum was added after the release tag and does not modify its binary artifacts.

## Limitations

- Recognition quality and language detection vary; review output, especially mixed-language speech and short Hebrew utterances.
- Optional progressive streaming is off by default and can commit text that later hypotheses contradict. It has not demonstrated reliable sub-second words.
- Clipboard fallback is opt-in and prior preservation checks failed. Unicode insertion is the default.
- Trusted-terminal mode is opt-in; terminal prompts may not expose whether they are sensitive. Never use it at an unknown password prompt. No automatic Enter is sent.
- A precise caret indicator is unavailable in some applications. Focus changes and Windows integrity boundaries can prevent insertion.
- Active memory and latency depend heavily on the selected model and backend. [Historical benchmarks](BENCHMARKS.md) cover different CUDA binaries and are not CPU-release measurements.

## Distribution and licensing

The CPU package contains MIT-licensed HebEN, Whisper/whisper.cpp and Silero components, and the miniaudio license. The optional ivrit.ai model and CUDA DLLs are not bundled; see [Licensing](LICENSING.md). Compared with earlier local trials, this public package uses a CPU-only small multilingual model, defaults trusted-terminal and experimental streaming to off, and does not include the larger Hebrew-tuned hybrid model.

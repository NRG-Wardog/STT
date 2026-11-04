# Benchmark notes and reproducibility

These are **historical measurements of different binaries**, not performance claims for the v1.0.0 CPU ZIP. Raw process logs and personal recordings remain local. The [redacted summary](benchmark-summary.json) contains the measurement identities and aggregate counts without audio, transcripts, user paths or desktop data.

Test machine: Windows 11 Pro build 26200; Intel Core i9-10900F (10 cores / 20 threads); NVIDIA GeForce RTX 2060, driver 32.0.16.1088; approximately 48 GiB RAM. CPU percentages are relative to one logical core. Private bytes and working set are process-specific. GPU counters are process-specific when available, not system totals.

## Runtime profile A — CUDA application, not CPU release

Binary SHA-256 `c5ff8e3761acea51f394ab75dd784db25defce801fc2ef1b7f2f506e0ccf625b`; CUDA; small Q5_1 multilingual plus ivrit.ai turbo Q5_0 hybrid and Silero VAD; streaming disabled. Idle: 59 one-second intervals, first sample excluded. Active: 20 samples at roughly 0.25-second intervals while the physical microphone was open; speaker playback was not detected as voice, so **no ASR inference occurred**. GPU readings were sparse and cannot represent a continuous profile.

| Metric | Result / conditions |
|---|---|
| Idle private bytes / working set, median | 4.00 / 13.56 MiB |
| Idle CPU | no CPU-time increment at counter resolution |
| Active private bytes / working set, median | 1,118.03 / 158.90 MiB |
| Active sampled private / working-set maximum | 1,118.04 / 184.58 MiB |
| Active CPU | 15.68% weighted mean of one logical core; 87.5% maximum sampled interval |
| GPU engine utilization | 0% in valid sparse samples only; not proof of zero sustained use |
| Dedicated / shared GPU memory | 485.70 / 64.00 MiB in a valid active sample |
| Idle GPU / VRAM | no matching process counters; **NOT MEASURED**, not zero |
| Full CUDA candidate folder | 1,323,123,330 bytes; includes both models and CUDA libraries |
| Model load | 1,779 ms in this one microphone activation; disk-cache state not controlled |
| Cold / warm startup | **NOT MEASURED** in this attached-process run |
| First / subsequent / final text latency | **NOT MEASURED**: no speech was detected |

## Speech profile B — offline hybrid replay, different executable

Replay fixture SHA-256 `517ca11ef313e22e4bb0928f36dff8027e8ebda306ab89b55003fa5f22f5b8b3`; CUDA, small Q5_1 plus ivrit.ai turbo Q5_0, Silero, four threads, automatic language. Prerecorded audio was fed into the worker; there was **no physical microphone, actual hotkey or editor**. Hebrew and English each used 30 FLEURS clips with dataset-provided, not independently reviewed references. The Hebrew subset was limited in speaker representation. The 30 mixed clips were artificially stitched from separate recordings; five stopped early. These are diagnostics, not representative conversational benchmarks.

| Category | Clips | Word edits / words | WER | Character edits / characters | CER |
|---|---:|---:|---:|---:|---:|
| Hebrew | 30 | 93 / 465 | 20.0% | 230 / 2,564 | 9.0% |
| English | 30 | 45 / 627 | 7.2% | 118 / 3,645 | 3.2% |
| Artificial mixed | 30 | 307 / 1,092 | 28.1% | 1,447 / 6,239 | 23.2% |

WER and CER use normalized edit distance. Punctuation and Hebrew diacritics are normalized; see `tests/compare-asr-runs.py`. The mixed early-stop problem affects interpretation. Do not compare these figures directly with the release CPU model or another benchmark's normalization.

## Streaming profile C — repeated prerecorded audio, different executable

Replay fixture SHA-256 `ed7e439fed6c4b221e5372dd21f5137f272b6d6a75bd335eab25acf8da016c5f`; CUDA hybrid, 1,000 ms interval, agreement 2 and one trailing word held. The **same** speaker-confirmed mixed recording was replayed five times, not five independent utterances. First committed partial after VAD voice onset: p50 **3,281 ms**, nearest-rank p95 **3,312 ms**. Subsequent-word and final-editor latency: **NOT MEASURED**. This is not a physical-microphone or editor measurement and cannot establish general latency or accuracy.

## Reproduce

Build the optional local replay fixture and obtain models using [Development](DEVELOPMENT.md). `tests/replay-hybrid-corpus.py --help`, `tests/run-inference-bench.py --help`, `tests/measure-idle.ps1`, and `tests/sample-process-gpu.ps1` describe the available harnesses. Use the same fixture, model files, input clips, decoding configuration and hardware for paired comparisons. Acquire [FLEURS](https://huggingface.co/datasets/google/fleurs) separately; it is not redistributed here. Never publish personal audio, private transcripts or unreviewed desktop logs.

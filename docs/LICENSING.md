# Licenses and redistribution

- **HebEN source and project assets:** [MIT](../LICENSE), as authorized by the project owner.
- **whisper.cpp:** [MIT](https://github.com/ggml-org/whisper.cpp/blob/master/LICENSE); the binary package includes its notice.
- **OpenAI Whisper code and model family:** [MIT](https://github.com/openai/whisper/blob/main/LICENSE); the small Q5_1 weights are distributed through the [whisper.cpp model repository](https://huggingface.co/ggerganov/whisper.cpp), whose model card states MIT. The package includes the OpenAI notice.
- **miniaudio:** [dual licensed](https://github.com/mackron/miniaudio/blob/master/LICENSE), including MIT-0; the full upstream license is included in the binary package.
- **Silero VAD:** [MIT](https://github.com/snakers4/silero-vad/blob/master/LICENSE). The converted [GGML VAD model repository](https://huggingface.co/ggml-org/whisper-vad) also states MIT; its notice is included.
- **Optional ivrit.ai Whisper turbo:** the [model card](https://huggingface.co/ivrit-ai/whisper-large-v3-turbo-ggml) states Apache-2.0. It is not included in the CPU MVP ZIP; review its own model terms when obtaining it.
- **NVIDIA CUDA:** CUDA components are governed by the [NVIDIA CUDA EULA](https://docs.nvidia.com/cuda/eula/index.html). The CPU MVP ZIP does not redistribute NVIDIA DLLs. A separately built GPU package requires a component-specific redistribution and license review.
- **FLEURS:** the [dataset card](https://huggingface.co/datasets/google/fleurs) states CC-BY-4.0. It is not included in the repository or release package. Respect attribution and terms if you obtain or redistribute it.

Third-party terms apply independently of HebEN's MIT license. Personal recordings, generated datasets and local diagnostic logs are excluded from publication.

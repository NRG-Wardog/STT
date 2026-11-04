// Controlled CPU benchmark of the same whisper decoding settings as the app.
// Input: 16 kHz mono signed PCM16 WAV. Output: one UTF-8 JSON object per run.
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include "whisper.h"
#include "../src/utterance_detector.hpp"
#ifdef HEBEN_CUDA_BENCH
#include <cuda_runtime_api.h>
#include <delayimp.h>
#endif
#include <chrono>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b-a).count();
}
static double cpu_ms() {
    FILETIME create{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user)) return -1;
    ULARGE_INTEGER k{}, u{}; k.LowPart=kernel.dwLowDateTime; k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime; u.HighPart=user.dwHighDateTime;
    return (k.QuadPart+u.QuadPart)/10000.0;
}
static std::string json(const std::string &s) {
    std::string result = "\"";
    const char *hex = "0123456789abcdef";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
        else if (c < 32) { result += "\\u00"; result += hex[c>>4]; result += hex[c&15]; }
        else result += char(c);
    }
    return result + "\"";
}
static bool read_exact(FILE *f, void *p, size_t n) { return fread(p, 1, n, f) == n; }
static bool wav(const wchar_t *path, std::vector<float> &samples) {
    FILE *f = nullptr;
    if (_wfopen_s(&f, path, L"rb") || !f) return false;
    char riff[4], wave[4], tag[4]; uint32_t size = 0; bool format = false, data = false;
    bool ok = read_exact(f, riff, 4) && read_exact(f, &size, 4) && read_exact(f, wave, 4) &&
              !memcmp(riff, "RIFF", 4) && !memcmp(wave, "WAVE", 4);
    while (ok && read_exact(f, tag, 4) && read_exact(f, &size, 4)) {
        if (size > 512u*1024*1024) { ok=false; break; }
        if (!memcmp(tag, "fmt ", 4)) {
            std::vector<unsigned char> bytes(size);
            ok = size >= 16 && read_exact(f, bytes.data(), size);
            if (ok) {
                uint16_t encoding, channels, bits; uint32_t rate;
                memcpy(&encoding, bytes.data(), 2); memcpy(&channels, bytes.data()+2, 2);
                memcpy(&rate, bytes.data()+4, 4); memcpy(&bits, bytes.data()+14, 2);
                format = encoding == 1 && channels == 1 && rate == 16000 && bits == 16;
            }
        } else if (!memcmp(tag, "data", 4)) {
            if (size % 2) { ok=false; break; }
            std::vector<int16_t> pcm(size/2);
            ok = read_exact(f, pcm.data(), size);
            if (ok) { samples.reserve(pcm.size()); for (auto v : pcm) samples.push_back(v/32768.f); data=true; }
        } else ok = fseek(f, static_cast<long>(size), SEEK_CUR) == 0;
        if (size & 1) ok = ok && fseek(f, 1, SEEK_CUR) == 0;
    }
    fclose(f); return ok && format && data && !samples.empty();
}
static whisper_context *open_model(const wchar_t *path, bool gpu) {
    FILE *fp = nullptr;
    if (_wfopen_s(&fp, path, L"rb") || !fp) return nullptr;
    whisper_model_loader loader{}; loader.context = fp;
    loader.read = [](void *c, void *p, size_t n) { return fread(p, 1, n, static_cast<FILE*>(c)); };
    loader.eof = [](void *c) { return feof(static_cast<FILE*>(c)) != 0; };
    loader.close = [](void*) {};
    auto params = whisper_context_default_params(); params.use_gpu = gpu;
    whisper_context *result = whisper_init_with_params(&loader, params);
    fclose(fp); return result;
}
int wmain(int argc, wchar_t **argv) {
    if (argc < 4 || argc > 6) { std::cerr << "usage: HebENInferenceBench model.bin audio.wav repetitions [cpu|gpu|vad:...] [auto|he|en]\n"; return 2; }
    const bool unload = argc == 5 && std::wstring(argv[4]) == L"gpu-unload-reload-hold";
    const bool reload = unload || (argc == 5 && std::wstring(argv[4]) == L"gpu-reset-reload-hold");
    const bool reset = reload || (argc == 5 && std::wstring(argv[4]) == L"gpu-reset-hold");
    const bool hold = reset || (argc == 5 && std::wstring(argv[4]) == L"gpu-hold");
    const std::wstring mode = argc >= 5 ? argv[4] : L"";
    const bool probe = mode==L"gpuprobe";
    const bool gpuVad = mode.rfind(L"gpuvad:",0) == 0;
    const bool gpu = hold || gpuVad || probe || mode == L"gpu";
    const bool useVad = gpuVad || mode.rfind(L"vad:",0) == 0;
    if (argc >= 5 && !gpu && !useVad && mode!=L"cpu") { std::cerr << "Unknown mode\n"; return 2; }
    std::string language="auto";
    if(argc==6) {
        std::wstring requested=argv[5];
        if(requested==L"he")language="he";else if(requested==L"en")language="en";
        else if(requested!=L"auto")return 2;
    }
    int repeats = _wtoi(argv[3]);
    std::vector<float> pcm;
    if (repeats < 1 || repeats > 100 || !wav(argv[2], pcm)) { std::cerr << "Invalid WAV or repeat count\n"; return 2; }
    std::vector<std::vector<float>> phrases;
    if (useVad) {
        size_t start=gpuVad ? 7 : 4;
        float threshold=static_cast<float>(_wtof(mode.c_str()+start));
        if (!(threshold >= .001f && threshold <= .2f)) return 2;
        auto separator=mode.find(L':',start);
        int silenceMs=separator == std::wstring::npos ? 700 : _wtoi(mode.c_str()+separator+1);
        if (silenceMs < 250 || silenceMs > 3000) return 2;
        auto separator2=separator == std::wstring::npos ? std::wstring::npos : mode.find(L':',separator+1);
        float maxCrossing=separator2 == std::wstring::npos ? 1.f : static_cast<float>(_wtof(mode.c_str()+separator2+1));
        if (maxCrossing <= 0.f || maxCrossing > 1.f) return 2;
        heben::Detector detector(threshold,silenceMs,15,[&](std::vector<float>&& x){
            if (x.size() >= heben::SAMPLE_RATE/4) phrases.push_back(std::move(x));
        },maxCrossing);
        for(size_t pos=0; pos<pcm.size();) {
            size_t count=std::min<size_t>(160,pcm.size()-pos); detector.Feed(pcm.data()+pos,count); pos+=count;
        }
        detector.Finish();
    } else phrases.push_back(pcm);
    auto start = Clock::now(); auto *ctx = open_model(argv[1], gpu); auto loaded = Clock::now();
    if (!ctx) { std::cerr << "Model load failed\n"; return 3; }
    if(probe) {
        auto before=Clock::now();std::vector<float> probabilities(static_cast<size_t>(whisper_lang_max_id()+1));
        if(whisper_pcm_to_mel(ctx,pcm.data(),static_cast<int>(pcm.size()),4)!=0){whisper_free(ctx);return 11;}
        int top=whisper_lang_auto_detect(ctx,0,4,probabilities.data());auto after=Clock::now();
        if(top<0){whisper_free(ctx);return 12;}
        auto prob=[&](const char *name){int id=whisper_lang_id(name);return id>=0?probabilities[static_cast<size_t>(id)]:0.f;};
        std::cout << "{\"top\":" << json(whisper_lang_str(top)) << ",\"he\":" << prob("he") << ",\"en\":" << prob("en")
                  << ",\"es\":" << prob("es") << ",\"ar\":" << prob("ar") << ",\"fa\":" << prob("fa")
                  << ",\"detection_ms\":" << ms(before,after) << "}\n";
        whisper_free(ctx);return 0;
    }
    std::string firstText;
    for (int i=0; i<repeats; ++i) {
        auto p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        p.n_threads=4; p.language=language.c_str(); p.translate=false; p.detect_language=false;
        wchar_t timestampMode[8]{};
        bool timestamps=GetEnvironmentVariableW(L"HEBEN_BENCH_TIMESTAMPS",timestampMode,8)>0 && timestampMode[0]==L'1';
        p.no_context=true; p.no_timestamps=!timestamps; p.single_segment=!timestamps;
        p.print_realtime=false; p.print_progress=false; p.print_timestamps=false; p.print_special=false;
        p.greedy.best_of=1;
        auto before=Clock::now(); double cpuBefore=cpu_ms();
        int rc=0; std::string output;double tokenLogSum=0;int tokenCount=0;
        for (const auto &phrase : phrases) {
            rc=whisper_full(ctx,p,phrase.data(),static_cast<int>(phrase.size()));
            if (rc) break;
            for (int j=0; j<whisper_full_n_segments(ctx); ++j) {
                output += whisper_full_get_segment_text(ctx,j);
                for(int k=0;k<whisper_full_n_tokens(ctx,j);++k) {
                    auto token=whisper_full_get_token_data(ctx,j,k);
                    if(token.id<whisper_token_eot(ctx) && std::isfinite(token.plog)){tokenLogSum+=token.plog;++tokenCount;}
                }
            }
        }
        double cpuAfter=cpu_ms(); auto after=Clock::now();
        if (rc) { whisper_free(ctx); return 4; }
        if (i == 0) firstText=output;
        PROCESS_MEMORY_COUNTERS_EX mem{}; mem.cb=sizeof(mem);
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem), sizeof(mem));
        int langId=phrases.empty()?-1:whisper_full_lang_id(ctx);
        const char *detected=langId<0?"unknown":whisper_lang_str(langId);
        std::cout << "{\"iteration\":" << i << ",\"language_requested\":" << json(language) << ",\"language_detected\":" << json(detected?detected:"unknown") << ",\"gpu_requested\":" << (gpu ? "true" : "false") << ",\"vad\":" << (useVad ? "true" : "false") << ",\"phrases\":" << phrases.size() << ",\"model_load_ms\":" << ms(start,loaded)
                  << ",\"inference_ms\":" << ms(before,after) << ",\"audio_s\":" << pcm.size()/16000.0
                  << ",\"cpu_ms\":" << cpuAfter-cpuBefore
                  << ",\"lexical_tokens\":" << tokenCount << ",\"mean_token_logp\":" << (tokenCount?tokenLogSum/tokenCount:0)
                  << ",\"working_set_bytes\":" << mem.WorkingSetSize << ",\"private_bytes\":" << mem.PrivateUsage
                  << ",\"peak_working_set_bytes\":" << mem.PeakWorkingSetSize << ",\"text\":" << json(output) << "}\n";
    }
    whisper_free(ctx);
    if (reset) {
#ifdef HEBEN_CUDA_BENCH
        cudaError_t error=cudaDeviceReset();
        std::cerr << "cudaDeviceReset: " << cudaGetErrorString(error) << "\n";
        if (error != cudaSuccess) return 5;
        if (unload && !__FUnloadDelayLoadedDLL2(HEBEN_CUBLAS_DLL)) {
            std::cerr << "Could not unload delay-loaded cuBLAS\n"; return 10;
        }
        if (reload) {
            for (int cycle=0;cycle<3;++cycle) {
                ctx=open_model(argv[1],true);
                if (!ctx) return 6;
                auto p=whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
                p.n_threads=4; p.language="auto"; p.translate=false; p.detect_language=false;
                p.no_context=true; p.no_timestamps=true; p.single_segment=true;
                p.print_realtime=false; p.print_progress=false; p.print_timestamps=false; p.print_special=false; p.greedy.best_of=1;
                std::string text;
                for (const auto &phrase:phrases) {
                    if (whisper_full(ctx,p,phrase.data(),static_cast<int>(phrase.size()))) { whisper_free(ctx); return 7; }
                    for (int j=0;j<whisper_full_n_segments(ctx);++j) text += whisper_full_get_segment_text(ctx,j);
                }
                whisper_free(ctx);
                if (text != firstText) { std::cerr << "Reload output mismatch\n"; return 8; }
                error=cudaDeviceReset();
                if (error != cudaSuccess) return 9;
                if (unload && !__FUnloadDelayLoadedDLL2(HEBEN_CUBLAS_DLL)) return 10;
                std::cerr << "Reload and reset cycle " << cycle+1 << " succeeded.\n";
            }
        }
#else
        std::cerr << "CUDA reset unavailable in CPU build\n"; return 5;
#endif
    }
    if (hold) {
        std::cerr << "Model freed; holding process alive for 20 seconds for residual-memory sampling.\n";
        Sleep(20000);
    }
    return 0;
}

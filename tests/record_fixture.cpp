// Test-only local microphone recorder. Nothing is uploaded.
#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_DSOUND
#define MA_NO_WINMM
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <vector>

struct Recording { std::mutex mutex; std::vector<float> pcm; };
static void callback(ma_device *device, void*, const void *input, ma_uint32 count) {
    if (!input) return;
    auto *r = static_cast<Recording*>(device->pUserData);
    std::lock_guard<std::mutex> lock(r->mutex);
    const float *p = static_cast<const float*>(input);
    r->pcm.insert(r->pcm.end(), p, p+count);
}
int wmain(int argc, wchar_t **argv) {
    if (argc != 3) { std::fprintf(stderr,"usage: HebENRecordFixture output.wav seconds\n"); return 2; }
    int seconds = _wtoi(argv[2]);
    if (seconds < 1 || seconds > 60 || GetFileAttributesW(argv[1]) != INVALID_FILE_ATTRIBUTES) return 2;
    Recording r; r.pcm.reserve(static_cast<size_t>(seconds+1)*16000);
    auto config = ma_device_config_init(ma_device_type_capture);
    config.capture.format=ma_format_f32; config.capture.channels=1; config.sampleRate=16000;
    config.dataCallback=callback; config.pUserData=&r;
    ma_device device{};
    if (ma_device_init(nullptr,&config,&device) != MA_SUCCESS) { std::fprintf(stderr,"Microphone init failed\n"); return 3; }
    std::fprintf(stderr,"Recording starts after a 5-second countdown and beep.\n");
    Sleep(5000); Beep(880,250);
    if (ma_device_start(&device) != MA_SUCCESS) { ma_device_uninit(&device); return 4; }
    Sleep(seconds*1000); ma_device_stop(&device); ma_device_uninit(&device); Beep(550,250);
    FILE *f=nullptr; if (_wfopen_s(&f,argv[1],L"wb") || !f) return 5;
    const uint32_t bytes=static_cast<uint32_t>(r.pcm.size()*2), riff=36+bytes, fmt=16, rate=16000, byteRate=32000;
    const uint16_t one=1, align=2, bits=16;
    fwrite("RIFF",1,4,f); fwrite(&riff,4,1,f); fwrite("WAVEfmt ",1,8,f); fwrite(&fmt,4,1,f);
    fwrite(&one,2,1,f); fwrite(&one,2,1,f); fwrite(&rate,4,1,f); fwrite(&byteRate,4,1,f);
    fwrite(&align,2,1,f); fwrite(&bits,2,1,f); fwrite("data",1,4,f); fwrite(&bytes,4,1,f);
    for (float v : r.pcm) { int16_t x=static_cast<int16_t>(std::clamp(v,-1.f,1.f)*32767.f); fwrite(&x,2,1,f); }
    bool ok = fclose(f) == 0;
    std::printf("samples=%zu duration_s=%.3f\n",r.pcm.size(),r.pcm.size()/16000.0);
    return ok ? 0 : 6;
}

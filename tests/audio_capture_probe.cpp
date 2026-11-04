// Test-only WASAPI capture probe: aggregates amplitude and frame counts only.
// It never saves audio or starts ASR, and does not change the default device.
#define NOMINMAX
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
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

struct State {std::atomic<unsigned long long> frames{0};std::atomic<int> peak{0};};
static void callback(ma_device *dev,void*,const void *input,ma_uint32 n) {
    if(!input)return;auto *s=static_cast<State*>(dev->pUserData);auto *v=static_cast<const float*>(input);
    int peak=0;for(ma_uint32 i=0;i<n;++i)if(std::isfinite(v[i]))peak=std::max(peak,static_cast<int>(std::min(1.f,std::abs(v[i]))*1000000));
    s->frames.fetch_add(n,std::memory_order_relaxed);int old=s->peak.load(std::memory_order_relaxed);
    while(peak>old&&!s->peak.compare_exchange_weak(old,peak,std::memory_order_relaxed)){}
}
int main(int argc,char **argv) {
    if(argc==2 && std::strcmp(argv[1],"--list")==0) {
        ma_context context{};auto rc=ma_context_init(nullptr,0,nullptr,&context);
        if(rc!=MA_SUCCESS){std::printf("{\"status\":\"FAIL_CONTEXT\",\"code\":%d}\n",rc);return 4;}
        ma_device_info *playback=nullptr,*capture=nullptr;ma_uint32 np=0,nc=0;
        rc=ma_context_get_devices(&context,&playback,&np,&capture,&nc);
        if(rc==MA_SUCCESS) {
            for(ma_uint32 i=0;i<np;++i){for(char *p=playback[i].name;*p;++p)if(*p=='"'||*p=='\\'||static_cast<unsigned char>(*p)<32)*p='?';
                std::printf("{\"type\":\"playback\",\"index\":%u,\"default\":%s,\"name\":\"%s\"}\n",i,playback[i].isDefault?"true":"false",playback[i].name);}
            for(ma_uint32 i=0;i<nc;++i){for(char *p=capture[i].name;*p;++p)if(*p=='"'||*p=='\\'||static_cast<unsigned char>(*p)<32)*p='?';
                std::printf("{\"type\":\"capture\",\"index\":%u,\"default\":%s,\"name\":\"%s\"}\n",i,capture[i].isDefault?"true":"false",capture[i].name);}
        }
        ma_context_uninit(&context);return rc==MA_SUCCESS?0:5;
    }
    State state;auto cfg=ma_device_config_init(ma_device_type_capture);cfg.capture.format=ma_format_f32;
    cfg.capture.channels=1;cfg.sampleRate=16000;cfg.dataCallback=callback;cfg.pUserData=&state;
    ma_device device{};auto init=ma_device_init(nullptr,&cfg,&device);
    if(init!=MA_SUCCESS){std::printf("{\"status\":\"FAIL_INIT\",\"code\":%d}\n",init);return 1;}
    ma_device_info info{};auto infoResult=ma_device_get_info(&device,ma_device_type_capture,&info);
    auto start=ma_device_start(&device);
    if(start!=MA_SUCCESS){ma_device_uninit(&device);std::printf("{\"status\":\"FAIL_START\",\"code\":%d}\n",start);return 2;}
    Sleep(3000);ma_device_stop(&device);
    auto frames=state.frames.load();auto peak=state.peak.load();auto rate=device.sampleRate;auto channels=device.capture.channels;
    auto internalRate=device.capture.internalSampleRate;auto internalChannels=device.capture.internalChannels;
    ma_device_uninit(&device);
    for(char *p=info.name;*p;++p)if(*p=='"'||*p=='\\'||static_cast<unsigned char>(*p)<32)*p='?';
    std::printf("{\"status\":\"%s\",\"frames\":%llu,\"output_rate\":%u,\"output_channels\":%u,\"device_rate\":%u,\"device_channels\":%u,\"peak_millionths\":%d,\"device_info_code\":%d,\"device_name\":\"%s\",\"audio_saved\":false}\n",
        frames?"PASS_PCM_CALLBACK":"FAIL_NO_FRAMES",frames,rate,channels,internalRate,internalChannels,peak,infoResult,info.name);
    return frames?0:3;
}

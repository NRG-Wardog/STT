// Offline-only comparison of whisper.cpp's native Silero VAD.
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include "whisper.h"
#include "../src/silero_stream.hpp"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int wmain(int argc,wchar_t **argv) {
    if(argc<3||argc>5) return 2;
    bool stress=argc==4 && std::wstring(argv[3])==L"stress100";
    bool extended=argc>=4 && std::wstring(argv[3])==L"stream600";
    bool streaming=stress || extended || (argc>=4 && std::wstring(argv[3])==L"stream");
    if(argc>=4&&!streaming)return 2;
    if(argc==5&&GetFileAttributesW(argv[4])!=INVALID_FILE_ATTRIBUTES)return 2;
    if(streaming)whisper_log_set([](ggml_log_level,const char*,void*){},nullptr);
    FILE *f=nullptr; if(_wfopen_s(&f,argv[2],L"rb")||!f) return 3;
    char tag[4]; uint32_t n=0; bool fmt=false,data=false; std::vector<float> pcm;
    if(fread(tag,1,4,f)!=4||memcmp(tag,"RIFF",4)||fread(&n,4,1,f)!=1||fread(tag,1,4,f)!=4||memcmp(tag,"WAVE",4)) return 4;
    while(fread(tag,1,4,f)==4 && fread(&n,4,1,f)==1) {
        if(n>512u*1024*1024) return 4;
        if(!memcmp(tag,"fmt ",4)) {
            std::vector<unsigned char> b(n); if(n<16||fread(b.data(),1,n,f)!=n) return 4;
            uint16_t type,channels,bits;uint32_t rate;
            memcpy(&type,b.data(),2);memcpy(&channels,b.data()+2,2);memcpy(&rate,b.data()+4,4);memcpy(&bits,b.data()+14,2);
            fmt=type==1&&channels==1&&rate==16000&&bits==16;
        } else if(!memcmp(tag,"data",4)) {
            if(n%2) return 4;std::vector<int16_t> raw(n/2);if(fread(raw.data(),1,n,f)!=n)return 4;
            for(auto x:raw)pcm.push_back(x/32768.f);data=true;
        } else if(fseek(f,static_cast<long>(n),SEEK_CUR)) return 4;
        if((n&1)&&fseek(f,1,SEEK_CUR))return 4;
    }
    fclose(f);if(!fmt||!data)return 4;
    FILE *model=nullptr;if(_wfopen_s(&model,argv[1],L"rb")||!model)return 5;
    whisper_model_loader loader{};loader.context=model;
    loader.read=[](void*c,void*p,size_t s){return fread(p,1,s,static_cast<FILE*>(c));};
    loader.eof=[](void*c){return feof(static_cast<FILE*>(c))!=0;};loader.close=[](void*){};
    auto cp=whisper_vad_default_context_params();cp.use_gpu=false;cp.n_threads=1;
    auto begin=std::chrono::steady_clock::now();auto *ctx=whisper_vad_init_with_params(&loader,cp);auto loaded=std::chrono::steady_clock::now();fclose(model);
    if(!ctx)return 6;
    if(stress) {
        size_t expected=0;
        for(int cycle=1;cycle<=100;++cycle) {
            if(cycle>1) {
                model=nullptr;if(_wfopen_s(&model,argv[1],L"rb")||!model)return 5;
                loader.context=model;ctx=whisper_vad_init_with_params(&loader,cp);fclose(model);if(!ctx)return 6;
            }
            int count=0;size_t emitted=0;
            heben::Detector detector(.013f,1200,15,[&](std::vector<float>&& x){++count;emitted+=x.size();},1.f,600);
            heben::SileroStream stream(ctx,detector);bool ok=true;
            for(size_t pos=0;pos<pcm.size()&&ok;){size_t block=std::min<size_t>(160,pcm.size()-pos);ok=stream.Feed(pcm.data()+pos,block);pos+=block;}
            if(ok)ok=stream.Finish();whisper_vad_free(ctx);ctx=nullptr;
            PROCESS_MEMORY_COUNTERS_EX mem{};mem.cb=sizeof(mem);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem),sizeof(mem));
            DWORD handles=0;GetProcessHandleCount(GetCurrentProcess(),&handles);
            std::printf("{\"cycle\":%d,\"segments\":%d,\"emitted_samples\":%zu,\"private_bytes\":%zu,\"working_set_bytes\":%zu,\"handles\":%lu}\n",cycle,count,emitted,mem.PrivateUsage,mem.WorkingSetSize,handles);
            if(!ok||count!=1||(cycle>1&&emitted!=expected))return 10;
            expected=emitted;
        }
        return 0;
    }
    if(streaming) {
        int count=0;size_t emitted=0;std::vector<float> output;
        heben::Detector detector(.013f,1200,15,[&](std::vector<float>&& x){++count;emitted+=x.size();if(argc==5)output.insert(output.end(),x.begin(),x.end());},1.f,extended?600:300);
        char thresholdEnv[32]{};float threshold=.5f;
        if(GetEnvironmentVariableA("HEBEN_VAD_TEST_THRESHOLD",thresholdEnv,sizeof(thresholdEnv))) {
            threshold=std::strtof(thresholdEnv,nullptr);
            if(threshold<=0.f||threshold>=1.f)return 2;
        }
        heben::SileroStream stream(ctx,detector,threshold);
        bool ok=true;
        for(size_t pos=0;pos<pcm.size()&&ok;){size_t block=std::min<size_t>(160,pcm.size()-pos);ok=stream.Feed(pcm.data()+pos,block);pos+=block;}
        if(ok)ok=stream.Finish();auto done=std::chrono::steady_clock::now();
        if(ok && argc==5) {
            FILE *out=nullptr;if(_wfopen_s(&out,argv[4],L"wb")||!out){whisper_vad_free(ctx);return 9;}
            uint32_t bytes=static_cast<uint32_t>(output.size()*2),riff=36+bytes,fmtSize=16,rate=16000,byteRate=32000;
            uint16_t one=1,align=2,bits=16;
            fwrite("RIFF",1,4,out);fwrite(&riff,4,1,out);fwrite("WAVEfmt ",1,8,out);fwrite(&fmtSize,4,1,out);
            fwrite(&one,2,1,out);fwrite(&one,2,1,out);fwrite(&rate,4,1,out);fwrite(&byteRate,4,1,out);
            fwrite(&align,2,1,out);fwrite(&bits,2,1,out);fwrite("data",1,4,out);fwrite(&bytes,4,1,out);
            for(float v:output){int16_t x=static_cast<int16_t>(std::clamp(v,-1.f,1.f)*32767.f);fwrite(&x,2,1,out);}
            if(fclose(out)!=0){whisper_vad_free(ctx);return 9;}
        }
        PROCESS_MEMORY_COUNTERS_EX mem{};mem.cb=sizeof(mem);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem),sizeof(mem));
        auto millis=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
        std::printf("{\"threshold\":%.3f,\"input_samples\":%zu,\"segments\":%d,\"emitted_samples\":%zu,\"quiet_end_samples\":%zu,\"model_load_ms\":%.3f,\"detection_ms\":%.3f,\"private_bytes\":%zu,\"working_set_bytes\":%zu}\n",threshold,pcm.size(),count,emitted,stream.QuietSamples(),millis(begin,loaded),millis(loaded,done),mem.PrivateUsage,mem.WorkingSetSize);
        whisper_vad_free(ctx);return ok?0:8;
    }
    auto p=whisper_vad_default_params();p.min_silence_duration_ms=1200;
    auto *segments=whisper_vad_segments_from_samples(ctx,p,pcm.data(),static_cast<int>(pcm.size()));auto done=std::chrono::steady_clock::now();
    if(!segments){whisper_vad_free(ctx);return 7;}
    int count=whisper_vad_segments_n_segments(segments);float duration=0;
    for(int i=0;i<count;++i)duration+=whisper_vad_segments_get_segment_t1(segments,i)-whisper_vad_segments_get_segment_t0(segments,i);
    PROCESS_MEMORY_COUNTERS_EX mem{};mem.cb=sizeof(mem);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem),sizeof(mem));
    auto millis=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
    std::printf("{\"input_samples\":%zu,\"segments\":%d,\"speech_seconds\":%.4f,\"model_load_ms\":%.3f,\"detection_ms\":%.3f,\"private_bytes\":%zu,\"working_set_bytes\":%zu}\n",pcm.size(),count,duration,millis(begin,loaded),millis(loaded,done),mem.PrivateUsage,mem.WorkingSetSize);
    whisper_vad_free_segments(segments);whisper_vad_free(ctx);return 0;
}

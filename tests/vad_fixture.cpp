// Exercise the real segmenter against a local 16 kHz PCM WAV fixture.
#include "../src/utterance_detector.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
int wmain(int argc, wchar_t **argv) {
    if (argc < 3 || argc > 5) return 2;
    FILE *f=nullptr; if (_wfopen_s(&f,argv[1],L"rb") || !f) return 3;
    char tag[4]; uint32_t n=0; bool fmt=false, data=false; std::vector<float> pcm;
    if (fread(tag,1,4,f)!=4 || memcmp(tag,"RIFF",4) || fread(&n,4,1,f)!=1 ||
        fread(tag,1,4,f)!=4 || memcmp(tag,"WAVE",4)) return 4;
    while (fread(tag,1,4,f)==4 && fread(&n,4,1,f)==1) {
        if (n>512u*1024*1024) return 4;
        if (!memcmp(tag,"fmt ",4)) {
            std::vector<unsigned char> b(n); if(n<16 || fread(b.data(),1,n,f)!=n) return 4;
            uint16_t encoding,channels,bits; uint32_t rate;
            memcpy(&encoding,b.data(),2); memcpy(&channels,b.data()+2,2); memcpy(&rate,b.data()+4,4); memcpy(&bits,b.data()+14,2);
            fmt=encoding==1 && channels==1 && rate==16000 && bits==16;
        } else if (!memcmp(tag,"data",4)) {
            if (n%2) return 4; std::vector<int16_t> raw(n/2); if(fread(raw.data(),1,n,f)!=n) return 4;
            for(auto x:raw) pcm.push_back(x/32768.f); data=true;
        } else if(fseek(f,static_cast<long>(n),SEEK_CUR)) return 4;
        if ((n&1) && fseek(f,1,SEEK_CUR)) return 4;
    }
    fclose(f); if(!fmt || !data) return 4;
    float threshold=static_cast<float>(_wtof(argv[2])); if(threshold<=0) return 2;
    int silenceMs=argc>=4 ? _wtoi(argv[3]) : 700;
    if (silenceMs < 20 || silenceMs > 10000) return 2;
    float maxCrossing=argc==5 ? static_cast<float>(_wtof(argv[4])) : 1.f;
    if (maxCrossing <= 0.f || maxCrossing > 1.f) return 2;
    int emitted=0; size_t total=0;
    heben::Detector d(threshold,silenceMs,15,[&](std::vector<float>&& x){++emitted;total+=x.size();},maxCrossing);
    for(size_t pos=0;pos<pcm.size();) { size_t count=std::min<size_t>(160,pcm.size()-pos); d.Feed(pcm.data()+pos,count); pos+=count; }
    d.Finish();
    std::printf("{\"threshold\":%.6f,\"silence_ms\":%d,\"max_crossing\":%.3f,\"input_samples\":%zu,\"phrases\":%d,\"emitted_samples\":%zu}\n",threshold,silenceMs,maxCrossing,pcm.size(),emitted,total);
    return 0;
}

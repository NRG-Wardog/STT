// Unit-test the independent audio segmenter on any OS, no microphone required.
#include "../src/utterance_detector.hpp"
#include "../src/audio_ring.hpp"
#include "../src/insertion_policy.hpp"
// The build runs this test in Release too: keep checks enabled with NDEBUG.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

static std::vector<float> Tone(int millis, float amp) {
    const int n = millis * heben::SAMPLE_RATE/1000;
    std::vector<float> data(static_cast<size_t>(n));
    for (int i=0; i<n; ++i) data[i]=amp*std::sin(2.0*3.141592653589793*440.0*i/heben::SAMPLE_RATE);
    return data;
}

int main() {
    {
        int starts=0,finals=0;
        heben::Detector d(.013f,700,15,[&](std::vector<float>&&){++finals;});
        d.SetSpeechStart([&]{++starts;});
        auto quiet=Tone(1000,0.f),voice=Tone(400,.1f),pause=Tone(800,0.f);
        d.FeedClassified(quiet.data(),quiet.size(),false);assert(starts==0);
        d.FeedClassified(voice.data(),voice.size(),true);assert(starts==1);
        d.FeedClassified(pause.data(),pause.size(),false);assert(finals==1);
        d.FeedClassified(voice.data(),voice.size(),true);assert(starts==2);
        d.Finish();assert(finals==2);
    }
    {
        int previews=0,finals=0;std::uint64_t first=0,finalId=0;
        heben::Detector *detector=nullptr;
        heben::Detector d(.013f,700,15,[&](std::vector<float>&&){++finals;finalId=detector->SegmentId();});
        detector=&d;
        d.SetPreview(1000,[&](std::vector<float>&& pcm,std::uint64_t id){
            ++previews;if(!first)first=id;assert(id==first);assert(pcm.size()>=heben::SAMPLE_RATE);
        });
        auto voice=Tone(3200,.1f),pause=Tone(800,0.f);
        d.FeedClassified(voice.data(),voice.size(),true);d.FeedClassified(pause.data(),pause.size(),false);
        assert(previews>=3 && finals==1 && first==finalId);
        d.Finish();assert(finals==1);
    }
    {
        using D=heben::InsertionDecision;
        assert(heben::DecideInsertion(false,false,false)==D::CancelFocus);
        assert(heben::DecideInsertion(false,true,true)==D::CancelFocus);
        assert(heben::DecideInsertion(true,true,false)==D::WaitForModifiers);
        assert(heben::DecideInsertion(true,true,true)==D::CancelTimeout);
        assert(heben::DecideInsertion(true,false,true)==D::Send);
    }
    {
        heben::AudioRing<5> ring;
        const float input[]={1,2,3,4,5,6,7,8}; float out[5]{};
        assert(ring.Push(input,4)); assert(ring.Pop(out,3)==3);
        assert(out[0]==1 && out[1]==2 && out[2]==3);
        assert(ring.Push(input+4,4)); assert(ring.Size()==5);
        assert(!ring.Push(input,1)); assert(ring.Size()==5);
        assert(ring.Pop(out,5)==5);
        assert(out[0]==4 && out[1]==5 && out[2]==6 && out[3]==7 && out[4]==8);
        ring.Reset(); assert(ring.Size()==0);
    }
    {
        int emitted=0;
        int lastSize=0;
        heben::Detector detector(.013f, 700, 15, [&](std::vector<float>&& pcm){
            ++emitted;
            lastSize=static_cast<int>(pcm.size());
        });
        auto soft = Tone(500,.005f);
        detector.Feed(soft.data(),soft.size());
        auto speech = Tone(800,.1f);
        detector.Feed(speech.data(),speech.size());
        auto pause = Tone(750,.0f);
        detector.Feed(pause.data(),pause.size());
        assert(emitted == 1);
        assert(lastSize >= heben::SAMPLE_RATE);
        detector.Finish();
        assert(emitted == 1);
    }
    {
        int emitted=0;
        heben::Detector detector(.013f, 700, 15, [&](std::vector<float>&&){++emitted;});
        auto shortClick=Tone(100,.1f);
        detector.Feed(shortClick.data(),shortClick.size());
        detector.Finish();
        assert(emitted == 0);
    }
    {
        int emitted=0;
        heben::Detector d(.013f,1200,15,[&](std::vector<float>&&){++emitted;});
        auto quiet=Tone(4000,0.f),voice=Tone(200,.1f),almost=Tone(4980,0.f),extra=Tone(20,0.f);
        d.FeedClassified(quiet.data(),quiet.size(),false);assert(d.QuietSamples()==64000);
        d.FeedClassified(voice.data(),voice.size(),true);assert(d.QuietSamples()==0);
        d.FeedClassified(almost.data(),almost.size(),false);assert(d.QuietSamples()==79680);
        d.FeedClassified(extra.data(),extra.size(),false);assert(d.QuietSamples()==80000);
        d.Finish();assert(emitted==1);
    }
    {
        // A two-second pause must not split speech with a five-second endpoint.
        int emitted=0;size_t size=0;
        heben::Detector d(.013f,5000,15,[&](std::vector<float>&& pcm){++emitted;size=pcm.size();});
        auto voice=Tone(300,.1f),pause=Tone(2000,0.f);
        d.FeedClassified(voice.data(),voice.size(),true);
        d.FeedClassified(pause.data(),pause.size(),false);
        assert(emitted==0);
        d.FeedClassified(voice.data(),voice.size(),true);
        d.Finish();assert(emitted==1 && size>=voice.size()*2+pause.size());
    }
    {
        int unfiltered=0, filtered=0;
        heben::Detector a(.013f,700,15,[&](std::vector<float>&&){++unfiltered;});
        heben::Detector b(.013f,700,15,[&](std::vector<float>&&){++filtered;},.35f);
        std::vector<float> alternating(heben::SAMPLE_RATE);
        for(size_t i=0;i<alternating.size();++i) alternating[i]=(i%2 ? .1f : -.1f);
        a.Feed(alternating.data(),alternating.size()); a.Finish();
        b.Feed(alternating.data(),alternating.size()); b.Finish();
        assert(unfiltered==1 && filtered==0);
    }
    {
        int emitted=0;
        heben::Detector detector(.013f, 700, 3, [&](std::vector<float>&& pcm){
            assert(!pcm.empty());
            ++emitted;
        });
        auto speech=Tone(4400,.10f);
        detector.Feed(speech.data(),speech.size());
        detector.Finish();
        assert(emitted >= 1);
    }
    {
        // Audio callback block sizes are not fixed; segmentation must not
        // depend on where an input stream is split into chunks.
        auto pre = Tone(400, 0.f);
        auto speech = Tone(600, .1f);
        auto pause = Tone(800, 0.f);
        std::vector<float> audio;
        audio.insert(audio.end(), pre.begin(), pre.end());
        audio.insert(audio.end(), speech.begin(), speech.end());
        audio.insert(audio.end(), pause.begin(), pause.end());
        std::vector<std::vector<float>> whole, chunked;
        heben::Detector a(.013f, 700, 15, [&](std::vector<float>&& pcm){ whole.push_back(std::move(pcm)); });
        heben::Detector b(.013f, 700, 15, [&](std::vector<float>&& pcm){ chunked.push_back(std::move(pcm)); });
        a.Feed(audio.data(), audio.size()); a.Finish();
        for (size_t pos=0; pos<audio.size();) {
            size_t count=std::min<size_t>(137, audio.size()-pos);
            b.Feed(audio.data()+pos, count); pos+=count;
        }
        b.Finish();
        assert(whole.size() == 1 && whole == chunked);
    }
    {
        // Classified chunks can straddle the segmenter's 20 ms frame boundary.
        auto speech=Tone(600,.1f), silence=Tone(1400,0.f);
        std::vector<std::vector<float>> aOut,bOut;
        heben::Detector a(.013f,1200,15,[&](std::vector<float>&& x){aOut.push_back(std::move(x));});
        heben::Detector b(.013f,1200,15,[&](std::vector<float>&& x){bOut.push_back(std::move(x));});
        a.FeedClassified(speech.data(),speech.size(),true);a.FeedClassified(silence.data(),silence.size(),false);a.Finish();
        for(size_t p=0;p<speech.size();) {size_t n=std::min<size_t>(137,speech.size()-p);b.FeedClassified(speech.data()+p,n,true);p+=n;}
        for(size_t p=0;p<silence.size();) {size_t n=std::min<size_t>(137,silence.size()-p);b.FeedClassified(silence.data()+p,n,false);p+=n;}
        b.Finish(); assert(aOut.size()==1 && aOut==bOut);
    }
    {
        int emitted=0;
        heben::Detector detector(.013f, 700, 15, [&](std::vector<float>&&){ ++emitted; });
        auto silence=Tone(3000, 0.f);
        detector.Feed(silence.data(), silence.size()); detector.Finish();
        assert(emitted == 0);
    }
    std::cout << "Detector tests passed (speech / silence / short click / max length).\n";
}

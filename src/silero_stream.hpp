#pragma once
#include "utterance_detector.hpp"
#include "whisper.h"
#include <algorithm>
#include <array>
#include <cstddef>

namespace heben {
// For the pinned Silero 6.2 16 kHz model, whisper.cpp uses 512-sample windows.
// This class must run off the audio callback and is owned by one thread.
class SileroStream {
public:
    SileroStream(whisper_vad_context *context, Detector &detector, float threshold=.5f)
        : context_(context), detector_(detector), threshold_(threshold) {
        // The streaming API intentionally does not reset its recurrent state.
        // A freshly allocated backend buffer is not guaranteed to be zeroed.
        whisper_vad_reset_state(context_);
    }
    bool Feed(const float *samples,size_t count) {
        while(count) {
            size_t n=std::min(count,window_.size()-used_);
            std::copy_n(samples,n,window_.data()+used_); used_+=n;samples+=n;count-=n;
            if(used_==window_.size()) { if(!Process(used_))return false;used_=0; }
        }
        return true;
    }
    bool Finish() {
        if(used_ && !Process(used_))return false;
        used_=0;detector_.Finish();return true;
    }
    size_t Frames() const { return frames_; }
    size_t VoicedFrames() const { return voicedFrames_; }
    size_t QuietSamples() const { return quietSamples_; }
    bool LastVoiced() const { return lastVoiced_; }
private:
    bool Process(size_t count) {
        if(!whisper_vad_detect_speech_no_reset(context_,window_.data(),static_cast<int>(count)))return false;
        if(whisper_vad_n_probs(context_)!=1)return false;
        const float *p=whisper_vad_probs(context_);
        if(!p)return false;
        bool voiced=p[0]>=threshold_;
        lastVoiced_=voiced;
        ++frames_;if(voiced)++voicedFrames_;
        if(voiced)quietSamples_=0;else quietSamples_+=count;
        detector_.FeedClassified(window_.data(),count,voiced);
        return true;
    }
    whisper_vad_context *context_;
    Detector &detector_;
    float threshold_;
    std::array<float,512> window_{};
    size_t used_=0;
    size_t frames_=0,voicedFrames_=0;
    size_t quietSamples_=0;
    bool lastVoiced_=false;
};
}

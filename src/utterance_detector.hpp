#pragma once
// Local 20ms RMS-based utterance segmentation; deliberately dependency-free.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <deque>
#include <functional>
#include <cstdint>
#include <utility>
#include <vector>

namespace heben {
inline constexpr int SAMPLE_RATE = 16000;
inline constexpr int FRAME_SIZE = 320;

// Ultra-light utterance segmentation: 20ms RMS energy gate, 300ms pre-roll,
// silence cutoff, and max phrase length. No neural VAD is loaded.
class Detector {
public:
    using OnPhrase = std::function<void(std::vector<float>&&)>;
    using OnPreview = std::function<void(std::vector<float>&&,std::uint64_t)>;
    using OnSpeechStart = std::function<void()>;
    Detector(float threshold, int silenceMs, int maxSeconds, OnPhrase cb, float maxZeroCrossing = 1.0f, int prerollMs = 300)
      : threshold_(threshold), silenceFrames_(std::max(1, silenceMs/20)),
        maxFrames_(maxSeconds*50), callback_(std::move(cb)), maxZeroCrossing_(maxZeroCrossing),
        prerollSamples_(static_cast<size_t>(SAMPLE_RATE*std::clamp(prerollMs,0,1500)/1000)) {
        active_.reserve(SAMPLE_RATE*maxSeconds + 16000);
    }
    void SetPreview(unsigned intervalMs,OnPreview cb) {
        previewFrames_=intervalMs?std::max(1u,intervalMs/20):0;
        previewCallback_=std::move(cb);
    }
    std::uint64_t SegmentId() const {return segmentId_;}
    void SetSpeechStart(OnSpeechStart cb) {speechStartCallback_=std::move(cb);}
    void Feed(const float* samples, size_t count) {
        for (size_t i=0;i<count;++i) {
            frame_[position_++] = samples[i];
            if (position_ == FRAME_SIZE) { ProcessFrame(false, false); position_ = 0; }
        }
    }
    // Neural VAD can classify a chunk independently of this segmenter's
    // 20 ms framing. Majority voting handles boundaries between the two.
    void FeedClassified(const float* samples, size_t count, bool voiced) {
        classifiedMode_ = true;
        for (size_t i=0;i<count;++i) {
            frame_[position_++] = samples[i];
            if (voiced) ++classifiedVoiced_;
            if (position_ == FRAME_SIZE) {
                ProcessFrame(true, classifiedVoiced_ >= FRAME_SIZE/2);
                position_ = 0; classifiedVoiced_ = 0;
            }
        }
    }
    void Finish() {
        if (position_ > 0) {
            while (position_ < FRAME_SIZE) frame_[position_++] = 0;
            ProcessFrame(classifiedMode_, classifiedVoiced_ >= FRAME_SIZE/2); position_ = 0; classifiedVoiced_ = 0;
        }
        Flush();
    }
    size_t QuietSamples() const { return quietSamples_; }
private:
    void ProcessFrame(bool classified, bool classifiedVoiced) {
        bool voiced=classifiedVoiced;
        if (!classified) {
            double sum = 0;
            int crossings = 0;
            for (size_t i=0;i<frame_.size();++i) {
                float v=frame_[i]; sum += static_cast<double>(v)*v;
                if (i && ((v < 0) != (frame_[i-1] < 0))) ++crossings;
            }
            voiced = std::sqrt(sum/FRAME_SIZE) >= threshold_ &&
                     crossings <= maxZeroCrossing_ * (FRAME_SIZE-1);
        }
        if(voiced)quietSamples_=0;else quietSamples_+=FRAME_SIZE;
        if (!speaking_) {
            for (float v: frame_) preroll_.push_back(v);
            while (preroll_.size() > prerollSamples_) preroll_.pop_front();
            if (voiced) ++startFrames_;
            else startFrames_ = 0;
            if (startFrames_ >= 2) {
                ++segmentId_;
                speaking_ = true;
                active_.assign(preroll_.begin(), preroll_.end());
                speechFrames_ = startFrames_;
                quietFrames_ = 0;
                preroll_.clear();
                lastPreviewFrame_=0;
                if(speechStartCallback_)speechStartCallback_();
            }
        } else {
            active_.insert(active_.end(), frame_.begin(), frame_.end());
            if (voiced) { ++speechFrames_; quietFrames_ = 0; }
            else ++quietFrames_;
            if (quietFrames_ >= silenceFrames_ ||
                static_cast<int>(active_.size()/FRAME_SIZE) >= maxFrames_) Flush();
            else if(previewCallback_ && previewFrames_ && speechFrames_>=8 &&
                    active_.size()/FRAME_SIZE>=lastPreviewFrame_+previewFrames_) {
                lastPreviewFrame_=static_cast<unsigned>(active_.size()/FRAME_SIZE);
                previewCallback_(std::vector<float>(active_),segmentId_);
            }
        }
    }
    void Flush() {
        // At least 160ms of non-silent frames, excluding the pre-roll.
        if (speaking_ && speechFrames_ >= 8) callback_(std::move(active_));
        active_.clear();
        speaking_ = false;
        startFrames_ = 0;
        quietFrames_ = 0;
        speechFrames_ = 0;
        preroll_.clear();
    }
    float threshold_;
    int silenceFrames_;
    int maxFrames_;
    OnPhrase callback_;
    float maxZeroCrossing_;
    size_t prerollSamples_;
    std::array<float,FRAME_SIZE> frame_{};
    size_t position_ = 0;
    size_t classifiedVoiced_ = 0;
    bool classifiedMode_ = false;
    size_t quietSamples_ = 0;
    std::deque<float> preroll_;
    std::vector<float> active_;
    bool speaking_ = false;
    OnPreview previewCallback_;
    OnSpeechStart speechStartCallback_;
    unsigned previewFrames_=0,lastPreviewFrame_=0;
    std::uint64_t segmentId_=0;
    int startFrames_ = 0, quietFrames_ = 0, speechFrames_ = 0;
};


} // namespace heben

#pragma once
#include <algorithm>
#include <array>
#include <cstddef>

namespace heben {
// Bounded FIFO. Caller supplies synchronization when shared between threads.
template<size_t Capacity> class AudioRing {
public:
    static_assert(Capacity>0,"Audio ring must have capacity");
    void Reset() { read_=write_=count_=0; }
    size_t Size() const { return count_; }
    bool Push(const float *input,size_t n) {
        if(!n)return true;
        if(!input)return false;
        if(n>Capacity-count_)return false;
        size_t first=std::min(n,Capacity-write_);
        std::copy_n(input,first,data_.data()+write_);
        std::copy_n(input+first,n-first,data_.data());
        write_=(write_+n)%Capacity;count_+=n;return true;
    }
    size_t Pop(float *output,size_t capacity) {
        if(!capacity || !output)return 0;
        size_t n=std::min(count_,capacity),first=std::min(n,Capacity-read_);
        std::copy_n(data_.data()+read_,first,output);
        std::copy_n(data_.data(),n-first,output+first);
        read_=(read_+n)%Capacity;count_-=n;return n;
    }
private:
    std::array<float,Capacity> data_{};
    size_t read_=0,write_=0,count_=0;
};
}

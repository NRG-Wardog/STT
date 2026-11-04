#pragma once
#include <cstddef>
#include <deque>
#include <string>
#include <utility>

namespace heben {
struct RetainedSegment { std::wstring text; bool mayBePartial=false; };

// Unsent segments are kept in memory, in arrival order, until the user
// explicitly copies them. A failed copy must not discard the segment.
class RetainedText {
public:
    void Add(std::wstring text,bool mayBePartial) {
        if(!text.empty())items_.push_back({std::move(text),mayBePartial});
    }
    const RetainedSegment *Front() const { return items_.empty()?nullptr:&items_.front(); }
    std::size_t Size() const { return items_.size(); }
    template<class Copy> bool CopyNext(Copy copy) {
        if(items_.empty() || !copy(items_.front().text))return false;
        items_.pop_front();return true;
    }
private:
    std::deque<RetainedSegment> items_;
};
}

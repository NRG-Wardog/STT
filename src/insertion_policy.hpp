#pragma once
namespace heben {
enum class InsertionDecision { CancelFocus, WaitForModifiers, CancelTimeout, Send };
inline InsertionDecision DecideInsertion(bool focusValid,bool modifiersHeld,bool expired) {
    if(!focusValid)return InsertionDecision::CancelFocus;
    if(!modifiersHeld)return InsertionDecision::Send;
    return expired?InsertionDecision::CancelTimeout:InsertionDecision::WaitForModifiers;
}
}

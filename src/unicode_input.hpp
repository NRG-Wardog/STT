#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace heben {
// SendInput acceptance only establishes that Windows queued the events; it
// does not prove that a particular editor accepted the resulting text.
struct UnicodeDispatchResult {
    UINT requested=0;
    UINT accepted=0;
    DWORD error=0;
    bool validUtf16=true;
    bool safeText=true;
    bool preflightPassed=true;
    bool allQueued() const { return validUtf16 && safeText && preflightPassed && accepted==requested; }
};
inline bool IsWellFormedUtf16(const std::wstring &text) {
    for(size_t i=0;i<text.size();++i) {
        unsigned c=static_cast<unsigned>(text[i]);
        if(c>=0xd800 && c<=0xdbff) {
            if(++i==text.size())return false;
            unsigned low=static_cast<unsigned>(text[i]);
            if(low<0xdc00 || low>0xdfff)return false;
        } else if(c>=0xdc00 && c<=0xdfff)return false;
    }
    return true;
}
inline bool IsSafeKeyboardText(const std::wstring &text) {
    for(wchar_t c:text)
        if(c<0x20 || c==0x7f || c==0x85 || c==0x2028 || c==0x2029)return false;
    return true;
}
inline std::vector<INPUT> UnicodeKeyEvents(const std::wstring &text) {
    std::vector<INPUT> inputs;inputs.reserve(text.size()*2);
    for(wchar_t c:text) {
        INPUT down{};down.type=INPUT_KEYBOARD;down.ki.wScan=static_cast<WORD>(c);down.ki.dwFlags=KEYEVENTF_UNICODE;
        INPUT up=down;up.ki.dwFlags|=KEYEVENTF_KEYUP;
        inputs.push_back(down);inputs.push_back(up);
    }
    return inputs;
}
// Native keyboard backend. Never reads or modifies the clipboard and never
// synthesizes Enter or Tab; callers must sanitize controls first.
template<class Preflight,class Sender,class Wait>
inline UnicodeDispatchResult DispatchUnicodeKeyboardWith(const std::wstring &text,Preflight preflight,unsigned delayMs,Sender sender,Wait wait) {
    UnicodeDispatchResult result;
    if(!IsWellFormedUtf16(text)) {
        result.validUtf16=false;result.error=ERROR_NO_UNICODE_TRANSLATION;return result;
    }
    if(!IsSafeKeyboardText(text)) {
        result.safeText=false;result.error=ERROR_INVALID_DATA;return result;
    }
    auto inputs=UnicodeKeyEvents(text);
    result.requested=static_cast<UINT>(inputs.size());
    if(inputs.empty())return result;
    if(delayMs==0) {
        // Windows cannot make focus and delivery atomic; acceptance still
        // needs verification by the caller.
        if(!preflight()) {result.preflightPassed=false;result.error=ERROR_CANCELLED;return result;}
        SetLastError(0);
        result.accepted=sender(result.requested,inputs.data(),sizeof(INPUT));
        result.error=GetLastError();
        return result;
    }
    // Experimental paced mode: recheck the target before each UTF-16 unit.
    // It is opt-in and may be slow for long text; no global timer is used.
    for(UINT i=0;i<result.requested;i+=2) {
        if(!preflight()) {result.preflightPassed=false;result.error=ERROR_CANCELLED;break;}
        SetLastError(0);UINT sent=sender(2,inputs.data()+i,sizeof(INPUT));DWORD error=GetLastError();
        result.accepted+=sent;
        if(sent!=2){result.error=error;break;}
        if(i+2<result.requested)wait(delayMs);
    }
    return result;
}
template<class Preflight>
inline UnicodeDispatchResult DispatchUnicodeKeyboard(const std::wstring &text,Preflight preflight,unsigned delayMs=0) {
    return DispatchUnicodeKeyboardWith(text,preflight,delayMs,
        [](UINT count,INPUT *input,int size){return SendInput(count,input,size);},
        [](unsigned ms){Sleep(ms);});
}
inline UnicodeDispatchResult DispatchUnicodeKeyboard(const std::wstring &text) {
    return DispatchUnicodeKeyboard(text,[]{return true;});
}
inline void SanitizeDictationControls(std::wstring &text) {
    std::wstring out;out.reserve(text.size());
    for(auto ch:text) {
        if(ch<0x20 || ch==0x7f || ch==0x85 || ch==0xa0 || ch==0x1680 ||
           (ch>=0x2000 && ch<=0x200a) || ch==0x2028 || ch==0x2029 ||
           ch==0x202f || ch==0x205f || ch==0x3000)ch=L' ';
        if(ch==L' ' && (out.empty() || out.back()==L' '))continue;
        out.push_back(ch);
    }
    if(!out.empty() && out.back()==L' ')out.pop_back();
    text.swap(out);
}
inline std::wstring JoinDictationPhrase(const std::wstring &text,bool followsOwnPhrase,wchar_t previous) {
    if(text.empty())return text;
    auto closes=[](wchar_t c){return c==L'.'||c==L','||c==L'!'||c==L'?'||c==L';'||c==L':'||c==L')'||c==L']'||c==L'}'||c==0x05c3;};
    auto opens=[](wchar_t c){return c==L'('||c==L'['||c==L'{';};
    if(followsOwnPhrase && previous!=L' ' && !opens(previous) && !closes(text.front()))return L" "+text;
    return text;
}
}

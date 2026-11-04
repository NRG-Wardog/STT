#include "../src/unicode_input.hpp"
#include "../src/retained_text.hpp"
#include "../src/terminal_trust.hpp"
#include "../src/input_mutation_guard.hpp"
#include "../src/clipboard_policy.hpp"
#include "../src/paste_input.hpp"
#include "../src/clipboard_text_transaction.hpp"
#include <cstdio>
int main() {
    if(heben::IsPhysicalEditingKey('A',LLKHF_INJECTED,false) ||
       heben::IsPhysicalEditingKey(VK_SHIFT,0,false) ||
       heben::IsPhysicalEditingKey('R',0,true) ||
       !heben::IsPhysicalEditingKey(VK_LEFT,0,false) ||
       !heben::IsPhysicalMousePress(WM_LBUTTONDOWN,0) ||
       heben::IsPhysicalMousePress(WM_LBUTTONDOWN,LLMHF_INJECTED) ||
       heben::IsPhysicalMousePress(WM_MOUSEMOVE,0))return 38;
    {
        heben::InputMutationGuard guard;
        if(!guard.Start('R',MOD_CONTROL|MOD_SHIFT,[]{}))return 39;
        guard.Stop();
    }
    const std::wstring text=L"\x05e9\x05dc\x05d5\x05dd, hello! \U0001f642";
    auto events=heben::UnicodeKeyEvents(text);
    if(events.size()!=text.size()*2)return 1;
    for(size_t i=0;i<text.size();++i) {
        const auto &down=events[2*i],&up=events[2*i+1];
        if(down.type!=INPUT_KEYBOARD || up.type!=INPUT_KEYBOARD || down.ki.wVk || up.ki.wVk ||
           down.ki.wScan!=static_cast<WORD>(text[i]) || up.ki.wScan!=down.ki.wScan ||
           down.ki.dwFlags!=KEYEVENTF_UNICODE || up.ki.dwFlags!=(KEYEVENTF_UNICODE|KEYEVENTF_KEYUP))return 2;
    }
    std::wstring controls=L"hello\r\nworld\t!\x2028";
    heben::SanitizeDictationControls(controls);
    if(controls!=L"hello world !")return 3;
    if(heben::JoinDictationPhrase(L"hello",false,0)!=L"hello")return 4;
    if(heben::JoinDictationPhrase(L"world",true,L'o')!=L" world")return 5;
    if(heben::JoinDictationPhrase(L".",true,L'o')!=L".")return 6;
    if(heben::JoinDictationPhrase(L"world",true,L'(')!=L"world")return 7;
    std::wstring first=L"\x05e9\x05dc\x05d5\x05dd",second=L"\x05e2\x05d5\x05dc\x05dd";
    if(heben::JoinDictationPhrase(first,false,0)+heben::JoinDictationPhrase(second,true,first.back())!=first+L" "+second)return 8;
    std::wstring pauses=L"  hello   world \t \r\n ";
    heben::SanitizeDictationControls(pauses);
    if(pauses!=L"hello world")return 9;
    std::wstring unicodeSpaces=L"\u00a0hello\u2003\u202fworld\u3000";
    heben::SanitizeDictationControls(unicodeSpaces);
    if(unicodeSpaces!=L"hello world")return 10;
    std::wstring onlySpaces=L" \t\r\n ";
    heben::SanitizeDictationControls(onlySpaces);
    if(!onlySpaces.empty() || !heben::JoinDictationPhrase(onlySpaces,true,L'o').empty())return 11;
    const std::wstring fixed=L"\u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd Hello World 123";
    if(!heben::IsWellFormedUtf16(fixed) || !heben::IsSafeKeyboardText(fixed))return 12;
    auto fixedEvents=heben::UnicodeKeyEvents(fixed);
    if(fixedEvents.size()!=fixed.size()*2)return 13;
    if(!heben::IsWellFormedUtf16(L"\U0001f642"))return 14;
    if(heben::IsWellFormedUtf16(std::wstring(1,static_cast<wchar_t>(0xd800))) ||
       heben::IsWellFormedUtf16(std::wstring(1,static_cast<wchar_t>(0xdc00))))return 15;
    auto invalid=heben::DispatchUnicodeKeyboard(std::wstring(1,static_cast<wchar_t>(0xd800)));
    if(invalid.allQueued() || invalid.validUtf16 || invalid.requested!=0)return 16;
    auto controlsRejected=heben::DispatchUnicodeKeyboard(L"never\nsubmit\tthis");
    if(controlsRejected.allQueued() || controlsRejected.safeText || controlsRejected.requested!=0)return 17;
    auto preflightRejected=heben::DispatchUnicodeKeyboard(fixed,[]{return false;});
    if(preflightRejected.allQueued() || preflightRejected.preflightPassed || preflightRejected.accepted!=0 ||
       preflightRejected.requested!=fixed.size()*2)return 22;
    auto pacedRejected=heben::DispatchUnicodeKeyboard(fixed,[]{return false;},5);
    if(pacedRejected.allQueued() || pacedRejected.preflightPassed || pacedRejected.accepted!=0 ||
       pacedRejected.requested!=fixed.size()*2)return 23;
    unsigned preflights=0,calls=0,waits=0;std::vector<INPUT> observed;
    auto paced=heben::DispatchUnicodeKeyboardWith(L"ABCD",[&]{return ++preflights<=2;},5,
        [&](UINT n,INPUT *p,int){++calls;observed.insert(observed.end(),p,p+n);return n;},
        [&](unsigned){++waits;});
    if(paced.allQueued() || paced.preflightPassed || paced.requested!=8 || paced.accepted!=4 ||
       calls!=2 || waits!=2 || observed.size()!=4 || observed[0].ki.wScan!=L'A' || observed[2].ki.wScan!=L'B')return 24;
    calls=0;waits=0;
    auto partial=heben::DispatchUnicodeKeyboardWith(L"ABC",[]{return true;},5,
        [&](UINT,INPUT *,int){++calls;return calls==1?2u:1u;},[&](unsigned){++waits;});
    if(partial.allQueued() || partial.accepted!=3 || calls!=2 || waits!=1)return 25;
    heben::RetainedText retained;retained.Add(L"first",false);retained.Add(L"second",true);
    if(retained.Size()!=2 || !retained.Front() || retained.Front()->text!=L"first")return 18;
    if(retained.CopyNext([](const std::wstring &){return false;}) || retained.Size()!=2)return 19;
    std::wstring copied;
    if(!retained.CopyNext([&](const std::wstring &s){copied=s;return true;}) || copied!=L"first" ||
       !retained.Front() || retained.Front()->text!=L"second" || !retained.Front()->mayBePartial)return 20;
    if(!retained.CopyNext([&](const std::wstring &s){copied=s;return true;}) || copied!=L"second" || retained.Size()!=0)return 21;
    if(heben::TrustedTerminalTarget(false,L"WindowsTerminal.exe",UIA_TextControlTypeId,true,false))return 26;
    if(!heben::TrustedTerminalTarget(true,L"WindowsTerminal.exe",UIA_TextControlTypeId,true,false))return 27;
    if(!heben::TrustedTerminalTarget(true,L"WindowsTerminal.exe",UIA_TextControlTypeId,false,true))return 28;
    if(heben::TrustedTerminalTarget(true,L"WindowsTerminal.exe",UIA_TextControlTypeId,true,true))return 29;
    if(!heben::TrustedTerminalTarget(true,L"Code.exe",UIA_TextControlTypeId,true,false))return 30;
    if(heben::TrustedTerminalTarget(true,L"chrome.exe",UIA_TextControlTypeId,true,false) ||
       !heben::TrustedTerminalTarget(true,L"WindowsTerminal.exe",UIA_EditControlTypeId,true,false) ||
       heben::TrustedTerminalTarget(true,L"Code.exe",UIA_EditControlTypeId,true,false))return 31;
    if(!heben::TrustedTerminalTarget(true,L"OpenConsole.exe",UIA_DocumentControlTypeId,false,false) ||
       !heben::TrustedTerminalTarget(true,L"conhost.exe",UIA_EditControlTypeId,true,false) ||
       heben::TrustedTerminalTarget(true,L"conhost.exe",UIA_TextControlTypeId,true,true))return 36;
    if(!heben::IsVsCodeTerminalSurface(L"Code.exe",UIA_EditControlTypeId,L"xterm-helper-textarea") ||
       heben::IsVsCodeTerminalSurface(L"Code.exe",UIA_EditControlTypeId,L"native-edit-context") ||
       heben::IsVsCodeTerminalSurface(L"chrome.exe",UIA_EditControlTypeId,L"xterm-helper-textarea") ||
       heben::TrustedTerminalTarget(false,L"Code.exe",UIA_EditControlTypeId,true,false,L"xterm-helper-textarea") ||
       !heben::TrustedTerminalTarget(true,L"Code.exe",UIA_EditControlTypeId,false,false,L"xterm-helper-textarea") ||
       heben::TrustedTerminalTarget(true,L"Code.exe",UIA_EditControlTypeId,true,true,L"xterm-helper-textarea"))return 37;
    using CR=heben::ClipboardRestoreDecision;
    const HWND owner=reinterpret_cast<HWND>(static_cast<UINT_PTR>(1));
    const HWND other=reinterpret_cast<HWND>(static_cast<UINT_PTR>(2));
    if(!heben::PreservableTextClipboardFormat(CF_UNICODETEXT) || heben::PreservableTextClipboardFormat(CF_HDROP))return 32;
    if(heben::DecideClipboardRestore(true,42,42,owner,owner)!=CR::Restore ||
       heben::DecideClipboardRestore(true,42,43,owner,owner)!=CR::Refuse ||
       heben::DecideClipboardRestore(true,42,42,owner,other)!=CR::Refuse ||
       heben::DecideClipboardRestore(true,0,0,owner,owner)!=CR::Refuse ||
       heben::DecideClipboardRestore(false,42,42,owner,owner)!=CR::Refuse)return 33;
    auto paste=heben::PasteKeyEvents();
    if(paste.size()!=4 || paste[0].ki.wVk!=VK_CONTROL || paste[0].ki.dwFlags!=0 ||
       paste[1].ki.wVk!='V' || paste[1].ki.dwFlags!=0 || paste[2].ki.wVk!='V' ||
       paste[2].ki.dwFlags!=KEYEVENTF_KEYUP || paste[3].ki.wVk!=VK_CONTROL || paste[3].ki.dwFlags!=KEYEVENTF_KEYUP)return 34;
    DWORD error=0;heben::ClipboardTextTransaction transaction;
    if(transaction.Begin(owner,L"unsafe\nline",error)!=heben::ClipboardBegin::InvalidText || transaction.Active())return 35;
    std::puts("Unicode input event sequencing and control-character sanitization passed; no input was sent.");
    return 0;
}

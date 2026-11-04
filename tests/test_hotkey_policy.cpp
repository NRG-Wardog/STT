#include "../src/hotkey_policy.hpp"
#include "../src/hotkey_recorder.hpp"
#include <cstdio>
static bool valid(const wchar_t *s) {heben::HotkeySpec v;std::wstring e;return heben::ParseHotkeySyntax(s,v,e)&&heben::ValidateUserHotkey(v,e);}
int main(){
    heben::HotkeySpec v;std::wstring e;
    if(!valid(L"Ctrl+Shift+R")||!valid(L"Ctrl+Alt+D")||!valid(L"control + alt + shift + f9"))return 1;
    if(!heben::ParseHotkeySyntax(L"shift + control + r",v,e)||v.canonical!=L"CTRL+SHIFT+R")return 2;
    if(!valid(L"RightCtrl+Shift+R")||!valid(L"Right Ctrl + Shift + R")||!valid(L"RCtrl+Shift+R")||
       !heben::ParseHotkeySyntax(L"rightcontrol+shift+r",v,e)||v.canonical!=L"CTRL+SHIFT+R")return 8;
    UINT questionKey=0,questionMods=0;
    if(!heben::QuestionMarkHotkey(questionKey,questionMods)||
       !heben::ParseHotkeySyntax(L"Right Ctrl+?",v,e)||!heben::ValidateUserHotkey(v,e)||
       v.key!=questionKey||(v.modifiers&questionMods)!=questionMods||v.canonical.find(L"QUESTION")==std::wstring::npos)return 10;
    heben::HotkeySpec persisted;std::wstring persistError;
    if(!heben::ParseHotkeySyntax(v.canonical,persisted,persistError)||persisted.key!=v.key||persisted.modifiers!=v.modifiers)return 11;
    if(valid(L"Ctrl+C")||valid(L"Ctrl+V")||valid(L"Ctrl+Shift+S")||valid(L"Win+L"))return 3;
    if(valid(L"Ctrl+Alt+F12")||valid(L"Ctrl+Ctrl+X")||valid(L"Ctrl+Alt+F1junk")||valid(L"Ctrl+Alt+Q+R"))return 4;
    if(heben::ParseHotkeySyntax(L"Ctrl+Alt+",v,e)||heben::ParseHotkeySyntax(L"+Ctrl+R",v,e))return 5;
    heben::HotkeyRecorder recorder;using R=heben::RecordEvent;
    if(recorder.Feed(VK_LCONTROL,true)!=R::Waiting||recorder.Feed(VK_LSHIFT,true)!=R::Waiting||
       recorder.Feed('R',true)!=R::Waiting||recorder.Feed('R',false)!=R::Waiting||
       recorder.Feed(VK_LSHIFT,false)!=R::Waiting||recorder.Feed(VK_LCONTROL,false)!=R::Complete||
       recorder.Recorded()!=L"CTRL+SHIFT+R")return 6;
    recorder.Reset();if(recorder.Feed(VK_ESCAPE,true)!=R::Cancelled)return 7;
    recorder.Reset();
    if(recorder.Feed(VK_RCONTROL,true)!=R::Waiting||recorder.Feed(VK_RSHIFT,true)!=R::Waiting||
       recorder.Feed('R',true)!=R::Waiting||recorder.Feed('R',false)!=R::Waiting||
       recorder.Feed(VK_RSHIFT,false)!=R::Waiting||recorder.Feed(VK_RCONTROL,false)!=R::Complete||
       recorder.Recorded()!=L"CTRL+SHIFT+R")return 9;
    if(questionMods==MOD_SHIFT){
        recorder.Reset();
        if(recorder.Feed(VK_RCONTROL,true)!=R::Waiting||recorder.Feed(VK_LSHIFT,true)!=R::Waiting||
           recorder.Feed(questionKey,true)!=R::Waiting||recorder.Feed(questionKey,false)!=R::Waiting||
           recorder.Feed(VK_LSHIFT,false)!=R::Waiting||recorder.Feed(VK_RCONTROL,false)!=R::Complete||
           recorder.Recorded()!=L"CTRL+SHIFT+QUESTION")return 12;
    }
    std::puts("PASS_HOTKEY_POLICY: syntax, normalization and common-shortcut rejection");
}

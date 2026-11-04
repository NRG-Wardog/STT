// Uses two private message-only windows. Registers rare chords briefly; never
// synthesizes keyboard input or changes user settings.
#include "../src/hotkey_registry.hpp"
#include <cstdio>

int main() {
    HWND a=CreateWindowExW(0,L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND b=CreateWindowExW(0,L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!a||!b)return 1;
    const UINT mods=MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_NOREPEAT;
    UINT first=0,second=0,third=0;
    for(UINT k=VK_F1;k<VK_F12;++k) {
        if(RegisterHotKey(a,1201,mods,k)){first=k;break;}
    }
    if(!first)return 2;
    for(UINT k=VK_F1;k<VK_F12;++k)if(k!=first && RegisterHotKey(b,3001,mods,k)){second=k;break;}
    if(!second)return 3;
    for(UINT k=VK_F1;k<VK_F12;++k)if(k!=first && k!=second && RegisterHotKey(b,3002,mods,k)){
        third=k;UnregisterHotKey(b,3002);break;
    }
    if(!third)return 4;
    heben::HotkeyRegistration current{1201,true,{mods,first,L"old"}};
    heben::HotkeySpec blocked{mods,second,L"blocked"},next{mods,third,L"new"};
    int writes=0;
    auto success=[&](const std::wstring&){++writes;return true;};
    if(heben::ChangeRegisteredHotkey(a,current,blocked,success)!=heben::HotkeyChange::Conflict || writes!=0 || current.id!=1201)return 5;
    if(heben::ChangeRegisteredHotkey(a,current,next,[](const std::wstring&){return false;})!=heben::HotkeyChange::PersistFailed || current.id!=1201)return 6;
    if(!RegisterHotKey(b,3002,mods,third))return 7;UnregisterHotKey(b,3002);
    if(heben::ChangeRegisteredHotkey(a,current,next,success)!=heben::HotkeyChange::Changed || current.id!=1203 || writes!=1)return 8;
    if(!RegisterHotKey(b,3002,mods,first))return 9;UnregisterHotKey(b,3002);
    if(RegisterHotKey(b,3002,mods,third))return 10;
    UnregisterHotKey(a,current.id);UnregisterHotKey(b,3001);DestroyWindow(a);DestroyWindow(b);
    std::puts("PASS_HOTKEY_REGISTRY: actual global conflict, persistence failure and replacement");return 0;
}

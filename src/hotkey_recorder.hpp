#pragma once
#include <windows.h>
#include <array>
#include "hotkey_policy.hpp"

namespace heben {
enum class RecordEvent { Waiting, Complete, Cancelled };
class HotkeyRecorder {
public:
    void Reset(){pressed_.fill(false);key_=0;keyDown_=false;modsAtKey_=0;}
    RecordEvent Feed(DWORD key,bool down) {
        if(key==VK_ESCAPE && down && !keyDown_)return RecordEvent::Cancelled;
        int index=ModifierIndex(key);
        if(index>=0)pressed_[static_cast<size_t>(index)]=down;
        else if(down && !key_) {key_=key;keyDown_=true;modsAtKey_=Modifiers();}
        else if(!down && key==key_)keyDown_=false;
        if(key_ && !keyDown_ && Modifiers()==0)return RecordEvent::Complete;
        return RecordEvent::Waiting;
    }
    std::wstring Recorded() const {return FormatHotkey(modsAtKey_,key_);}
private:
    static int ModifierIndex(DWORD key) {
        switch(key){case VK_CONTROL:case VK_LCONTROL:return 0;case VK_RCONTROL:return 1;
        case VK_SHIFT:case VK_LSHIFT:return 2;case VK_RSHIFT:return 3;case VK_MENU:case VK_LMENU:return 4;
        case VK_RMENU:return 5;case VK_LWIN:return 6;case VK_RWIN:return 7;default:return -1;}
    }
    UINT Modifiers() const {
        return (pressed_[0]||pressed_[1]?MOD_CONTROL:0)|(pressed_[2]||pressed_[3]?MOD_SHIFT:0)|
               (pressed_[4]||pressed_[5]?MOD_ALT:0)|(pressed_[6]||pressed_[7]?MOD_WIN:0);
    }
    std::array<bool,8> pressed_{};DWORD key_=0;bool keyDown_=false;UINT modsAtKey_=0;
};
}

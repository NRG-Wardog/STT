#pragma once
#include <windows.h>
#include <algorithm>
#include <string>
#include <utility>

namespace heben {
struct HotkeySpec {UINT modifiers=0;UINT key=0;std::wstring canonical;};

// Resolve '?' on the active keyboard layout, including the modifiers used to
// type it. Windows global hotkeys still do not distinguish left/right Ctrl.
inline bool QuestionMarkHotkey(UINT &key,UINT &mods) {
    SHORT mapped=VkKeyScanExW(L'?',GetKeyboardLayout(0));
    if(mapped==-1)return false;
    const unsigned shiftState=(static_cast<unsigned short>(mapped)>>8)&0xff;
    if(shiftState&~7u)return false;
    key=static_cast<unsigned char>(mapped&0xff);
    if(shiftState&1u)mods|=MOD_SHIFT;
    if(shiftState&2u)mods|=MOD_CONTROL;
    if(shiftState&4u)mods|=MOD_ALT;
    return key!=0;
}

inline std::wstring HotkeyTrim(std::wstring value) {
    auto blank=[](wchar_t c){return c==L' '||c==L'\t'||c==L'\r'||c==L'\n';};
    size_t a=0,b=value.size();while(a<b&&blank(value[a]))++a;while(b>a&&blank(value[b-1]))--b;
    return value.substr(a,b-a);
}
inline std::wstring HotkeyUpper(std::wstring value) {
    for(auto &c:value)if(c>=L'a'&&c<=L'z')c=static_cast<wchar_t>(c-L'a'+L'A');
    return value;
}
inline std::wstring FormatHotkey(UINT mods,UINT key) {
    std::wstring text;
    if(mods&MOD_CONTROL)text+=L"CTRL+";if(mods&MOD_ALT)text+=L"ALT+";
    if(mods&MOD_SHIFT)text+=L"SHIFT+";if(mods&MOD_WIN)text+=L"WIN+";
    UINT questionKey=0,questionMods=0;
    if(QuestionMarkHotkey(questionKey,questionMods) && key==questionKey)
        text+=(mods&questionMods)==questionMods?L"QUESTION":L"QUESTIONKEY";
    else if(key==VK_SPACE)text+=L"SPACE";else if(key>=VK_F1&&key<=VK_F12)text+=L"F"+std::to_wstring(key-VK_F1+1);
    else if((key>=L'A'&&key<=L'Z')||(key>=L'0'&&key<=L'9'))text.push_back(static_cast<wchar_t>(key));
    else return L"";
    return text;
}
inline bool ParseHotkeySyntax(const std::wstring &input,HotkeySpec &out,std::wstring &error) {
    UINT mods=0,key=0;size_t pos=0;bool haveKey=false;
    if(input.empty()){error=L"Enter or record a shortcut.";return false;}
    while(pos<input.size()) {
        size_t sep=input.find(L'+',pos);
        auto part=HotkeyUpper(HotkeyTrim(input.substr(pos,sep==std::wstring::npos?sep:sep-pos)));
        if(part.empty()){error=L"Use a format such as Ctrl+Shift+R.";return false;}
        auto modifierName=part;
        modifierName.erase(std::remove_if(modifierName.begin(),modifierName.end(),[](wchar_t c){return c==L' '||c==L'\t'||c==L'-';}),modifierName.end());
        // Windows RegisterHotKey does not distinguish left/right modifier
        // keys. Accept the familiar names and canonicalize them to CTRL.
        UINT mod=modifierName==L"CTRL"||modifierName==L"CONTROL"||modifierName==L"RCTRL"||modifierName==L"LCTRL"||
                 modifierName==L"RIGHTCTRL"||modifierName==L"LEFTCTRL"||modifierName==L"RIGHTCONTROL"||modifierName==L"LEFTCONTROL"?MOD_CONTROL:modifierName==L"ALT"?MOD_ALT:
                 modifierName==L"SHIFT"?MOD_SHIFT:modifierName==L"WIN"?MOD_WIN:0;
        if(mod) {
            if(haveKey || (mods&mod)){error=L"Each modifier must appear once, before the key.";return false;}
            mods|=mod;
        } else {
            if(haveKey){error=L"Choose exactly one main key.";return false;}
            if(part==L"?"||part==L"QUESTION") {
                if(!QuestionMarkHotkey(key,mods)){error=L"The question-mark key is unavailable on this keyboard layout.";return false;}
            }
            else if(part==L"QUESTIONKEY"||part==L"/"||part==L"SLASH") {
                UINT unusedMods=0;
                if(!QuestionMarkHotkey(key,unusedMods)){error=L"The question-mark key is unavailable on this keyboard layout.";return false;}
            }
            else if(part==L"SPACE")key=VK_SPACE;
            else if(part.size()==1 && ((part[0]>=L'A'&&part[0]<=L'Z')||(part[0]>=L'0'&&part[0]<=L'9')))key=static_cast<UINT>(part[0]);
            else if(part.size()>=2 && part[0]==L'F') {
                unsigned n=0;for(size_t i=1;i<part.size();++i){if(part[i]<L'0'||part[i]>L'9'){error=L"Unknown key.";return false;}n=n*10+(part[i]-L'0');if(n>12)break;}
                if(n<1||n>12){error=L"Use F1 through F12.";return false;}key=VK_F1+n-1;
            } else {error=L"Use a letter, number, Space, or F1–F12.";return false;}
            haveKey=true;
        }
        if(sep==std::wstring::npos)break;
        pos=sep+1;if(pos==input.size()){error=L"The shortcut cannot end with +.";return false;}
    }
    if(!haveKey||!mods){error=L"Add a modifier and a main key.";return false;}
    std::wstring text=FormatHotkey(mods,key);
    out={mods|MOD_NOREPEAT,key,std::move(text)};error.clear();return true;
}
inline bool ValidateUserHotkey(const HotkeySpec &spec,std::wstring &error) {
    UINT mods=spec.modifiers&(MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_WIN);
    if(mods&MOD_WIN){error=L"Windows-key shortcuts are reserved for Windows. Choose Ctrl, Alt and/or Shift.";return false;}
    unsigned count=!!(mods&MOD_CONTROL)+!!(mods&MOD_ALT)+!!(mods&MOD_SHIFT);
    UINT questionKey=0,questionMods=0;
    bool questionOnly=mods==MOD_CONTROL && QuestionMarkHotkey(questionKey,questionMods) &&
        spec.key==questionKey && !(questionKey>=L'A'&&questionKey<=L'Z') && !(questionKey>=L'0'&&questionKey<=L'9');
    if(count<2 && !questionOnly){error=L"Use at least two modifiers, such as Ctrl+Shift. Common shortcuts like Ctrl+C are not allowed.";return false;}
    if(spec.key==VK_F12){error=L"F12 is reserved by Windows debugging. Choose a different key.";return false;}
    if(mods==(MOD_CONTROL|MOD_SHIFT)) {
        const std::wstring common=L"ACFNOPSTUVWXYZ";
        if(spec.key==VK_SPACE || common.find(static_cast<wchar_t>(spec.key))!=std::wstring::npos) {
            error=L"That shortcut is commonly used by applications. Choose a different combination.";return false;
        }
    }
    error.clear();return true;
}
}

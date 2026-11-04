#pragma once
#include <windows.h>
#include <string>
#include <utility>

namespace heben {
struct HotkeySpec {UINT modifiers=0;UINT key=0;std::wstring canonical;};

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
    if(key==VK_SPACE)text+=L"SPACE";else if(key>=VK_F1&&key<=VK_F12)text+=L"F"+std::to_wstring(key-VK_F1+1);
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
        UINT mod=part==L"CTRL"||part==L"CONTROL"?MOD_CONTROL:part==L"ALT"?MOD_ALT:
                 part==L"SHIFT"?MOD_SHIFT:part==L"WIN"?MOD_WIN:0;
        if(mod) {
            if(haveKey || (mods&mod)){error=L"Each modifier must appear once, before the key.";return false;}
            mods|=mod;
        } else {
            if(haveKey){error=L"Choose exactly one main key.";return false;}
            if(part==L"SPACE")key=VK_SPACE;
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
    if(count<2){error=L"Use at least two modifiers, such as Ctrl+Shift. Common shortcuts like Ctrl+C are not allowed.";return false;}
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

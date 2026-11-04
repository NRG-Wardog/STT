// Controlled settings-window test on a separate desktop, never switched to.
// It changes only its own controls and does not send keyboard input.
#include "../src/hotkey_settings.hpp"
#include <cstdio>

int main() {
    HDESK desktop=CreateDesktopW(L"HebENHotkeyWindowTest",nullptr,nullptr,0,DESKTOP_CREATEWINDOW|DESKTOP_READOBJECTS|DESKTOP_WRITEOBJECTS|DESKTOP_SWITCHDESKTOP,nullptr);
    if(!desktop){std::printf("BLOCKED: CreateDesktop failed %lu\n",GetLastError());return 2;}
    if(!SetThreadDesktop(desktop)){std::printf("BLOCKED: SetThreadDesktop failed %lu\n",GetLastError());CloseDesktop(desktop);return 3;}
    heben::HotkeySettingsWindow window;std::wstring stored=L"CTRL+SHIFT+R";
    bool shown=window.Show(stored,[&](const std::wstring &value,std::wstring &error){
        heben::HotkeySpec spec;if(!heben::ParseHotkeySyntax(value,spec,error)||!heben::ValidateUserHotkey(spec,error))return false;
        if(spec.canonical==L"CTRL+ALT+Q"){error=L"That global shortcut is already in use.";return false;}
        stored=spec.canonical;return true;
    });
    if(!shown||!window.Handle())return 4;
    HWND h=window.Handle(),edit=GetDlgItem(h,101),record=GetDlgItem(h,102),status=GetDlgItem(h,105);
    if(!edit||!record||!status)return 5;
    SetWindowTextW(edit,L"CTRL+C");SendMessageW(h,WM_COMMAND,MAKEWPARAM(103,BN_CLICKED),0);
    if(!window.Handle()||stored!=L"CTRL+SHIFT+R")return 6;
    SetWindowTextW(edit,L"CTRL+ALT+Q");SendMessageW(h,WM_COMMAND,MAKEWPARAM(103,BN_CLICKED),0);
    if(!window.Handle()||stored!=L"CTRL+SHIFT+R")return 7;
    SetWindowTextW(edit,L"ctrl+alt+d");SendMessageW(h,WM_COMMAND,MAKEWPARAM(103,BN_CLICKED),0);
    if(window.Handle()||stored!=L"CTRL+ALT+D")return 8;
    CloseDesktop(desktop);
    std::puts("PASS_HOTKEY_WINDOW: invalid and conflicting values retained; valid value accepted on isolated desktop");return 0;
}

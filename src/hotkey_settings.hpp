#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <utility>
#include "hotkey_recorder.hpp"
#include "app_icon.hpp"

namespace heben {
// Small modeless Win32 settings window; no work runs while it is closed.
class HotkeySettingsWindow {
public:
    using Save=std::function<bool(const std::wstring&,std::wstring&)>;
    ~HotkeySettingsWindow(){Close();}
    HWND Handle() const{return window_;}
    void Close(){if(window_)DestroyWindow(window_);}
    bool Show(const std::wstring &current,Save save,bool activate=true) {
        if(window_){ShowWindow(window_,SW_RESTORE);SetForegroundWindow(window_);return true;}
        save_=std::move(save);HINSTANCE instance=GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=instance;wc.lpfnWndProc=WndProc;
        wc.lpszClassName=L"HebENHotkeySettings";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.hIcon=LoadLargeAppIcon(instance);wc.hIconSm=LoadSmallAppIcon(instance);
        if(!wc.hIcon || !wc.hIconSm)return false;
        wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
        window_=CreateWindowExW(WS_EX_APPWINDOW,wc.lpszClassName,L"HebEN keyboard shortcut",
            WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,510,270,
            nullptr,nullptr,instance,this);
        if(!window_)return false;
        SetWindowTextW(edit_,current.c_str());ShowWindow(window_,activate?SW_SHOW:SW_SHOWNOACTIVATE);
        if(activate){SetForegroundWindow(window_);SetFocus(edit_);}return true;
    }
private:
    static constexpr int kEdit=101,kRecord=102,kSave=103,kCancel=104,kStatus=105;
    static constexpr UINT kRecorded=WM_APP+200,kRecordCancelled=WM_APP+201;
#ifdef HEBEN_HOTKEY_UI_TEST
    static constexpr UINT kTestRecordEvent=WM_APP+202;
#endif
    inline static HotkeySettingsWindow *recorderWindow_=nullptr;
    HWND window_=nullptr,edit_=nullptr,status_=nullptr,record_=nullptr;
    HHOOK hook_=nullptr;bool finishing_=false;HotkeyRecorder recorder_;Save save_;

    static LRESULT CALLBACK Keyboard(int code,WPARAM message,LPARAM data) {
        auto *self=recorderWindow_;
        if(!self || code!=HC_ACTION)return CallNextHookEx(nullptr,code,message,data);
        auto *event=reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        if(event->flags&LLKHF_INJECTED)return CallNextHookEx(nullptr,code,message,data);
        if(GetForegroundWindow()!=self->window_) {
            if(!self->finishing_){self->finishing_=true;PostMessageW(self->window_,kRecordCancelled,0,0);}
            return CallNextHookEx(nullptr,code,message,data);
        }
        bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN;
        bool up=message==WM_KEYUP||message==WM_SYSKEYUP;
        if(!down&&!up)return CallNextHookEx(nullptr,code,message,data);
        if(!self->finishing_) {
            auto result=self->recorder_.Feed(event->vkCode,down);
            if(result!=RecordEvent::Waiting) {
                self->finishing_=true;
                PostMessageW(self->window_,result==RecordEvent::Complete?kRecorded:kRecordCancelled,0,0);
            }
        }
        // While recording, suppress the chord so it cannot invoke an existing
        // app or global shortcut. Hook is removed immediately after release.
        return 1;
    }
    bool StartRecording() {
        StopRecording();recorder_.Reset();finishing_=false;recorderWindow_=this;
        hook_=SetWindowsHookExW(WH_KEYBOARD_LL,Keyboard,GetModuleHandleW(nullptr),0);
        if(!hook_){recorderWindow_=nullptr;SetStatus(L"Recording unavailable. You can type the shortcut instead.");return false;}
        SetStatus(L"Press the shortcut, then release all keys. Press Esc to cancel.");
        SetWindowTextW(record_,L"Recording...");return true;
    }
    void StopRecording() {
        if(hook_)UnhookWindowsHookEx(hook_);hook_=nullptr;
        if(recorderWindow_==this)recorderWindow_=nullptr;finishing_=false;
        if(record_)SetWindowTextW(record_,L"Record shortcut");
    }
    void SetStatus(const wchar_t *text){if(status_)SetWindowTextW(status_,text);}
    void FinishRecording() {
        auto value=recorder_.Recorded();StopRecording();
        if(value.empty()){SetStatus(L"That key is not supported. Use a letter, number, ? key, Space or F1–F12.");return;}
        SetWindowTextW(edit_,value.c_str());SetStatus(L"Shortcut recorded. Select Save to apply it.");
    }
    void SaveValue() {
        if(hook_){SetStatus(L"Finish recording or press Esc first.");return;}
        wchar_t value[128]{};GetWindowTextW(edit_,value,128);std::wstring error;
        if(!save_ || !save_(value,error)){SetStatus(error.empty()?L"The shortcut could not be saved.":error.c_str());return;}
        DestroyWindow(window_);
    }
    static HWND Child(HWND parent,const wchar_t *cls,const wchar_t *text,DWORD style,int x,int y,int w,int h,int id) {
        HWND child=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(child)SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return child;
    }
    bool CreateControls() {
        Child(window_,L"STATIC",L"Keyboard shortcut",0,20,18,220,20,0);
        edit_=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
            20,42,280,27,window_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEdit)),GetModuleHandleW(nullptr),nullptr);
        if(edit_)SendMessageW(edit_,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        record_=Child(window_,L"BUTTON",L"Record shortcut",WS_TABSTOP|BS_PUSHBUTTON,310,41,160,28,kRecord);
        Child(window_,L"STATIC",L"Type a shortcut or select Record and press it.",0,20,80,450,20,0);
        Child(window_,L"STATIC",L"Windows checks global conflicts; app-specific shortcuts may still conflict.",0,20,105,455,34,0);
        status_=Child(window_,L"STATIC",L"Use at least two modifiers, for example Ctrl+Shift+R.",0,20,145,450,34,kStatus);
        Child(window_,L"BUTTON",L"Save",WS_TABSTOP|BS_DEFPUSHBUTTON,290,192,85,28,kSave);
        Child(window_,L"BUTTON",L"Cancel",WS_TABSTOP|BS_PUSHBUTTON,385,192,85,28,kCancel);
        return edit_&&status_&&record_;
    }
    static LRESULT CALLBACK WndProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
        auto *self=reinterpret_cast<HotkeySettingsWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<HotkeySettingsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window_=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));return DefWindowProcW(hwnd,message,wp,lp);}
        if(!self)return DefWindowProcW(hwnd,message,wp,lp);
        switch(message) {
        case WM_CREATE:return self->CreateControls()?0:-1;
        case WM_DPICHANGED: {
            auto dpi=HIWORD(wp);auto instance=GetModuleHandleW(nullptr);
            auto iconSmall=LoadAppIcon(instance,GetSystemMetricsForDpi(SM_CXSMICON,dpi),GetSystemMetricsForDpi(SM_CYSMICON,dpi));
            auto iconLarge=LoadAppIcon(instance,GetSystemMetricsForDpi(SM_CXICON,dpi),GetSystemMetricsForDpi(SM_CYICON,dpi));
            if(iconSmall)SendMessageW(hwnd,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(iconSmall));
            if(iconLarge)SendMessageW(hwnd,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(iconLarge));
            return 0;
        }
        case WM_COMMAND:
            if(HIWORD(wp)==BN_CLICKED) {
                if(LOWORD(wp)==kRecord)self->StartRecording();
                else if(LOWORD(wp)==kSave)self->SaveValue();
                else if(LOWORD(wp)==kCancel)DestroyWindow(hwnd);
            }
            return 0;
        case kRecorded:self->FinishRecording();return 0;
        case kRecordCancelled:self->StopRecording();self->SetStatus(L"Recording cancelled. You can type a shortcut.");return 0;
#ifdef HEBEN_HOTKEY_UI_TEST
        case kTestRecordEvent: {
            if(!self->hook_)return 0;
            auto event=self->recorder_.Feed(static_cast<DWORD>(wp),lp!=0);
            if(event==RecordEvent::Complete)self->FinishRecording();
            else if(event==RecordEvent::Cancelled)self->StopRecording();
            return 1;
        }
#endif
        case WM_DESTROY:self->StopRecording();self->window_=self->edit_=self->status_=self->record_=nullptr;self->save_={};return 0;
        }
        return DefWindowProcW(hwnd,message,wp,lp);
    }
};
}

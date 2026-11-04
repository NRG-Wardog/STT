#pragma once
#include <windows.h>
#include <atomic>
#include <functional>
#include <thread>
#include <utility>

namespace heben {
// Active only during experimental streaming. Processes event types and
// shortcut keys in memory; does not record or transmit keystrokes. It reduces
// same-control caret/selection races but cannot observe programmatic edits.
inline bool IsModifierKey(DWORD key) {
    return key==VK_CONTROL || key==VK_LCONTROL || key==VK_RCONTROL || key==VK_SHIFT || key==VK_LSHIFT || key==VK_RSHIFT ||
           key==VK_MENU || key==VK_LMENU || key==VK_RMENU || key==VK_LWIN || key==VK_RWIN;
}
inline bool IsPhysicalEditingKey(DWORD key,DWORD flags,bool shortcutMatch) {
    return !(flags&LLKHF_INJECTED) && !IsModifierKey(key) && !shortcutMatch;
}
inline bool IsPhysicalMousePress(WPARAM message,DWORD flags) {
    return !(flags&LLMHF_INJECTED) && (message==WM_LBUTTONDOWN || message==WM_RBUTTONDOWN ||
           message==WM_MBUTTONDOWN || message==WM_XBUTTONDOWN);
}

class InputMutationGuard {
public:
    InputMutationGuard()=default;
    InputMutationGuard(const InputMutationGuard&)=delete;
    InputMutationGuard& operator=(const InputMutationGuard&)=delete;
    ~InputMutationGuard(){Stop();}

    bool Start(UINT shortcutKey,UINT shortcutModifiers,std::function<void()> onInput) {
        if(worker_.joinable())return false;
        shortcutKey_=shortcutKey;shortcutModifiers_=shortcutModifiers;onInput_=std::move(onInput);
        ready_=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!ready_)return false;
        try {worker_=std::thread([this]{Run();});}
        catch(...) {CloseHandle(ready_);ready_=nullptr;return false;}
        if(WaitForSingleObject(ready_,5000)!=WAIT_OBJECT_0 || !installed_.load(std::memory_order_acquire)) {
            Stop();return false;
        }
        return true;
    }
    void Stop() {
        DWORD tid=threadId_.load(std::memory_order_acquire);
        if(tid)PostThreadMessageW(tid,WM_QUIT,0,0);
        if(worker_.joinable())worker_.join();
        if(ready_){CloseHandle(ready_);ready_=nullptr;}
        threadId_.store(0,std::memory_order_release);installed_.store(false,std::memory_order_release);onInput_={};
    }
private:
    static thread_local InputMutationGuard *active_;
    static LRESULT CALLBACK Keyboard(int code,WPARAM msg,LPARAM data) {
        auto *self=active_;
        if(self && code==HC_ACTION && (msg==WM_KEYDOWN || msg==WM_SYSKEYDOWN)) {
            const auto *k=reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);bool match=k->vkCode==self->shortcutKey_;
            const struct {UINT mod;int vk;} mods[]={{MOD_CONTROL,VK_CONTROL},{MOD_SHIFT,VK_SHIFT},{MOD_ALT,VK_MENU}};
            for(const auto &m:mods)if(bool(self->shortcutModifiers_&m.mod)!=bool(GetAsyncKeyState(m.vk)&0x8000))match=false;
            const bool winDown=bool(GetAsyncKeyState(VK_LWIN)&0x8000)||bool(GetAsyncKeyState(VK_RWIN)&0x8000);
            if(bool(self->shortcutModifiers_&MOD_WIN)!=winDown)match=false;
            if(IsPhysicalEditingKey(k->vkCode,k->flags,match) && self->onInput_)self->onInput_();
        }
        return CallNextHookEx(nullptr,code,msg,data);
    }
    static LRESULT CALLBACK Mouse(int code,WPARAM msg,LPARAM data) {
        auto *self=active_;
        if(self && code==HC_ACTION) {
            const auto *m=reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
            if(IsPhysicalMousePress(msg,m->flags) && self->onInput_)self->onInput_();
        }
        return CallNextHookEx(nullptr,code,msg,data);
    }
    void Run() {
        MSG msg{};PeekMessageW(&msg,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
        threadId_.store(GetCurrentThreadId(),std::memory_order_release);active_=this;
        HHOOK keyboard=SetWindowsHookExW(WH_KEYBOARD_LL,Keyboard,GetModuleHandleW(nullptr),0);
        HHOOK mouse=SetWindowsHookExW(WH_MOUSE_LL,Mouse,GetModuleHandleW(nullptr),0);
        installed_.store(keyboard && mouse,std::memory_order_release);SetEvent(ready_);
        if(keyboard && mouse)while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
        if(keyboard)UnhookWindowsHookEx(keyboard);if(mouse)UnhookWindowsHookEx(mouse);active_=nullptr;
    }
    std::thread worker_;HANDLE ready_=nullptr;std::atomic<DWORD> threadId_{0};std::atomic<bool> installed_{false};
    UINT shortcutKey_=0,shortcutModifiers_=0;std::function<void()> onInput_;
};
inline thread_local InputMutationGuard *InputMutationGuard::active_=nullptr;
}

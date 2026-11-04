#pragma once
#include <windows.h>
#include <string>
#include "hotkey_policy.hpp"

namespace heben {
struct HotkeyRegistration {
    int id=1201;bool registered=false;HotkeySpec spec;
};
enum class HotkeyChange { Changed, Unchanged, Conflict, PersistFailed, ReleaseFailed, RollbackFailed };

// Reserve the replacement before releasing the current shortcut. Persistence
// failures leave the old registration in place; no desktop input is sent.
template<class Write>
HotkeyChange ChangeRegisteredHotkey(HWND window,HotkeyRegistration &current,const HotkeySpec &next,Write write) {
    if(current.registered && current.spec.key==next.key && current.spec.modifiers==next.modifiers)
        return HotkeyChange::Unchanged;
    int candidate=current.id==1201?1203:1201;
    if(!RegisterHotKey(window,candidate,next.modifiers,next.key))return HotkeyChange::Conflict;
    if(!write(next.canonical)) {UnregisterHotKey(window,candidate);return HotkeyChange::PersistFailed;}
    if(current.registered && !UnregisterHotKey(window,current.id)) {
        const bool restored=write(current.spec.canonical);
        UnregisterHotKey(window,candidate);
        return restored?HotkeyChange::ReleaseFailed:HotkeyChange::RollbackFailed;
    }
    current.id=candidate;current.registered=true;current.spec=next;return HotkeyChange::Changed;
}
}

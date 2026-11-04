#pragma once
#include <windows.h>

namespace heben {
inline bool PreservableTextClipboardFormat(UINT format) {
    return format==CF_UNICODETEXT || format==CF_TEXT || format==CF_OEMTEXT || format==CF_LOCALE;
}
enum class ClipboardRestoreDecision { Refuse, Restore };
inline ClipboardRestoreDecision DecideClipboardRestore(bool active,DWORD ownedSequence,DWORD currentSequence,
                                                        HWND owner,HWND currentOwner) {
    if(!active || !ownedSequence || !currentSequence || ownedSequence!=currentSequence || !owner || owner!=currentOwner)
        return ClipboardRestoreDecision::Refuse;
    return ClipboardRestoreDecision::Restore;
}
}

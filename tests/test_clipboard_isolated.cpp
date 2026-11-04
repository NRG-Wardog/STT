// Clipboard tests in a newly created non-interactive window station only.
// Abort before touching a clipboard unless isolation is positively verified.
#include <windows.h>
#include <cstdio>
#include <string>
#include <cstring>
#include "../src/clipboard_text_transaction.hpp"

static bool PutText(HWND owner,const std::wstring &text,bool addUnsupported=false) {
    HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t));if(!h)return false;
    void *p=GlobalLock(h);if(!p){GlobalFree(h);return false;}
    std::memcpy(p,text.c_str(),(text.size()+1)*sizeof(wchar_t));GlobalUnlock(h);
    HGLOBAL extra=addUnsupported?GlobalAlloc(GMEM_MOVEABLE,32):nullptr;
    if(addUnsupported&&!extra){GlobalFree(h);return false;}
    if(!OpenClipboard(owner)){GlobalFree(h);if(extra)GlobalFree(extra);return false;}
    if(!EmptyClipboard()){CloseClipboard();GlobalFree(h);if(extra)GlobalFree(extra);return false;}
    bool ok=SetClipboardData(CF_UNICODETEXT,h)!=nullptr;if(ok)h=nullptr;
    if(ok && extra){ok=SetClipboardData(CF_HDROP,extra)!=nullptr;if(ok)extra=nullptr;}
    CloseClipboard();if(h)GlobalFree(h);if(extra)GlobalFree(extra);return ok;
}
static std::wstring ReadText(HWND owner) {
    if(!OpenClipboard(owner))return {};
    HANDLE h=GetClipboardData(CF_UNICODETEXT);const wchar_t *p=h?static_cast<const wchar_t*>(GlobalLock(h)):nullptr;
    std::wstring result=p?p:L"";if(p)GlobalUnlock(h);CloseClipboard();return result;
}
int main() {
    wchar_t name[96]{};swprintf_s(name,L"HebEN_Clipboard_Test_%lu_%llu",GetCurrentProcessId(),GetTickCount64());
    HWINSTA station=CreateWindowStationW(name,0,WINSTA_ALL_ACCESS,nullptr);
    if(!station){std::printf("{\"status\":\"BLOCKED_CREATE_STATION\",\"error\":%lu}\n",GetLastError());return 2;}
    if(!SetProcessWindowStation(station)){std::printf("{\"status\":\"BLOCKED_SET_STATION\",\"error\":%lu}\n",GetLastError());return 2;}
    USEROBJECTFLAGS flags{};DWORD needed=0;
    if(GetProcessWindowStation()!=station || !GetUserObjectInformationW(station,UOI_FLAGS,&flags,sizeof(flags),&needed) || (flags.dwFlags&WSF_VISIBLE)) {
        std::puts("{\"status\":\"BLOCKED_ISOLATION_UNVERIFIED\"}");return 3;
    }
    HDESK desk=CreateDesktopW(L"HebENClipboardTest",nullptr,nullptr,0,DESKTOP_CREATEWINDOW|DESKTOP_READOBJECTS|DESKTOP_WRITEOBJECTS,nullptr);
    if(!desk){std::printf("{\"status\":\"BLOCKED_CREATE_DESKTOP\",\"error\":%lu}\n",GetLastError());return 4;}
    if(!SetThreadDesktop(desk)){std::printf("{\"status\":\"BLOCKED_SET_DESKTOP\",\"error\":%lu}\n",GetLastError());return 4;}
    HWND owner=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!owner){std::puts("{\"status\":\"BLOCKED_TEST_WINDOW\"}");return 5;}
    const std::wstring original=L"original \u05e9\u05dc\u05d5\u05dd";
    const std::wstring payload=L"\u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd Hello World 123";
    DWORD error=0;bool initial=PutText(owner,original);
    heben::ClipboardTextTransaction first;
    bool began=initial && first.Begin(owner,payload,error)==heben::ClipboardBegin::Ready;
    bool changed=began && ReadText(owner)==payload;
    UINT exclude=RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
    bool privacy=began && exclude && IsClipboardFormatAvailable(exclude);
    bool restored=began && first.Restore(owner,error)==heben::ClipboardRestore::Restored && ReadText(owner)==original;
    heben::ClipboardTextTransaction second;
    bool secondBegin=restored && second.Begin(owner,payload,error)==heben::ClipboardBegin::Ready;
    bool external=secondBegin && PutText(owner,L"newer content");
    bool preservedNew=external && second.Restore(owner,error)==heben::ClipboardRestore::ChangedByOther && ReadText(owner)==L"newer content";
    bool unsupportedSetup=preservedNew && PutText(owner,original,true);
    DWORD seq=GetClipboardSequenceNumber();heben::ClipboardTextTransaction third;
    bool refused=unsupportedSetup && third.Begin(owner,payload,error)==heben::ClipboardBegin::UnsupportedFormat && GetClipboardSequenceNumber()==seq;
    heben::ClipboardTextTransaction fourth;seq=GetClipboardSequenceNumber();
    bool controlRefused=refused && fourth.Begin(owner,L"unsafe\ninput",error)==heben::ClipboardBegin::InvalidText && GetClipboardSequenceNumber()==seq;
    std::printf("{\"mode\":\"private_noninteractive_window_station\",\"isolated\":true,\"snapshot_and_set\":%s,\"privacy_marker\":%s,\"restore_original\":%s,\"newer_content_preserved\":%s,\"unsupported_format_refused\":%s,\"newline_refused\":%s}\n",
        changed?"true":"false",privacy?"true":"false",restored?"true":"false",preservedNew?"true":"false",refused?"true":"false",controlRefused?"true":"false");
    DestroyWindow(owner);
    return changed&&privacy&&restored&&preservedNew&&refused&&controlRefused?0:6;
}

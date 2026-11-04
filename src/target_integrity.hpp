#pragma once
#include <windows.h>
#include <vector>

namespace heben {
inline bool IntegrityLevelsAllow(DWORD selfRid,DWORD targetRid) {
    return targetRid<SECURITY_MANDATORY_HIGH_RID && targetRid<=selfRid;
}
inline bool IsNativePasswordControl(HWND control) {
    wchar_t className[32]{};
    return GetClassNameW(control,className,32) && _wcsicmp(className,L"Edit")==0 &&
           (GetWindowLongPtrW(control,GWL_STYLE)&ES_PASSWORD)!=0;
}
inline bool ReadIntegrity(HANDLE process,DWORD &rid,DWORD &error) {
    HANDLE token=nullptr;
    if(!OpenProcessToken(process,TOKEN_QUERY,&token)){error=GetLastError();return false;}
    DWORD bytes=0;GetTokenInformation(token,TokenIntegrityLevel,nullptr,0,&bytes);
    if(!bytes){error=GetLastError();CloseHandle(token);return false;}
    std::vector<unsigned char> buffer(bytes);
    if(!GetTokenInformation(token,TokenIntegrityLevel,buffer.data(),bytes,&bytes)) {
        error=GetLastError();CloseHandle(token);return false;
    }
    auto *label=reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer.data());
    if(!IsValidSid(label->Label.Sid)){error=ERROR_INVALID_SID;CloseHandle(token);return false;}
    UCHAR count=*GetSidSubAuthorityCount(label->Label.Sid);
    if(!count){error=ERROR_INVALID_SID;CloseHandle(token);return false;}
    rid=*GetSidSubAuthority(label->Label.Sid,count-1);
    CloseHandle(token);return true;
}
// Elevated targets are refused even if this process is also elevated.
inline bool SafeProcessIntegrity(DWORD pid,DWORD &targetRid,DWORD &error) {
    if(!pid){error=ERROR_INVALID_PARAMETER;return false;}
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process){error=GetLastError();return false;}
    DWORD selfRid=0;bool ok=ReadIntegrity(process,targetRid,error);CloseHandle(process);
    if(!ok || !ReadIntegrity(GetCurrentProcess(),selfRid,error))return false;
    if(!IntegrityLevelsAllow(selfRid,targetRid)){error=ERROR_ACCESS_DENIED;return false;}
    error=0;return true;
}
inline bool SafeWindowIntegrity(HWND window,DWORD &targetRid,DWORD &error) {
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(!pid){error=ERROR_INVALID_WINDOW_HANDLE;return false;}
    return SafeProcessIntegrity(pid,targetRid,error);
}
}

#pragma once
#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <string>

namespace heben {
inline bool IsVsCodeTerminalSurface(const std::wstring &processName,CONTROLTYPEID type,const std::wstring &className) {
    const bool code=_wcsicmp(processName.c_str(),L"Code.exe")==0 ||
                    _wcsicmp(processName.c_str(),L"Code - Insiders.exe")==0;
    return code && type==UIA_EditControlTypeId && _wcsicmp(className.c_str(),L"xterm-helper-textarea")==0;
}
// This is an explicit user opt-in for an otherwise unclassifiable terminal
// surface. It does not assert that a terminal prompt is not sensitive.
inline bool TrustedTerminalTarget(bool enabled,const std::wstring &processName,CONTROLTYPEID type,
                                  bool passwordKnown,bool isPassword,const std::wstring &className=L"") {
    if(!enabled || (passwordKnown && isPassword))return false;
    const bool console=_wcsicmp(processName.c_str(),L"WindowsTerminal.exe")==0 ||
                       _wcsicmp(processName.c_str(),L"OpenConsole.exe")==0 ||
                       _wcsicmp(processName.c_str(),L"conhost.exe")==0;
    const bool code=_wcsicmp(processName.c_str(),L"Code.exe")==0 ||
                    _wcsicmp(processName.c_str(),L"Code - Insiders.exe")==0;
    if(console)return type==UIA_TextControlTypeId || type==UIA_EditControlTypeId || type==UIA_DocumentControlTypeId;
    // Code hosts ordinary editors as well as terminals; only the opt-in Text
    // surface is recognized until a terminal-specific provider is verified.
    return (code && type==UIA_TextControlTypeId) || IsVsCodeTerminalSurface(processName,type,className);
}

inline std::wstring ProcessBaseName(DWORD pid) {
    HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!p)return {};
    wchar_t path[32768]{};DWORD count=static_cast<DWORD>(sizeof(path)/sizeof(path[0]));
    const bool ok=QueryFullProcessImageNameW(p,0,path,&count)!=FALSE;CloseHandle(p);
    if(!ok)return {};
    std::wstring value(path,count);auto pos=value.find_last_of(L"\\/");
    return pos==std::wstring::npos?value:value.substr(pos+1);
}
}

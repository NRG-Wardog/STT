#pragma once
#include <windows.h>
#include <cstdio>
#include <mutex>
#include <string>

// Explicit opt-in: set HEBEN_DIAGNOSTICS_PATH before launching. Events contain
// fixed stage/reason names and numeric metadata only, never text or audio.
namespace heben::diagnostics {
inline std::wstring &Path() {
    static std::wstring path=[] {
        DWORD n=GetEnvironmentVariableW(L"HEBEN_DIAGNOSTICS_PATH",nullptr,0);
        if (!n || n>32768) return std::wstring{};
        std::wstring value(n,L'\0');
        DWORD got=GetEnvironmentVariableW(L"HEBEN_DIAGNOSTICS_PATH",value.data(),n);
        if (!got || got>=n) return std::wstring{};
        value.resize(got); return value;
    }();
    return path;
}
inline bool Enabled() { return !Path().empty(); }
inline void Record(const char *stage,const char *result,long long value=0,long long code=0) noexcept {
    if (!Enabled()) return;
    static std::mutex mutex;
    std::lock_guard<std::mutex> guard(mutex);
    SYSTEMTIME t{};GetSystemTime(&t);
    char line[512]{};
    int n=std::snprintf(line,sizeof(line),
        "{\"utc\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\",\"tick_ms\":%llu,\"pid\":%lu,\"stage\":\"%s\",\"result\":\"%s\",\"value\":%lld,\"code\":%lld}\n",
        t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,
        static_cast<unsigned long long>(GetTickCount64()),static_cast<unsigned long>(GetCurrentProcessId()),stage,result,value,code);
    if(n<=0 || n>=static_cast<int>(sizeof(line)))return;
    HANDLE file=CreateFileW(Path().c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,
                            nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    DWORD written=0;WriteFile(file,line,static_cast<DWORD>(n),&written,nullptr);CloseHandle(file);
}
}

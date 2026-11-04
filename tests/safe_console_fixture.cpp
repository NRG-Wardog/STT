// Test-owned console input sink. Records characters but never runs commands.
// No Enter is generated; if received, it is counted rather than executed.
#include <windows.h>
#include <cstdio>
#include <string>

int wmain(int argc,wchar_t **argv) {
    if(argc!=3)return 2;
    HANDLE file=CreateFileW(argv[1],GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return 3;
    HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,argv[2]);
    if(!stop){CloseHandle(file);return 4;}
    HANDLE input=GetStdHandle(STD_INPUT_HANDLE),output=GetStdHandle(STD_OUTPUT_HANDLE);
    if(input==INVALID_HANDLE_VALUE||output==INVALID_HANDLE_VALUE){CloseHandle(stop);CloseHandle(file);return 5;}
    DWORD oldMode=0;if(!GetConsoleMode(input,&oldMode)){CloseHandle(stop);CloseHandle(file);return 6;}
    SetConsoleMode(input,ENABLE_EXTENDED_FLAGS|ENABLE_WINDOW_INPUT);
    const wchar_t *banner=L"HebEN test input: no commands are executed.\r\n";DWORD written=0;
    WriteConsoleW(output,banner,static_cast<DWORD>(wcslen(banner)),&written,nullptr);
    // Header identifies this process and makes readiness observable without
    // reading or modifying any other terminal session.
    char ready[80]{};int n=std::snprintf(ready,sizeof(ready),"PID=%lu\n",GetCurrentProcessId());
    WriteFile(file,ready,static_cast<DWORD>(n),&written,nullptr);FlushFileBuffers(file);
    HANDLE waits[2]={stop,input};unsigned enterCount=0;
    for(;;) {
        DWORD wait=WaitForMultipleObjects(2,waits,FALSE,INFINITE);
        if(wait==WAIT_OBJECT_0)break;
        if(wait!=WAIT_OBJECT_0+1)break;
        INPUT_RECORD rec{};DWORD count=0;
        if(!ReadConsoleInputW(input,&rec,1,&count)||!count)continue;
        if(rec.EventType!=KEY_EVENT || !rec.Event.KeyEvent.bKeyDown)continue;
        wchar_t c=rec.Event.KeyEvent.uChar.UnicodeChar;
        if(c==L'\r'||c==L'\n'){++enterCount;continue;}
        if(c<0x20||c==0x7f)continue;
        char bytes[8]{};int len=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,&c,1,bytes,sizeof(bytes),nullptr,nullptr);
        if(len>0){WriteFile(file,bytes,static_cast<DWORD>(len),&written,nullptr);FlushFileBuffers(file);WriteConsoleW(output,&c,1,&written,nullptr);}
    }
    char tail[80]{};n=std::snprintf(tail,sizeof(tail),"\nENTER_COUNT=%u\n",enterCount);
    WriteFile(file,tail,static_cast<DWORD>(n),&written,nullptr);CloseHandle(file);
    SetConsoleMode(input,oldMode);CloseHandle(stop);return 0;
}

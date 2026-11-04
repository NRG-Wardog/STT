// Read the cursor line of a caller-owned, newly launched console process.
// The caller verifies process identity and uses only a test-owned prompt.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int wmain(int argc,wchar_t **argv) {
    if(argc!=2)return 2;
    DWORD pid=static_cast<DWORD>(_wtoi(argv[1]));if(!pid)return 2;
    HANDLE out=nullptr,initial=GetStdHandle(STD_OUTPUT_HANDLE);
    if(initial && initial!=INVALID_HANDLE_VALUE)
        DuplicateHandle(GetCurrentProcess(),initial,GetCurrentProcess(),&out,0,FALSE,DUPLICATE_SAME_ACCESS);
    FreeConsole();
    if(!AttachConsole(pid)){std::printf("{\"status\":\"ATTACH_FAILED\",\"code\":%lu}\n",GetLastError());return 3;}
    HANDLE h=CreateFileW(L"CONOUT$",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    if(h==INVALID_HANDLE_VALUE){FreeConsole();return 4;}
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if(!GetConsoleScreenBufferInfo(h,&info)){CloseHandle(h);FreeConsole();return 5;}
    int width=info.dwSize.X;if(width<=0||width>10000){CloseHandle(h);FreeConsole();return 6;}
    std::vector<wchar_t> text(static_cast<size_t>(width)+1);DWORD count=0;COORD pos{0,info.dwCursorPosition.Y};
    bool ok=ReadConsoleOutputCharacterW(h,text.data(),static_cast<DWORD>(width),pos,&count)!=FALSE;
    CloseHandle(h);FreeConsole();if(!ok)return 7;
    while(count&&text[count-1]==L' ')--count;
    int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(count),nullptr,0,nullptr,nullptr);
    if(bytes<0)return 8;std::string utf8(static_cast<size_t>(bytes),'\0');
    if(bytes)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(count),utf8.data(),bytes,nullptr,nullptr);
    DWORD written=0;if(out)WriteFile(out,utf8.data(),static_cast<DWORD>(utf8.size()),&written,nullptr);
    if(out)CloseHandle(out);
    return 0;
}

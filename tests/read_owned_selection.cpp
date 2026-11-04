// Query only selection offsets in a test-owned Notepad edit control.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

int wmain(int argc,wchar_t **argv) {
    if(argc!=3 && argc!=5)return 2;
    HWND hwnd=reinterpret_cast<HWND>(static_cast<UINT_PTR>(_wcstoui64(argv[1],nullptr,10)));
    DWORD expected=static_cast<DWORD>(_wtoi(argv[2])),owner=0;
    DWORD tid=GetWindowThreadProcessId(hwnd,&owner);
    if(!IsWindow(hwnd)||owner!=expected||GetForegroundWindow()!=hwnd)return 3;
    GUITHREADINFO info{};info.cbSize=sizeof(info);
    if(!GetGUIThreadInfo(tid,&info)||!info.hwndFocus)return 4;
    DWORD focusOwner=0;GetWindowThreadProcessId(info.hwndFocus,&focusOwner);
    if(focusOwner!=expected)return 5;
    wchar_t cls[128]{};GetClassNameW(info.hwndFocus,cls,128);
    // Restrict the query to native Edit/RichEdit controls; never read content.
    if(_wcsicmp(cls,L"Edit")!=0 && _wcsnicmp(cls,L"RichEdit",8)!=0) {
        std::wprintf(L"{\"status\":\"UNSUPPORTED_CONTROL\",\"class\":\"%ls\"}\n",cls);return 6;
    }
    DWORD_PTR result=0;
    if(argc==5) {
        int start=_wtoi(argv[3]),end=_wtoi(argv[4]);
        if(start<0||end<start||end>64)return 8;
        if(!SendMessageTimeoutW(info.hwndFocus,EM_SETSEL,static_cast<WPARAM>(start),static_cast<LPARAM>(end),SMTO_ABORTIFHUNG,2000,&result))return 9;
    }
    if(!SendMessageTimeoutW(info.hwndFocus,EM_GETSEL,0,0,SMTO_ABORTIFHUNG,2000,&result))return 7;
    unsigned start=LOWORD(result),end=HIWORD(result);
    std::wprintf(L"{\"status\":\"PASS_OFFSETS\",\"class\":\"%ls\",\"start\":%u,\"end\":%u}\n",cls,start,end);
    return 0;
}

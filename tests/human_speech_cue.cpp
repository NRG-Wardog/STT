// Temporary non-activating cue for a supervised microphone test only.
// No editor text, microphone data, or keyboard events are read.
#include <windows.h>
#include <shellapi.h>
#include <string>

struct Cue {HANDLE begin=nullptr,ready=nullptr,finish=nullptr;ULONGLONG start=0;bool began=false;};
static LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto *s=reinterpret_cast<Cue*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){s=static_cast<Cue*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));return TRUE;}
    if(!s)return DefWindowProcW(hwnd,msg,wp,lp);
    switch(msg) {
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_TIMER:
        if(WaitForSingleObject(s->finish,0)==WAIT_OBJECT_0 || GetTickCount64()-s->start>30000){DestroyWindow(hwnd);return 0;}
        if(!s->began && GetTickCount64()-s->start>=5000){s->began=true;SetEvent(s->begin);}
        InvalidateRect(hwnd,nullptr,TRUE);return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);RECT box{};GetClientRect(hwnd,&box);
        HBRUSH bg=CreateSolidBrush(RGB(24,32,46));FillRect(dc,&box,bg);DeleteObject(bg);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
        HFONT title=CreateFontW(38,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        HGDIOBJ old=SelectObject(dc,title);std::wstring status;
        if(WaitForSingleObject(s->ready,0)==WAIT_OBJECT_0)status=L"SPEAK NOW";
        else if(s->began)status=L"Starting microphone...";
        else {int remaining=5-static_cast<int>((GetTickCount64()-s->start)/1000);status=L"Get ready: "+std::to_wstring(remaining);}
        RECT headline{20,10,box.right-20,70};DrawTextW(dc,status.c_str(),-1,&headline,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(dc,old);DeleteObject(title);
        HFONT body=CreateFontW(22,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        old=SelectObject(dc,body);
        const wchar_t *script=L"שלום עולם, היום אנחנו בודקים הכתבה בעברית ובאנגלית,\r\nthis is a streaming dictation test,\r\nוהטקסט צריך להופיע בזמן שאני מדבר.";
        RECT bodyRect{24,75,box.right-24,box.bottom-15};DrawTextW(dc,script,-1,&bodyRect,DT_CENTER|DT_WORDBREAK);
        SelectObject(dc,old);DeleteObject(body);EndPaint(hwnd,&ps);return 0;
    }
    case WM_DESTROY:KillTimer(hwnd,1);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    int count=0;wchar_t **args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args||count!=5){if(args)LocalFree(args);return 2;}
    Cue cue;cue.begin=OpenEventW(EVENT_MODIFY_STATE,FALSE,args[1]);cue.ready=OpenEventW(SYNCHRONIZE,FALSE,args[2]);cue.finish=OpenEventW(SYNCHRONIZE,FALSE,args[3]);
    HWND owner=reinterpret_cast<HWND>(static_cast<UINT_PTR>(_wcstoui64(args[4],nullptr,10)));LocalFree(args);
    if(!cue.begin||!cue.ready||!cue.finish||!IsWindow(owner))return 3;
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=instance;wc.lpfnWndProc=Proc;wc.lpszClassName=L"HebENHumanSpeechTestCue";
    if(!RegisterClassExW(&wc))return 4;
    MONITORINFO info{};info.cbSize=sizeof(info);if(!GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&info))return 5;
    int width=760,height=220,x=info.rcWork.left+(info.rcWork.right-info.rcWork.left-width)/2,y=info.rcWork.top+(info.rcWork.bottom-info.rcWork.top-height)/2;
    cue.start=GetTickCount64();
    HWND hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,wc.lpszClassName,L"HebEN test cue",WS_POPUP,x,y,width,height,nullptr,nullptr,instance,&cue);
    if(!hwnd)return 6;SetWindowPos(hwnd,HWND_TOPMOST,x,y,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);SetTimer(hwnd,1,100,nullptr);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    CloseHandle(cue.begin);CloseHandle(cue.ready);CloseHandle(cue.finish);return 0;
}

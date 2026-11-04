#include "../src/listening_indicator.hpp"
#include <cstdio>

int main() {
    RECT bounds{0,0,800,600}, caret{100,100,101,120};
    POINT p=ListeningIndicator::TestPlacement(caret,bounds,12,12);
    if (p.x!=94 || p.y!=104) return 1;
    RECT edge{2,2,3,22}; p=ListeningIndicator::TestPlacement(edge,bounds,12,12);
    if (p.x!=-4 || p.y!=6) return 2;
    RECT seam{},ltrBefore{100,100,110,120},ltrAfter{110,100,120,120};
    if(!ListeningIndicator::TestSeam(ltrBefore,ltrAfter,seam) || seam.left!=110)return 13;
    if(!ListeningIndicator::TestSeam(ltrAfter,ltrBefore,seam) || seam.left!=110)return 14;
    RECT distant{140,100,150,120};if(ListeningIndicator::TestSeam(ltrBefore,distant,seam))return 15;
    HWND foreground=GetForegroundWindow();
    DWORD clipboard=GetClipboardSequenceNumber();
    ListeningIndicator indicator;
    // Keep this automated UI check off the user's active desktop area.
    RECT offscreen{-5000,-5000,-4999,-4980};
    indicator.TestAnchor(offscreen);
    indicator.ShowListening(nullptr);
    HWND window=indicator.TestWindow();
    if (!window || !IsWindowVisible(window)) return 3;
    LONG_PTR style=GetWindowLongPtrW(window,GWL_EXSTYLE);
    if (!(style & WS_EX_NOACTIVATE) || !(style & WS_EX_TRANSPARENT) || !(style & WS_EX_LAYERED)) return 4;
    if (GetForegroundWindow()!=foreground) return 5;
    RECT r{}; if (!GetWindowRect(window,&r) || r.right-r.left!=12 || r.bottom-r.top!=12 || r.left!=-5006 || r.top!=-4996) return 6;
    if (SendMessageW(window,WM_NCHITTEST,0,0)!=HTTRANSPARENT) return 7;
    HDC dc=GetDC(window);
    if (!dc) return 11;
    COLORREF center=GetPixel(dc,6,6);
    ReleaseDC(window,dc);
    if (center!=RGB(220,45,45)) { std::fprintf(stderr,"Unexpected dot color: %lu\n",static_cast<unsigned long>(center)); return 12; }
    indicator.ShowProcessing(nullptr);
    ULONGLONG processDeadline=GetTickCount64()+4500;
    while(GetTickCount64()<processDeadline) {
        MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        Sleep(20);
    }
    if(!indicator.TestWindow() || !IsWindowVisible(indicator.TestWindow()))return 16;
    indicator.ShowError(nullptr,L"Microphone unavailable");
    if (!IsWindowVisible(indicator.TestWindow()) || GetForegroundWindow()!=foreground) return 8;
    ULONGLONG deadline=GetTickCount64()+5500;
    while (indicator.TestWindow() && GetTickCount64()<deadline) {
        MSG msg{};
        while (PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(20);
    }
    if (indicator.TestWindow()) return 9;
    if (GetClipboardSequenceNumber()!=clipboard) return 10;
    std::puts("Indicator smoke test passed: placement, visibility, focus, click-through, error timeout, clipboard.");
    return 0;
}

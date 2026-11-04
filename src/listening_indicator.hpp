#pragma once

#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <algorithm>
#include <cmath>
#include <string>
#include "diagnostics.hpp"

// UI-thread-only, no-activate overlay. A timer runs only while the overlay is
// visible so it can follow carets in applications that do not emit WinEvents.
class ListeningIndicator {
public:
    ~ListeningIndicator() { Hide(); }
    ListeningIndicator(const ListeningIndicator&) = delete;
    ListeningIndicator& operator=(const ListeningIndicator&) = delete;
    ListeningIndicator() = default;

    void ShowListening(IUIAutomation *automation) {
        automation_ = automation;
        error_=false;
        transient_=false;
        if (!EnsureWindow()) return;
        UpdatePosition();
        InvalidateRect(window_,nullptr,TRUE);
        UpdateWindow(window_);
        SetTimer(window_, kTimer, 400, nullptr);
    }

    void ShowError(IUIAutomation *automation, const wchar_t *message) {
        automation_ = automation;
        (void)message;
        error_ = true;
        transient_=true;
        expires_ = GetTickCount64() + 4000;
        if (!EnsureWindow()) return;
        UpdatePosition();
        InvalidateRect(window_, nullptr, TRUE);
        UpdateWindow(window_);
        SetTimer(window_, kTimer, 400, nullptr);
    }

    void ShowProcessing(IUIAutomation *automation) {
        automation_=automation;error_=true;transient_=false;
        if(!EnsureWindow())return;
        UpdatePosition();InvalidateRect(window_,nullptr,TRUE);UpdateWindow(window_);
        SetTimer(window_,kTimer,400,nullptr);
    }

    void Hide() {
        if (window_) {
            KillTimer(window_, kTimer);
            DestroyWindow(window_);
            window_ = nullptr;
        }
        automation_ = nullptr;
        lastCaretSource_=-1;
    }

#ifdef HEBEN_INDICATOR_TEST
    HWND TestWindow() const { return window_; }
    void TestAnchor(RECT caret) { testCaret_=caret; testCaretEnabled_=true; }
    static POINT TestPlacement(RECT caret, RECT bounds, int width, int height) {
        return Place(caret,bounds,width,height);
    }
    static bool TestSeam(RECT before,RECT after,RECT &caret) { return AdjacentSeam(before,after,caret); }
#endif

private:
    static constexpr UINT_PTR kTimer = 1;
    static constexpr COLORREF kTransparent = RGB(255,0,255);
    HWND window_ = nullptr;
    IUIAutomation *automation_ = nullptr; // owned by the UI thread/app
    bool error_=false;
    bool transient_=false;
    ULONGLONG expires_ = 0;
    int lastCaretSource_=-1;
#ifdef HEBEN_INDICATOR_TEST
    RECT testCaret_{};
    bool testCaretEnabled_=false;
#endif

    static POINT Place(RECT caret, RECT bounds, int width, int height) {
        (void)bounds;
        LONG x = caret.left - width/2;
        LONG y = caret.top + ((caret.bottom-caret.top)-height)/2;
        return {x,y};
    }

    static bool NativeCaret(RECT &rect) {
        HWND foreground=GetForegroundWindow();
        if (!foreground) return false;
        GUITHREADINFO info{}; info.cbSize=sizeof(info);
        DWORD tid=GetWindowThreadProcessId(foreground,nullptr);
        if (!GetGUIThreadInfo(tid,&info) || !info.hwndCaret) return false;
        POINT top{info.rcCaret.left,info.rcCaret.top};
        POINT bottom{info.rcCaret.right,info.rcCaret.bottom};
        if (!ClientToScreen(info.hwndCaret,&top) || !ClientToScreen(info.hwndCaret,&bottom)) return false;
        if (bottom.y<=top.y) return false;
        rect={top.x,top.y,std::max(top.x+1,bottom.x),bottom.y};
        return true;
    }

    static bool AdjacentSeam(RECT a,RECT b,RECT &caret) {
        LONG top=std::max(a.top,b.top),bottom=std::min(a.bottom,b.bottom);
        if(bottom<=top || a.right<=a.left || b.right<=b.left)return false;
        LONG first=std::abs(a.right-b.left),second=std::abs(a.left-b.right);
        // Only accept an unambiguous shared edge on the same visual line.
        if(std::min(first,second)>2 || first==second)return false;
        LONG x=first<second?(a.right+b.left)/2:(a.left+b.right)/2;
        caret={x,top,x+1,bottom};return true;
    }

    static bool RangeRect(IUIAutomationTextRange *range,RECT &rect) {
        SAFEARRAY *bounds=nullptr;
        if(!range || FAILED(range->GetBoundingRectangles(&bounds)) || !bounds)return false;
        LONG lo=0,hi=-1;bool found=false;
        if(SUCCEEDED(SafeArrayGetLBound(bounds,1,&lo)) && SUCCEEDED(SafeArrayGetUBound(bounds,1,&hi)) && hi-lo==3) {
            double v[4]{};bool ok=true;
            for(LONG i=0;i<4;++i){LONG index=lo+i;if(FAILED(SafeArrayGetElement(bounds,&index,&v[i])))ok=false;}
            if(ok && std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]) && std::isfinite(v[3]) &&
               v[0]>-100000 && v[0]<100000 && v[1]>-100000 && v[1]<100000 &&
               v[2]>=0 && v[2]<100000 && v[3]>0 && v[3]<100000) {
                rect={static_cast<LONG>(v[0]),static_cast<LONG>(v[1]),
                      static_cast<LONG>(v[0]+std::max(1.0,v[2])),static_cast<LONG>(v[1]+v[3])};found=true;
            }
        }
        SafeArrayDestroy(bounds);return found;
    }

    static bool RangeCaret(IUIAutomationTextRange *range,RECT &rect) {
        if(RangeRect(range,rect) && rect.right-rect.left<=2)return true;
        // Degenerate ranges often expose no rectangle. Infer the caret only
        // when both adjacent character boxes meet at an unambiguous edge.
        // No characters or document contents are read.
        IUIAutomationTextRange *before=nullptr,*after=nullptr;RECT left{},right{};
        int movedBefore=0,movedAfter=0;bool ok=false;
        if(SUCCEEDED(range->Clone(&before)) && before && SUCCEEDED(range->Clone(&after)) && after &&
           SUCCEEDED(before->MoveEndpointByUnit(TextPatternRangeEndpoint_Start,TextUnit_Character,-1,&movedBefore)) && movedBefore==-1 &&
           SUCCEEDED(after->MoveEndpointByUnit(TextPatternRangeEndpoint_End,TextUnit_Character,1,&movedAfter)) && movedAfter==1 &&
           RangeRect(before,left) && RangeRect(after,right))ok=AdjacentSeam(left,right,rect);
        if(before)before->Release();if(after)after->Release();return ok;
    }

    bool AutomationCaret(RECT &rect) const {
        if (!automation_) return false;
        IUIAutomationElement *element=nullptr;
        if (FAILED(automation_->GetFocusedElement(&element)) || !element) return false;
        bool found=false;
        IUnknown *unknown=nullptr;
        if (SUCCEEDED(element->GetCurrentPattern(UIA_TextPattern2Id,&unknown)) && unknown) {
            IUIAutomationTextPattern2 *pattern=nullptr;
            if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&pattern))) && pattern) {
                BOOL active=FALSE; IUIAutomationTextRange *range=nullptr;
                if (SUCCEEDED(pattern->GetCaretRange(&active,&range)) && active && range) {
                    found=RangeCaret(range,rect);
                }
                if (range) range->Release();
                pattern->Release();
            }
            unknown->Release();
        }
        if(!found) {
            BOOL focused=FALSE;
            IUnknown *candidate=nullptr;
            if(SUCCEEDED(element->get_CurrentHasKeyboardFocus(&focused)) && focused &&
               SUCCEEDED(element->GetCurrentPattern(UIA_TextPatternId,&candidate)) && candidate) {
                IUIAutomationTextPattern *pattern=nullptr;
                if(SUCCEEDED(candidate->QueryInterface(IID_PPV_ARGS(&pattern))) && pattern) {
                    IUIAutomationTextRangeArray *selection=nullptr;
                    if(SUCCEEDED(pattern->GetSelection(&selection)) && selection) {
                        int length=0;selection->get_Length(&length);
                        IUIAutomationTextRange *range=nullptr;
                        if(length==1 && SUCCEEDED(selection->GetElement(0,&range)) && range) {
                            int comparison=1;
                            if(SUCCEEDED(range->CompareEndpoints(TextPatternRangeEndpoint_Start,range,TextPatternRangeEndpoint_End,&comparison)) && comparison==0)
                                found=RangeCaret(range,rect);
                        }
                        if(range)range->Release();selection->Release();
                    }
                    pattern->Release();
                }
                candidate->Release();
            }
        }
        element->Release();
        return found;
    }

    bool EnsureWindow() {
        if (window_) return true;
        HINSTANCE instance=GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.hInstance=instance;
        wc.lpszClassName=L"HebENListeningIndicator"; wc.lpfnWndProc=WndProc;
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if (!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return false;
        window_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST,
            wc.lpszClassName,L"HebEN listening indicator",WS_POPUP,0,0,18,18,nullptr,nullptr,instance,this);
        if (!window_) return false;
        if (!SetLayeredWindowAttributes(window_,kTransparent,255,LWA_COLORKEY)) { DestroyWindow(window_); window_=nullptr; return false; }
        return true;
    }

    void UpdatePosition() {
        if (!window_) return;
        RECT caret{};
        bool found=false;int source=0;
#ifdef HEBEN_INDICATOR_TEST
        if (testCaretEnabled_) { caret=testCaret_; found=true;source=3; }
#endif
        if(!found && NativeCaret(caret)){found=true;source=1;}
        if(!found && AutomationCaret(caret)){found=true;source=2;}
        if(source!=lastCaretSource_) {
            heben::diagnostics::Record("indicator",source==1?"native_caret":source==2?"uia_caret":source==3?"test_anchor":"caret_unavailable");
            lastCaretSource_=source;
        }
        if (!found) {
            // Do not claim an approximate field corner is the insertion point.
            ShowWindow(window_,SW_HIDE);
            return;
        }
        POINT location{caret.left,caret.top};
        HMONITOR monitor=MonitorFromPoint(location,MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{}; info.cbSize=sizeof(info);
        if (!GetMonitorInfoW(monitor,&info)) SystemParametersInfoW(SPI_GETWORKAREA,0,&info.rcWork,0);
        int width=12, height=12;
        POINT p=Place(caret,info.rcWork,width,height);
        SetWindowPos(window_,HWND_TOPMOST,p.x,p.y,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    }

    static LRESULT CALLBACK WndProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
        auto *self=reinterpret_cast<ListeningIndicator*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if (message==WM_NCCREATE) {
            self=static_cast<ListeningIndicator*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd,message,wp,lp);
        switch(message) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_ERASEBKGND: return 1;
        case WM_TIMER:
            if (wp==kTimer) {
                if (self->transient_ && GetTickCount64()>=self->expires_) self->Hide();
                else self->UpdatePosition();
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps); RECT r{}; GetClientRect(hwnd,&r);
            HBRUSH transparent=CreateSolidBrush(kTransparent); FillRect(dc,&r,transparent); DeleteObject(transparent);
            HBRUSH brush=CreateSolidBrush(self->error_?RGB(230,150,0):RGB(220,45,45)); HPEN pen=CreatePen(PS_SOLID,1,RGB(255,255,255));
            HGDIOBJ oldBrush=SelectObject(dc,brush), oldPen=SelectObject(dc,pen);
            Ellipse(dc,1,1,11,11);
            SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(brush); DeleteObject(pen);
            EndPaint(hwnd,&ps); return 0;
        }
        default: return DefWindowProcW(hwnd,message,wp,lp);
        }
    }
};

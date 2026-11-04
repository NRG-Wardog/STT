// Inspect only HebEN's diagnostic icon in Explorer's temporary overflow popup.
#include <windows.h>
#include <shellapi.h>
#include <UIAutomation.h>
#include <cstdio>
#include <vector>
#include <string>

static IUIAutomationElement* FindNamed(IUIAutomation *uia,IUIAutomationElement *root,const wchar_t *name,int *matches=nullptr){
    VARIANT value{};value.vt=VT_BSTR;value.bstrVal=SysAllocString(name);IUIAutomationCondition *condition=nullptr;IUIAutomationElement *found=nullptr;
    if(SUCCEEDED(uia->CreatePropertyCondition(UIA_NamePropertyId,value,&condition))){
        if(matches){IUIAutomationElementArray *items=nullptr;if(SUCCEEDED(root->FindAll(TreeScope_Descendants,condition,&items))&&items){items->get_Length(matches);if(*matches>0)items->GetElement(0,&found);items->Release();}}
        else root->FindFirst(TreeScope_Descendants,condition,&found);
    }
    if(condition)condition->Release();VariantClear(&value);return found;
}
static bool Capture(const RECT &r,const wchar_t *path){
    int w=r.right-r.left,h=r.bottom-r.top;if(w<=0||h<=0||w>128||h>128)return false;
    HDC screen=GetDC(nullptr),mem=CreateCompatibleDC(screen);HBITMAP bmp=CreateCompatibleBitmap(screen,w,h);if(!screen||!mem||!bmp)return false;
    auto old=SelectObject(mem,bmp);bool ok=BitBlt(mem,0,0,w,h,screen,r.left,r.top,SRCCOPY)!=FALSE;SelectObject(mem,old);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    std::vector<unsigned char> pixels(static_cast<size_t>(w)*h*4);ok=ok&&GetDIBits(mem,bmp,0,h,pixels.data(),&info,DIB_RGB_COLORS)==h;
    DeleteObject(bmp);DeleteDC(mem);ReleaseDC(nullptr,screen);if(!ok)return false;
    BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+static_cast<DWORD>(pixels.size());
    HANDLE out=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(out==INVALID_HANDLE_VALUE)return false;
    DWORD n=0;ok=WriteFile(out,&file,sizeof(file),&n,nullptr)&&WriteFile(out,&info.bmiHeader,sizeof(info.bmiHeader),&n,nullptr)&&WriteFile(out,pixels.data(),static_cast<DWORD>(pixels.size()),&n,nullptr);
    CloseHandle(out);return ok;
}
int wmain(int argc,wchar_t **argv){
    if(argc!=4)return 2;LASTINPUTINFO last{sizeof(last),0};GetLastInputInfo(&last);
    HWND iconOwner=reinterpret_cast<HWND>(static_cast<UINT_PTR>(_wcstoui64(argv[2],nullptr,10)));
    if(static_cast<DWORD>(GetTickCount()-last.dwTime)<30000){printf("{\"status\":\"BLOCKED_USER_ACTIVE\"}\n");return 3;}
    HWND popup=FindWindowW(L"TopLevelWindowForOverflowXamlIsland",nullptr);
    if(popup&&IsWindowVisible(popup)){printf("{\"status\":\"BLOCKED_OVERFLOW_ALREADY_OPEN\"}\n");return 4;}
    HWND foreground=GetForegroundWindow();CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    IUIAutomation *uia=nullptr;IUIAutomationElement *tray=nullptr,*arrow=nullptr,*popupElement=nullptr,*icon=nullptr;IUIAutomationInvokePattern *invoke=nullptr;
    HRESULT hr=CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&uia));
    if(SUCCEEDED(hr))hr=uia->ElementFromHandle(FindWindowW(L"Shell_TrayWnd",nullptr),&tray);
    if(SUCCEEDED(hr))arrow=FindNamed(uia,tray,L"Show Hidden Icons");
    IUnknown *unknown=nullptr;if(arrow)hr=arrow->GetCurrentPattern(UIA_InvokePatternId,&unknown);else hr=E_FAIL;
    if(SUCCEEDED(hr)&&unknown)hr=unknown->QueryInterface(IID_PPV_ARGS(&invoke));if(unknown)unknown->Release();
    bool opened=false,captured=false;RECT rect{};HRESULT rectResult=E_FAIL;int matches=0;
    if(SUCCEEDED(hr)&&invoke){hr=invoke->Invoke();opened=SUCCEEDED(hr);}
    if(opened){
        for(int i=0;i<30;++i){
            Sleep(100);popup=FindWindowW(L"TopLevelWindowForOverflowXamlIsland",nullptr);
            if(!popup||!IsWindowVisible(popup))continue;
            if(popupElement){popupElement->Release();popupElement=nullptr;}
            if(SUCCEEDED(uia->ElementFromHandle(popup,&popupElement))&&popupElement){
                icon=FindNamed(uia,popupElement,argv[3],&matches);if(icon)break;
            }
        }
        NOTIFYICONIDENTIFIER ni{};ni.cbSize=sizeof(ni);ni.hWnd=iconOwner;ni.uID=1202;
        rectResult=Shell_NotifyIconGetRect(&ni,&rect);
        RECT popupRect{};if(popup)GetWindowRect(popup,&popupRect);
        bool contained=SUCCEEDED(rectResult)&&rect.left>=popupRect.left&&rect.top>=popupRect.top&&rect.right<=popupRect.right&&rect.bottom<=popupRect.bottom;
        if(icon){BOOL off=TRUE;RECT r{};icon->get_CurrentBoundingRectangle(&r);icon->get_CurrentIsOffscreen(&off);if(!off){rect=r;contained=true;}}
        LASTINPUTINFO during{sizeof(during),0};GetLastInputInfo(&during);
        if(contained && during.dwTime==last.dwTime)captured=Capture(rect,argv[1]);
        if(invoke)invoke->Invoke(); // Close only the popup this probe opened.
    }
    bool unchanged=GetForegroundWindow()==foreground;
    LASTINPUTINFO now{sizeof(now),0};GetLastInputInfo(&now);
    if(!unchanged && now.dwTime==last.dwTime && foreground){
        MSG ignored{};PeekMessageW(&ignored,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
        DWORD here=GetCurrentThreadId(),active=GetWindowThreadProcessId(GetForegroundWindow(),nullptr);
        bool attached=active&&active!=here&&AttachThreadInput(here,active,TRUE);
        SetForegroundWindow(foreground);
        if(attached)AttachThreadInput(here,active,FALSE);
    }
    printf("{\"status\":\"%s\",\"opened\":%s,\"found\":%s,\"matching_icons\":%d,\"captured\":%s,\"rect\":[%ld,%ld,%ld,%ld],\"rect_hresult\":%ld,\"foreground_unchanged\":%s,\"foreground_restored\":%s,\"user_input_during_test\":%s,\"hresult\":%ld}\n",captured?"CAPTURED":"NOT_CAPTURED",opened?"true":"false",icon?"true":"false",matches,captured?"true":"false",rect.left,rect.top,rect.right,rect.bottom,rectResult,unchanged?"true":"false",GetForegroundWindow()==foreground?"true":"false",now.dwTime==last.dwTime?"false":"true",hr);
    if(icon)icon->Release();if(popupElement)popupElement->Release();if(invoke)invoke->Release();if(arrow)arrow->Release();if(tray)tray->Release();if(uia)uia->Release();CoUninitialize();return captured?0:1;
}

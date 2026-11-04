#include <windows.h>
#include <UIAutomation.h>
#include <cstdio>
#include <string>
#include <algorithm>
#include <cwctype>
int wmain(){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);IUIAutomation *uia=nullptr;IUIAutomationElement *root=nullptr;
    IUIAutomationCondition *all=nullptr;IUIAutomationElementArray *items=nullptr;
    HWND tray=FindWindowW(L"Shell_TrayWnd",nullptr);
    HRESULT hr=CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&uia));
    if(SUCCEEDED(hr))hr=uia->ElementFromHandle(tray,&root);
    if(SUCCEEDED(hr))hr=uia->CreateTrueCondition(&all);
    if(SUCCEEDED(hr))hr=root->FindAll(TreeScope_Descendants,all,&items);
    int count=0;if(items)items->get_Length(&count);
    for(int i=0;i<count;i++){
        IUIAutomationElement *e=nullptr;BSTR n=nullptr,a=nullptr;if(FAILED(items->GetElement(i,&e))||!e)continue;
        e->get_CurrentName(&n);e->get_CurrentAutomationId(&a);std::wstring v=n?n:L"",id=a?a:L"";
        std::wstring match=v+L" "+id;std::transform(match.begin(),match.end(),match.begin(),towlower);
        if(match.find(L"hidden")!=std::wstring::npos || match.find(L"overflow")!=std::wstring::npos || match.find(L"heben")!=std::wstring::npos){
            RECT r{};BOOL off=FALSE;e->get_CurrentBoundingRectangle(&r);e->get_CurrentIsOffscreen(&off);
            wprintf(L"name=[%ls] id=[%ls] rect=%ld,%ld,%ld,%ld offscreen=%d\n",v.c_str(),id.c_str(),r.left,r.top,r.right,r.bottom,off);
        }
        if(n)SysFreeString(n);if(a)SysFreeString(a);e->Release();
    }
    if(items)items->Release();if(all)all->Release();if(root)root->Release();if(uia)uia->Release();CoUninitialize();return 0;
}

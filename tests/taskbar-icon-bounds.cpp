// Return only the bounds of our own settings taskbar button, if exposed.
#include <windows.h>
#include <UIAutomation.h>
#include <cstdio>
int wmain() {
    HWND shell=FindWindowW(L"Shell_TrayWnd",nullptr);if(!shell)return 2;
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 3;
    IUIAutomation *uia=nullptr;IUIAutomationElement *root=nullptr,*found=nullptr;IUIAutomationCondition *condition=nullptr;IUIAutomationElementArray *items=nullptr;
    HRESULT hr=CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&uia));
    if(SUCCEEDED(hr))hr=uia->ElementFromHandle(shell,&root);
    if(SUCCEEDED(hr))hr=uia->CreateTrueCondition(&condition);
    if(SUCCEEDED(hr))hr=root->FindAll(TreeScope_Descendants,condition,&items);
    int count=0;if(items)items->get_Length(&count);
    for(int i=0;i<count && !found;++i){
        IUIAutomationElement *element=nullptr;BSTR title=nullptr;
        if(SUCCEEDED(items->GetElement(i,&element)) && element){
            element->get_CurrentName(&title);
            if(title && wcsstr(title,L"HebEN keyboard shortcut")){found=element;element=nullptr;}
            if(title)SysFreeString(title);if(element)element->Release();
        }
    }
    RECT rect{};BOOL offscreen=TRUE;
    if(SUCCEEDED(hr)&&found){found->get_CurrentBoundingRectangle(&rect);found->get_CurrentIsOffscreen(&offscreen);}
    std::printf("{\"found\":%s,\"offscreen\":%s,\"rect\":[%ld,%ld,%ld,%ld],\"hresult\":%ld}\n",found?"true":"false",offscreen?"true":"false",rect.left,rect.top,rect.right,rect.bottom,hr);
    if(found)found->Release();if(items)items->Release();if(condition)condition->Release();if(root)root->Release();if(uia)uia->Release();CoUninitialize();
    return found&&!offscreen&&rect.right>rect.left&&rect.bottom>rect.top?0:1;
}

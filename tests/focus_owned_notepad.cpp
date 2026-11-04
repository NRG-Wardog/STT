// Focus a writable non-password document in a test-owned Notepad window.
// Only accessibility metadata is read; no editor value or document text.
#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <cstdio>
#include <cstdlib>

int wmain(int argc,wchar_t **argv) {
    if(argc!=3)return 2;
    HWND hwnd=reinterpret_cast<HWND>(static_cast<UINT_PTR>(_wcstoui64(argv[1],nullptr,10)));
    DWORD expected=static_cast<DWORD>(_wtoi(argv[2])),owner=0;GetWindowThreadProcessId(hwnd,&owner);
    if(!IsWindow(hwnd)||owner!=expected||GetForegroundWindow()!=hwnd)return 3;
    HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);IUIAutomation *uia=nullptr;
    HRESULT hr=CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&uia));
    if(FAILED(hr)||!uia)return 4;
    IUIAutomationElement *root=nullptr,*selected=nullptr;IUIAutomationCondition *cond=nullptr;IUIAutomationElementArray *items=nullptr;
    hr=uia->ElementFromHandle(hwnd,&root);VARIANT v;VariantInit(&v);v.vt=VT_I4;v.lVal=UIA_DocumentControlTypeId;
    if(SUCCEEDED(hr)&&root)hr=uia->CreatePropertyCondition(UIA_ControlTypePropertyId,v,&cond);
    if(SUCCEEDED(hr)&&cond)hr=root->FindAll(TreeScope_Descendants,cond,&items);
    int count=0;if(items)items->get_Length(&count);
    for(int i=0;i<count&&!selected;++i){
        IUIAutomationElement *e=nullptr;if(FAILED(items->GetElement(i,&e))||!e)continue;
        int pid=0;BOOL password=TRUE,enabled=FALSE,readonly=TRUE,offscreen=TRUE;IUnknown *raw=nullptr;
        bool ok=SUCCEEDED(e->get_CurrentProcessId(&pid))&&static_cast<DWORD>(pid)==expected &&
            SUCCEEDED(e->get_CurrentIsPassword(&password))&&!password && SUCCEEDED(e->get_CurrentIsEnabled(&enabled))&&enabled &&
            SUCCEEDED(e->get_CurrentIsOffscreen(&offscreen))&&!offscreen &&
            SUCCEEDED(e->GetCurrentPattern(UIA_ValuePatternId,&raw))&&raw;
        if(ok){IUIAutomationValuePattern *value=nullptr;ok=SUCCEEDED(raw->QueryInterface(IID_PPV_ARGS(&value)))&&value;
            if(ok)ok=SUCCEEDED(value->get_CurrentIsReadOnly(&readonly))&&!readonly;if(value)value->Release();}
        if(raw)raw->Release();if(ok)selected=e;else e->Release();
    }
    hr=selected&&GetForegroundWindow()==hwnd?selected->SetFocus():E_ACCESSDENIED;
    if(SUCCEEDED(hr)) {
        bool matched=false;
        for(int i=0;i<20 && !matched;++i){IUIAutomationElement *current=nullptr;BOOL same=FALSE;
            if(SUCCEEDED(uia->GetFocusedElement(&current))&&current){matched=SUCCEEDED(uia->CompareElements(selected,current,&same))&&same;current->Release();}
            if(!matched)Sleep(50);
        }
        if(!matched)hr=E_FAIL;
    }
    std::printf("{\"status\":\"%s\",\"hr\":%ld,\"document_count\":%d}\n",SUCCEEDED(hr)?"FOCUS_CONFIRMED":"FOCUS_FAILED",hr,count);
    if(selected)selected->Release();if(items)items->Release();if(cond)cond->Release();if(root)root->Release();uia->Release();if(SUCCEEDED(init))CoUninitialize();
    return SUCCEEDED(hr)?0:5;
}

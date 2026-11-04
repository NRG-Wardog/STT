// Test-only: focus one named, non-password control inside a caller-owned HWND.
// The caller supplies and checks the owned Chrome PID; no editor value is read.
#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <cstdio>
#include <cstdlib>

int wmain(int argc,wchar_t **argv) {
    if(argc!=4)return 2;
    HWND hwnd=reinterpret_cast<HWND>(static_cast<UINT_PTR>(_wcstoui64(argv[1],nullptr,10)));
    DWORD expected=static_cast<DWORD>(_wtoi(argv[2])),owner=0;
    GetWindowThreadProcessId(hwnd,&owner);
    if(!IsWindow(hwnd)||owner!=expected||GetForegroundWindow()!=hwnd)return 3;
    if(wcscmp(argv[3],L"Local input") && wcscmp(argv[3],L"Local textarea") && wcscmp(argv[3],L"Local contenteditable"))return 4;
    HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);IUIAutomation *uia=nullptr;
    HRESULT hr=CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&uia));
    if(FAILED(hr)||!uia)return 5;
    IUIAutomationElement *root=nullptr,*element=nullptr;IUIAutomationCondition *condition=nullptr;
    hr=uia->ElementFromHandle(hwnd,&root);
    VARIANT value;VariantInit(&value);value.vt=VT_BSTR;value.bstrVal=SysAllocString(argv[3]);
    if(SUCCEEDED(hr)&&root)hr=uia->CreatePropertyCondition(UIA_NamePropertyId,value,&condition);
    if(SUCCEEDED(hr)&&condition)hr=root->FindFirst(TreeScope_Descendants,condition,&element);
    VariantClear(&value);
    BOOL password=TRUE,enabled=FALSE;int pid=0;CONTROLTYPEID type=0;
    if(!element || FAILED(element->get_CurrentProcessId(&pid)) || static_cast<DWORD>(pid)!=expected ||
       FAILED(element->get_CurrentIsPassword(&password)) || password || FAILED(element->get_CurrentIsEnabled(&enabled)) || !enabled ||
       FAILED(element->get_CurrentControlType(&type)) || type!=UIA_EditControlTypeId || GetForegroundWindow()!=hwnd)hr=E_ACCESSDENIED;
    else {
        hr=element->SetFocus();
        if(SUCCEEDED(hr)) {
            bool matched=false;
            for(int i=0;i<20 && !matched;++i) {
                IUIAutomationElement *current=nullptr;BOOL same=FALSE;
                if(SUCCEEDED(uia->GetFocusedElement(&current))&&current) {
                    matched=SUCCEEDED(uia->CompareElements(element,current,&same)) && same;
                    current->Release();
                }
                if(!matched)Sleep(50);
            }
            if(!matched)hr=E_FAIL;
        }
    }
    std::printf("{\"status\":\"%s\",\"hr\":%ld,\"control_type\":%d}\n",SUCCEEDED(hr)?"FOCUS_REQUESTED":"FOCUS_FAILED",hr,type);
    if(element)element->Release();if(condition)condition->Release();if(root)root->Release();uia->Release();if(SUCCEEDED(init))CoUninitialize();
    return SUCCEEDED(hr)?0:6;
}

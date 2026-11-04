// Metadata-only checks against hidden controls created by this test process.
// No user editor reads, input events, foreground changes or clipboard writes.
#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <cstdio>
#include "../src/target_integrity.hpp"
#include "../src/uia_writability.hpp"

struct Metadata { bool ok=false;int pid=0;BOOL password=TRUE;BOOL enabled=FALSE;BOOL readOnly=TRUE;bool value=false; };

static Metadata Inspect(IUIAutomation *uia,HWND hwnd) {
    Metadata m;IUIAutomationElement *element=nullptr;
    if(FAILED(uia->ElementFromHandle(hwnd,&element)) || !element)return m;
    CONTROLTYPEID type=0;
    HRESULT a=element->get_CurrentProcessId(&m.pid),b=element->get_CurrentIsPassword(&m.password),
            c=element->get_CurrentIsEnabled(&m.enabled),d=element->get_CurrentControlType(&type);
    IUnknown *pattern=nullptr;
    if(SUCCEEDED(element->GetCurrentPattern(UIA_ValuePatternId,&pattern))&&pattern) {
        IUIAutomationValuePattern *value=nullptr;
        if(SUCCEEDED(pattern->QueryInterface(IID_PPV_ARGS(&value)))&&value) {
            m.value=SUCCEEDED(value->get_CurrentIsReadOnly(&m.readOnly));value->Release();
        }
        pattern->Release();
    }
    m.ok=SUCCEEDED(a)&&SUCCEEDED(b)&&SUCCEEDED(c)&&SUCCEEDED(d)&&type==UIA_EditControlTypeId;
    element->Release();return m;
}

int main() {
    HWND fg=GetForegroundWindow();DWORD clip=GetClipboardSequenceNumber();
    HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    HWND parent=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,300,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND normal=parent?CreateWindowExW(0,L"EDIT",L"",WS_CHILD,0,0,100,20,parent,nullptr,GetModuleHandleW(nullptr),nullptr):nullptr;
    HWND password=parent?CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_PASSWORD,0,30,100,20,parent,nullptr,GetModuleHandleW(nullptr),nullptr):nullptr;
    HWND readonly=parent?CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_READONLY,0,60,100,20,parent,nullptr,GetModuleHandleW(nullptr),nullptr):nullptr;
    IUIAutomation *uia=nullptr;HRESULT hr=CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&uia));
    if(!parent||!normal||!password||!readonly||FAILED(hr)||!uia) {
        if(uia)uia->Release();if(parent)DestroyWindow(parent);if(SUCCEEDED(init))CoUninitialize();return 2;
    }
    auto n=Inspect(uia,normal),p=Inspect(uia,password),r=Inspect(uia,readonly);
    DWORD rid=0,error=0;
    bool own=n.ok&&n.pid==static_cast<int>(GetCurrentProcessId())&&heben::SafeProcessIntegrity(static_cast<DWORD>(n.pid),rid,error);
    bool normalOk=n.ok&&n.enabled&&!n.password&&n.value&&!n.readOnly;
    bool passwordOk=p.ok&&p.password&&heben::IsNativePasswordControl(password);
    bool readonlyOk=r.ok&&r.value&&r.readOnly;
    VARIANT attribute{};attribute.vt=VT_BOOL;attribute.boolVal=VARIANT_FALSE;
    bool rangeRule=heben::WritableFromReadOnlyAttribute(attribute)==heben::WritableState::Writable;
    attribute.boolVal=VARIANT_TRUE;rangeRule=rangeRule&&heben::WritableFromReadOnlyAttribute(attribute)==heben::WritableState::ReadOnly;
    attribute.vt=VT_EMPTY;rangeRule=rangeRule&&heben::WritableFromReadOnlyAttribute(attribute)==heben::WritableState::Unknown;
    HRESULT detail=S_OK;rangeRule=rangeRule&&heben::TextPatternWritability(nullptr,detail)==heben::WritableState::Unknown&&detail==E_POINTER;
    bool focus=GetForegroundWindow()==fg,clipboard=GetClipboardSequenceNumber()==clip;
    std::printf("{\"mode\":\"hidden_local_uia_metadata_only\",\"own_process\":%s,\"normal_writable\":%s,\"password_detected\":%s,\"readonly_detected\":%s,\"range_writability_rules\":%s,\"foreground_unchanged\":%s,\"clipboard_unchanged\":%s}\n",
        own?"true":"false",normalOk?"true":"false",passwordOk?"true":"false",readonlyOk?"true":"false",rangeRule?"true":"false",focus?"true":"false",clipboard?"true":"false");
    uia->Release();DestroyWindow(parent);if(SUCCEEDED(init))CoUninitialize();
    return own&&normalOk&&passwordOk&&readonlyOk&&rangeRule&&focus&&clipboard?0:3;
}

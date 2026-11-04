// Read-only caret geometry probe. Never requests text or changes focus.
// IA2 COM method order: Chromium third_party/iaccessible2/ia2_api_all.idl.
#include <windows.h>
#include <oleacc.h>
#include <servprov.h>
#include <UIAutomation.h>
#include <cstdio>
#include <cwchar>

MIDL_INTERFACE("24FD2FFB-3AAD-4a08-8335-A3AD89C0FB4B")
IAccessibleTextCaret : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE addSelection(long,long)=0;
    virtual HRESULT STDMETHODCALLTYPE get_attributes(long,long*,long*,BSTR*)=0;
public:
    virtual HRESULT STDMETHODCALLTYPE get_caretOffset(long*)=0;
    virtual HRESULT STDMETHODCALLTYPE get_characterExtents(long,int,long*,long*,long*,long*)=0;
};

int main() {
    HWND fg=GetForegroundWindow();DWORD pid=0;GetWindowThreadProcessId(fg,&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);wchar_t path[32768]{};DWORD n=32768;
    if(!process || !QueryFullProcessImageNameW(process,0,path,&n)){if(process)CloseHandle(process);return 2;}
    CloseHandle(process);const wchar_t *base=wcsrchr(path,L'\\');base=base?base+1:path;
    if(_wcsicmp(base,L"chrome.exe")!=0 && _wcsicmp(base,L"msedge.exe")!=0){std::puts("{\"status\":\"SKIPPED_NOT_BROWSER\"}");return 0;}
    HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    IUIAutomation *uia=nullptr;HRESULT hr=CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&uia));
    if(FAILED(hr)||!uia)return 3;
    IUIAutomationElement *el=nullptr;hr=uia->GetFocusedElement(&el);
    if(FAILED(hr)||!el){uia->Release();if(SUCCEEDED(init))CoUninitialize();return 4;}
    BOOL focused=FALSE,password=TRUE;CONTROLTYPEID type=0;
    HRESULT hf=el->get_CurrentHasKeyboardFocus(&focused),hp=el->get_CurrentIsPassword(&password),ht=el->get_CurrentControlType(&type);
    if(FAILED(hf)||!focused||FAILED(hp)||password||FAILED(ht)||type!=UIA_EditControlTypeId){
        std::puts("{\"status\":\"SKIPPED_NOT_SAFE_EDIT\"}");el->Release();uia->Release();if(SUCCEEDED(init))CoUninitialize();return 0;
    }
    IUIAutomationLegacyIAccessiblePattern *legacy=nullptr;IAccessible *accessible=nullptr;
    IServiceProvider *service=nullptr;IAccessibleTextCaret *text=nullptr;
    HRESULT hlegacy=el->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId,IID_PPV_ARGS(&legacy));
    HRESULT hacc=legacy?legacy->GetIAccessible(&accessible):E_NOINTERFACE;
    HRESULT hservice=accessible?accessible->QueryInterface(IID_PPV_ARGS(&service)):E_NOINTERFACE;
    HRESULT htext=service?service->QueryService(__uuidof(IAccessibleTextCaret),__uuidof(IAccessibleTextCaret),reinterpret_cast<void**>(&text)):E_NOINTERFACE;
    int msaaDepth=0;HRESULT hmsaa=E_NOINTERFACE;
    if(!text) {
        IAccessible *node=nullptr;
        hmsaa=AccessibleObjectFromWindow(fg,OBJID_CLIENT,IID_IAccessible,reinterpret_cast<void**>(&node));
        for(int depth=0;node && depth<12;++depth) {
            IServiceProvider *inspect=nullptr;IAccessibleTextCaret *candidate=nullptr;
            HRESULT hs=node->QueryInterface(IID_PPV_ARGS(&inspect));
            HRESULT hc=inspect?inspect->QueryService(__uuidof(IAccessibleTextCaret),__uuidof(IAccessibleTextCaret),reinterpret_cast<void**>(&candidate)):E_NOINTERFACE;
            long pos=-999,cx=0,cy=0,cw=0,ch=0;
            HRESULT hp2=candidate?candidate->get_caretOffset(&pos):E_NOINTERFACE;
            HRESULT hr2=candidate?candidate->get_characterExtents(-2,0,&cx,&cy,&cw,&ch):E_NOINTERFACE;
            std::printf("{\"status\":\"ANCESTOR_METADATA\",\"depth\":%d,\"service_hr\":%ld,\"ia2_hr\":%ld,\"caret_hr\":%ld,\"rect_hr\":%ld,\"offset\":%ld,\"rect\":[%ld,%ld,%ld,%ld]}\n",depth,hs,hc,hp2,hr2,pos,cx,cy,cw,ch);
            if(candidate)candidate->Release();if(inspect)inspect->Release();
            VARIANT v;VariantInit(&v);HRESULT hv=node->get_accFocus(&v);IAccessible *next=nullptr;
            if(SUCCEEDED(hv) && v.vt==VT_DISPATCH && v.pdispVal)v.pdispVal->QueryInterface(IID_PPV_ARGS(&next));
            else if(SUCCEEDED(hv) && v.vt==VT_I4 && v.lVal!=CHILDID_SELF) {
                IDispatch *child=nullptr;
                if(SUCCEEDED(node->get_accChild(v,&child))&&child){child->QueryInterface(IID_PPV_ARGS(&next));child->Release();}
            }
            VariantClear(&v);
            if(!next)break;
            node->Release();node=next;++msaaDepth;
        }
        if(node) {
            IServiceProvider *sp=nullptr;
            if(SUCCEEDED(node->QueryInterface(IID_PPV_ARGS(&sp)))&&sp) {
                htext=sp->QueryService(__uuidof(IAccessibleTextCaret),__uuidof(IAccessibleTextCaret),reinterpret_cast<void**>(&text));
                sp->Release();
            }
            node->Release();
        }
    }
    long offset=-999,x=0,y=0,w=0,h=0;HRESULT hoff=text?text->get_caretOffset(&offset):E_NOINTERFACE;
    HRESULT hrect=text?text->get_characterExtents(-2,0,&x,&y,&w,&h):E_NOINTERFACE;
    std::printf("{\"status\":\"METADATA_ONLY\",\"contents_read\":false,\"legacy_hr\":%ld,\"accessible_hr\":%ld,\"service_hr\":%ld,\"msaa_hr\":%ld,\"msaa_focus_depth\":%d,\"ia2_hr\":%ld,\"caret_hr\":%ld,\"rect_hr\":%ld,\"offset\":%ld,\"rect\":[%ld,%ld,%ld,%ld]}\n",hlegacy,hacc,hservice,hmsaa,msaaDepth,htext,hoff,hrect,offset,x,y,w,h);
    if(text)text->Release();if(service)service->Release();if(accessible)accessible->Release();if(legacy)legacy->Release();
    el->Release();uia->Release();if(SUCCEEDED(init))CoUninitialize();return 0;
}

// Inspect only our controlled fixture's accessibility metadata; never read value/text.
#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <cstdio>
#include <cstdlib>
int wmain(int argc,wchar_t **argv) {
    if(argc!=2 && argc!=3)return 2;
    if(argc==3){FILE *out=nullptr;if(_wfreopen_s(&out,argv[2],L"wb",stdout)!=0 || !out)return 6;}
    HWND hwnd=reinterpret_cast<HWND>(static_cast<UINT_PTR>(_wcstoui64(argv[1],nullptr,10)));
    if(!IsWindow(hwnd))return 3;
    GUITHREADINFO gui{};gui.cbSize=sizeof(gui);DWORD tid=GetWindowThreadProcessId(hwnd,nullptr);
    BOOL guiOk=GetGUIThreadInfo(tid,&gui);
    std::printf("{\"stage\":\"gui_thread\",\"available\":%s,\"caret_present\":%s,\"focus_present\":%s,\"caret_height\":%ld}\n",
        guiOk?"true":"false",gui.hwndCaret?"true":"false",gui.hwndFocus?"true":"false",gui.rcCaret.bottom-gui.rcCaret.top);
    HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    IUIAutomation *uia=nullptr;HRESULT hr=CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&uia));
    if(FAILED(hr)||!uia){std::printf("{\"stage\":\"uia_create\",\"hr\":%ld}\n",hr);return 4;}
    IUIAutomationElement *root=nullptr;hr=uia->ElementFromHandle(hwnd,&root);
    if(FAILED(hr)||!root){std::printf("{\"stage\":\"element_from_handle\",\"hr\":%ld}\n",hr);uia->Release();if(SUCCEEDED(init))CoUninitialize();return 5;}
    IUIAutomationCondition *condition=nullptr;VARIANT type{};type.vt=VT_I4;type.lVal=UIA_EditControlTypeId;
    hr=uia->CreatePropertyCondition(UIA_ControlTypePropertyId,type,&condition);
    IUIAutomationElementArray *items=nullptr;
    if(SUCCEEDED(hr))hr=root->FindAll(TreeScope_Descendants,condition,&items);
    int length=0;if(items)items->get_Length(&length);
    std::printf("{\"stage\":\"edit_elements\",\"hr\":%ld,\"count\":%d}",hr,length);
    for(int i=0;i<length;++i) {
        IUIAutomationElement *e=nullptr;if(FAILED(items->GetElement(i,&e))||!e)continue;
        BOOL password=TRUE,focused=FALSE,enabled=FALSE;CONTROLTYPEID control=0;
        BSTR name=nullptr;e->get_CurrentName(&name);
        bool fixtureName=name && wcsstr(name,L"Test ")!=nullptr;
        HRESULT a=e->get_CurrentIsPassword(&password),b=e->get_CurrentHasKeyboardFocus(&focused),c=e->get_CurrentIsEnabled(&enabled),d=e->get_CurrentControlType(&control);
        IUnknown *value=nullptr;HRESULT vp=e->GetCurrentPattern(UIA_ValuePatternId,&value);
        BOOL readonly=TRUE;HRESULT ro=E_NOINTERFACE;
        if(value){IUIAutomationValuePattern *v=nullptr;if(SUCCEEDED(value->QueryInterface(IID_PPV_ARGS(&v)))&&v){ro=v->get_CurrentIsReadOnly(&readonly);v->Release();}value->Release();}
        IUnknown *text=nullptr;HRESULT tp=e->GetCurrentPattern(UIA_TextPattern2Id,&text);bool text2=text!=nullptr;if(text)text->Release();
        std::printf("\n{\"stage\":\"edit\",\"index\":%d,\"control\":%d,\"fixture_name\":%s,\"password\":%d,\"focused\":%d,\"enabled\":%d,\"hr_password\":%ld,\"hr_focus\":%ld,\"hr_enabled\":%ld,\"hr_type\":%ld,\"value_pattern_hr\":%ld,\"readonly\":%d,\"readonly_hr\":%ld,\"text2\":%s,\"text2_hr\":%ld}",i,control,fixtureName?"true":"false",password,focused,enabled,a,b,c,d,vp,readonly,ro,text2?"true":"false",tp);
        if(name)SysFreeString(name);
        e->Release();
    }
    IUIAutomationCondition *allCondition=nullptr;IUIAutomationElementArray *allItems=nullptr;
    if(SUCCEEDED(uia->CreateTrueCondition(&allCondition)) && allCondition &&
       SUCCEEDED(root->FindAll(TreeScope_Descendants,allCondition,&allItems)) && allItems) {
        int allLength=0;allItems->get_Length(&allLength);int matched=0;
        for(int i=0;i<allLength && i<3000;++i) {
            IUIAutomationElement *element=nullptr;if(FAILED(allItems->GetElement(i,&element))||!element)continue;
            BSTR name=nullptr;element->get_CurrentName(&name);
            bool fixture=name && wcsstr(name,L"Test ")!=nullptr;
            if(fixture) {
                CONTROLTYPEID control=0;BOOL focus=FALSE,password=TRUE;
                element->get_CurrentControlType(&control);element->get_CurrentHasKeyboardFocus(&focus);element->get_CurrentIsPassword(&password);
                IUnknown *text=nullptr;element->GetCurrentPattern(UIA_TextPattern2Id,&text);bool text2=text!=nullptr;if(text)text->Release();
                IUnknown *value=nullptr;element->GetCurrentPattern(UIA_ValuePatternId,&value);bool hasValue=value!=nullptr;if(value)value->Release();
                std::printf("\n{\"stage\":\"named_fixture\",\"control\":%d,\"focused\":%d,\"password\":%d,\"text2\":%s,\"value\":%s}",control,focus,password,text2?"true":"false",hasValue?"true":"false");++matched;
            }
            if(name)SysFreeString(name);element->Release();
        }
        std::printf("\n{\"stage\":\"tree_summary\",\"elements\":%d,\"named_fixture_matches\":%d}",allLength,matched);
    }
    if(allItems)allItems->Release();if(allCondition)allCondition->Release();
    std::printf("\n");if(items)items->Release();if(condition)condition->Release();root->Release();uia->Release();if(SUCCEEDED(init))CoUninitialize();return 0;
}

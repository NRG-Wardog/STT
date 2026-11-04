// Read-only focused Chrome caret metadata. Never reads editor values or text.
#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <cstdio>
#include <cwchar>

static int RectCount(IUIAutomationTextRange *range) {
    SAFEARRAY *a=nullptr;if(!range || FAILED(range->GetBoundingRectangles(&a)) || !a)return -1;
    LONG lo=0,hi=-1;int result=-1;
    if(SUCCEEDED(SafeArrayGetLBound(a,1,&lo)) && SUCCEEDED(SafeArrayGetUBound(a,1,&hi)))result=(hi-lo+1)/4;
    SafeArrayDestroy(a);return result;
}
static bool OneRect(IUIAutomationTextRange *range,double *v) {
    SAFEARRAY *a=nullptr;if(!range||FAILED(range->GetBoundingRectangles(&a))||!a)return false;
    LONG lo=0,hi=-1;bool ok=SUCCEEDED(SafeArrayGetLBound(a,1,&lo))&&SUCCEEDED(SafeArrayGetUBound(a,1,&hi))&&hi-lo==3;
    for(LONG i=0;ok&&i<4;++i){LONG index=lo+i;ok=SUCCEEDED(SafeArrayGetElement(a,&index,&v[i]));}
    SafeArrayDestroy(a);return ok;
}
int main() {
    HWND fg=GetForegroundWindow();DWORD pid=0;GetWindowThreadProcessId(fg,&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);wchar_t path[32768]{};DWORD n=32768;
    if(!process || !QueryFullProcessImageNameW(process,0,path,&n)){if(process)CloseHandle(process);return 2;}
    CloseHandle(process);const wchar_t *base=wcsrchr(path,L'\\');base=base?base+1:path;
    if(_wcsicmp(base,L"chrome.exe")!=0 && _wcsicmp(base,L"msedge.exe")!=0){std::puts("{\"status\":\"SKIPPED_NOT_BROWSER\"}");return 0;}
    HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    IUIAutomation *uia=nullptr;HRESULT hr=CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&uia));
    if(FAILED(hr)||!uia)return 3;
    IUIAutomationElement *element=nullptr;hr=uia->GetFocusedElement(&element);
    if(FAILED(hr)||!element){std::printf("{\"status\":\"NO_FOCUSED_ELEMENT\",\"hr\":%ld}\n",hr);uia->Release();if(SUCCEEDED(init))CoUninitialize();return 0;}
    BOOL focused=FALSE,password=TRUE;CONTROLTYPEID type=0;
    element->get_CurrentHasKeyboardFocus(&focused);element->get_CurrentIsPassword(&password);element->get_CurrentControlType(&type);
    if(!focused || password || type!=UIA_EditControlTypeId){std::printf("{\"status\":\"SKIPPED_NOT_SAFE_EDIT\",\"type\":%d,\"focused\":%d,\"password\":%d}\n",type,focused,password);element->Release();uia->Release();if(SUCCEEDED(init))CoUninitialize();return 0;}
    bool has2=false,hasText=false;int active=-1,caretRects=-2,selections=-2,selectionCompare=999,selectionRects=-2,atStart=999,atEnd=999;
    int movedBefore=999,movedAfter=999;double before[4]{},after[4]{},selectionRect[4]{};bool beforeRect=false,afterRect=false,selectionHasRect=false;
    IUnknown *unknown=nullptr;
    if(SUCCEEDED(element->GetCurrentPattern(UIA_TextPattern2Id,&unknown))&&unknown){
        IUIAutomationTextPattern2 *p=nullptr;if(SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&p)))&&p){
            has2=true;BOOL a=FALSE;IUIAutomationTextRange *r=nullptr;
            if(SUCCEEDED(p->GetCaretRange(&a,&r))&&r){active=a;caretRects=RectCount(r);r->Release();}p->Release();}
        unknown->Release();unknown=nullptr;
    }
    if(SUCCEEDED(element->GetCurrentPattern(UIA_TextPatternId,&unknown))&&unknown){
        IUIAutomationTextPattern *p=nullptr;if(SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&p)))&&p){
            hasText=true;IUIAutomationTextRangeArray *ranges=nullptr;
            if(SUCCEEDED(p->GetSelection(&ranges))&&ranges){ranges->get_Length(&selections);IUIAutomationTextRange *r=nullptr;
                if(selections==1&&SUCCEEDED(ranges->GetElement(0,&r))&&r){
                    r->CompareEndpoints(TextPatternRangeEndpoint_Start,r,TextPatternRangeEndpoint_End,&selectionCompare);selectionRects=RectCount(r);
                    selectionHasRect=OneRect(r,selectionRect);
                    if(selectionCompare==0){
                        IUIAutomationTextRange *b=nullptr,*a=nullptr;
                        if(SUCCEEDED(r->Clone(&b))&&b&&SUCCEEDED(b->MoveEndpointByUnit(TextPatternRangeEndpoint_Start,TextUnit_Character,-1,&movedBefore))&&movedBefore==-1)beforeRect=OneRect(b,before);
                        if(SUCCEEDED(r->Clone(&a))&&a&&SUCCEEDED(a->MoveEndpointByUnit(TextPatternRangeEndpoint_End,TextUnit_Character,1,&movedAfter))&&movedAfter==1)afterRect=OneRect(a,after);
                        if(b)b->Release();if(a)a->Release();
                    }
                    IUIAutomationTextRange *doc=nullptr;if(SUCCEEDED(p->get_DocumentRange(&doc))&&doc){
                        r->CompareEndpoints(TextPatternRangeEndpoint_Start,doc,TextPatternRangeEndpoint_Start,&atStart);
                        r->CompareEndpoints(TextPatternRangeEndpoint_End,doc,TextPatternRangeEndpoint_End,&atEnd);doc->Release();}r->Release();}
                ranges->Release();}p->Release();}unknown->Release();
    }
    std::printf("{\"status\":\"METADATA_ONLY\",\"contents_read\":false,\"type\":%d,\"text_pattern\":%s,\"text2\":%s,\"caret_active\":%d,\"caret_rectangles\":%d,\"selection_count\":%d,\"selection_compare\":%d,\"selection_rectangles\":%d,\"selection_rect\":%s,\"selection\":[%.1f,%.1f,%.1f,%.1f],\"selection_vs_doc_start\":%d,\"selection_vs_doc_end\":%d,\"move_before\":%d,\"before_rect\":%s,\"before\":[%.1f,%.1f,%.1f,%.1f],\"move_after\":%d,\"after_rect\":%s,\"after\":[%.1f,%.1f,%.1f,%.1f]}\n",
        type,hasText?"true":"false",has2?"true":"false",active,caretRects,selections,selectionCompare,selectionRects,selectionHasRect?"true":"false",selectionRect[0],selectionRect[1],selectionRect[2],selectionRect[3],atStart,atEnd,movedBefore,beforeRect?"true":"false",before[0],before[1],before[2],before[3],movedAfter,afterRect?"true":"false",after[0],after[1],after[2],after[3]);
    element->Release();uia->Release();if(SUCCEEDED(init))CoUninitialize();return 0;
}

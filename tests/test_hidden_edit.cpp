// In-process hidden Win32 edit control. WM_CHAR is a simulation, not SendInput.
#include <windows.h>
#include <string>
#include <cstdio>
#include "../src/target_integrity.hpp"

static bool SendAndRead(HWND edit,const std::wstring &input,const std::wstring &expected) {
    SetWindowTextW(edit,L"");
    for(wchar_t c:input)SendMessageW(edit,WM_CHAR,c,1);
    int n=GetWindowTextLengthW(edit);
    std::wstring actual(static_cast<size_t>(n)+1,L'\0');
    int got=GetWindowTextW(edit,actual.data(),n+1);
    actual.resize(static_cast<size_t>(got));
    return actual==expected;
}
static bool InsertAndRead(HWND edit,const std::wstring &initial,int start,int end,
                          const std::wstring &input,const std::wstring &expected) {
    SetWindowTextW(edit,initial.c_str());
    SendMessageW(edit,EM_SETSEL,start,end);
    for(wchar_t c:input)SendMessageW(edit,WM_CHAR,c,1);
    int n=GetWindowTextLengthW(edit);std::wstring actual(static_cast<size_t>(n)+1,L'\0');
    int got=GetWindowTextW(edit,actual.data(),n+1);actual.resize(static_cast<size_t>(got));
    return actual==expected;
}
int main() {
    HWND parent=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,400,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!parent)return 1;
    HWND edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_MULTILINE|ES_WANTRETURN|ES_AUTOVSCROLL,
                              0,0,300,150,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!edit){DestroyWindow(parent);return 2;}
    HWND password=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_PASSWORD,0,0,100,30,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!password){DestroyWindow(parent);return 4;}
    HWND before=GetForegroundWindow();DWORD clipboard=GetClipboardSequenceNumber();
    bool english=SendAndRead(edit,L"Hello, world! 123.",L"Hello, world! 123.");
    bool hebrew=SendAndRead(edit,L"\x05e9\x05dc\x05d5\x05dd \x05e2\x05d5\x05dc\x05dd!",L"\x05e9\x05dc\x05d5\x05dd \x05e2\x05d5\x05dc\x05dd!");
    bool mixed=SendAndRead(edit,L"\x05e9\x05dc\x05d5\x05dd, hello 42!",L"\x05e9\x05dc\x05d5\x05dd, hello 42!");
    bool multiline=SendAndRead(edit,L"first\rsecond",L"first\r\nsecond");
    const std::wstring fixed=L"\u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd Hello World 123";
    bool beginning=InsertAndRead(edit,L"ABCD",0,0,fixed,fixed+L"ABCD");
    bool middle=InsertAndRead(edit,L"ABCD",2,2,fixed,L"AB"+fixed+L"CD");
    bool end=InsertAndRead(edit,L"ABCD",4,4,fixed,L"ABCD"+fixed);
    bool selection=InsertAndRead(edit,L"ABCD",1,3,fixed,L"A"+fixed+L"D");
    bool nonBmp=SendAndRead(edit,L"A \U0001f642 B",L"A \U0001f642 B");
    bool focus=GetForegroundWindow()==before, clip=GetClipboardSequenceNumber()==clipboard;
    DWORD rid=0,error=0;bool integrity=heben::SafeWindowIntegrity(parent,rid,error);
    bool integrityRules=heben::IntegrityLevelsAllow(SECURITY_MANDATORY_MEDIUM_RID,SECURITY_MANDATORY_MEDIUM_RID) &&
        !heben::IntegrityLevelsAllow(SECURITY_MANDATORY_MEDIUM_RID,SECURITY_MANDATORY_HIGH_RID) &&
        !heben::IntegrityLevelsAllow(SECURITY_MANDATORY_HIGH_RID,SECURITY_MANDATORY_HIGH_RID);
    bool passwordGuard=heben::IsNativePasswordControl(password) && !heben::IsNativePasswordControl(edit);
    DWORD pidRid=0,pidError=0;bool ownProcess=heben::SafeProcessIntegrity(GetCurrentProcessId(),pidRid,pidError);
    DWORD invalidRid=0,invalidError=0;bool zeroRejected=!heben::SafeProcessIntegrity(0,invalidRid,invalidError)&&invalidError==ERROR_INVALID_PARAMETER;
    std::printf("{\"mode\":\"hidden_edit_wm_char_simulation\",\"english\":%s,\"hebrew\":%s,\"mixed\":%s,\"multiline\":%s,\"beginning\":%s,\"middle\":%s,\"end\":%s,\"selection\":%s,\"non_bmp\":%s,\"foreground_unchanged\":%s,\"clipboard_unchanged\":%s,\"native_password_guard\":%s,\"current_integrity_allowed\":%s,\"integrity_rules\":%s,\"own_process_allowed\":%s,\"zero_process_rejected\":%s}\n",
        english?"true":"false",hebrew?"true":"false",mixed?"true":"false",multiline?"true":"false",beginning?"true":"false",middle?"true":"false",end?"true":"false",selection?"true":"false",nonBmp?"true":"false",focus?"true":"false",clip?"true":"false",passwordGuard?"true":"false",integrity?"true":"false",integrityRules?"true":"false",ownProcess?"true":"false",zeroRejected?"true":"false");
    DestroyWindow(parent);
    return english&&hebrew&&mixed&&multiline&&beginning&&middle&&end&&selection&&nonBmp&&focus&&clip&&integrity&&integrityRules&&passwordGuard&&ownProcess&&zeroRejected?0:3;
}

// Deterministic fault/race tests for the actual clipboard transaction code.
// All Win32 clipboard calls below are redirected to an in-memory fake.
#include <windows.h>
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include "../src/unicode_input.hpp"
#include "../src/clipboard_policy.hpp"

struct Block {std::vector<unsigned char> bytes;};
static std::map<UINT,HGLOBAL> clip;
static bool opened=false;static HWND openOwner=nullptr,clipOwner=nullptr;
static DWORD seq=1,lastError=0;static UINT failOnce=0;static int getCalls=0;static std::vector<UINT> setOrder;
static bool incrementOnClose=false,changeOwnerOnClose=false,corruptUnicodeOnSet=false;
static HGLOBAL WINAPI FakeAlloc(UINT,SIZE_T n){auto *b=new Block;b->bytes.resize(n);return reinterpret_cast<HGLOBAL>(b);}
static HGLOBAL WINAPI FakeFree(HGLOBAL h){delete reinterpret_cast<Block*>(h);return nullptr;}
static void *WINAPI FakeLock(HGLOBAL h){return h?reinterpret_cast<Block*>(h)->bytes.data():nullptr;}
static BOOL WINAPI FakeUnlock(HGLOBAL){return TRUE;}
static SIZE_T WINAPI FakeSize(HGLOBAL h){return h?reinterpret_cast<Block*>(h)->bytes.size():0;}
static BOOL WINAPI FakeOpen(HWND h){if(opened){lastError=ERROR_ACCESS_DENIED;return FALSE;}opened=true;openOwner=h;return TRUE;}
static BOOL WINAPI FakeClose(){opened=false;if(incrementOnClose){++seq;incrementOnClose=false;}if(changeOwnerOnClose){clipOwner=reinterpret_cast<HWND>(static_cast<UINT_PTR>(2));++seq;changeOwnerOnClose=false;}return TRUE;}
static BOOL WINAPI FakeEmpty(){if(!opened)return FALSE;for(auto &x:clip)FakeFree(x.second);clip.clear();clipOwner=openOwner;++seq;return TRUE;}
static UINT WINAPI FakeEnum(UINT previous){auto i=previous?clip.upper_bound(previous):clip.begin();return i==clip.end()?0:i->first;}
static HANDLE WINAPI FakeGet(UINT format){++getCalls;auto i=clip.find(format);return i==clip.end()?nullptr:i->second;}
static HANDLE WINAPI FakeSet(UINT format,HANDLE value){
    setOrder.push_back(format);
    if(failOnce==format){failOnce=0;lastError=ERROR_NOT_ENOUGH_MEMORY;return nullptr;}
    auto i=clip.find(format);if(i!=clip.end())FakeFree(i->second);
    if(corruptUnicodeOnSet && format==CF_UNICODETEXT) {
        auto *block=reinterpret_cast<Block*>(value);
        if(!block->bytes.empty())block->bytes[0]^=1;
        corruptUnicodeOnSet=false;
    }
    clip[format]=reinterpret_cast<HGLOBAL>(value);++seq;return value;
}
static DWORD WINAPI FakeSeq(){return seq;}
static HWND WINAPI FakeOwner(){return clipOwner;}
static UINT WINAPI FakeRegister(LPCWSTR){return 0xc123;}
static DWORD WINAPI FakeLastError(){return lastError;}
static void WINAPI FakeSetLastError(DWORD e){lastError=e;}

#define GlobalAlloc FakeAlloc
#define GlobalFree FakeFree
#define GlobalLock FakeLock
#define GlobalUnlock FakeUnlock
#define GlobalSize FakeSize
#define OpenClipboard FakeOpen
#define CloseClipboard FakeClose
#define EmptyClipboard FakeEmpty
#define EnumClipboardFormats FakeEnum
#define GetClipboardData FakeGet
#define SetClipboardData FakeSet
#define GetClipboardSequenceNumber FakeSeq
#define GetClipboardOwner FakeOwner
#define RegisterClipboardFormatW FakeRegister
#define GetLastError FakeLastError
#define SetLastError FakeSetLastError
#include "../src/clipboard_text_transaction.hpp"
#undef GlobalAlloc
#undef GlobalFree
#undef GlobalLock
#undef GlobalUnlock
#undef GlobalSize
#undef OpenClipboard
#undef CloseClipboard
#undef EmptyClipboard
#undef EnumClipboardFormats
#undef GetClipboardData
#undef SetClipboardData
#undef GetClipboardSequenceNumber
#undef GetClipboardOwner
#undef RegisterClipboardFormatW
#undef GetLastError
#undef SetLastError

static HGLOBAL Text(const std::wstring &s){HGLOBAL h=FakeAlloc(GMEM_MOVEABLE,(s.size()+1)*sizeof(wchar_t));std::memcpy(FakeLock(h),s.c_str(),(s.size()+1)*sizeof(wchar_t));return h;}
static std::wstring Read(){auto i=clip.find(CF_UNICODETEXT);return i==clip.end()?L"":static_cast<const wchar_t*>(FakeLock(i->second));}
static void SetExternal(HWND owner,const std::wstring &s,bool unsupported=false){FakeOpen(owner);FakeEmpty();FakeSet(CF_UNICODETEXT,Text(s));if(unsupported)FakeSet(CF_HDROP,FakeAlloc(GMEM_MOVEABLE,8));FakeClose();}

int main() {
    HWND app=reinterpret_cast<HWND>(static_cast<UINT_PTR>(1)),other=reinterpret_cast<HWND>(static_cast<UINT_PTR>(2));
    DWORD error=0;SetExternal(other,L"original");setOrder.clear();incrementOnClose=true;heben::ClipboardTextTransaction a;
    if(a.Begin(app,L"payload",error)!=heben::ClipboardBegin::Ready || Read()!=L"payload" || !a.Unchanged() ||
       !clip.count(0xc123) || setOrder.size()!=2 || setOrder[0]!=0xc123 || setOrder[1]!=CF_UNICODETEXT)return 1;
    if(a.Restore(app,error)!=heben::ClipboardRestore::Restored || Read()!=L"original")return 2;
    heben::ClipboardTextTransaction b;if(b.Begin(app,L"payload",error)!=heben::ClipboardBegin::Ready)return 3;
    SetExternal(other,L"newer");if(b.Restore(app,error)!=heben::ClipboardRestore::ChangedByOther || Read()!=L"newer")return 4;
    SetExternal(other,L"rich",true);DWORD before=seq;getCalls=0;heben::ClipboardTextTransaction c;
    if(c.Begin(app,L"payload",error)!=heben::ClipboardBegin::UnsupportedFormat || seq!=before || getCalls!=0 || Read()!=L"rich")return 5;
    SetExternal(other,L"original");failOnce=0xc123;heben::ClipboardTextTransaction d;
    if(d.Begin(app,L"payload",error)!=heben::ClipboardBegin::Failed || d.RecoveryFailed() || Read()!=L"original")return 6;
    SetExternal(other,L"original");failOnce=CF_UNICODETEXT;heben::ClipboardTextTransaction e;
    if(e.Begin(app,L"payload",error)!=heben::ClipboardBegin::Failed || e.RecoveryFailed() || Read()!=L"original")return 7;
    heben::ClipboardTextTransaction f;if(f.Begin(app,L"payload",error)!=heben::ClipboardBegin::Ready)return 8;
    failOnce=CF_UNICODETEXT;if(f.Restore(app,error)!=heben::ClipboardRestore::Failed || !f.RecoveryFailed())return 9;
    before=seq;if(f.Begin(app,L"again",error)!=heben::ClipboardBegin::Failed || seq!=before)return 10;
    SetExternal(other,L"original");changeOwnerOnClose=true;heben::ClipboardTextTransaction g;
    if(g.Begin(app,L"payload",error)!=heben::ClipboardBegin::ChangedByOther || g.Active())return 11;
    SetExternal(other,L"original");heben::ClipboardTextTransaction h;
    if(h.Begin(app,L"payload",error)!=heben::ClipboardBegin::Ready)return 12;
    opened=true;if(h.Restore(app,error)!=heben::ClipboardRestore::Busy || !h.Active())return 13;
    opened=false;if(h.Restore(app,error)!=heben::ClipboardRestore::Restored || Read()!=L"original")return 14;
    SetExternal(other,L"original");before=seq;opened=true;heben::ClipboardTextTransaction i;
    if(i.Begin(app,L"payload",error)!=heben::ClipboardBegin::Busy || seq!=before)return 15;
    opened=false;SetExternal(other,L"original");seq=0;heben::ClipboardTextTransaction j;
    if(j.Begin(app,L"payload",error)!=heben::ClipboardBegin::Failed || Read()!=L"original")return 16;
    seq=1;SetExternal(other,L"original");heben::ClipboardTextTransaction k;
    if(k.Begin(app,L"payload",error)!=heben::ClipboardBegin::Ready)return 17;
    corruptUnicodeOnSet=true;
    if(k.Restore(app,error)!=heben::ClipboardRestore::Failed || error!=ERROR_CRC || !k.RecoveryFailed())return 18;
    FakeOpen(app);FakeEmpty();FakeClose();
    std::puts("PASS_MOCK_CLIPBOARD_TRANSACTION: content/owner/sequence/failure checks; no OS clipboard was touched.");return 0;
}

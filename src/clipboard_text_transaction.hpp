#pragma once
#include <windows.h>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include "unicode_input.hpp"
#include "clipboard_policy.hpp"

namespace heben {
enum class ClipboardBegin { Ready, Busy, UnsupportedFormat, TooLarge, InvalidText, ChangedByOther, Failed };
enum class ClipboardRestore { Restored, ChangedByOther, Busy, Failed, NotActive };

// Explicit opt-in only. Preserves a limited set of HGLOBAL text formats; any
// other clipboard format is refused before mutation. No contents are logged.
class ClipboardTextTransaction {
public:
    ClipboardTextTransaction()=default;
    ClipboardTextTransaction(const ClipboardTextTransaction&)=delete;
    ClipboardTextTransaction& operator=(const ClipboardTextTransaction&)=delete;
    ~ClipboardTextTransaction(){FreeSaved();}

    ClipboardBegin Begin(HWND owner,const std::wstring &text,DWORD &error) {
        error=0;
        if(recoveryFailed_){error=recoveryError_?recoveryError_:ERROR_INVALID_STATE;return ClipboardBegin::Failed;}
        if(!saved_.empty()){error=ERROR_INVALID_STATE;return ClipboardBegin::Failed;}
        recoveryError_=0;
        if(active_ || !owner || text.empty() || !IsWellFormedUtf16(text) || !IsSafeKeyboardText(text)) {
            error=ERROR_INVALID_DATA;return ClipboardBegin::InvalidText;
        }
        if(text.size()>kMaxBytes/sizeof(wchar_t)-1){error=ERROR_FILE_TOO_LARGE;return ClipboardBegin::TooLarge;}
        const UINT exclude=RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
        if(!exclude){error=GetLastError();return ClipboardBegin::Failed;}
        HGLOBAL replacement=MakeBytes(text.c_str(),(text.size()+1)*sizeof(wchar_t));
        const DWORD marker=1;HGLOBAL privacy=MakeBytes(&marker,sizeof(marker));
        if(!replacement||!privacy) {
            if(replacement)GlobalFree(replacement);if(privacy)GlobalFree(privacy);
            error=ERROR_NOT_ENOUGH_MEMORY;return ClipboardBegin::Failed;
        }
        if(!OpenClipboard(owner)) {
            error=GetLastError();GlobalFree(replacement);GlobalFree(privacy);return ClipboardBegin::Busy;
        }
        if(!GetClipboardSequenceNumber()) {
            error=ERROR_ACCESS_DENIED;CloseClipboard();GlobalFree(replacement);GlobalFree(privacy);return ClipboardBegin::Failed;
        }
        UINT format=0;size_t total=0;ClipboardBegin status=ClipboardBegin::Ready;std::vector<UINT> formats;
        for(;;) {
            SetLastError(0);format=EnumClipboardFormats(format);
            if(!format){error=GetLastError();if(error)status=ClipboardBegin::Failed;break;}
            if(!PreservableTextClipboardFormat(format)) {
                status=ClipboardBegin::UnsupportedFormat;error=ERROR_NOT_SUPPORTED;break;
            }
            try {formats.push_back(format);}
            catch(...) {status=ClipboardBegin::Failed;error=ERROR_NOT_ENOUGH_MEMORY;break;}
        }
        // Do not request delayed-rendered data until every advertised format
        // has passed the whitelist; unsupported clipboards stay untouched.
        for(UINT entry:formats) {
            if(status!=ClipboardBegin::Ready)break;
            format=entry;
            HANDLE source=GetClipboardData(format);
            if(!source){status=ClipboardBegin::Failed;error=GetLastError();break;}
            SIZE_T size=GlobalSize(source);
            if(!size || size>kMaxBytes || total>kMaxBytes-size) {
                status=ClipboardBegin::TooLarge;error=ERROR_FILE_TOO_LARGE;break;
            }
            void *data=GlobalLock(source);
            if(!data){status=ClipboardBegin::Failed;error=GetLastError();break;}
            HGLOBAL copy=MakeBytes(data,size);GlobalUnlock(source);
            if(!copy){status=ClipboardBegin::Failed;error=ERROR_NOT_ENOUGH_MEMORY;break;}
            try {saved_.push_back({format,copy});}
            catch(...) {GlobalFree(copy);status=ClipboardBegin::Failed;error=ERROR_NOT_ENOUGH_MEMORY;break;}
            total+=size;
        }
        if(status!=ClipboardBegin::Ready) {
            CloseClipboard();GlobalFree(replacement);GlobalFree(privacy);FreeSaved();return status;
        }
        if(!EmptyClipboard()) {
            error=GetLastError();CloseClipboard();GlobalFree(replacement);GlobalFree(privacy);FreeSaved();return ClipboardBegin::Failed;
        }
        // Set the Windows history/cloud exclusion marker before placing any
        // transcript bytes on the clipboard.
        if(!SetClipboardData(exclude,privacy)) {
            error=GetLastError();GlobalFree(privacy);GlobalFree(replacement);
            recoveryFailed_=!RestoreWhileOpen(recoveryError_);
            CloseClipboard();return ClipboardBegin::Failed;
        }
        privacy=nullptr;
        if(!SetClipboardData(CF_UNICODETEXT,replacement)) {
            error=GetLastError();GlobalFree(replacement);
            if(!EmptyClipboard()){recoveryError_=GetLastError();recoveryFailed_=true;}
            else recoveryFailed_=!RestoreWhileOpen(recoveryError_);
            CloseClipboard();return ClipboardBegin::Failed;
        }
        replacement=nullptr; // Clipboard owns it.
        sequence_=GetClipboardSequenceNumber();owner_=owner;
        active_=sequence_!=0 && GetClipboardOwner()==owner_;
        if(!active_) {
            error=ERROR_ACCESS_DENIED;
            if(!EmptyClipboard()){recoveryError_=GetLastError();recoveryFailed_=true;}
            else recoveryFailed_=!RestoreWhileOpen(recoveryError_);
        }
        CloseClipboard();
        if(!active_)return ClipboardBegin::Failed;
        // Some providers update the sequence when closing the clipboard. Only
        // adopt the resulting value while our window remains the owner.
        DWORD afterClose=GetClipboardSequenceNumber();
        if(!afterClose || GetClipboardOwner()!=owner_) {
            active_=false;FreeSaved();error=ERROR_CANCELLED;return ClipboardBegin::ChangedByOther;
        }
        sequence_=afterClose;
        return ClipboardBegin::Ready;
    }

    bool Unchanged() const {
        return DecideClipboardRestore(active_,sequence_,GetClipboardSequenceNumber(),owner_,GetClipboardOwner())==ClipboardRestoreDecision::Restore;
    }
    bool Active() const {return active_;}
    bool RecoveryFailed() const {return recoveryFailed_;}
    DWORD RecoveryError() const {return recoveryError_;}

    ClipboardRestore Restore(HWND owner,DWORD &error) {
        error=0;if(!active_)return ClipboardRestore::NotActive;
        if(!Unchanged()){active_=false;FreeSaved();return ClipboardRestore::ChangedByOther;}
        if(!OpenClipboard(owner)){error=GetLastError();return ClipboardRestore::Busy;}
        if(GetClipboardSequenceNumber()!=sequence_ || GetClipboardOwner()!=owner_) {
            CloseClipboard();active_=false;FreeSaved();return ClipboardRestore::ChangedByOther;
        }
        if(!EmptyClipboard()) {error=GetLastError();CloseClipboard();return ClipboardRestore::Busy;}
        bool ok=RestoreWhileOpen(error);
        active_=false;CloseClipboard();FreeSaved();
        if(!ok){recoveryFailed_=true;recoveryError_=error;}
        return ok?ClipboardRestore::Restored:ClipboardRestore::Failed;
    }
private:
    struct Saved {UINT format;HGLOBAL data;};
    static constexpr size_t kMaxBytes=16u*1024u*1024u;
    static HGLOBAL MakeBytes(const void *source,size_t count) {
        HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE,count);if(!h)return nullptr;
        void *p=GlobalLock(h);if(!p){GlobalFree(h);return nullptr;}
        std::memcpy(p,source,count);GlobalUnlock(h);return h;
    }
    bool RestoreWhileOpen(DWORD &error) {
        // SetClipboardData accepting a handle alone does not prove that the
        // clipboard still holds the preserved bytes. Verify the formats while
        // the clipboard is open, without logging their contents.
        struct Expected {UINT format;std::vector<unsigned char> bytes;};
        std::vector<Expected> expected;
        bool ok=true;
        try {
            for(const auto &item:saved_) {
                SIZE_T size=GlobalSize(item.data);
                void *data=size?GlobalLock(item.data):nullptr;
                if(!data){if(ok)error=ERROR_INVALID_DATA;ok=false;continue;}
                try {
                    const auto *first=static_cast<const unsigned char *>(data);
                    expected.push_back({item.format,std::vector<unsigned char>(first,first+size)});
                } catch(...) {GlobalUnlock(item.data);throw;}
                GlobalUnlock(item.data);
            }
        } catch(...) {if(ok)error=ERROR_NOT_ENOUGH_MEMORY;ok=false;}
        for(auto &item:saved_) {
            if(item.data && SetClipboardData(item.format,item.data))item.data=nullptr;
            else {if(ok)error=GetLastError();ok=false;}
        }
        for(const auto &item:expected) {
            HANDLE actual=GetClipboardData(item.format);
            SIZE_T size=actual?GlobalSize(actual):0;
            void *data=size>=item.bytes.size()?GlobalLock(actual):nullptr;
            bool matches=data && std::memcmp(data,item.bytes.data(),item.bytes.size())==0;
            if(data)GlobalUnlock(actual);
            if(!matches){if(ok)error=ERROR_CRC;ok=false;}
        }
        FreeSaved();return ok;
    }
    void FreeSaved(){for(auto &item:saved_)if(item.data)GlobalFree(item.data);saved_.clear();}
    std::vector<Saved> saved_;DWORD sequence_=0,recoveryError_=0;HWND owner_=nullptr;bool active_=false,recoveryFailed_=false;
};
}

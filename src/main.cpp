// HebEN Dictation - a tiny Windows tray dictation utility.
// Speech stays on-device; only build/dependency download needs network.
// License: MIT (see LICENSE).
//
// Provenance: this is source code for an MVP. Build/run on Windows for verification.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <ole2.h>
#include <UIAutomation.h>

// Only microphone capture + conversion; omit unused miniaudio subsystems.
#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_DSOUND
#define MA_NO_WINMM
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "whisper.h"
#include "ggml-backend.h"
#ifdef HEBEN_CUDA_BUILD
#include <cuda_runtime_api.h>
#include <delayimp.h>
#endif
#include "utterance_detector.hpp"
#include "silero_stream.hpp"
#include "stable_prefix.hpp"
#include "input_mutation_guard.hpp"
#include "hotkey_settings.hpp"
#include "hotkey_registry.hpp"
#include "audio_ring.hpp"
#include "listening_indicator.hpp"
#include "diagnostics.hpp"
#include "target_integrity.hpp"
#include "insertion_policy.hpp"
#include "unicode_input.hpp"
#include "uia_writability.hpp"
#include "retained_text.hpp"
#include "terminal_trust.hpp"
#include "clipboard_text_transaction.hpp"
#include "paste_input.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {
constexpr UINT MSG_TRAY = WM_APP + 1;
constexpr UINT MSG_UI_EVENT = WM_APP + 2;
constexpr UINT MSG_WORK_DONE = WM_APP + 3;
constexpr UINT MSG_FOCUS_LOST = WM_APP + 4;
constexpr UINT MSG_AUTO_STOP = WM_APP + 5;
constexpr UINT MSG_VAD_FAILURE = WM_APP + 6;
constexpr UINT MSG_USER_EDIT = WM_APP + 7;
constexpr UINT_PTR INSERT_TIMER = 7361;
constexpr UINT_PTR CLIPBOARD_TIMER = 7362;
constexpr UINT HOTKEY_ID = 1201;
constexpr UINT TRAY_ID = 1202;
constexpr UINT MENU_TOGGLE = 2001;
constexpr UINT MENU_EDIT = 2002;
constexpr UINT MENU_RELOAD = 2003;
constexpr UINT MENU_COPY = 2004;
constexpr UINT MENU_COPY_RETAINED = 2006;
constexpr UINT MENU_HOTKEY_SETTINGS = 2007;
constexpr UINT MENU_EXIT = 2005;
constexpr UINT MENU_LANG_AUTO = 2010;
constexpr UINT MENU_LANG_HE = 2011;
constexpr UINT MENU_LANG_EN = 2012;
constexpr UINT MENU_TRUST_TERMINAL = 2013;
constexpr UINT MENU_INPUT_UNICODE = 2014;
constexpr UINT MENU_INPUT_CLIPBOARD = 2015;
constexpr UINT MENU_RESTORE_CLIPBOARD = 2016;
constexpr int SAMPLE_RATE = heben::SAMPLE_RATE;

struct Focus {
    HWND window = nullptr;
    HWND control = nullptr;
    DWORD processId=0;
    DWORD threadId=0;
    DWORD elementProcessId=0;
    bool trustedTerminal=false;
    std::vector<int> runtimeId;
    unsigned long long generation = 0;
};

// Accessed only by the UI thread. No field value or document text is read.
IUIAutomation *automation = nullptr;

struct Settings {
    UINT modifiers = MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT;
    UINT key = 'R';
    std::wstring hotkey = L"CTRL+SHIFT+R";
    fs::path model;
    fs::path hebrewModel;
    fs::path vadModel;
    std::string language = "auto";
    float threshold = 0.013f;
    float maxZeroCrossing = 0.35f;
    int silenceMs = 700;
    int prerollMs = 600;
    int maxPhraseSeconds = 15;
    unsigned unicodeDelayMs = 0;
    int threads = 4;
    int idleSeconds = 45;
    bool gpu = false;
    bool silero = true;
    bool autoHebrewFallback = false;
    bool hybrid = false;
    bool trustedTerminalInput = false;
    bool clipboardPaste = false;
    bool streamingPrototype = false;
    bool warmOnVoice = false;
    unsigned streamingIntervalMs = 1000;
    unsigned streamingAgreement = 6;
    unsigned streamingHoldWords = 1;
};

struct Job {
    std::vector<float> pcm;
    Focus focus;
    fs::path model;
    fs::path hebrewModel;
    std::string language;
    int threads = 4;
    bool gpu = false;
    bool autoHebrewFallback = false;
    bool hybrid = false;
    bool streaming = false;
    bool partial = false;
    std::uint64_t segmentId = 0;
    unsigned streamingAgreement = 6;
    unsigned streamingHoldWords = 1;
};

struct UiEvent {
    enum Kind { Transcript, Error, Backlog, StreamingConflict } kind;
    std::wstring text;
    Focus focus;
};

struct PendingInsertion {
    std::wstring text;
    Focus focus;
    Clock::time_point deadline;
    bool waitingLogged=false;
};

unsigned ModifierMask(bool asynchronous) {
    const int keys[]={VK_CONTROL,VK_SHIFT,VK_MENU,VK_LWIN,VK_RWIN};
    unsigned result=0;
    for(size_t i=0;i<sizeof(keys)/sizeof(keys[0]);++i) {
        SHORT state=asynchronous?GetAsyncKeyState(keys[i]):GetKeyState(keys[i]);
        if(state&0x8000)result|=1u<<i;
    }
    return result;
}
bool ModifiersHeld() { return ModifierMask(true)!=0; }
void RecordModifiers(const char *stage) {
    if(heben::diagnostics::Enabled())
        heben::diagnostics::Record(stage,"async_and_thread_queue_masks",ModifierMask(true),ModifierMask(false));
}

fs::path ExeDir() {
    std::wstring buffer(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!n || n >= buffer.size()) return fs::current_path();
    buffer.resize(n);
    return fs::path(buffer).parent_path();
}

std::wstring ReadIni(const fs::path &p, const wchar_t *name, const wchar_t *fallback) {
    wchar_t value[2048]{};
    GetPrivateProfileStringW(L"General", name, fallback, value, 2048, p.c_str());
    return value;
}

std::wstring Upper(std::wstring s) {
    for (auto &c : s) if (c >= L'a' && c <= L'z') c -= 32;
    return s;
}

std::wstring Trim(const std::wstring &s) {
    auto isBlank = [](wchar_t c){ return c == L' ' || c == L'\r' || c == L'\n' || c == L'\t'; };
    size_t first = 0, last = s.size();
    while (first < last && isBlank(s[first])) ++first;
    while (last > first && isBlank(s[last-1])) --last;
    return s.substr(first, last-first);
}

bool ParseHotkey(const std::wstring &s, UINT &mod, UINT &key) {
    UINT m = MOD_NOREPEAT;
    UINT k = 0;
    bool hasMod = false;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t sep = s.find(L'+', pos);
        std::wstring part = Upper(Trim(s.substr(pos, sep == std::wstring::npos ? sep : sep-pos)));
        if (part == L"CTRL" || part == L"CONTROL") { m |= MOD_CONTROL; hasMod = true; }
        else if (part == L"ALT") { m |= MOD_ALT; hasMod = true; }
        else if (part == L"SHIFT") { m |= MOD_SHIFT; hasMod = true; }
        else if (part == L"WIN") { m |= MOD_WIN; hasMod = true; }
        else if (part == L"SPACE") k = VK_SPACE;
        else if (part.size() == 1 && ((part[0] >= L'A' && part[0] <= L'Z') ||
                     (part[0] >= L'0' && part[0] <= L'9'))) k = static_cast<UINT>(part[0]);
        else if (part.size() >= 2 && part[0] == L'F') {
            int idx = _wtoi(part.c_str()+1);
            if (idx >= 1 && idx <= 12) k = VK_F1 + idx - 1;
            else return false;
        }
        else return false;
        if (sep == std::wstring::npos) break;
        pos = sep + 1;
    }
    if (!hasMod || !k) return false;
    mod = m;
    key = k;
    return true;
}

Settings LoadSettings(const fs::path &ini) {
    Settings s;
    s.hotkey = ReadIni(ini, L"hotkey", L"CTRL+SHIFT+R");
    if (!ParseHotkey(s.hotkey, s.modifiers, s.key)) {
        s.hotkey = L"CTRL+SHIFT+R";
        ParseHotkey(s.hotkey, s.modifiers, s.key);
    }
    std::wstring relative = ReadIni(ini, L"model", L"models\\ggml-base-q5_1.bin");
    s.model = fs::path(relative);
    if (s.model.is_relative()) s.model = ExeDir() / s.model;
    s.model = s.model.lexically_normal();
    s.hybrid = GetPrivateProfileIntW(L"General", L"hybrid", 0, ini.c_str()) != 0;
    if (s.hybrid) {
        s.hebrewModel = fs::path(ReadIni(ini, L"hebrew_model", L"models\\ivrit-whisper-large-v3-turbo-q5_0.bin"));
        if (s.hebrewModel.is_relative()) s.hebrewModel = ExeDir() / s.hebrewModel;
        s.hebrewModel = s.hebrewModel.lexically_normal();
    }
    s.vadModel = fs::path(ReadIni(ini, L"vad_model", L"models\\ggml-silero-v6.2.0.bin"));
    if (s.vadModel.is_relative()) s.vadModel = ExeDir() / s.vadModel;
    s.vadModel = s.vadModel.lexically_normal();
    s.silero = Upper(Trim(ReadIni(ini, L"vad_backend", L"silero"))) != L"ENERGY";
    std::wstring lang = Upper(Trim(ReadIni(ini, L"language", L"auto")));
    s.language = lang == L"HE" ? "he" : lang == L"EN" ? "en" : "auto";
    s.autoHebrewFallback = GetPrivateProfileIntW(L"General", L"auto_hebrew_fallback", 0, ini.c_str()) != 0;
    s.trustedTerminalInput = GetPrivateProfileIntW(L"General", L"trusted_terminal_input", 0, ini.c_str()) != 0;
    s.clipboardPaste = Upper(Trim(ReadIni(ini,L"input_method",L"unicode"))) == L"CLIPBOARD";
    s.streamingPrototype = GetPrivateProfileIntW(L"General",L"streaming_prototype",0,ini.c_str()) != 0;
    s.warmOnVoice = GetPrivateProfileIntW(L"General",L"model_warm_on_voice",0,ini.c_str()) != 0;
    s.streamingIntervalMs = std::clamp(GetPrivateProfileIntW(L"General",L"streaming_interval_ms",1000,ini.c_str()),500u,3000u);
    s.streamingAgreement = std::clamp(GetPrivateProfileIntW(L"General",L"streaming_agreement",6,ini.c_str()),2u,8u);
    s.streamingHoldWords = std::clamp(GetPrivateProfileIntW(L"General",L"streaming_hold_words",1,ini.c_str()),1u,5u);
    s.gpu = Upper(Trim(ReadIni(ini, L"backend", L"cpu"))) == L"CUDA";
    try {
        s.threshold = std::clamp(std::stof(ReadIni(ini, L"rms_threshold", L"0.013")), 0.001f, 0.20f);
    } catch (...) {}
    try {
        s.maxZeroCrossing = std::clamp(std::stof(ReadIni(ini, L"max_zero_crossing", L"0.35")), 0.1f, 1.0f);
    } catch (...) {}
    s.silenceMs = std::clamp(GetPrivateProfileIntW(L"General", L"silence_ms", 700, ini.c_str()), 250u, 5000u);
    s.prerollMs = std::clamp(GetPrivateProfileIntW(L"General", L"preroll_ms", 600, ini.c_str()), 100u, 1500u);
    s.maxPhraseSeconds = std::clamp(GetPrivateProfileIntW(L"General", L"max_phrase_seconds", 15, ini.c_str()), 3u, 25u);
    s.unicodeDelayMs = std::clamp(GetPrivateProfileIntW(L"General", L"unicode_delay_ms", 0, ini.c_str()), 0u, 20u);
    int threadInput = static_cast<int>(GetPrivateProfileIntW(L"General", L"threads", 0, ini.c_str()));
    unsigned cpu = std::max(1u, std::thread::hardware_concurrency());
    s.threads = threadInput == 0 ? static_cast<int>(std::min(cpu, 4u)) : std::clamp(threadInput, 1, 16);
    s.idleSeconds = std::clamp(GetPrivateProfileIntW(L"General", L"model_idle_seconds", 45, ini.c_str()), 5u, 3600u);
    return s;
}

Focus CurrentFocus(bool allowTrustedTerminal=false) {
    Focus focus;
    // UI Automation can create several helper threads. Load it only when a
    // dictation target actually needs to be validated, not at tray startup.
    HRESULT init=S_OK;
    if (!automation) init=CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&automation));
    if (!automation) { heben::diagnostics::Record("target","uia_unavailable",0,init); return focus; }
    focus.window = GetForegroundWindow();
    if (!focus.window) { heben::diagnostics::Record("target","no_foreground"); return focus; }
    DWORD foregroundPid=0;
    DWORD foregroundTid=GetWindowThreadProcessId(focus.window,&foregroundPid);
    heben::diagnostics::Record("foreground","hwnd_and_pid",static_cast<long long>(reinterpret_cast<INT_PTR>(focus.window)),foregroundPid);
    heben::diagnostics::Record("foreground","thread_id",foregroundTid);
    DWORD integrity=0,integrityError=0;
    if(!heben::SafeWindowIntegrity(focus.window,integrity,integrityError)) {
        heben::diagnostics::Record("target","integrity_refused",integrity,integrityError);return {};
    }
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    DWORD tid = GetWindowThreadProcessId(focus.window, &focus.processId);
    focus.threadId=tid;
    SetLastError(0);
    const bool guiInfo=GetGUIThreadInfo(tid, &info)!=FALSE;
    const DWORD guiError=guiInfo?0:GetLastError();
    if (guiInfo && info.hwndFocus) focus.control = info.hwndFocus;
    else focus.control = focus.window;
    heben::diagnostics::Record("gui_thread",guiInfo?(info.hwndFocus?"focus_observed":"focus_unavailable"):"query_failed",
        static_cast<long long>(reinterpret_cast<INT_PTR>(info.hwndFocus)),guiError);
    heben::diagnostics::Record("gui_thread","caret_hwnd",static_cast<long long>(reinterpret_cast<INT_PTR>(info.hwndCaret)));
    if(heben::diagnostics::Enabled()) {
        DWORD selfIntegrity=0,selfError=0;
        const bool selfOk=heben::ReadIntegrity(GetCurrentProcess(),selfIntegrity,selfError);
        heben::diagnostics::Record("integrity",selfOk?"self_and_target":"self_unavailable",selfIntegrity,integrity);
    }
    if(heben::IsNativePasswordControl(focus.control)) {
        heben::diagnostics::Record("target","native_password_refused");return {};
    }
    DWORD controlPid=0;GetWindowThreadProcessId(focus.control,&controlPid);
    if(!controlPid) {heben::diagnostics::Record("target","control_process_unknown");return {};}
    if(controlPid!=focus.processId) {
        DWORD controlIntegrity=0,controlError=0;
        if(!heben::SafeProcessIntegrity(controlPid,controlIntegrity,controlError)) {
            heben::diagnostics::Record("target","control_integrity_refused",controlIntegrity,controlError);return {};
        }
    }
    heben::diagnostics::Record("gui_thread","focus_pid",controlPid);
    IUIAutomationElement *element = nullptr;
    HRESULT focusResult=automation->GetFocusedElement(&element);
    if (FAILED(focusResult) || !element) { heben::diagnostics::Record("target","focused_element_unavailable",0,focusResult); return {}; }
    BOOL password = TRUE, enabled = FALSE, focused = FALSE;
    CONTROLTYPEID type = 0;
    SAFEARRAY *id = nullptr;
    HRESULT hp=element->get_CurrentIsPassword(&password);
    HRESULT he=element->get_CurrentIsEnabled(&enabled);
    HRESULT hf=element->get_CurrentHasKeyboardFocus(&focused);
    HRESULT ht=element->get_CurrentControlType(&type);
    int elementPid=0;HRESULT hid=element->get_CurrentProcessId(&elementPid);
    heben::diagnostics::Record("uia","element_process_id",elementPid,hid);
    heben::diagnostics::Record("uia","type_and_state_bits",type,
        (SUCCEEDED(hp)&&password?1:0)|(SUCCEEDED(he)&&enabled?2:0)|(SUCCEEDED(hf)&&focused?4:0));
    BSTR className=nullptr;
    HRESULT hc=element->get_CurrentClassName(&className);
    std::wstring focusedClass=SUCCEEDED(hc)&&className?std::wstring(className,SysStringLen(className)):L"";
    if(className)SysFreeString(className);
    const bool maybeVsCodeTerminal=_wcsicmp(focusedClass.c_str(),L"xterm-helper-textarea")==0;
    const std::wstring processName=(allowTrustedTerminal || maybeVsCodeTerminal)?heben::ProcessBaseName(foregroundPid):L"";
    if(maybeVsCodeTerminal && processName.empty()) {
        heben::diagnostics::Record("target","terminal_process_unknown",0,0);
        if(id)SafeArrayDestroy(id);
        element->Release();return {};
    }
    const bool vsCodeTerminal=SUCCEEDED(ht) && heben::IsVsCodeTerminalSurface(processName,type,focusedClass);
    const bool trusted=allowTrustedTerminal && SUCCEEDED(ht) && heben::TrustedTerminalTarget(true,
        processName,type,SUCCEEDED(hp),password!=FALSE,focusedClass);
    if(vsCodeTerminal && !allowTrustedTerminal) {
        heben::diagnostics::Record("target","vscode_terminal_opt_in_required",type,0);
        if(id)SafeArrayDestroy(id);
        element->Release();return {};
    }
    const bool passwordAllowed=trusted ? !(SUCCEEDED(hp)&&password) : (SUCCEEDED(hp)&&!password);
    const bool supportedType=trusted || type==UIA_EditControlTypeId || type==UIA_DocumentControlTypeId;
    bool ok = passwordAllowed && SUCCEEDED(he) && enabled && SUCCEEDED(hf) && focused && SUCCEEDED(ht) &&
        SUCCEEDED(hid) && elementPid>0 &&
        supportedType &&
        SUCCEEDED(element->GetRuntimeId(&id)) && id;
    if(!ok) {
        const char *reason=FAILED(hp)&&!trusted?"password_state_unknown":SUCCEEDED(hp)&&password?"password_refused":FAILED(he)?"enabled_state_unknown":!enabled?"disabled":FAILED(hf)?"focus_state_unknown":!focused?"not_focused":FAILED(ht)?"type_unknown":FAILED(hid)||elementPid<=0?"element_process_unknown":!supportedType?"unsupported_control_type":"runtime_id_unavailable";
        heben::diagnostics::Record("target",reason,type,FAILED(hp)?hp:FAILED(he)?he:FAILED(hf)?hf:FAILED(ht)?ht:FAILED(hid)?hid:0);
    }
    if(ok) {
        DWORD elementIntegrity=0,elementError=0;
        if(!heben::SafeProcessIntegrity(static_cast<DWORD>(elementPid),elementIntegrity,elementError)) {
            heben::diagnostics::Record("target","element_integrity_refused",elementIntegrity,elementError);ok=false;
        } else {
            focus.elementProcessId=static_cast<DWORD>(elementPid);
            heben::diagnostics::Record("integrity","element",elementIntegrity);
        }
    }
    if(ok && trusted) {
        focus.trustedTerminal=true;
        heben::diagnostics::Record("target","explicit_trusted_terminal",type,SUCCEEDED(hp)?1:0);
    }
    if (ok && !trusted) {
        IUnknown *pattern = nullptr;
        if (SUCCEEDED(element->GetCurrentPattern(UIA_ValuePatternId, &pattern)) && pattern) {
            IUIAutomationValuePattern *value = nullptr;
            if (SUCCEEDED(pattern->QueryInterface(IID_PPV_ARGS(&value))) && value) {
                BOOL readOnly = TRUE;
                ok = SUCCEEDED(value->get_CurrentIsReadOnly(&readOnly)) && !readOnly;
                heben::diagnostics::Record("uia",ok?"value_writable":"value_readonly_or_unknown");
                if(!ok)heben::diagnostics::Record("target","readonly_or_unknown");
                value->Release();
            } else ok = false;
            pattern->Release();
        } else {
            HRESULT detail=S_OK;
            auto state=heben::TextPatternWritability(element,detail);
            ok=state==heben::WritableState::Writable;
            heben::diagnostics::Record("uia",ok?"text_pattern_writable":state==heben::WritableState::ReadOnly?"text_pattern_readonly":"writability_unknown_refused",0,detail);
        }
        if(heben::diagnostics::Enabled()) {
            IUnknown *textPattern=nullptr;
            HRESULT textResult=element->GetCurrentPattern(UIA_TextPatternId,&textPattern);
            heben::diagnostics::Record("uia",SUCCEEDED(textResult)&&textPattern?"text_pattern_available":"text_pattern_unavailable",0,textResult);
            if(textPattern)textPattern->Release();
        }
    }
    if (ok) {
        LONG lower = 0, upper = -1;
        ok = SUCCEEDED(SafeArrayGetLBound(id, 1, &lower)) &&
             SUCCEEDED(SafeArrayGetUBound(id, 1, &upper)) && upper >= lower && upper-lower < 256;
        for (LONG i = lower; ok && i <= upper; ++i) {
            int part = 0;
            ok = SUCCEEDED(SafeArrayGetElement(id, &i, &part));
            if (ok) focus.runtimeId.push_back(part);
        }
    }
    if (id) SafeArrayDestroy(id);
    element->Release();
    if(ok && GetForegroundWindow()!=focus.window) {heben::diagnostics::Record("target","foreground_changed_during_check");ok=false;}
    if(ok)heben::diagnostics::Record("target","accepted",type,integrity);
    return ok ? focus : Focus{};
}

bool SameFocus(const Focus &a) {
    if (!a.window) return false;
    Focus b = CurrentFocus(a.trustedTerminal);
    return a.window == b.window && a.control == b.control &&
           a.processId == b.processId && a.threadId == b.threadId && a.elementProcessId == b.elementProcessId && a.trustedTerminal==b.trustedTerminal &&
           !a.runtimeId.empty() && a.runtimeId == b.runtimeId;
}

bool PreflightTarget(const Focus &expected,const std::atomic<unsigned long long> &generation) {
    if(generation.load(std::memory_order_relaxed)!=expected.generation || GetForegroundWindow()!=expected.window) {
        heben::diagnostics::Record("preflight","foreground_or_generation_changed");return false;
    }
    GUITHREADINFO info{};info.cbSize=sizeof(info);
    if(!GetGUIThreadInfo(expected.threadId,&info)) {
        heben::diagnostics::Record("preflight","gui_thread_query_failed",0,GetLastError());return false;
    }
    HWND control=info.hwndFocus?info.hwndFocus:expected.window;
    if(control!=expected.control) {heben::diagnostics::Record("preflight","native_focus_changed");return false;}
    if(ModifiersHeld()) {heben::diagnostics::Record("preflight","modifier_repressed");return false;}
    return true;
}

heben::UnicodeDispatchResult TypeUnicode(const std::wstring &text,const Focus &expected,
                                        const std::atomic<unsigned long long> &generation,unsigned delayMs) {
    // KEYEVENTF_UNICODE bypasses keyboard layout and does not touch clipboard.
    // A single batch reduces interleaving, but does not make focus atomic.
    const auto result=heben::DispatchUnicodeKeyboard(text,[&] {return PreflightTarget(expected,generation);},delayMs);
    const char *state=!result.validUtf16?"invalid_utf16":!result.safeText?"unsafe_control_refused":
        !result.preflightPassed?"preflight_refused":
        result.allQueued()?"events_accepted_editor_unverified":"partial_or_blocked";
    heben::diagnostics::Record("sendinput",state,result.accepted,result.error);
    heben::diagnostics::Record("sendinput","events_requested",result.requested);
    heben::diagnostics::Record("sendinput","delay_ms_per_utf16_unit",delayMs);
    return result;
}

std::wstring FromUtf8(const std::string &s) {
    if (s.empty()) return L"";
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (count <= 0) return L"";
    std::wstring out(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), count);
    return out;
}

bool CopyUnicode(const std::wstring &s, HWND owner) {
    SIZE_T bytes = (s.size()+1)*sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) return false;
    void *dst = GlobalLock(mem);
    if (!dst) { GlobalFree(mem); return false; }
    memcpy(dst, s.c_str(), bytes);
    GlobalUnlock(mem);
    if (!OpenClipboard(owner)) { GlobalFree(mem); return false; }
    if (!EmptyClipboard()) { GlobalFree(mem); CloseClipboard(); return false; }
    if (!SetClipboardData(CF_UNICODETEXT, mem)) { GlobalFree(mem); CloseClipboard(); return false; }
    CloseClipboard();
    return true;
}


class App {
public:
    explicit App(bool iconInspection=false):iconInspection_(iconInspection) {}
    bool Initialize(HINSTANCE instance) {
        heben::diagnostics::Record("application","starting");
        // Runtime has no console/log file. In particular Silero streaming must
        // not format and emit several diagnostic lines every 32 ms.
        whisper_log_set([](ggml_log_level,const char*,void*){},nullptr);
        ini_ = ExeDir() / L"config.ini";
        config_ = LoadSettings(ini_);
        idleSeconds_.store(config_.idleSeconds);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
#if defined(HEBEN_FIXED_INPUT_PROBE)
        wc.lpszClassName = L"HebENFixedInputProbeHiddenClass";
#elif defined(HEBEN_HEADLESS_TEST)
        wc.lpszClassName = L"HebENReplayFixtureHiddenClass";
#elif defined(HEBEN_DESKTOP_AUDIO_TEST)
        wc.lpszClassName = L"HebENDesktopAudioTestHiddenClass";
#elif defined(HEBEN_DESKTOP_MIC_TEST)
        wc.lpszClassName = L"HebENDesktopMicTestHiddenClass";
#elif defined(HEBEN_HOTKEY_UI_TEST)
        wc.lpszClassName = L"HebENHotkeyUiTestHiddenClass";
#else
        wc.lpszClassName = iconInspection_ ? L"HebENIconInspectionHiddenClass" : L"HebENDictationHiddenClass";
#endif
        wc.lpfnWndProc = WndProc;
        appIcon_=heben::LoadSmallAppIcon(instance);
        HICON largeIcon=heben::LoadLargeAppIcon(instance);
        heben::diagnostics::Record("icon_load",appIcon_&&largeIcon?"ok":"failed",0,GetLastError());
        if(!appIcon_ || !largeIcon)return false;
        wc.hIcon=largeIcon;wc.hIconSm=appIcon_;
        if (!RegisterClassExW(&wc)) return false;
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName,
            L"HebEN Dictation", WS_OVERLAPPED, 0,0,0,0,
            nullptr, nullptr, instance, this);
        if (!hwnd_) return false;

#ifndef HEBEN_HEADLESS_TEST
#if !defined(HEBEN_FIXED_INPUT_PROBE) && !defined(HEBEN_DESKTOP_AUDIO_TEST) && !defined(HEBEN_DESKTOP_MIC_TEST) && !defined(HEBEN_HOTKEY_UI_TEST)
        taskbarCreatedMessage_=RegisterWindowMessageW(L"TaskbarCreated");
        if(!AddTrayIcon())return false;
#endif

#ifdef HEBEN_FIXED_INPUT_PROBE
        // Test-only: a smoke run can skip registering a global shortcut and
        // therefore cannot inject into the user's desktop accidentally.
        wchar_t disabled[2]{};
        if(GetEnvironmentVariableW(L"HEBEN_PROBE_NO_HOTKEY",disabled,2)>0 && disabled[0]==L'1') {
            heben::diagnostics::Record("hotkey_register","disabled_for_smoke");
        } else if(!RegisterHotKey(hwnd_,HOTKEY_ID,config_.modifiers,config_.key)) {
            heben::diagnostics::Record("hotkey_register","failed",config_.key,GetLastError());
            return false;
        } else {hotkeyRegistered_=true;heben::diagnostics::Record("hotkey_register","ok",config_.key,config_.modifiers);}
#else
        if (iconInspection_) {
            heben::diagnostics::Record("icon_inspection","no_hotkey_or_worker");
        } else if (!RegisterHotKey(hwnd_, HOTKEY_ID, config_.modifiers, config_.key)) {
            heben::diagnostics::Record("hotkey_register","failed",0,GetLastError());
            Balloon(L"Hotkey in use", L"Edit config.ini and choose another shortcut.");
            indicator_.ShowError(automation,L"Shortcut already in use");
        } else {hotkeyRegistered_ = true;heben::diagnostics::Record("hotkey_register","ok",config_.key,config_.modifiers);}
#endif
#else
        heben::diagnostics::Record("test_fixture","headless_no_mic_no_injection");
#endif
#if !defined(HEBEN_FIXED_INPUT_PROBE) && !defined(HEBEN_HOTKEY_UI_TEST)
        if(!iconInspection_)worker_ = std::thread([this]{ WorkerLoop(); });
#endif
        return true;
    }

    void Run() {
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if(hotkeyWindow_.Handle() && IsDialogMessageW(hotkeyWindow_.Handle(),&msg))continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        App *self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto *cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = reinterpret_cast<App*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            return TRUE;
        }
        return self ? self->Dispatch(hwnd, msg, wp, lp) : DefWindowProcW(hwnd,msg,wp,lp);
    }

    LRESULT Dispatch(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if(taskbarCreatedMessage_ && msg==taskbarCreatedMessage_) {
            trayCreated_=false;
            AddTrayIcon();
            return 0;
        }
        switch (msg) {
            case WM_APP + 205:
                if(iconInspection_){UpdateTray();return 1;}
                return 0;
            case WM_APP + 206:
                return iconInspection_ && StartIconInspectionCapture() ? 1 : 0;
            case WM_APP + 207:
                if(iconInspection_){StopIconInspectionCapture();return 1;}
                return 0;
#ifdef HEBEN_HEADLESS_TEST
            case WM_APP + 40: return StartReplay() ? 1 : 0;
            case WM_APP + 41: StopReplay(); return 1;
            case WM_APP + 42: return capturing_ ? 1 : 0;
            case WM_APP + 43: return static_cast<LRESULT>(outstanding_.load(std::memory_order_relaxed));
            case WM_APP + 44: return static_cast<LRESULT>(captureTarget_.generation);
            case WM_COPYDATA: {
                const auto *data=reinterpret_cast<const COPYDATASTRUCT*>(lp);
                if(!capturing_ || !vadContext_ || !data || data->dwData!=0x48455043 || !data->lpData ||
                   !data->cbData || data->cbData>65536 || data->cbData%sizeof(float))return 0;
                size_t n=data->cbData/sizeof(float);
                ma_device simulated{};simulated.pUserData=this;
                CaptureCallback(&simulated,nullptr,data->lpData,static_cast<ma_uint32>(n));
                return 1;
            }
#endif
#ifdef HEBEN_DESKTOP_AUDIO_TEST
            case WM_COPYDATA: {
                const auto *data=reinterpret_cast<const COPYDATASTRUCT*>(lp);
                if(!capturing_ || !vadContext_ || !data || data->dwData!=0x48455043 || !data->lpData ||
                   !data->cbData || data->cbData>65536 || data->cbData%sizeof(float))return 0;
                ma_device simulated{};simulated.pUserData=this;
                CaptureCallback(&simulated,nullptr,data->lpData,static_cast<ma_uint32>(data->cbData/sizeof(float)));
                return 1;
            }
#endif
#ifdef HEBEN_DESKTOP_MIC_TEST
            case WM_APP + 43: return static_cast<LRESULT>(outstanding_.load(std::memory_order_relaxed));
#endif
#ifdef HEBEN_TEST_HOOKS
            // Compiled only into the separate integration fixture, never the
            // distributed application. Exercises the real focus/insertion path.
            case WM_APP + 30:
                captureTarget_ = CurrentFocus(config_.trustedTerminalInput);
                if(!captureTarget_.window || !BeginFocusGuard())return 0;
                testFocus_=captureTarget_;
                return 1;
            case WM_COPYDATA: {
                const auto *data = reinterpret_cast<const COPYDATASTRUCT*>(lp);
                if (!data || data->dwData != 0x48454245 || !data->lpData ||
                    data->cbData < sizeof(wchar_t) || data->cbData > 8192 || data->cbData % sizeof(wchar_t)) return 0;
                const auto *text = static_cast<const wchar_t*>(data->lpData);
                size_t count = data->cbData/sizeof(wchar_t);
                if (text[count-1] != L'\0' || testFocus_.generation!=focusGeneration_.load() || !SameFocus(testFocus_)) return 0;
                QueueTranscript(std::wstring(text,count-1),testFocus_);
                return 1;
            }
#endif
            case WM_HOTKEY:
                if(iconInspection_)return 0;
                if (wp == static_cast<WPARAM>(activeHotkeyId_)) {
                    if(hotkeyWindow_.Handle() && GetForegroundWindow()==hotkeyWindow_.Handle())return 0;
#ifdef HEBEN_HOTKEY_UI_TEST
                    heben::diagnostics::Record("test_fixture","hotkey_received_without_capture");return 0;
#endif
                    heben::diagnostics::Record("hotkey","received",capturing_?1:0);
                    RecordModifiers("hotkey_modifiers");
#ifdef HEBEN_FIXED_INPUT_PROBE
                    if(!hotkeyRegistered_) {heben::diagnostics::Record("probe","disarmed_hotkey_ignored");return 0;}
                    captureTarget_=CurrentFocus(config_.trustedTerminalInput);
                    if(!captureTarget_.window) {heben::diagnostics::Record("probe","target_refused");return 0;}
                    if(!BeginFocusGuard()) {heben::diagnostics::Record("probe","focus_guard_failed");return 0;}
                    heben::diagnostics::Record("probe","fixed_text_queued");
                    QueueTranscript(L"שלום עולם Hello World 123",captureTarget_);
#else
                    Toggle();
#endif
                }
                return 0;
            case WM_TIMER:
                if(wp==INSERT_TIMER){FlushPending();return 0;}
                if(wp==CLIPBOARD_TIMER){RestoreClipboard();return 0;}
                return DefWindowProcW(hwnd,msg,wp,lp);
            case MSG_WORK_DONE:
                if(outstanding_.load(std::memory_order_relaxed)>0)outstanding_.fetch_sub(1,std::memory_order_relaxed);
                MaybeEndFocusGuard();
                return 0;
            case MSG_FOCUS_LOST:
                if(capturing_ && captureTarget_.generation==static_cast<unsigned long long>(wp)) {
                    heben::diagnostics::Record("capture","stopped_on_foreground_change");
                    StopCapture();
                }
                return 0;
            case MSG_USER_EDIT:
                heben::diagnostics::Record("streaming","physical_input_cancelled");
                if(capturing_ && captureTarget_.generation==static_cast<unsigned long long>(wp))StopCapture();
                return 0;
            case MSG_AUTO_STOP:
                if(capturing_ && captureTarget_.generation==static_cast<unsigned long long>(wp)) {
                    heben::diagnostics::Record("capture","stopped_after_5s_silence");
                    StopCapture();
                }
                return 0;
            case MSG_VAD_FAILURE:
                if(capturing_ && captureTarget_.generation==static_cast<unsigned long long>(wp)) {
                    heben::diagnostics::Record("capture","stopped_after_vad_failure");
                    StopCapture();
                }
                return 0;
            case MSG_TRAY:
                if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) ShowMenu();
                else if (lp == WM_LBUTTONDBLCLK && !iconInspection_) Toggle();
                return 0;
            case WM_COMMAND:
                switch (LOWORD(wp)) {
                    case MENU_TOGGLE: if(!iconInspection_)Toggle(); break;
                    case MENU_HOTKEY_SETTINGS: ShowHotkeySettings(); break;
                    case MENU_EDIT: {
                        std::wstring argument = L"\"" + ini_.wstring() + L"\"";
                        ShellExecuteW(hwnd_,L"open",L"notepad.exe",argument.c_str(),nullptr,SW_SHOWNORMAL);
                        break;
                    }
                    case MENU_RELOAD: Reload(); break;
                    case MENU_COPY:
                        if (lastTranscript_.empty()) Balloon(L"No transcript yet", L"Dictate a phrase first.");
                        else if (!CopyUnicode(lastTranscript_, hwnd_)) Balloon(L"Clipboard error", L"Could not copy text.");
                        break;
                    case MENU_COPY_RETAINED: {
                        const auto *next=retained_.Front();
                        if(!next) {Balloon(L"No unsent text",L"There are no retained segments.");break;}
                        bool partial=next->mayBePartial;
                        if(!retained_.CopyNext([this](const std::wstring &text){return CopyUnicode(text,hwnd_);}))
                            Balloon(L"Clipboard error",L"The retained text remains available.");
                        else if(partial)
                            Balloon(L"Copied",L"Some text may already have been inserted. Check before pasting.");
                        break;
                    }
                    case MENU_EXIT: DestroyWindow(hwnd_); break;
                    case MENU_LANG_AUTO: SetLanguage("auto"); break;
                    case MENU_LANG_HE: SetLanguage("he"); break;
                    case MENU_LANG_EN: SetLanguage("en"); break;
                    case MENU_TRUST_TERMINAL: SetTrustedTerminal(!config_.trustedTerminalInput); break;
                    case MENU_INPUT_UNICODE: SetInputMethod(false); break;
                    case MENU_INPUT_CLIPBOARD: SetInputMethod(true); break;
                    case MENU_RESTORE_CLIPBOARD:
                        clipboardDeadline_=Clock::now()+std::chrono::seconds(5);RestoreClipboard();break;
                }
                return 0;
            case MSG_UI_EVENT: {
                std::unique_ptr<UiEvent> event(reinterpret_cast<UiEvent*>(lp));
                if (!event) return 0;
                if (event->kind == UiEvent::Transcript) {
                    std::wstring output = Trim(event->text);
                    if (output.empty()) return 0;
                    heben::diagnostics::Record("transcript","received_for_insertion",output.size());
#ifdef HEBEN_HEADLESS_TEST
                    WriteReplayText(output);
#else
                    QueueTranscript(std::move(output),event->focus);
#endif
                } else if(event->kind == UiEvent::StreamingConflict) {
                    heben::diagnostics::Record("streaming","final_prefix_conflict_retained");
#ifndef HEBEN_HEADLESS_TEST
                    retained_.Add(event->text,true);
                    Balloon(L"Dictation needs review",L"A later hypothesis changed earlier words. Full text is retained in the tray; check for duplicates before copying.");
#endif
                } else if (event->kind == UiEvent::Error) {
#ifdef HEBEN_HEADLESS_TEST
                    heben::diagnostics::Record("test_fixture","error");
#else
                    Balloon(L"Dictation error", event->text);
                    if (capturing_) StopCapture();
                    indicator_.ShowError(automation,L"Dictation error");
#endif
                } else Balloon(L"Dictation is behind", L"Transcription queue full. Speak more slowly or choose a smaller model.");
                return 0;
            }
            case WM_DESTROY:
                Shutdown();
                PostQuitMessage(0);
                return 0;
            default: return DefWindowProcW(hwnd, msg, wp, lp);
        }
    }

#ifdef HEBEN_HEADLESS_TEST
    bool StartReplay() {
        if(capturing_ || !fs::exists(config_.model))return false;
        heben::diagnostics::Record("test_fixture","replay_started");
        captureTarget_.generation=focusGeneration_.fetch_add(1,std::memory_order_relaxed)+1;
        detector_=std::make_unique<heben::Detector>(config_.threshold,config_.silenceMs,config_.maxPhraseSeconds,
            [this](std::vector<float>&& pcm){QueuePhrase(std::move(pcm));},config_.maxZeroCrossing,config_.prerollMs);
        if(config_.streamingPrototype)detector_->SetPreview(config_.streamingIntervalMs,
            [this](std::vector<float>&& pcm,std::uint64_t id){QueuePhrase(std::move(pcm),true,id);});
        voiceWarmRequested_.store(false,std::memory_order_relaxed);
        if(config_.warmOnVoice)detector_->SetSpeechStart([this]{RequestWarmOnVoice();});
        vadContext_=OpenVadModel(config_.vadModel);
        if(!vadContext_){detector_.reset();heben::diagnostics::Record("vad_model","failed");return false;}
        heben::diagnostics::Record("vad_model","loaded");
        capturedFrames_.store(0);capturePeak_.store(0);clippedFrames_.store(0);autoStopPosted_.store(false);
        try {
            audioRing_=std::make_unique<heben::AudioRing<10*SAMPLE_RATE>>();
            {std::lock_guard<std::mutex> lock(audioMutex_);audioStopped_=audioOverflow_=false;}
            vadThread_=std::thread([this]{SileroLoop();});
        } catch(...) {audioRing_.reset();whisper_vad_free(vadContext_);vadContext_=nullptr;detector_.reset();return false;}
        // Test-only ablation: estimate the cost of eager warm-up during
        // silence without changing the shipping application's policy.
        wchar_t skipWarm[2]{};
        const bool warmOnStart=!config_.warmOnVoice && !(GetEnvironmentVariableW(L"HEBEN_TEST_SKIP_MODEL_WARM",skipWarm,2)>0 && skipWarm[0]==L'1');
        {std::lock_guard<std::mutex> lock(jobsMutex_);capturing_=true;warmRequested_=warmOnStart;warmModel_=config_.model;warmGpu_=config_.gpu;}
        jobsCv_.notify_one();return true;
    }
    void StopReplay() {
        if(!capturing_)return;
        StopSilero();detector_.reset();
        {std::lock_guard<std::mutex> lock(jobsMutex_);capturing_=false;}
        heben::diagnostics::Record("pcm","simulated_total_frames",capturedFrames_.load());
        jobsCv_.notify_one();
    }
    void WriteReplayText(const std::wstring &text) {
        wchar_t path[32768]{};
        DWORD n=GetEnvironmentVariableW(L"HEBEN_TEST_TRANSCRIPT",path,32768);
        if(!n || n>=32768){heben::diagnostics::Record("test_sink","path_missing");return;}
        int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
        if(bytes<=0){heben::diagnostics::Record("test_sink","encoding_failed");return;}
        std::string utf8(static_cast<size_t>(bytes),'\0');
        WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),utf8.data(),bytes,nullptr,nullptr);
        std::wstring destination(path);
        if(++replayTranscriptCount_>1)destination+=L"."+std::to_wstring(replayTranscriptCount_);
        HANDLE file=CreateFileW(destination.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE){heben::diagnostics::Record("test_sink","open_failed",0,GetLastError());return;}
        DWORD written=0;BOOL ok=WriteFile(file,utf8.data(),static_cast<DWORD>(utf8.size()),&written,nullptr);CloseHandle(file);
        heben::diagnostics::Record("test_sink",ok&&written==utf8.size()?"written":"write_failed",written);
    }
#endif

    static void CALLBACK FocusEvent(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD) {
        App *self=guardOwner_;
        if(!self || !self->captureTarget_.window)return;
        bool changed=false;
        if(event==EVENT_SYSTEM_FOREGROUND && window && window!=self->captureTarget_.window) {
            DWORD eventPid=0;GetWindowThreadProcessId(window,&eventPid);
            heben::diagnostics::Record("focus_event","foreground_changed_pid",eventPid,self->captureTarget_.processId);
            if(self->capturing_)PostMessageW(self->hwnd_,MSG_FOCUS_LOST,static_cast<WPARAM>(self->captureTarget_.generation),0);
            changed=true;
        }
        else if(event==EVENT_OBJECT_FOCUS && object==OBJID_CLIENT && window &&
                GetForegroundWindow()==self->captureTarget_.window && !SameFocus(self->captureTarget_)) {
            heben::diagnostics::Record("focus_event","element_changed_or_unavailable");changed=true;
        }
        if(changed) {
            self->focusGeneration_.fetch_add(1,std::memory_order_relaxed);
            heben::diagnostics::Record("target","focus_change_event");
        }
    }

    bool BeginFocusGuard() {
        if(!foregroundHook_) {
            guardOwner_=this;
            foregroundHook_=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,FocusEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
            focusHook_=SetWinEventHook(EVENT_OBJECT_FOCUS,EVENT_OBJECT_FOCUS,nullptr,FocusEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
            if(!foregroundHook_ || !focusHook_) { EndFocusGuard(); return false; }
        }
        captureTarget_.generation=focusGeneration_.fetch_add(1,std::memory_order_relaxed)+1;
        mutationGuard_.Stop();
        if(config_.streamingPrototype) {
            const auto generation=captureTarget_.generation;
            if(!mutationGuard_.Start(config_.key,config_.modifiers,[this,generation]{
                auto expected=generation;
                if(focusGeneration_.compare_exchange_strong(expected,generation+1,std::memory_order_relaxed))
                    PostMessageW(hwnd_,MSG_USER_EDIT,static_cast<WPARAM>(generation),0);
            })) {EndFocusGuard();return false;}
        }
        return true;
    }

    void EndFocusGuard() {
        mutationGuard_.Stop();
        if(foregroundHook_)UnhookWinEvent(foregroundHook_);
        if(focusHook_)UnhookWinEvent(focusHook_);
        foregroundHook_=focusHook_=nullptr;
        if(guardOwner_==this)guardOwner_=nullptr;
    }

    void MaybeEndFocusGuard() {
        if(!capturing_ && outstanding_.load(std::memory_order_relaxed)==0 && pending_.empty()) {
            if(waitingForText_){indicator_.Hide();waitingForText_=false;}
            EndFocusGuard();
        }
    }

    void RestoreClipboard(bool resumePending=true) {
        KillTimer(hwnd_,CLIPBOARD_TIMER);
        if(!clipboard_.Active())return;
        DWORD error=0;auto state=clipboard_.Restore(hwnd_,error);
        if(state==heben::ClipboardRestore::Busy) {
            heben::diagnostics::Record("clipboard_restore","busy",0,error);
            if(Clock::now()<clipboardDeadline_ && SetTimer(hwnd_,CLIPBOARD_TIMER,250,nullptr))return;
            clipboardBlockedGeneration_=clipboardTargetGeneration_;
            if(resumePending)while(!pending_.empty()){retained_.Add(pending_.front().text,false);pending_.pop_front();}
            Balloon(L"Clipboard restore pending",L"Use the tray menu to retry restoring the previous clipboard.");
            MaybeEndFocusGuard();return;
        }
        const char *message=state==heben::ClipboardRestore::Restored?"restored":
            state==heben::ClipboardRestore::ChangedByOther?"changed_by_other_preserved":
            state==heben::ClipboardRestore::Failed?"restore_failed":"not_active";
        heben::diagnostics::Record("clipboard_restore",message,0,error);
        if(state==heben::ClipboardRestore::ChangedByOther || state==heben::ClipboardRestore::Failed)
            clipboardBlockedGeneration_=clipboardTargetGeneration_;
        if(state==heben::ClipboardRestore::Failed)
            Balloon(L"Clipboard restore failed",L"Check your clipboard; it may not contain its previous data.");
        if(resumePending)FlushPending();
    }

    heben::UnicodeDispatchResult PasteUsingClipboard(const std::wstring &text,const Focus &target) {
        heben::UnicodeDispatchResult result;result.requested=4;
        // Terminal paste shortcuts and raw modes have not been verified. The
        // trusted-terminal option does not silently opt into terminal paste.
        if(target.trustedTerminal) {
            result.preflightPassed=false;result.error=ERROR_NOT_SUPPORTED;
            heben::diagnostics::Record("clipboard_paste","terminal_paste_unverified");return result;
        }
        if(!PreflightTarget(target,focusGeneration_)) {
            result.preflightPassed=false;result.error=ERROR_CANCELLED;return result;
        }
        DWORD error=0;auto state=clipboard_.Begin(hwnd_,text,error);
        if(state!=heben::ClipboardBegin::Ready) {
            const char *reason=state==heben::ClipboardBegin::Busy?"clipboard_busy":
                state==heben::ClipboardBegin::UnsupportedFormat?"unsupported_clipboard_format":
                state==heben::ClipboardBegin::TooLarge?"clipboard_too_large":
                state==heben::ClipboardBegin::ChangedByOther?"changed_during_setup":
                state==heben::ClipboardBegin::InvalidText?"invalid_text":"begin_failed";
            result.preflightPassed=false;result.error=error;
            heben::diagnostics::Record("clipboard_paste",reason,0,error);
            if(state==heben::ClipboardBegin::ChangedByOther || clipboard_.RecoveryFailed())
                clipboardBlockedGeneration_=target.generation;
            if(clipboard_.RecoveryFailed()) {
                heben::diagnostics::Record("clipboard_restore","recovery_failed",0,clipboard_.RecoveryError());
                Balloon(L"Clipboard restore failed",L"Check your clipboard; its previous data may not be intact.");
            }
            return result;
        }
        clipboardTargetGeneration_=target.generation;
        clipboardDeadline_=Clock::now()+std::chrono::seconds(10);
        if(!SetTimer(hwnd_,CLIPBOARD_TIMER,1500,nullptr)) {
            result.preflightPassed=false;result.error=GetLastError();
            heben::diagnostics::Record("clipboard_paste","restore_timer_failed",0,result.error);
            RestoreClipboard(false);return result;
        }
        // Recheck target and clipboard after acquiring it. Never paste twice.
        if(!clipboard_.Unchanged()) {
            result.preflightPassed=false;result.error=ERROR_CANCELLED;
            heben::diagnostics::Record("clipboard_paste","clipboard_changed_before_paste");
            RestoreClipboard(false);return result;
        }
        if(!SameFocus(target) || !PreflightTarget(target,focusGeneration_)) {
            result.preflightPassed=false;result.error=ERROR_CANCELLED;
            heben::diagnostics::Record("clipboard_paste","target_changed_before_paste");
            RestoreClipboard(false);return result;
        }
        if(!clipboard_.Unchanged()) {
            result.preflightPassed=false;result.error=ERROR_CANCELLED;
            heben::diagnostics::Record("clipboard_paste","clipboard_changed_before_send");
            RestoreClipboard(false);return result;
        }
        auto keys=heben::PasteKeyEvents();SetLastError(0);
        result.accepted=SendInput(static_cast<UINT>(keys.size()),keys.data(),sizeof(INPUT));result.error=GetLastError();
        heben::diagnostics::Record("clipboard_paste",result.allQueued()?"events_accepted_editor_unverified":"partial_or_blocked",result.accepted,result.error);
        return result;
    }

    void FlushPending() {
        while(!pending_.empty()) {
            auto &item=pending_.front();
            bool same=item.focus.generation==focusGeneration_.load(std::memory_order_relaxed) && SameFocus(item.focus);
            auto decision=heben::DecideInsertion(same,ModifiersHeld(),Clock::now()>=item.deadline);
            if(decision==heben::InsertionDecision::CancelFocus) {
                heben::diagnostics::Record("insertion","focus_changed");
                retained_.Add(item.text,false);
                Balloon(L"Focus changed",L"Text kept in tray > Copy oldest unsent text.");
                pending_.pop_front();continue;
            }
            if(decision==heben::InsertionDecision::WaitForModifiers || decision==heben::InsertionDecision::CancelTimeout) {
                if(!item.waitingLogged){heben::diagnostics::Record("insertion","waiting_for_modifiers");RecordModifiers("insertion_wait_modifiers");item.waitingLogged=true;}
                if(decision==heben::InsertionDecision::CancelTimeout) {
                    heben::diagnostics::Record("insertion","modifier_timeout");
                    retained_.Add(item.text,false);
                    Balloon(L"Insertion paused",L"Release modifier keys and use Copy oldest unsent text.");
                    pending_.pop_front();continue;
                }
                if(!SetTimer(hwnd_,INSERT_TIMER,50,nullptr)) {
                    heben::diagnostics::Record("insertion","timer_failed",0,GetLastError());
                    retained_.Add(item.text,false);
                    Balloon(L"Insertion unavailable",L"Use tray > Copy oldest unsent text.");
                    pending_.pop_front();continue;
                }
                return;
            }
            if(config_.clipboardPaste && item.focus.generation==clipboardBlockedGeneration_) {
                heben::diagnostics::Record("clipboard_paste","session_blocked_after_clipboard_change");
                retained_.Add(item.text,false);pending_.pop_front();continue;
            }
            std::wstring output=heben::JoinDictationPhrase(item.text,item.focus.generation==lastTypedGeneration_,lastTypedCharacter_);
            RecordModifiers("insertion_send_modifiers");
            if(config_.clipboardPaste && clipboard_.Active()) {KillTimer(hwnd_,INSERT_TIMER);return;}
            auto sent=config_.clipboardPaste?PasteUsingClipboard(output,item.focus):TypeUnicode(output,item.focus,focusGeneration_,config_.unicodeDelayMs);
            if(!sent.allQueued()) {
                retained_.Add(item.text,sent.accepted>0);
                if(config_.clipboardPaste && sent.accepted>0)
                    Balloon(L"Paste incomplete",L"Release Ctrl and check the field before copying or pasting.");
                else Balloon(L"Insertion failed",L"Text retained. Check the field before copying or pasting.");
            }
            else if(!item.text.empty()){lastTypedGeneration_=item.focus.generation;lastTypedCharacter_=item.text.back();}
            heben::diagnostics::Record("editor_acceptance","unverified");
            pending_.pop_front();
        }
        KillTimer(hwnd_,INSERT_TIMER);
        MaybeEndFocusGuard();
    }

    void QueueTranscript(std::wstring output, const Focus &focus) {
        // Keep control characters from acting as Enter, Tab or other commands.
        heben::SanitizeDictationControls(output);
        // Explicit copy must be as safe as automatic insertion by default.
        lastTranscript_=output;
        pending_.push_back({std::move(output),focus,Clock::now()+std::chrono::seconds(3),false});
        FlushPending();
    }

    void Balloon(const wchar_t *title, const std::wstring &body) {
        if (!trayCreated_) return;
        NOTIFYICONDATAW ni{};
        ni.cbSize = sizeof(ni);
        ni.hWnd = hwnd_;
        ni.uID = TRAY_ID;
        ni.uFlags = NIF_INFO;
        wcsncpy_s(ni.szInfoTitle, title, _TRUNCATE);
        wcsncpy_s(ni.szInfo, body.c_str(), _TRUNCATE);
        ni.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &ni);
    }

    bool AddTrayIcon() {
        NOTIFYICONDATAW ni{};
        ni.cbSize=sizeof(ni);ni.hWnd=hwnd_;ni.uID=TRAY_ID;
        ni.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;ni.uCallbackMessage=MSG_TRAY;
        ni.hIcon=appIcon_;
        wcscpy_s(ni.szTip,iconInspection_?L"HebEN Dictation | Icon inspection | Ready":
            capturing_?L"HebEN Dictation | Listening":L"HebEN Dictation | Ready");
        SetLastError(ERROR_SUCCESS);
        trayCreated_=Shell_NotifyIconW(NIM_ADD,&ni)!=FALSE;
        heben::diagnostics::Record("tray_icon_add",trayCreated_?"ok":"failed",reinterpret_cast<INT_PTR>(ni.hIcon),GetLastError());
        return trayCreated_;
    }

    static void IconInspectionAudio(ma_device *,void *,const void *,ma_uint32) {
        // The inspection test discards audio immediately: no text, model or file.
    }
    bool StartIconInspectionCapture() {
        if(deviceInitialized_ || capturing_)return false;
        auto config=ma_device_config_init(ma_device_type_capture);
        config.capture.format=ma_format_f32;config.capture.channels=1;config.sampleRate=SAMPLE_RATE;
        config.dataCallback=IconInspectionAudio;
        auto result=ma_device_init(nullptr,&config,&captureDevice_);
        if(result!=MA_SUCCESS){heben::diagnostics::Record("icon_mic","init_failed",0,result);return false;}
        deviceInitialized_=true;result=ma_device_start(&captureDevice_);
        if(result!=MA_SUCCESS){ma_device_uninit(&captureDevice_);deviceInitialized_=false;heben::diagnostics::Record("icon_mic","start_failed",0,result);return false;}
        capturing_=true;UpdateTray();heben::diagnostics::Record("icon_mic","capture_started_no_audio_saved");return true;
    }
    void StopIconInspectionCapture() {
        if(!deviceInitialized_)return;
        ma_device_stop(&captureDevice_);ma_device_uninit(&captureDevice_);deviceInitialized_=false;
        capturing_=false;UpdateTray();heben::diagnostics::Record("icon_mic","capture_stopped");
    }

    void UpdateTray() {
        // A read-only window property lets local test tools observe the real
        // capture state without inspecting microphone data or adding polling.
        if (capturing_) SetPropW(hwnd_, L"HebEN.Capturing", reinterpret_cast<HANDLE>(1));
        else RemovePropW(hwnd_, L"HebEN.Capturing");
        if (!trayCreated_) return;
        NOTIFYICONDATAW ni{};
        ni.cbSize = sizeof(ni);
        ni.hWnd = hwnd_;
        ni.uID = TRAY_ID;
        ni.uFlags = NIF_ICON | NIF_TIP;
        ni.hIcon = appIcon_;
        wcscpy_s(ni.szTip, iconInspection_ ?
            (capturing_ ? L"HebEN Dictation | Icon inspection | Listening" : L"HebEN Dictation | Icon inspection | Ready") :
            (capturing_ ? L"HebEN Dictation | Listening" : L"HebEN Dictation | Ready"));
        SetLastError(ERROR_SUCCESS);
        bool updated=Shell_NotifyIconW(NIM_MODIFY, &ni)!=FALSE;
        heben::diagnostics::Record("tray_icon_modify",updated?"ok":"failed",reinterpret_cast<INT_PTR>(ni.hIcon),GetLastError());
    }

    void ShowMenu() {
        HMENU menu = CreatePopupMenu();
        if (!menu) return;
        AppendMenuW(menu, MF_STRING, MENU_TOGGLE, capturing_ ? L"Stop listening" : L"Start listening");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, MENU_HOTKEY_SETTINGS, L"Keyboard shortcut...");
        AppendMenuW(menu, MF_STRING, MENU_EDIT, L"Edit config.ini");
        AppendMenuW(menu, MF_STRING, MENU_RELOAD, L"Reload settings");
        AppendMenuW(menu, MF_STRING, MENU_COPY, L"Copy last transcript");
        AppendMenuW(menu, MF_STRING|(retained_.Size()?0:MF_GRAYED), MENU_COPY_RETAINED, L"Copy oldest unsent text");
        AppendMenuW(menu, MF_STRING|(config_.trustedTerminalInput?MF_CHECKED:0), MENU_TRUST_TERMINAL, L"Trusted terminal input (opt-in)");
        HMENU inputMethods=CreatePopupMenu();
        if(inputMethods) {
            AppendMenuW(inputMethods,MF_STRING|(!config_.clipboardPaste?MF_CHECKED:0),MENU_INPUT_UNICODE,L"Unicode keyboard");
            AppendMenuW(inputMethods,MF_STRING|(config_.clipboardPaste?MF_CHECKED:0),MENU_INPUT_CLIPBOARD,L"Clipboard paste (opt-in)");
            AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(inputMethods),L"Text input method");
        }
        AppendMenuW(menu,MF_STRING|(clipboard_.Active()?0:MF_GRAYED),MENU_RESTORE_CLIPBOARD,L"Retry clipboard restoration");
        HMENU languages=CreatePopupMenu();
        if(languages) {
            AppendMenuW(languages,MF_STRING|(config_.language=="auto"?MF_CHECKED:0),MENU_LANG_AUTO,L"Automatic");
            AppendMenuW(languages,MF_STRING|(config_.language=="he"?MF_CHECKED:0),MENU_LANG_HE,L"Hebrew");
            AppendMenuW(languages,MF_STRING|(config_.language=="en"?MF_CHECKED:0),MENU_LANG_EN,L"English");
            AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(languages),L"Language");
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, MENU_EXIT, L"Exit");
        if(foregroundHook_) {
            focusGeneration_.fetch_add(1,std::memory_order_relaxed);
            heben::diagnostics::Record("target","tray_menu_focus_change");
        }
        POINT pt{}; GetCursorPos(&pt);
        SetForegroundWindow(hwnd_);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x,pt.y,0,hwnd_,nullptr);
        PostMessageW(hwnd_, WM_NULL, 0,0);
        DestroyMenu(menu);
    }

    void SetLanguage(const char *language) {
        if(capturing_)StopCapture();
        const wchar_t *value=std::strcmp(language,"he")==0?L"he":std::strcmp(language,"en")==0?L"en":L"auto";
        if(!WritePrivateProfileStringW(L"General",L"language",value,ini_.c_str())) {
            heben::diagnostics::Record("language_setting","write_failed",0,GetLastError());
            Balloon(L"Settings unavailable",L"Could not save the language setting.");return;
        }
        config_.language=language;
        heben::diagnostics::Record("language_setting",language);
        Balloon(L"Language updated",value);
    }

    void SetTrustedTerminal(bool enabled) {
        if(capturing_)StopCapture();
        if(!WritePrivateProfileStringW(L"General",L"trusted_terminal_input",enabled?L"1":L"0",ini_.c_str())) {
            heben::diagnostics::Record("terminal_setting","write_failed",0,GetLastError());
            Balloon(L"Settings unavailable",L"Could not save the terminal setting.");return;
        }
        // Pending text must not be sent under a different trust decision.
        focusGeneration_.fetch_add(1,std::memory_order_relaxed);
        config_.trustedTerminalInput=enabled;
        heben::diagnostics::Record("terminal_setting",enabled?"enabled":"disabled");
        if(enabled)Balloon(L"Trusted terminal input enabled",L"Use only in a known non-sensitive prompt.");
        else Balloon(L"Trusted terminal input disabled",L"Unknown terminal inputs will be refused.");
    }

    void SetInputMethod(bool clipboard) {
        if(capturing_)StopCapture();
        if(!WritePrivateProfileStringW(L"General",L"input_method",clipboard?L"clipboard":L"unicode",ini_.c_str())) {
            heben::diagnostics::Record("input_method","write_failed",0,GetLastError());
            Balloon(L"Settings unavailable",L"Could not save the input method.");return;
        }
        focusGeneration_.fetch_add(1,std::memory_order_relaxed);
        config_.clipboardPaste=clipboard;
        heben::diagnostics::Record("input_method",clipboard?"clipboard_opt_in":"unicode");
        if(clipboard)Balloon(L"Clipboard paste enabled",L"It temporarily uses the clipboard and refuses unsupported formats.");
        else Balloon(L"Unicode keyboard enabled",L"Automatic insertion will not use the clipboard.");
    }

    void ShowHotkeySettings() {
        if(!hotkeyWindow_.Show(config_.hotkey,[this](const std::wstring &value,std::wstring &error){return SetHotkey(value,error);},!iconInspection_))
            Balloon(L"Shortcut settings unavailable",L"Could not open the shortcut window.");
    }

    bool SetHotkey(const std::wstring &value,std::wstring &error) {
        heben::HotkeySpec next;
        if(!heben::ParseHotkeySyntax(value,next,error))return false;
        if(hotkeyRegistered_ && next.key==config_.key && next.modifiers==config_.modifiers)return true;
        if(!heben::ValidateUserHotkey(next,error))return false;
        if(capturing_)StopCapture();
        heben::HotkeyRegistration current{activeHotkeyId_,hotkeyRegistered_,{config_.modifiers,config_.key,config_.hotkey}};
        auto write=[this](const std::wstring &text){return WritePrivateProfileStringW(L"General",L"hotkey",text.c_str(),ini_.c_str())!=FALSE;};
        auto result=heben::ChangeRegisteredHotkey(hwnd_,current,next,write);
        if(result==heben::HotkeyChange::Conflict){error=L"That global shortcut is already in use or reserved by Windows.";return false;}
        if(result==heben::HotkeyChange::PersistFailed){error=L"Could not save settings. The previous shortcut is still active.";return false;}
        if(result==heben::HotkeyChange::ReleaseFailed || result==heben::HotkeyChange::RollbackFailed){
            error=result==heben::HotkeyChange::RollbackFailed?L"Could not complete or restore settings. Check config.ini.":L"Could not release the old shortcut. It remains active.";return false;
        }
        activeHotkeyId_=current.id;hotkeyRegistered_=current.registered;
        config_.hotkey=next.canonical;config_.modifiers=next.modifiers;config_.key=next.key;
        focusGeneration_.fetch_add(1,std::memory_order_relaxed);
        heben::diagnostics::Record("hotkey","changed",next.key,next.modifiers);
        Balloon(L"Shortcut updated",next.canonical);return true;
    }

    void Reload() {
        if (capturing_) {if(iconInspection_)StopIconInspectionCapture();else StopCapture();}
        Settings next = LoadSettings(ini_);
        // Reserve a different shortcut before releasing the current one.
        const bool same=hotkeyRegistered_ && next.modifiers==config_.modifiers && next.key==config_.key;
        int candidate=activeHotkeyId_==1201?1203:1201;
        bool acquired=same || RegisterHotKey(hwnd_,candidate,next.modifiers,next.key)!=FALSE;
        if(acquired && !same && hotkeyRegistered_ && !UnregisterHotKey(hwnd_,activeHotkeyId_)) {
            UnregisterHotKey(hwnd_,candidate);acquired=false;
        }
        if (acquired) {
            if(!same)activeHotkeyId_=candidate;
            config_ = std::move(next);
            idleSeconds_.store(config_.idleSeconds);
            {
                std::lock_guard<std::mutex> lock(jobsMutex_);
                settingsChanged_ = true;
            }
            jobsCv_.notify_one();
            hotkeyRegistered_ = true;
            Balloon(L"Settings reloaded", L"Hotkey changed successfully.");
        } else {
            Balloon(L"Hotkey unavailable", L"Choose a different combination in config.ini.");
        }
    }

    static void CaptureCallback(ma_device *device, void*, const void *input, ma_uint32 n) {
        if (!input) return;
        auto *self = static_cast<App*>(device->pUserData);
        if (!self || !self->detector_) return;
        if(heben::diagnostics::Enabled()) {
            self->capturedFrames_.fetch_add(n,std::memory_order_relaxed);
            const float *samples=static_cast<const float*>(input);int peak=0;unsigned clipped=0;
            for(ma_uint32 i=0;i<n;++i) if(std::isfinite(samples[i])) {
                float magnitude=std::abs(samples[i]);if(magnitude>=.999f)++clipped;
                peak=std::max(peak,static_cast<int>(std::min(1.f,magnitude)*1000000));
            }
            self->clippedFrames_.fetch_add(clipped,std::memory_order_relaxed);
            int old=self->capturePeak_.load(std::memory_order_relaxed);
            while(peak>old && !self->capturePeak_.compare_exchange_weak(old,peak,std::memory_order_relaxed)) {}
        }
        if (self->vadContext_) self->EnqueueAudio(static_cast<const float*>(input), n);
        else {
            self->detector_->Feed(static_cast<const float*>(input), n);
            if(self->detector_->QuietSamples()>=5u*SAMPLE_RATE)self->RequestAutoStop();
        }
    }

    void RequestAutoStop() {
        bool expected=false;
        if(!autoStopPosted_.compare_exchange_strong(expected,true,std::memory_order_relaxed))return;
        if(!PostMessageW(hwnd_,MSG_AUTO_STOP,static_cast<WPARAM>(captureTarget_.generation),0))
            autoStopPosted_.store(false,std::memory_order_relaxed);
    }

    void RequestWarmOnVoice() {
        bool expected=false;
        if(!voiceWarmRequested_.compare_exchange_strong(expected,true,std::memory_order_relaxed))return;
        {
            std::lock_guard<std::mutex> lock(jobsMutex_);
            if(shutting_)return;
            warmRequested_=true;warmModel_=config_.model;warmGpu_=config_.gpu;
        }
        heben::diagnostics::Record("asr_model","warm_requested_on_voice");
        jobsCv_.notify_one();
    }

    void EnqueueAudio(const float *input, size_t n) {
        {
            std::lock_guard<std::mutex> lock(audioMutex_);
            if (!audioRing_ || !audioRing_->Push(input,n)) audioOverflow_ = true;
        }
        audioCv_.notify_one();
    }

    static whisper_vad_context *OpenVadModel(const fs::path &path) {
        FILE *fp=nullptr;
        if (_wfopen_s(&fp,path.c_str(),L"rb") != 0 || !fp) return nullptr;
        whisper_model_loader loader{}; loader.context=fp;
        loader.read=[](void *ctx,void *out,size_t n)->size_t {return fread(out,1,n,static_cast<FILE*>(ctx));};
        loader.eof=[](void *ctx)->bool {return feof(static_cast<FILE*>(ctx))!=0;};
        loader.close=[](void*){};
        auto params=whisper_vad_default_context_params(); params.use_gpu=false; params.n_threads=1;
        auto *ctx=whisper_vad_init_with_params(&loader,params); fclose(fp); return ctx;
    }

    void SileroLoop() {
        heben::SileroStream stream(vadContext_,*detector_);
#ifdef HEBEN_HEADLESS_TEST
        // Test-only controlled worker stall; never compiled into the app.
        wchar_t delay[16]{};
        if(GetEnvironmentVariableW(L"HEBEN_TEST_VAD_START_DELAY_MS",delay,16)) {
            int ms=std::clamp(_wtoi(delay),0,9000);if(ms)Sleep(static_cast<DWORD>(ms));
        }
#endif
        std::array<float,512> block{};
        bool ok=true,overflow=false,loggedPcm=false,loggedSpeech=false,lastVoiced=false;
        for (;;) {
            size_t n=0;
            {
                std::unique_lock<std::mutex> lock(audioMutex_);
                audioCv_.wait(lock,[this]{return audioStopped_ || audioOverflow_ || audioRing_->Size()>0;});
                if (audioOverflow_) { ok=false;overflow=true;break; }
                if (!audioRing_->Size() && audioStopped_) break;
                n=audioRing_->Pop(block.data(),block.size());
            }
            if(n && !loggedPcm){heben::diagnostics::Record("pcm","received",n);loggedPcm=true;}
            size_t beforeFrames=stream.Frames();
            if (n && !stream.Feed(block.data(),n)) { ok=false; break; }
            if(stream.Frames()!=beforeFrames && stream.LastVoiced()!=lastVoiced) {
                lastVoiced=stream.LastVoiced();
                heben::diagnostics::Record("vad",lastVoiced?"voice_started":"voice_ended",stream.Frames());
            }
            if(!loggedSpeech && stream.VoicedFrames()){heben::diagnostics::Record("vad","speech_detected",stream.VoicedFrames());loggedSpeech=true;}
            if(stream.QuietSamples()>=5u*SAMPLE_RATE)RequestAutoStop();
        }
        if (ok) ok=stream.Finish();
        heben::diagnostics::Record("vad",ok?"finished":overflow?"audio_overflow":"processing_failed",stream.Frames(),stream.VoicedFrames());
        if (!ok) {
            PostUi(UiEvent::Error,L"Voice detection failed or the audio buffer overflowed; the current phrase was not sent.",{});
            if(!PostMessageW(hwnd_,MSG_VAD_FAILURE,static_cast<WPARAM>(captureTarget_.generation),0))
                heben::diagnostics::Record("vad","failure_stop_post_failed",0,GetLastError());
        }
    }

    void StopSilero() {
        if (!vadContext_) return;
        {
            std::lock_guard<std::mutex> lock(audioMutex_);
            audioStopped_=true;
        }
        audioCv_.notify_one();
        if (vadThread_.joinable()) vadThread_.join();
        whisper_vad_free(vadContext_); vadContext_=nullptr;
        {std::lock_guard<std::mutex> lock(audioMutex_);audioRing_.reset();}
    }

    void Toggle() {
        if (capturing_) StopCapture();
        else {
            indicator_.ShowError(automation,L"Starting microphone...");
            StartCapture();
        }
    }

    void StartCapture() {
        heben::diagnostics::Record("capture","start_requested");
#ifndef HEBEN_CUDA_BUILD
        if (config_.gpu) {
            Balloon(L"CUDA build required", L"This executable is CPU-only. Set backend=cpu or use a CUDA-enabled build.");
            indicator_.ShowError(automation,L"CUDA build required");
            return;
        }
#endif
        if (!fs::exists(config_.model)) {
            heben::diagnostics::Record("asr_model","missing");
            Balloon(L"Model missing", L"Run build.ps1 or copy a ggml multilingual model into the models folder.");
            indicator_.ShowError(automation,L"Speech model missing");
            return;
        }
        captureTarget_ = CurrentFocus(config_.trustedTerminalInput);
        if (!captureTarget_.window) {
            heben::diagnostics::Record("capture","unsafe_target");
            Balloon(L"No safe text target", L"Focus an editable, non-password field and press the shortcut again.");
            indicator_.ShowError(automation,L"Select an editable field");
            return;
        }
        if(!BeginFocusGuard()) {
            heben::diagnostics::Record("target","focus_guard_failed",0,GetLastError());
            Balloon(L"Dictation unavailable",L"Could not monitor the target focus safely.");
            indicator_.ShowError(automation,L"Dictation unavailable");
            return;
        }
        autoStopPosted_.store(false,std::memory_order_relaxed);
        waitingForText_=false;
        detector_ = std::make_unique<heben::Detector>(config_.threshold,config_.silenceMs,
            config_.maxPhraseSeconds, [this](std::vector<float>&& pcm) { QueuePhrase(std::move(pcm)); },
            config_.maxZeroCrossing,config_.prerollMs);
        if(config_.streamingPrototype)detector_->SetPreview(config_.streamingIntervalMs,
            [this](std::vector<float>&& pcm,std::uint64_t id){QueuePhrase(std::move(pcm),true,id);});
        voiceWarmRequested_.store(false,std::memory_order_relaxed);
        if(config_.warmOnVoice)detector_->SetSpeechStart([this]{RequestWarmOnVoice();});
        if (config_.silero) {
            auto vadStart=Clock::now();
            vadContext_=OpenVadModel(config_.vadModel);
            heben::diagnostics::Record("vad_model",vadContext_?"loaded":"failed",std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-vadStart).count());
            if (!vadContext_) {
                detector_.reset();
                MaybeEndFocusGuard();
                Balloon(L"Voice model unavailable", L"Install the Silero VAD model or set vad_backend=energy in config.ini.");
                indicator_.ShowError(automation,L"Voice model unavailable");
                return;
            }
        }
        capturedFrames_.store(0);capturePeak_.store(0);clippedFrames_.store(0);
#ifndef HEBEN_DESKTOP_AUDIO_TEST
        ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
        deviceConfig.capture.format = ma_format_f32;
        deviceConfig.capture.channels = 1;
        deviceConfig.sampleRate = SAMPLE_RATE;
        deviceConfig.dataCallback = CaptureCallback;
        deviceConfig.pUserData = this;
        // miniaudio negotiates device format and resamples to 16kHz mono for us.
        ma_result initResult=ma_device_init(nullptr, &deviceConfig, &captureDevice_);
        if (initResult != MA_SUCCESS) {
            heben::diagnostics::Record("microphone","init_failed",0,initResult);
            if (vadContext_) { whisper_vad_free(vadContext_); vadContext_=nullptr; }
            detector_.reset();
            MaybeEndFocusGuard();
            Balloon(L"Microphone unavailable", L"Check Windows microphone privacy settings and the default recording device.");
            indicator_.ShowError(automation,L"Microphone unavailable");
            return;
        }
        deviceInitialized_ = true;
        heben::diagnostics::Record("microphone","opened_output_rate_channels",captureDevice_.sampleRate,captureDevice_.capture.channels);
        heben::diagnostics::Record("microphone","device_rate_channels",captureDevice_.capture.internalSampleRate,captureDevice_.capture.internalChannels);
        heben::diagnostics::Record("microphone","output_format_f32",captureDevice_.capture.format);
        if(captureDevice_.sampleRate!=SAMPLE_RATE || captureDevice_.capture.channels!=1 || captureDevice_.capture.format!=ma_format_f32) {
            heben::diagnostics::Record("microphone","unexpected_callback_format",captureDevice_.sampleRate,captureDevice_.capture.channels);
            ma_device_uninit(&captureDevice_);deviceInitialized_=false;
            if(vadContext_){whisper_vad_free(vadContext_);vadContext_=nullptr;}
            detector_.reset();MaybeEndFocusGuard();
            Balloon(L"Microphone format unavailable",L"Could not obtain 16 kHz mono float audio.");
            indicator_.ShowError(automation,L"Microphone unavailable");return;
        }
#endif
        if (vadContext_) {
            try {
                audioRing_=std::make_unique<heben::AudioRing<10*SAMPLE_RATE>>();
                { std::lock_guard<std::mutex> lock(audioMutex_); audioStopped_=audioOverflow_=false; }
                vadThread_=std::thread([this]{SileroLoop();});
            }
            catch (...) {
                audioRing_.reset();
                if(deviceInitialized_){ma_device_uninit(&captureDevice_);deviceInitialized_=false;}
                whisper_vad_free(vadContext_); vadContext_=nullptr; detector_.reset();
                MaybeEndFocusGuard();
                Balloon(L"Voice detection unavailable", L"Could not start the voice detection worker.");
                indicator_.ShowError(automation,L"Voice detection unavailable");
                return;
            }
        }
#ifndef HEBEN_DESKTOP_AUDIO_TEST
        ma_result startResult=ma_device_start(&captureDevice_);
        if (startResult != MA_SUCCESS) {
            heben::diagnostics::Record("microphone","start_failed",0,startResult);
            ma_device_uninit(&captureDevice_);
            deviceInitialized_ = false;
            StopSilero();
            detector_.reset();
            MaybeEndFocusGuard();
            Balloon(L"Microphone error", L"Unable to start recording.");
            indicator_.ShowError(automation,L"Microphone unavailable");
            return;
        }
        heben::diagnostics::Record("microphone","started");
#else
        heben::diagnostics::Record("test_fixture","synthetic_capture_no_microphone");
#endif
        {
            std::lock_guard<std::mutex> lock(jobsMutex_);
            capturing_ = true;
            if(!config_.warmOnVoice) {
                warmRequested_ = true;
                warmModel_ = config_.model;
                warmGpu_ = config_.gpu;
            }
        }
        jobsCv_.notify_one();
        UpdateTray();
        indicator_.ShowListening(automation);
    }

    void StopCapture() {
        if (!capturing_) return;
        // ma_device_stop waits for the callback to finish, so detector_ is safe.
        if (deviceInitialized_) {
            ma_device_stop(&captureDevice_);
            ma_device_uninit(&captureDevice_);
            deviceInitialized_ = false;
        }
        if (vadContext_) StopSilero();
        else if (detector_) detector_->Finish();
        heben::diagnostics::Record("pcm","capture_stopped_frames_peak_millionths",capturedFrames_.load(),capturePeak_.load());
        heben::diagnostics::Record("pcm","clipped_frames",clippedFrames_.load());
        detector_.reset();
        {
            std::lock_guard<std::mutex> lock(jobsMutex_);
            capturing_ = false;
        }
#ifndef HEBEN_HEADLESS_TEST
        UpdateTray();
        if(outstanding_.load(std::memory_order_relaxed)>0 || !pending_.empty()) {
            waitingForText_=true;indicator_.ShowProcessing(automation);
            heben::diagnostics::Record("indicator","processing");
        } else indicator_.Hide();
#endif
        jobsCv_.notify_one();
        MaybeEndFocusGuard();
    }

    void QueuePhrase(std::vector<float>&& samples,bool partial=false,std::uint64_t segmentId=0) {
        if (samples.size() < SAMPLE_RATE/4) {heben::diagnostics::Record("segment","too_short",samples.size());return;}
        if(!partial && detector_)segmentId=detector_->SegmentId();
        heben::diagnostics::Record("segment",partial?"preview_emitted":"emitted",samples.size());
        Job job;
        job.pcm = std::move(samples);
        job.focus = captureTarget_;
        job.model = config_.model;
        job.hebrewModel = config_.hebrewModel;
        job.hybrid = config_.hybrid;
        job.language = config_.language;
        job.autoHebrewFallback = config_.autoHebrewFallback;
        job.threads = config_.threads;
        job.gpu = config_.gpu;
        job.streaming=config_.streamingPrototype;
        job.partial=partial;
        job.segmentId=segmentId;
        job.streamingAgreement=config_.streamingAgreement;
        job.streamingHoldWords=config_.streamingHoldWords;
        bool dropped = false;
        bool replaced = false;
        {
            std::lock_guard<std::mutex> lock(jobsMutex_);
            if(partial) {
                for(auto it=jobs_.rbegin();it!=jobs_.rend();++it) {
                    if(it->streaming && it->partial && it->segmentId==job.segmentId &&
                       it->focus.generation==job.focus.generation) {*it=std::move(job);replaced=true;break;}
                }
            }
            if(!replaced) {
                if (jobs_.size() >= 8) dropped = true;
                else { jobs_.push_back(std::move(job));outstanding_.fetch_add(1,std::memory_order_relaxed); }
            }
        }
        if (dropped && !partial) {heben::diagnostics::Record("segment","queue_full");PostUi(UiEvent::Backlog,L"",{});}
        else if(dropped)heben::diagnostics::Record("streaming","preview_dropped");
        else {heben::diagnostics::Record("segment",replaced?"preview_replaced":partial?"preview_queued":"queued");jobsCv_.notify_one();}
    }

    void PostUi(UiEvent::Kind kind, std::wstring text, Focus focus) {
        auto *event = new UiEvent{kind,std::move(text),focus};
        if (!PostMessageW(hwnd_, MSG_UI_EVENT,0,reinterpret_cast<LPARAM>(event))) delete event;
    }

    // A Unicode-aware model loader: works even when Windows username/path is Hebrew.
    static whisper_context *OpenModel(const fs::path &path, bool gpu) {
#ifdef HEBEN_CUDA_BUILD
        // Do not silently pretend a requested GPU backend is available.
        if (gpu) {
            HMODULE library=LoadLibraryA(HEBEN_CUBLAS_DLL);
            if (!library) return nullptr;
            FreeLibrary(library);
            if (!ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU)) return nullptr;
        }
#else
        if (gpu) return nullptr;
#endif
        FILE *fp = nullptr;
        if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp) return nullptr;
        whisper_model_loader loader{};
        loader.context = fp;
        loader.read = [](void *ctx, void *out, size_t n) -> size_t {
            return fread(out,1,n,static_cast<FILE*>(ctx));
        };
        loader.eof = [](void *ctx)->bool { return feof(static_cast<FILE*>(ctx)) != 0; };
        loader.close = [](void*){}; // Keep alive until whisper_init_with_params returns.
        auto cfg = whisper_context_default_params();
        cfg.use_gpu = gpu;
        whisper_context *model = whisper_init_with_params(&loader,cfg);
        fclose(fp);
        return model;
    }

    static bool ReleaseModels(whisper_context *&model, whisper_context *&hebrewModel, bool gpu) {
        if (!model && !hebrewModel) return true;
        if (hebrewModel) whisper_free(hebrewModel);
        hebrewModel = nullptr;
        if (model) whisper_free(model);
        model = nullptr;
#ifdef HEBEN_CUDA_BUILD
        // The CUDA runtime keeps a device context after whisper_free. Reset it
        // only on the dedicated worker thread and only after all model work is
        // complete, so idle GPU allocations do not persist unnecessarily.
        if (gpu) {
            if (cudaDeviceReset() != cudaSuccess) return false;
            // A loaded cuBLAS and its dependent DLL are no longer needed
            // once the worker has destroyed the model and reset its device.
            if (GetModuleHandleA(HEBEN_CUBLAS_DLL) && !__FUnloadDelayLoadedDLL2(HEBEN_CUBLAS_DLL)) return false;
        }
#else
        (void)gpu;
#endif
        return true;
    }

    void FinishJob() {
        if(!PostMessageW(hwnd_,MSG_WORK_DONE,0,0))heben::diagnostics::Record("worker","completion_post_failed",0,GetLastError());
    }

    void WorkerLoop() {
        whisper_context *model = nullptr;
        whisper_context *hebrewModel = nullptr;
        fs::path loadedPath;
        fs::path loadedHebrewPath;
        bool loadedGpu = false;
        Clock::time_point lastUsed = Clock::now();
        heben::StablePrefix streamAgreement(3,1);
        unsigned long long streamGeneration=0;
        std::uint64_t streamSegment=0;
        for (;;) {
            Job job;
            fs::path pathToWarm;
            bool gpuToWarm = false;
            bool haveJob = false;
            {
                std::unique_lock<std::mutex> lock(jobsMutex_);
                // Sleep indefinitely without a model. A loaded, inactive model
                // uses one deadline rather than waking the worker every second.
                if (model && !capturing_) {
                    jobsCv_.wait_until(lock, lastUsed + std::chrono::seconds(idleSeconds_.load()), [this]{
                        return shutting_ || !jobs_.empty() || warmRequested_ || settingsChanged_ || capturing_;
                    });
                } else {
                    jobsCv_.wait(lock, [this, &model]{
                        return shutting_ || !jobs_.empty() || warmRequested_ || settingsChanged_ || (model && !capturing_);
                    });
                }
                if (shutting_) break;
                settingsChanged_ = false;
                if (!jobs_.empty()) {
                    job = std::move(jobs_.front());
                    jobs_.pop_front();
                    haveJob = true;
                } else if (warmRequested_) { pathToWarm = warmModel_; gpuToWarm = warmGpu_; }
                warmRequested_ = false;
            }
            fs::path wanted = haveJob ? job.model : pathToWarm;
            bool wantedGpu = haveJob ? job.gpu : gpuToWarm;
            if (!wanted.empty()) {
                if (model && (wanted != loadedPath || wantedGpu != loadedGpu)) {
                    if (!ReleaseModels(model, hebrewModel, loadedGpu)) {
                        PostUi(UiEvent::Error,L"Could not release CUDA resources.",{});
                        if(haveJob)FinishJob();
                        continue;
                    }
                    loadedHebrewPath.clear();
                }
                if (!model) {
                    auto started=Clock::now();
                    model = OpenModel(wanted, wantedGpu);
                    heben::diagnostics::Record("asr_model",model?"loaded":"failed",std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-started).count(),wantedGpu?1:0);
                    if (model) { loadedPath = wanted; loadedGpu = wantedGpu; }
                    else PostUi(UiEvent::Error,L"Could not load speech model or requested backend.",{});
                }
                lastUsed = Clock::now();
            }
            if (haveJob && model) {
                whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
                p.n_threads = job.threads;
                p.language = job.language.c_str();
                p.translate = false;
                p.detect_language = false; // "auto" in language triggers detection during decoding.
                p.no_context = true;
                p.no_timestamps = true;
                p.single_segment = true;
                p.print_realtime = false;
                p.print_progress = false;
                p.print_timestamps = false;
                p.print_special = false;
                p.greedy.best_of = 1;
                auto inferenceStart=Clock::now();
                heben::diagnostics::Record("asr","started",job.pcm.size());
                int inferenceResult=whisper_full(model,p,job.pcm.data(),static_cast<int>(job.pcm.size()));
                heben::diagnostics::Record("asr",inferenceResult==0?"finished":"failed",std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-inferenceStart).count(),inferenceResult);
                if (inferenceResult == 0) {
                    int languageId=whisper_full_lang_id(model);
                    const char *detected=languageId<0?nullptr:whisper_lang_str(languageId);
                    heben::diagnostics::Record("asr_language",detected?detected:"unknown",languageId);
                    std::string utf8;
                    for (int i=0;i<whisper_full_n_segments(model);++i)
                        utf8 += whisper_full_get_segment_text(model,i);
                    if(job.hybrid && (job.language=="he" ||
                       (job.language=="auto" && detected && std::strcmp(detected,"en")!=0))) {
                        if (hebrewModel && loadedHebrewPath != job.hebrewModel) {
                            whisper_free(hebrewModel);hebrewModel=nullptr;loadedHebrewPath.clear();
                        }
                        if (!hebrewModel) {
                            auto start=Clock::now();
                            hebrewModel=OpenModel(job.hebrewModel,job.gpu);
                            heben::diagnostics::Record("asr_hybrid_model",hebrewModel?"loaded":"failed",
                                std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count());
                            if(hebrewModel)loadedHebrewPath=job.hebrewModel;
                            else {
                                utf8.clear();
                                PostUi(UiEvent::Error,L"Could not load the Hebrew model; this segment was not inserted.",{});
                            }
                        }
                        if (hebrewModel) {
                            auto start=Clock::now();p.language="he";
                            int retry=whisper_full(hebrewModel,p,job.pcm.data(),static_cast<int>(job.pcm.size()));
                            heben::diagnostics::Record("asr_hybrid",retry==0?"finished":"failed",
                                std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count(),retry);
                            if(retry==0) {
                                std::string replacement;
                                for(int i=0;i<whisper_full_n_segments(hebrewModel);++i)replacement+=whisper_full_get_segment_text(hebrewModel,i);
                                if(!Trim(FromUtf8(replacement)).empty())utf8=std::move(replacement);
                                else {
                                    utf8.clear();
                                    PostUi(UiEvent::Error,L"Hebrew recognition returned no text; this segment was not inserted.",{});
                                }
                            } else {
                                utf8.clear();
                                PostUi(UiEvent::Error,L"Hebrew recognition failed; this segment was not inserted.",{});
                            }
                        }
                    } else if(job.autoHebrewFallback && job.language=="auto" && detected &&
                       std::strcmp(detected,"he")!=0 && std::strcmp(detected,"en")!=0) {
                        auto retryStart=Clock::now();
                        p.language="he";
                        int retry=whisper_full(model,p,job.pcm.data(),static_cast<int>(job.pcm.size()));
                        heben::diagnostics::Record("asr_hebrew_fallback",retry==0?"finished":"failed",
                            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-retryStart).count(),retry);
                        if(retry==0) {
                            std::string replacement;
                            for(int i=0;i<whisper_full_n_segments(model);++i)replacement+=whisper_full_get_segment_text(model,i);
                            if(!Trim(FromUtf8(replacement)).empty())utf8=std::move(replacement);
                        }
                    }
                    std::wstring output = Trim(FromUtf8(utf8));
                    heben::diagnostics::Record("asr_text",output.empty()?"empty_or_invalid_utf8":"produced",output.size());
                    if(job.streaming) {
                        if(streamGeneration!=job.focus.generation || streamSegment!=job.segmentId) {
                            streamAgreement=heben::StablePrefix(job.streamingAgreement,job.streamingHoldWords);
                            streamGeneration=job.focus.generation;streamSegment=job.segmentId;
                        }
                        auto decision=streamAgreement.Observe(output,!job.partial);
                        heben::diagnostics::Record("streaming",job.partial?"hypothesis":"final_hypothesis",output.size());
                        if(decision.conflict)PostUi(UiEvent::StreamingConflict,std::move(output),job.focus);
                        if(!decision.append.empty()) {
                            heben::diagnostics::Record("streaming","append_committed",decision.append.size(),job.partial?1:0);
                            heben::diagnostics::Record("streaming","commit_audio_samples",job.pcm.size(),job.partial?1:0);
                            PostUi(UiEvent::Transcript,std::move(decision.append),job.focus);
                        }
                    } else if (!output.empty()) PostUi(UiEvent::Transcript,std::move(output),job.focus);
                } else PostUi(UiEvent::Error,L"Transcription failed.",{});
                lastUsed = Clock::now();
            }
            if(haveJob)FinishJob();
            if (model && !capturing_ &&
                Clock::now() - lastUsed >= std::chrono::seconds(idleSeconds_.load())) {
                bool released=ReleaseModels(model, hebrewModel, loadedGpu);
                heben::diagnostics::Record("asr_models",released?"unloaded_after_inactivity":"unload_failed");
                if (!released)
                    PostUi(UiEvent::Error,L"Could not release CUDA resources.",{});
                loadedPath.clear();
                loadedHebrewPath.clear();
            }
        }
        if (model || hebrewModel) ReleaseModels(model, hebrewModel, loadedGpu);
    }

    void Shutdown() {
        heben::diagnostics::Record("application","shutdown_requested");
        if (capturing_) StopCapture();
        hotkeyWindow_.Close();
        if (hotkeyRegistered_) UnregisterHotKey(hwnd_,activeHotkeyId_);
        {
            std::lock_guard<std::mutex> lock(jobsMutex_);
            shutting_ = true;
            jobs_.clear();
        }
        jobsCv_.notify_all();
        if (worker_.joinable()) worker_.join();
        KillTimer(hwnd_,INSERT_TIMER);
        KillTimer(hwnd_,CLIPBOARD_TIMER);
        if(clipboard_.Active()) {
            DWORD error=0;auto restored=clipboard_.Restore(hwnd_,error);
            heben::diagnostics::Record("clipboard_restore",restored==heben::ClipboardRestore::Restored?"restored_on_shutdown":
                restored==heben::ClipboardRestore::ChangedByOther?"changed_by_other_preserved":"shutdown_restore_incomplete",0,error);
            if(restored==heben::ClipboardRestore::Busy || restored==heben::ClipboardRestore::Failed)
                Balloon(L"Clipboard restore incomplete",L"Check the clipboard before relying on its contents.");
        }
        pending_.clear();EndFocusGuard();
        if (trayCreated_) {
            NOTIFYICONDATAW ni{};
            ni.cbSize=sizeof(ni); ni.hWnd=hwnd_; ni.uID=TRAY_ID;
            Shell_NotifyIconW(NIM_DELETE,&ni);
        }
    }

    HWND hwnd_ = nullptr;
    HICON appIcon_ = nullptr; // Shared resource handle, owned by Windows.
    bool iconInspection_=false;
    UINT taskbarCreatedMessage_=0;
    heben::HotkeySettingsWindow hotkeyWindow_;
    int activeHotkeyId_=HOTKEY_ID;
    inline static App *guardOwner_=nullptr;
    HWINEVENTHOOK foregroundHook_=nullptr,focusHook_=nullptr;
    std::atomic<unsigned long long> focusGeneration_{0};
    heben::InputMutationGuard mutationGuard_;
    std::atomic<unsigned> outstanding_{0};
    std::deque<PendingInsertion> pending_;
    bool waitingForText_=false;
    unsigned long long lastTypedGeneration_=0;
    wchar_t lastTypedCharacter_=0;
    ListeningIndicator indicator_;
    fs::path ini_;
    Settings config_;
    std::atomic<int> idleSeconds_{45};
    Focus captureTarget_;
    bool trayCreated_ = false;
    bool hotkeyRegistered_ = false;
    std::atomic<bool> capturing_{false};
    bool deviceInitialized_ = false;
    ma_device captureDevice_{};
    std::unique_ptr<heben::Detector> detector_;
    whisper_vad_context *vadContext_ = nullptr;
    std::thread vadThread_;
    std::mutex audioMutex_;
    std::condition_variable audioCv_;
    // Allocate only while recording; enough for a cold GPU DLL initialization stall.
    std::unique_ptr<heben::AudioRing<10*SAMPLE_RATE>> audioRing_;
    std::atomic<unsigned long long> capturedFrames_{0};
    std::atomic<int> capturePeak_{0};
    std::atomic<unsigned long long> clippedFrames_{0};
    std::atomic<bool> autoStopPosted_{false};
    std::atomic<bool> voiceWarmRequested_{false};
    bool audioStopped_=false,audioOverflow_=false;
    std::wstring lastTranscript_;
    heben::RetainedText retained_;
    heben::ClipboardTextTransaction clipboard_;
    Clock::time_point clipboardDeadline_{};
    unsigned long long clipboardTargetGeneration_=0,clipboardBlockedGeneration_=0;
#ifdef HEBEN_TEST_HOOKS
    Focus testFocus_;
#endif
#ifdef HEBEN_HEADLESS_TEST
    unsigned replayTranscriptCount_=0;
#endif
    std::thread worker_;
    std::mutex jobsMutex_;
    std::condition_variable jobsCv_;
    std::deque<Job> jobs_;
    bool warmRequested_ = false;
    bool settingsChanged_ = false;
    fs::path warmModel_;
    bool warmGpu_ = false;
    bool shutting_ = false;
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    // Explicit icon-inspection instance: no hotkey or ASR worker. Its only
    // microphone path is an explicitly requested local diagnostic that drops
    // every audio frame and never writes or transcribes it.
    const bool iconInspection=commandLine && wcscmp(commandLine,L"--icon-inspection")==0;
    // Single instance, so duplicate hotkey listeners don't accumulate.
#if defined(HEBEN_FIXED_INPUT_PROBE)
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\HebEN_FixedInputProbe_SingleInstance");
#elif defined(HEBEN_HEADLESS_TEST)
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\HebEN_ReplayFixture_SingleInstance");
#elif defined(HEBEN_DESKTOP_AUDIO_TEST)
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\HebEN_DesktopAudioTest_SingleInstance");
#elif defined(HEBEN_DESKTOP_MIC_TEST)
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\HebEN_DesktopMicTest_SingleInstance");
#elif defined(HEBEN_HOTKEY_UI_TEST)
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\HebEN_HotkeyUiTest_SingleInstance");
#else
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, iconInspection?L"Local\\HebEN_IconInspection_SingleInstance":L"Local\\HebEN_Dictation_SingleInstance");
#endif
    if (!instanceMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (instanceMutex) CloseHandle(instanceMutex);
#ifndef HEBEN_FIXED_INPUT_PROBE
        MessageBoxW(nullptr,L"HebEN Dictation is already running.",L"HebEN Dictation",MB_OK|MB_ICONINFORMATION);
#endif
        return 0;
    }
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    App app(iconInspection);
    if (!app.Initialize(instance)) {
        if (automation) automation->Release();
        if (SUCCEEDED(com)) CoUninitialize();
        CloseHandle(instanceMutex);
#ifndef HEBEN_FIXED_INPUT_PROBE
        MessageBoxW(nullptr,L"Failed to initialize HebEN Dictation.",L"HebEN Dictation",MB_OK|MB_ICONERROR);
#endif
        return 1;
    }
    app.Run();
    if (automation) automation->Release();
    if (SUCCEEDED(com)) CoUninitialize();
    CloseHandle(instanceMutex);
    return 0;
}

"""Exercise app hotkey settings on a never-switched Windows desktop.

The app fixture has no microphone or ASR worker. This tests manual entry,
validation, a real RegisterHotKey conflict, persistence, and registration
transfer. It does not synthesize keyboard or mouse events.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import threading
import time
import uuid

ROOT=Path(__file__).resolve().parent.parent
class STARTUPINFO(ctypes.Structure):
    _fields_=[('cb',wintypes.DWORD),('lpReserved',wintypes.LPWSTR),('lpDesktop',wintypes.LPWSTR),('lpTitle',wintypes.LPWSTR),
        ('dwX',wintypes.DWORD),('dwY',wintypes.DWORD),('dwXSize',wintypes.DWORD),('dwYSize',wintypes.DWORD),
        ('dwXCountChars',wintypes.DWORD),('dwYCountChars',wintypes.DWORD),('dwFillAttribute',wintypes.DWORD),('dwFlags',wintypes.DWORD),
        ('wShowWindow',wintypes.WORD),('cbReserved2',wintypes.WORD),('lpReserved2',ctypes.c_void_p),
        ('hStdInput',wintypes.HANDLE),('hStdOutput',wintypes.HANDLE),('hStdError',wintypes.HANDLE)]
class PROCESSINFO(ctypes.Structure):
    _fields_=[('hProcess',wintypes.HANDLE),('hThread',wintypes.HANDLE),('dwProcessId',wintypes.DWORD),('dwThreadId',wintypes.DWORD)]


def main():
    p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--capture-tool',type=Path);a=p.parse_args()
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);exe=out/'HebENHotkeyUiFixture.exe';shutil.copyfile(a.exe,exe)
    ini=out/'config.ini';ini.write_text('[General]\nhotkey=CTRL+ALT+SHIFT+F9\nkeep_sentinel=untouched\n',encoding='ascii')
    user=ctypes.WinDLL('user32',use_last_error=True);kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    user.CreateDesktopW.argtypes=[wintypes.LPCWSTR,wintypes.LPCWSTR,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p];user.CreateDesktopW.restype=wintypes.HANDLE
    user.SetThreadDesktop.argtypes=[wintypes.HANDLE];user.SetThreadDesktop.restype=wintypes.BOOL;user.CloseDesktop.argtypes=[wintypes.HANDLE]
    user.GetForegroundWindow.restype=wintypes.HWND;user.GetClipboardSequenceNumber.restype=wintypes.DWORD
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user.GetClassNameW.argtypes=[wintypes.HWND,wintypes.LPWSTR,ctypes.c_int]
    callback=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
    user.EnumDesktopWindows.argtypes=[wintypes.HANDLE,callback,wintypes.LPARAM]
    user.GetDlgItem.argtypes=[wintypes.HWND,ctypes.c_int];user.GetDlgItem.restype=wintypes.HWND
    user.SendMessageTimeoutW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM,wintypes.UINT,wintypes.UINT,ctypes.POINTER(ctypes.c_size_t)];user.SendMessageTimeoutW.restype=ctypes.c_void_p
    user.IsWindow.argtypes=[wintypes.HWND];user.IsWindow.restype=wintypes.BOOL
    user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
    user.CreateWindowExW.argtypes=[wintypes.DWORD,wintypes.LPCWSTR,wintypes.LPCWSTR,wintypes.DWORD,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,wintypes.HWND,wintypes.HMENU,wintypes.HINSTANCE,ctypes.c_void_p];user.CreateWindowExW.restype=wintypes.HWND
    user.RegisterHotKey.argtypes=[wintypes.HWND,ctypes.c_int,wintypes.UINT,wintypes.UINT];user.RegisterHotKey.restype=wintypes.BOOL
    user.UnregisterHotKey.argtypes=[wintypes.HWND,ctypes.c_int];user.DestroyWindow.argtypes=[wintypes.HWND]
    kernel.CreateProcessW.argtypes=[wintypes.LPCWSTR,wintypes.LPWSTR,ctypes.c_void_p,ctypes.c_void_p,wintypes.BOOL,wintypes.DWORD,ctypes.c_void_p,wintypes.LPCWSTR,ctypes.POINTER(STARTUPINFO),ctypes.POINTER(PROCESSINFO)];kernel.CreateProcessW.restype=wintypes.BOOL
    kernel.WaitForSingleObject.argtypes=[wintypes.HANDLE,wintypes.DWORD];kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    before=user.GetForegroundWindow();clip=user.GetClipboardSequenceNumber();name='HebENHotkeyUi_'+uuid.uuid4().hex
    desktop=user.CreateDesktopW(name,None,None,0,0x01ff,None)
    result={'status':'BLOCKED','mode':'test-only app and UI on a separate, never-switched desktop; no keyboard input or microphone',
            'exe_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'checks':[]}
    if not desktop:raise OSError(ctypes.get_last_error(),'CreateDesktopW')
    pi=PROCESSINFO();hidden=None;release=threading.Event();ready=threading.Event();rival_state={}
    try:
        si=STARTUPINFO();si.cb=ctypes.sizeof(si);si.lpDesktop=name
        line=ctypes.create_unicode_buffer(subprocess.list2cmdline([str(exe)]))
        if not kernel.CreateProcessW(str(exe),line,None,None,False,0,None,str(out),ctypes.byref(si),ctypes.byref(pi)):
            raise OSError(ctypes.get_last_error(),'CreateProcessW')
        kernel.CloseHandle(pi.hThread);pi.hThread=None
        def find(cls):
            found=[]
            @callback
            def visit(h,unused):
                owner=wintypes.DWORD();user.GetWindowThreadProcessId(h,ctypes.byref(owner));c=ctypes.create_unicode_buffer(128);user.GetClassNameW(h,c,128)
                if owner.value==pi.dwProcessId and c.value==cls:found.append(h)
                return True
            user.EnumDesktopWindows(desktop,visit,0)
            return found[0] if len(found)==1 else None
        def send(h,msg,w=0,l=0):
            answer=ctypes.c_size_t()
            if not user.SendMessageTimeoutW(h,msg,w,l,2,5000,ctypes.byref(answer)):raise OSError(ctypes.get_last_error(),f'message {msg} failed')
            return answer.value
        for _ in range(100):
            hidden=find('HebENHotkeyUiTestHiddenClass')
            if hidden:break
            if kernel.WaitForSingleObject(pi.hProcess,0)==0:raise RuntimeError('Fixture exited')
            time.sleep(.05)
        if not hidden:raise TimeoutError('Fixture main window missing')
        send(hidden,0x0111,2007,0)
        for _ in range(100):
            dialog=find('HebENHotkeySettings')
            if dialog:break
            time.sleep(.05)
        if not dialog:raise TimeoutError('Shortcut window missing')
        if a.capture_tool:
            capture=a.capture_tool.resolve(); capture_out=out/'window.bmp'; capture_pi=PROCESSINFO()
            capture_si=STARTUPINFO();capture_si.cb=ctypes.sizeof(capture_si);capture_si.lpDesktop=name
            line=ctypes.create_unicode_buffer(subprocess.list2cmdline([str(capture),str(dialog),str(pi.dwProcessId),str(capture_out)]))
            if not kernel.CreateProcessW(str(capture),line,None,None,False,0,None,str(out),ctypes.byref(capture_si),ctypes.byref(capture_pi)):
                raise OSError(ctypes.get_last_error(),'Capture CreateProcessW')
            kernel.CloseHandle(capture_pi.hThread)
            if kernel.WaitForSingleObject(capture_pi.hProcess,10000)!=0:raise TimeoutError('Capture helper timed out')
            kernel.CloseHandle(capture_pi.hProcess)
            if not capture_out.is_file():raise RuntimeError('Capture helper did not produce an image')
        edit=user.GetDlgItem(dialog,101);status=user.GetDlgItem(dialog,105)
        if not edit or not status:raise RuntimeError('Expected controls missing')
        def set_value(text):
            buf=ctypes.create_unicode_buffer(text);send(edit,0x000c,0,ctypes.addressof(buf))
        def status_text():
            buf=ctypes.create_unicode_buffer(512);send(status,0x000d,512,ctypes.addressof(buf));return buf.value
        def edit_text():
            buf=ctypes.create_unicode_buffer(128);send(edit,0x000d,128,ctypes.addressof(buf));return buf.value
        def config():return ini.read_text(encoding='ascii')
        set_value('CTRL+C');send(dialog,0x0111,103,0)
        result['checks'].append({'case':'common_copy','pass':bool(user.IsWindow(dialog)) and 'hotkey=CTRL+ALT+SHIFT+F9' in config(), 'message':status_text()})
        def reserve():
            if not user.SetThreadDesktop(desktop):rival_state['error']='SetThreadDesktop failed';ready.set();return
            h=user.CreateWindowExW(0,'STATIC','',0,0,0,0,0,wintypes.HWND(-3),None,None,None)
            if not h:rival_state['error']='CreateWindowEx failed';ready.set();return
            rival_state['reserved']=bool(user.RegisterHotKey(h,3001,0x4000|0x1|0x2|0x4,0x79));ready.set()
            release.wait(20)
            if rival_state['reserved']:user.UnregisterHotKey(h,3001)
            user.DestroyWindow(h)
        thread=threading.Thread(target=reserve);thread.start();ready.wait(5)
        if not rival_state.get('reserved'):raise RuntimeError('Could not reserve test conflict: '+str(rival_state))
        set_value('CTRL+ALT+SHIFT+F10');send(dialog,0x0111,103,0)
        result['checks'].append({'case':'global_conflict','pass':bool(user.IsWindow(dialog)) and 'hotkey=CTRL+ALT+SHIFT+F9' in config(), 'message':status_text()})
        release.set();thread.join(timeout=5)
        send(dialog,0x0111,102,0)
        for key,down in [(0xa2,1),(0xa4,1),(0xa0,1),(0x79,1),(0x79,0),(0xa0,0),(0xa4,0),(0xa2,0)]:
            send(dialog,0x8000+202,key,down)
        result['checks'].append({'case':'recording_simulated','pass':edit_text()=='CTRL+ALT+SHIFT+F10',
                                 'note':'Test-only event feed to recorder; no physical keyboard event sent.'})
        send(dialog,0x0111,103,0)
        result['checks'].append({'case':'valid_saved','pass':not user.IsWindow(dialog) and 'hotkey=CTRL+ALT+SHIFT+F10' in config() and 'keep_sentinel=untouched' in config()})
        result['status']='PASS_ISOLATED_UI_AND_REGISTRATION' if all(x['pass'] for x in result['checks']) else 'FAIL_ISOLATED_UI'
    except Exception as exc:result['error']=str(exc)
    finally:
        release.set()
        if 'thread' in locals():thread.join(timeout=5)
        if hidden:user.PostMessageW(hidden,0x0010,0,0)
        if pi.hProcess:
            if kernel.WaitForSingleObject(pi.hProcess,10000)!=0:result['fixture_shutdown']='TIMEOUT_left_running'
            kernel.CloseHandle(pi.hProcess)
        result['foreground_unchanged']=user.GetForegroundWindow()==before
        result['clipboard_sequence_unchanged']=user.GetClipboardSequenceNumber()==clip
        result['desktop_closed']=bool(user.CloseDesktop(desktop))
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print(json.dumps(result))


if __name__=='__main__':main()

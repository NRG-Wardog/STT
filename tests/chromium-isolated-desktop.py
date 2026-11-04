"""Inspect a controlled Chromium UIA tree on a separate, never-switched desktop.

No Windows keyboard input is sent. Never attaches to a user's Chrome profile.
"""
import argparse
import ctypes
from ctypes import wintypes
import json
import pathlib
import subprocess
import time
import urllib.request
import uuid
import websocket

ROOT=pathlib.Path(__file__).resolve().parent.parent
CHROME=pathlib.Path(r'C:\Program Files\Google\Chrome\Application\chrome.exe')

class STARTUPINFO(ctypes.Structure):
    _fields_=[('cb',wintypes.DWORD),('lpReserved',wintypes.LPWSTR),('lpDesktop',wintypes.LPWSTR),('lpTitle',wintypes.LPWSTR),
        ('dwX',wintypes.DWORD),('dwY',wintypes.DWORD),('dwXSize',wintypes.DWORD),('dwYSize',wintypes.DWORD),
        ('dwXCountChars',wintypes.DWORD),('dwYCountChars',wintypes.DWORD),('dwFillAttribute',wintypes.DWORD),('dwFlags',wintypes.DWORD),
        ('wShowWindow',wintypes.WORD),('cbReserved2',wintypes.WORD),('lpReserved2',ctypes.c_void_p),
        ('hStdInput',wintypes.HANDLE),('hStdOutput',wintypes.HANDLE),('hStdError',wintypes.HANDLE)]
class PROCESSINFO(ctypes.Structure):
    _fields_=[('hProcess',wintypes.HANDLE),('hThread',wintypes.HANDLE),('dwProcessId',wintypes.DWORD),('dwThreadId',wintypes.DWORD)]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',type=pathlib.Path,required=True);ap.add_argument('--probe',type=pathlib.Path,required=True)
    ap.add_argument('--cdp-typing',action='store_true',help='Exercise CDP text input and readback only; not Windows SendInput or HebEN')
    args=ap.parse_args()
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=False);profile=out/'profile';profile.mkdir()
    user=ctypes.WinDLL('user32',use_last_error=True);kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    user.CreateDesktopW.argtypes=[wintypes.LPCWSTR,wintypes.LPCWSTR,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p];user.CreateDesktopW.restype=wintypes.HANDLE
    user.CloseDesktop.argtypes=[wintypes.HANDLE];user.CloseDesktop.restype=wintypes.BOOL
    user.GetForegroundWindow.restype=wintypes.HWND
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user.GetClassNameW.argtypes=[wintypes.HWND,wintypes.LPWSTR,ctypes.c_int]
    callback=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
    user.EnumDesktopWindows.argtypes=[wintypes.HANDLE,callback,wintypes.LPARAM];user.EnumDesktopWindows.restype=wintypes.BOOL
    kernel.CreateProcessW.argtypes=[wintypes.LPCWSTR,wintypes.LPWSTR,ctypes.c_void_p,ctypes.c_void_p,wintypes.BOOL,wintypes.DWORD,ctypes.c_void_p,wintypes.LPCWSTR,ctypes.POINTER(STARTUPINFO),ctypes.POINTER(PROCESSINFO)]
    kernel.CreateProcessW.restype=wintypes.BOOL
    kernel.WaitForSingleObject.argtypes=[wintypes.HANDLE,wintypes.DWORD];kernel.WaitForSingleObject.restype=wintypes.DWORD
    kernel.GetExitCodeProcess.argtypes=[wintypes.HANDLE,ctypes.POINTER(wintypes.DWORD)];kernel.GetExitCodeProcess.restype=wintypes.BOOL
    kernel.CloseHandle.argtypes=[wintypes.HANDLE];kernel.TerminateProcess.argtypes=[wintypes.HANDLE,wintypes.UINT]
    before=user.GetForegroundWindow();name='HebENRecovery_'+uuid.uuid4().hex
    desktop=user.CreateDesktopW(name,None,None,0,0x01ff,None)
    result={'mode':'separate desktop, no desktop switch or SendInput','status':'BLOCKED'}
    if not desktop:
        result['error']='CreateDesktopW failed '+str(ctypes.get_last_error())
        (out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf8');print(json.dumps(result));return
    pi=PROCESSINFO();ws=None;browser=None
    try:
        cmd=[str(CHROME),'--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-component-update',
             '--disable-sync','--disable-extensions','--host-resolver-rules=MAP * ~NOTFOUND, EXCLUDE localhost',
             '--force-renderer-accessibility','--remote-debugging-port=0','--user-data-dir='+str(profile),'about:blank']
        si=STARTUPINFO();si.cb=ctypes.sizeof(si);si.lpDesktop=name
        line=ctypes.create_unicode_buffer(subprocess.list2cmdline(cmd))
        if not kernel.CreateProcessW(str(CHROME),line,None,None,False,0,None,str(out),ctypes.byref(si),ctypes.byref(pi)):
            raise OSError(ctypes.get_last_error(),'CreateProcessW on isolated desktop failed')
        kernel.CloseHandle(pi.hThread);pi.hThread=None
        active=profile/'DevToolsActivePort'
        for _ in range(200):
            if active.exists() and len(active.read_text().splitlines())>=2:break
            if kernel.WaitForSingleObject(pi.hProcess,0)==0:raise RuntimeError('Chromium exited before CDP ready')
            time.sleep(.1)
        else:raise TimeoutError('Chromium did not expose CDP')
        port,browser_path=active.read_text().splitlines()[:2]
        pages=json.load(urllib.request.urlopen('http://127.0.0.1:'+port+'/json/list',timeout=5))
        page=next(x for x in pages if x['type']=='page')
        ws=websocket.create_connection(page['webSocketDebuggerUrl'],timeout=10,suppress_origin=True);seq=0
        def call(method,params=None):
            nonlocal seq
            seq+=1;current=seq;ws.send(json.dumps({'id':current,'method':method,'params':params or {}}))
            while True:
                item=json.loads(ws.recv())
                if item.get('id')!=current:continue
                if 'error' in item:raise RuntimeError(str(item['error']))
                return item.get('result',{})
        def evaluate(expr):return call('Runtime.evaluate',{'expression':expr,'returnByValue':True})['result'].get('value')
        fixture='desktop-controlled-editor.html' if args.cdp_typing else 'chromium-editor.html'
        call('Page.enable');call('Runtime.enable');call('Page.navigate',{'url':(ROOT/'tests/fixtures'/fixture).resolve().as_uri()})
        for _ in range(100):
            if evaluate('document.readyState')=='complete' and evaluate("!!document.getElementById('editable')"):break
            time.sleep(.05)
        else:raise TimeoutError('Controlled editor did not load')
        call('Emulation.setFocusEmulationEnabled',{'enabled':True})
        evaluate("document.getElementById('editable').focus()")
        result['dom_active_element']=evaluate('document.activeElement.id')
        result['document_has_focus']=evaluate('document.hasFocus()')
        if args.cdp_typing:
            expected='שלום עולם Hello World 123';result['cdp_checks']=[]
            for field in ('input','textarea','editable'):
                evaluate("document.getElementById('"+field+"').focus()")
                call('Input.insertText',{'text':expected})
                value=evaluate("document.getElementById('"+field+"')."+('textContent' if field=='editable' else 'value'))
                result['cdp_checks'].append({'field':field,'observed':value,'status':'PASS' if value==expected else 'FAIL',
                                             'submissions':evaluate('window.testSubmissions')})
            result['typing_note']='CDP browser-internal text input only: no Windows hotkey, SendInput, microphone, ASR, or HebEN.'
        handles=[]
        @callback
        def visit(hwnd,unused):
            pid=wintypes.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(pid))
            if pid.value==pi.dwProcessId:
                cls=ctypes.create_unicode_buffer(128);user.GetClassNameW(hwnd,cls,128)
                if cls.value=='Chrome_WidgetWin_1':handles.append(int(hwnd))
            return True
        for _ in range(100):
            handles.clear();user.EnumDesktopWindows(desktop,visit,0)
            if handles:break
            time.sleep(.1)
        if not handles:raise RuntimeError('Chrome top-level window not found on isolated desktop')
        result['owned_window_count']=len(handles)
        probePath=args.probe.resolve();probeInfo=PROCESSINFO();probeStartup=STARTUPINFO();probeStartup.cb=ctypes.sizeof(probeStartup);probeStartup.lpDesktop=name
        probeLine=ctypes.create_unicode_buffer(subprocess.list2cmdline([str(probePath),str(handles[0]),str(out/'uia-probe.stdout')]))
        if not kernel.CreateProcessW(str(probePath),probeLine,None,None,False,0,None,str(out),ctypes.byref(probeStartup),ctypes.byref(probeInfo)):
            raise OSError(ctypes.get_last_error(),'Could not start UIA probe on isolated desktop')
        kernel.CloseHandle(probeInfo.hThread)
        if kernel.WaitForSingleObject(probeInfo.hProcess,20000)!=0:
            kernel.TerminateProcess(probeInfo.hProcess,1);kernel.WaitForSingleObject(probeInfo.hProcess,5000)
            result['uia_probe_timeout']=True
        exit_code=wintypes.DWORD();kernel.GetExitCodeProcess(probeInfo.hProcess,ctypes.byref(exit_code));kernel.CloseHandle(probeInfo.hProcess)
        result['uia_probe_exit']=exit_code.value
        result['status']=('PASS_CDP_ONLY' if args.cdp_typing and all(x['status']=='PASS' and x['submissions']==0 for x in result['cdp_checks'])
                          else 'FAIL_CDP_ONLY' if args.cdp_typing else 'PASS_METADATA_ONLY') if exit_code.value==0 else 'BLOCKED_UIA'
    except Exception as exc:result['error']=str(exc)
    finally:
        if ws:ws.close()
        try:
            if 'browser_path' in locals():
                browser=websocket.create_connection('ws://127.0.0.1:'+port+browser_path,timeout=3,suppress_origin=True)
                browser.send(json.dumps({'id':1,'method':'Browser.close'}));browser.close()
        except Exception:pass
        if pi.hProcess:
            if kernel.WaitForSingleObject(pi.hProcess,10000)!=0:
                kernel.TerminateProcess(pi.hProcess,1);kernel.WaitForSingleObject(pi.hProcess,5000)
                result['owned_process_forced_termination']=True
            kernel.CloseHandle(pi.hProcess)
        result['desktop_closed']=bool(user.CloseDesktop(desktop));result['foreground_same_at_end']=user.GetForegroundWindow()==before
        (out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    print(json.dumps(result))

if __name__=='__main__':main()

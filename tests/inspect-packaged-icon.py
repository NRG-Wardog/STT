"""Run the actual packaged exe on a never-switched desktop, inspect its window.

The app's explicit --icon-inspection mode has no hotkey, worker or microphone.
The helper captures only the test-owned settings window. No desktop is switched.
"""
import argparse, ctypes, hashlib, importlib.util, json, os, subprocess, time, uuid
from ctypes import wintypes
from pathlib import Path

root=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('hotkey_ui',root/'tests/hotkey-ui-isolated.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
STARTUPINFO,PROCESSINFO=module.STARTUPINFO,module.PROCESSINFO
p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--helper',type=Path,required=True);p.add_argument('--taskbar-helper',type=Path);p.add_argument('--overflow-helper',type=Path);p.add_argument('--real-mic-test',action='store_true');p.add_argument('--mic-cycles',type=int,default=0,choices=range(0,21));p.add_argument('--out',type=Path,required=True);p.add_argument('--active-desktop',action='store_true');a=p.parse_args()
exe=a.exe.resolve();helper=a.helper.resolve();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
user=ctypes.WinDLL('user32',use_last_error=True);kernel=ctypes.WinDLL('kernel32',use_last_error=True);shell=ctypes.WinDLL('shell32',use_last_error=True)
user.CreateDesktopW.argtypes=[wintypes.LPCWSTR,wintypes.LPCWSTR,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p];user.CreateDesktopW.restype=wintypes.HANDLE
user.OpenInputDesktop.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];user.OpenInputDesktop.restype=wintypes.HANDLE
user.CloseDesktop.argtypes=[wintypes.HANDLE];user.GetForegroundWindow.restype=wintypes.HWND
user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
user.GetClassNameW.argtypes=[wintypes.HWND,wintypes.LPWSTR,ctypes.c_int]
user.GetWindowTextW.argtypes=[wintypes.HWND,wintypes.LPWSTR,ctypes.c_int]
user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM];user.PostMessageW.restype=wintypes.BOOL
user.GetClassLongPtrW.argtypes=[wintypes.HWND,ctypes.c_int];user.GetClassLongPtrW.restype=ctypes.c_size_t
user.SendMessageTimeoutW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM,wintypes.UINT,wintypes.UINT,ctypes.POINTER(ctypes.c_size_t)];user.SendMessageTimeoutW.restype=ctypes.c_void_p
callback=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM);user.EnumDesktopWindows.argtypes=[wintypes.HANDLE,callback,wintypes.LPARAM]
kernel.CreateProcessW.argtypes=[wintypes.LPCWSTR,wintypes.LPWSTR,ctypes.c_void_p,ctypes.c_void_p,wintypes.BOOL,wintypes.DWORD,ctypes.c_void_p,wintypes.LPCWSTR,ctypes.POINTER(STARTUPINFO),ctypes.POINTER(PROCESSINFO)];kernel.CreateProcessW.restype=wintypes.BOOL
kernel.WaitForSingleObject.argtypes=[wintypes.HANDLE,wintypes.DWORD];kernel.CloseHandle.argtypes=[wintypes.HANDLE]
class GUID(ctypes.Structure):_fields_=[('a',wintypes.DWORD),('b',wintypes.WORD),('c',wintypes.WORD),('d',ctypes.c_ubyte*8)]
class ID(ctypes.Structure):_fields_=[('cbSize',wintypes.DWORD),('hWnd',wintypes.HWND),('uID',wintypes.UINT),('guidItem',GUID)]
shell.Shell_NotifyIconGetRect.argtypes=[ctypes.POINTER(ID),ctypes.POINTER(wintypes.RECT)];shell.Shell_NotifyIconGetRect.restype=ctypes.c_long
name=None if a.active_desktop else 'HebENIconInspection_'+uuid.uuid4().hex
desktop=user.OpenInputDesktop(0,False,0x01ff) if a.active_desktop else user.CreateDesktopW(name,None,None,0,0x01ff,None)
if not desktop:raise OSError(ctypes.get_last_error(),'CreateDesktopW')
original=user.GetForegroundWindow();pi=PROCESSINFO();hidden=None;trace=out/'trace.jsonl';old=os.environ.get('HEBEN_DIAGNOSTICS_PATH');os.environ['HEBEN_DIAGNOSTICS_PATH']=str(trace)
result={'status':'FAIL','exe':str(exe),'sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'mode':'exact app in --icon-inspection mode; '+('active desktop, settings shown without activation' if a.active_desktop else 'separate never-switched desktop')+'; no hotkey, inference or audio saved'+('; real microphone opened briefly' if a.real_mic_test else '; microphone not opened')}
def start(path,args,working):
    si=STARTUPINFO();si.cb=ctypes.sizeof(si);si.lpDesktop=name;proc=PROCESSINFO()
    command=ctypes.create_unicode_buffer(subprocess.list2cmdline([str(path),*args]))
    if not kernel.CreateProcessW(str(path),command,None,None,False,0,None,str(working),ctypes.byref(si),ctypes.byref(proc)):raise OSError(ctypes.get_last_error(),'CreateProcessW')
    kernel.CloseHandle(proc.hThread);return proc
def send(h,msg,w=0,l=0):
    reply=ctypes.c_size_t()
    if not user.SendMessageTimeoutW(h,msg,w,l,2,5000,ctypes.byref(reply)):raise OSError(ctypes.get_last_error(),f'SendMessageTimeout {msg}')
    return reply.value
def find(classname):
    found=[]
    @callback
    def visit(h,_):
        owner=wintypes.DWORD();user.GetWindowThreadProcessId(h,ctypes.byref(owner));cls=ctypes.create_unicode_buffer(128);user.GetClassNameW(h,cls,128)
        if owner.value==pi.dwProcessId and cls.value==classname:found.append(h)
        return True
    user.EnumDesktopWindows(desktop,visit,0);return found[0] if len(found)==1 else None
try:
    pi=start(exe,['--icon-inspection'],exe.parent)
    for _ in range(150):
        hidden=find('HebENIconInspectionHiddenClass')
        if hidden:break
        if kernel.WaitForSingleObject(pi.hProcess,0)==0:raise RuntimeError('App exited during initialization')
        time.sleep(.05)
    if not hidden:raise TimeoutError('Inspection window missing')
    for _ in range(100):
        events=[json.loads(line) for line in trace.read_text(encoding='utf8').splitlines()] if trace.exists() else []
        if any(e['stage']=='icon_inspection' for e in events):break
        time.sleep(.05)
    send(hidden,0x8000+205)
    send(hidden,0x0111,2007)
    for _ in range(100):
        dialog=find('HebENHotkeySettings')
        if dialog:break
        time.sleep(.05)
    if not dialog:raise TimeoutError('Settings window missing')
    caption=ctypes.create_unicode_buffer(128);user.GetWindowTextW(dialog,caption,128);result['settings_caption']=caption.value
    result['settings_class_large_icon']=int(user.GetClassLongPtrW(dialog,-14))
    result['settings_class_small_icon']=int(user.GetClassLongPtrW(dialog,-34))
    capture=start(helper,[str(dialog),str(pi.dwProcessId),str(out/'settings.bmp')],out)
    if kernel.WaitForSingleObject(capture.hProcess,10000)!=0:raise TimeoutError('Screenshot helper timed out')
    kernel.CloseHandle(capture.hProcess)
    if not (out/'settings.bmp').exists():raise RuntimeError('No settings screenshot')
    if a.active_desktop and a.taskbar_helper:
        time.sleep(.8) # Allow Explorer to create the taskbar button.
        taskbar=subprocess.run([str(a.taskbar_helper.resolve())],capture_output=True,text=True,timeout=10,creationflags=subprocess.CREATE_NO_WINDOW)
        result['taskbar_query']=json.loads(taskbar.stdout) if taskbar.stdout.strip() else {'error':taskbar.stderr}
        item=result['taskbar_query'];bounds=item.get('rect',[])
        if item.get('found') and not item.get('offscreen') and len(bounds)==4 and 0 < bounds[2]-bounds[0] <= 256 and 0 < bounds[3]-bounds[1] <= 128:
            from PIL import ImageGrab
            ImageGrab.grab(bbox=tuple(bounds),all_screens=True).save(out/'taskbar-button.png')
    ni=ID();ni.cbSize=ctypes.sizeof(ID);ni.hWnd=hidden;ni.uID=1202;rect=wintypes.RECT()
    hr=shell.Shell_NotifyIconGetRect(ctypes.byref(ni),ctypes.byref(rect))
    result['tray_rect_hresult']=f'0x{hr & 0xffffffff:08x}'
    if hr==0:
        result['tray_rect']=[rect.left,rect.top,rect.right,rect.bottom]
        if a.active_desktop and 0 < rect.right-rect.left <= 128 and 0 < rect.bottom-rect.top <= 128:
            # Only capture the bounds Explorer reports for this test icon.
            from PIL import ImageGrab
            time.sleep(.3)
            ImageGrab.grab(bbox=tuple(result['tray_rect']),all_screens=True).save(out/'tray-cell.png')
    if a.active_desktop and a.overflow_helper:
        overflow=subprocess.run([str(a.overflow_helper.resolve()),str(out/'tray-idle.bmp'),str(hidden),'HebEN Dictation | Icon inspection | Ready'],capture_output=True,text=True,timeout=15,creationflags=subprocess.CREATE_NO_WINDOW)
        result['overflow_query']=json.loads(overflow.stdout) if overflow.stdout.strip() else {'error':overflow.stderr}
    if a.real_mic_test:
        started=send(hidden,0x8000+206)==1;result['physical_microphone_started']=started
        if started:
            try:
                if a.active_desktop and a.overflow_helper:
                    overflow=subprocess.run([str(a.overflow_helper.resolve()),str(out/'tray-listening.bmp'),str(hidden),'HebEN Dictation | Icon inspection | Listening'],capture_output=True,text=True,timeout=15,creationflags=subprocess.CREATE_NO_WINDOW)
                    result['overflow_listening']=json.loads(overflow.stdout) if overflow.stdout.strip() else {'error':overflow.stderr}
            finally:
                result['physical_microphone_stopped']=send(hidden,0x8000+207)==1
    if a.mic_cycles:
        import psutil
        proc=psutil.Process(pi.dwProcessId);cycle_samples=[]
        for index in range(a.mic_cycles):
            if send(hidden,0x8000+206)!=1:raise RuntimeError(f'Microphone cycle {index+1} failed to start')
            time.sleep(.1)
            if send(hidden,0x8000+207)!=1:raise RuntimeError(f'Microphone cycle {index+1} failed to stop')
            time.sleep(.1)
            mi=proc.memory_info();cycle_samples.append({'cycle':index+1,'handles':proc.num_handles(),'threads':proc.num_threads(),'private_bytes':mi.private,'working_set_bytes':mi.rss})
        result['microphone_cycle_samples']=cycle_samples
    events=[json.loads(line) for line in trace.read_text(encoding='utf8').splitlines()]
    result['icon_events']=[e for e in events if e['stage'] in ('icon_load','tray_icon_add','tray_icon_modify','icon_inspection')]
    add=[e for e in events if e['stage']=='tray_icon_add'];modify=[e for e in events if e['stage']=='tray_icon_modify']
    result['same_shell_icon_handle']=bool(add and modify and add[-1]['result']=='ok' and modify[-1]['result']=='ok' and add[-1]['value']!=0 and add[-1]['value']==modify[-1]['value'])
    result['status']='PASS_API_AND_WINDOW_CAPTURE' if result['same_shell_icon_handle'] and result['settings_class_large_icon'] and result['settings_class_small_icon'] else 'FAIL'
except Exception as exc:result['error']=str(exc)
finally:
    if hidden:user.PostMessageW(hidden,0x0010,0,0)
    if pi.hProcess:
        result['clean_exit']=kernel.WaitForSingleObject(pi.hProcess,10000)==0;kernel.CloseHandle(pi.hProcess)
        if hidden and result['clean_exit']:
            ni=ID();ni.cbSize=ctypes.sizeof(ID);ni.hWnd=hidden;ni.uID=1202;rect=wintypes.RECT()
            result['post_exit_tray_rect_hresult']=f'0x{shell.Shell_NotifyIconGetRect(ctypes.byref(ni),ctypes.byref(rect)) & 0xffffffff:08x}'
    if old is None:os.environ.pop('HEBEN_DIAGNOSTICS_PATH',None)
    else:os.environ['HEBEN_DIAGNOSTICS_PATH']=old
    result['foreground_unchanged']=user.GetForegroundWindow()==original
    result['desktop_closed']=bool(user.CloseDesktop(desktop))
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
print(json.dumps(result,indent=2))

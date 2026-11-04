"""Open a new Explorer view of the test package, capture it, and close only it."""
import argparse,ctypes,json,subprocess,time
from ctypes import wintypes
from pathlib import Path
import win32com.client

p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--helper',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
exe=a.exe.resolve();folder=str(exe.parent).casefold();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
user=ctypes.WinDLL('user32',use_last_error=True);kernel=ctypes.WinDLL('kernel32');shell32=ctypes.WinDLL('shell32')
class INPUT(ctypes.Structure):_fields_=[('cbSize',wintypes.UINT),('dwTime',wintypes.DWORD)]
user.GetLastInputInfo.argtypes=[ctypes.POINTER(INPUT)];user.GetForegroundWindow.restype=wintypes.HWND
user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
kernel.GetTickCount.restype=wintypes.DWORD
shell32.ShellExecuteW.argtypes=[wintypes.HWND,wintypes.LPCWSTR,wintypes.LPCWSTR,wintypes.LPCWSTR,wintypes.LPCWSTR,ctypes.c_int];shell32.ShellExecuteW.restype=ctypes.c_void_p
def idle():
    info=INPUT(ctypes.sizeof(INPUT),0);user.GetLastInputInfo(ctypes.byref(info));return (kernel.GetTickCount()-info.dwTime)&0xffffffff,info.dwTime
def windows():
    entries=[]
    for window in win32com.client.Dispatch('Shell.Application').Windows():
        try:
            path=str(window.Document.Folder.Self.Path).casefold()
            entries.append((int(window.HWND),path))
        except Exception:pass
    return entries
elapsed,input_before=idle();before=user.GetForegroundWindow();known={h for h,_ in windows()};result={'status':'BLOCKED','idle_seconds_before':elapsed/1000,'exe':str(exe)};target=None
try:
    if elapsed<90000:raise RuntimeError('Desktop has been active in the last 90 seconds.')
    success=shell32.ShellExecuteW(None,'open','explorer.exe','/select,"'+str(exe)+'"',None,4)
    if not success or success<=32:raise RuntimeError('ShellExecute Explorer failed.')
    for _ in range(100):
        matches=[h for h,path in windows() if h not in known and path==folder]
        if len(matches)==1:target=matches[0];break
        time.sleep(.1)
    if not target:raise RuntimeError('No unique newly opened Explorer window for the package.')
    time.sleep(1);elapsed,input_now=idle()
    if input_now!=input_before:raise RuntimeError('User input detected; stopped without capturing.')
    owner=wintypes.DWORD();user.GetWindowThreadProcessId(target,ctypes.byref(owner))
    done=subprocess.run([str(a.helper.resolve()),str(target),str(owner.value),str(out/'explorer.bmp'),'CabinetWClass'],creationflags=subprocess.CREATE_NO_WINDOW,timeout=10)
    result['capture_exit_code']=done.returncode
    result['status']='CAPTURED_NEW_EXPLORER_WINDOW' if done.returncode==0 and (out/'explorer.bmp').is_file() else 'CAPTURE_FAILED'
except Exception as e:result['error']=str(e)
finally:
    if target:user.PostMessageW(target,0x10,0,0)
    result['foreground_unchanged']=user.GetForegroundWindow()==before
    result['user_input_during_test']=idle()[1]!=input_before
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
print(json.dumps(result,indent=2))

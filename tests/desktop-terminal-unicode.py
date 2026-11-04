"""Real Windows Terminal + real probe hotkey; test-owned console never executes input."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import psutil
import subprocess
import time
import uuid

ROOT=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('desktop_common',ROOT/'tests/desktop-chrome-unicode.py')
common=importlib.util.module_from_spec(spec);spec.loader.exec_module(common)


def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--probe-dir',type=Path,required=True)
    p.add_argument('--min-idle-seconds',type=int,default=90);p.add_argument('--wait-idle-seconds',type=int,default=0)
    p.add_argument('--powershell',action='store_true',help='Use a new no-profile PowerShell prompt; never send Enter')
    a=p.parse_args()
    if a.min_idle_seconds<60 or not 0<=a.wait_idle_seconds<=600:raise ValueError('Invalid idle guard')
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
    exe=(a.probe_dir/'HebENFixedInputProbe.exe').resolve();meta=json.loads((a.probe_dir/'metadata.json').read_text(encoding='utf-8-sig'))
    if hashlib.sha256(exe.read_bytes()).hexdigest()!=meta['sha256']:raise ValueError('Probe hash mismatch')
    ini=(a.probe_dir/'config.ini').read_text(encoding='utf-8-sig').lower()
    if 'hotkey=ctrl+alt+shift+f12' not in ini or 'input_method=unicode' not in ini or 'trusted_terminal_input=1' not in ini:
        raise ValueError('Expected explicitly trusted test-only terminal probe')
    wt=Path(os.environ['LOCALAPPDATA'])/'Microsoft/WindowsApps/wt.exe'
    fixture=(ROOT/'build/candidate/Release/HebENSafeConsoleFixture.exe').resolve()
    if not wt.exists() or not fixture.is_file():raise FileNotFoundError('Windows Terminal or safe console fixture unavailable')
    user=ctypes.WinDLL('user32',use_last_error=True);kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    user.GetForegroundWindow.restype=wintypes.HWND;user.GetClipboardSequenceNumber.restype=wintypes.DWORD
    user.GetLastInputInfo.argtypes=[ctypes.POINTER(common.LASTINPUTINFO)];kernel.GetTickCount64.restype=ctypes.c_ulonglong
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user.GetWindowThreadProcessId.restype=wintypes.DWORD
    user.GetWindowTextW.argtypes=[wintypes.HWND,wintypes.LPWSTR,ctypes.c_int]
    user.ShowWindow.argtypes=[wintypes.HWND,ctypes.c_int];user.SetForegroundWindow.argtypes=[wintypes.HWND]
    user.AttachThreadInput.argtypes=[wintypes.DWORD,wintypes.DWORD,wintypes.BOOL];user.AttachThreadInput.restype=wintypes.BOOL
    user.GetAsyncKeyState.argtypes=[ctypes.c_int];user.GetAsyncKeyState.restype=ctypes.c_short
    user.PeekMessageW.argtypes=[ctypes.POINTER(wintypes.MSG),wintypes.HWND,wintypes.UINT,wintypes.UINT,wintypes.UINT]
    kernel.GetCurrentThreadId.restype=wintypes.DWORD
    user.SendInput.argtypes=[wintypes.UINT,ctypes.POINTER(common.INPUT),ctypes.c_int];user.SendInput.restype=wintypes.UINT
    user.FindWindowW.argtypes=[wintypes.LPCWSTR,wintypes.LPCWSTR];user.FindWindowW.restype=wintypes.HWND
    user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
    kernel.CreateEventW.argtypes=[ctypes.c_void_p,wintypes.BOOL,wintypes.BOOL,wintypes.LPCWSTR];kernel.CreateEventW.restype=wintypes.HANDLE
    kernel.SetEvent.argtypes=[wintypes.HANDLE];kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    callback=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM);user.EnumWindows.argtypes=[callback,wintypes.LPARAM]
    def idle():
        x=common.LASTINPUTINFO(ctypes.sizeof(common.LASTINPUTINFO),0)
        if not user.GetLastInputInfo(ctypes.byref(x)):raise OSError(ctypes.get_last_error(),'GetLastInputInfo')
        return ((kernel.GetTickCount64()&0xffffffff)-x.dwTime&0xffffffff)/1000
    def last_tick():
        x=common.LASTINPUTINFO(ctypes.sizeof(common.LASTINPUTINFO),0)
        if not user.GetLastInputInfo(ctypes.byref(x)):raise OSError(ctypes.get_last_error(),'GetLastInputInfo')
        return x.dwTime
    deadline=time.monotonic()+a.wait_idle_seconds
    while idle()<a.min_idle_seconds and time.monotonic()<deadline:time.sleep(2)
    before=user.GetForegroundWindow();clip=user.GetClipboardSequenceNumber();probe=launcher=None;hwnd=None;last_test_tick=None
    result={'status':'BLOCKED','mode':('actual Windows Terminal with new no-profile PowerShell; no dictated input is submitted' if a.powershell else 'actual Windows Terminal window; real Windows fixed-string hotkey and SendInput; controlled console does not execute commands'),
            'idle_seconds_before':idle(),'probe_sha256':meta['sha256'],'no_enter_sent':True}
    nonce=uuid.uuid4().hex;title='HebEN-TEST-'+nonce;event_name='Local\\HebENSafeConsole-'+nonce
    event=kernel.CreateEventW(None,True,False,event_name)
    if not event:raise OSError(ctypes.get_last_error(),'CreateEventW')
    try:
        if idle()<a.min_idle_seconds:raise RuntimeError('Recent user input; no terminal launched')
        log=out/'console-input.txt';prior={p.info['pid'] for p in psutil.process_iter(['pid','name']) if (p.info.get('name') or '').lower()=='powershell.exe'}
        shell=Path(os.environ['WINDIR'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
        target=([str(shell),'-NoLogo','-NoProfile','-NoExit'] if a.powershell else [str(fixture),str(log),event_name])
        args=[str(wt),'-w','new','new-tab','--startingDirectory',str(out),'--title',title,'--suppressApplicationTitle',*target]
        si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=7
        launcher=subprocess.Popen(args,cwd=out,startupinfo=si,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        windows=[]
        @callback
        def visit(h,unused):
            name=ctypes.create_unicode_buffer(512);user.GetWindowTextW(h,name,512)
            if title in name.value:windows.append(h)
            return True
        for _ in range(200):
            windows.clear();user.EnumWindows(visit,0)
            if windows and (a.powershell or (log.exists() and log.stat().st_size)):break
            time.sleep(.1)
        if len(windows)!=1 or (not a.powershell and not log.exists()):raise RuntimeError('Unique controlled terminal and ready signal not found')
        hwnd=windows[0];owner=wintypes.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
        if psutil.Process(owner.value).name().lower()!='windowsterminal.exe':raise RuntimeError('Target is not Windows Terminal')
        if a.powershell:
            safe_pid=0
            for _ in range(100):
                new=[p.info['pid'] for p in psutil.process_iter(['pid','name']) if (p.info.get('name') or '').lower()=='powershell.exe' and p.info['pid'] not in prior]
                if len(new)==1:safe_pid=new[0];break
                if len(new)>1:raise RuntimeError('Multiple new PowerShell processes; cannot safely identify target')
                time.sleep(.1)
            if not safe_pid or Path(psutil.Process(safe_pid).exe()).resolve()!=shell.resolve():raise RuntimeError('PowerShell process identity mismatch')
        else:
            first=log.read_text(encoding='utf8').splitlines()[0]
            if not first.startswith('PID='):raise RuntimeError('Safe console did not identify itself')
            safe_pid=int(first[4:])
            if Path(psutil.Process(safe_pid).exe()).resolve()!=fixture:raise RuntimeError('Console process identity mismatch')
        result['terminal_pid']=owner.value;result['console_pid']=safe_pid;result['owned_window_hwnd']=int(hwnd);result['owned_window_title']=title
        if user.GetForegroundWindow() not in (before,hwnd) or idle()<a.min_idle_seconds:raise RuntimeError('User input or focus changed before test')
        user.ShowWindow(hwnd,9)
        if user.GetForegroundWindow()!=hwnd:user.SetForegroundWindow(hwnd)
        if user.GetForegroundWindow()!=hwnd:
            if user.GetForegroundWindow()!=before or idle()<a.min_idle_seconds or any(user.GetAsyncKeyState(k)&0x8000 for k in (0x10,0x11,0x12,0x5b,0x5c)):
                raise RuntimeError('User activity or modifier prevented foreground request')
            message=wintypes.MSG();user.PeekMessageW(ctypes.byref(message),None,0,0,0)
            source=kernel.GetCurrentThreadId();foreground_thread=user.GetWindowThreadProcessId(before,None)
            attached=bool(foreground_thread and foreground_thread!=source and user.AttachThreadInput(source,foreground_thread,True))
            try:
                if attached:user.SetForegroundWindow(hwnd)
            finally:
                if attached:user.AttachThreadInput(source,foreground_thread,False)
            result['temporary_input_queue_attach']=attached
        if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Windows did not grant foreground to test terminal')
        if a.powershell:
            reader=ROOT/'build/candidate/Release/HebENReadOwnedConsole.exe';initial=''
            for _ in range(100):
                read=subprocess.run([str(reader),str(safe_pid)],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=5)
                if read.returncode==0:
                    initial=read.stdout.decode('utf8',errors='replace').rstrip()
                    if initial.startswith('PS ') and initial.endswith('>'):break
                time.sleep(.1)
            else:raise RuntimeError('Known empty PowerShell prompt not verified; no input sent')
            result['initial_prompt_verified']=True
        trace=out/'probe-trace.jsonl';env=dict(os.environ,HEBEN_DIAGNOSTICS_PATH=str(trace))
        probe=subprocess.Popen([str(exe)],cwd=exe.parent,env=env,creationflags=subprocess.CREATE_NO_WINDOW)
        for _ in range(100):
            if trace.exists() and '"hotkey_register","result":"ok"' in trace.read_text(encoding='utf8'):break
            if probe.poll() is not None:raise RuntimeError('Probe exited')
            time.sleep(.05)
        else:raise RuntimeError('Probe shortcut not registered')
        if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed before test hotkey')
        keys=[(0x11,0),(0x12,0),(0x10,0),(0x7b,0),(0x7b,2),(0x10,2),(0x12,2),(0x11,2)]
        inputs=(common.INPUT*len(keys))(*[common.INPUT(type=1,ki=common.KEYBDINPUT(wVk=k,dwFlags=f)) for k,f in keys])
        if user.SendInput(len(keys),inputs,ctypes.sizeof(common.INPUT))!=len(keys):raise RuntimeError('Hotkey send incomplete')
        observed=''
        for _ in range(150):
            if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed during test')
            if a.powershell:
                read=subprocess.run([str(reader),str(safe_pid)],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=5)
                line=read.stdout.decode('utf8',errors='replace').rstrip() if read.returncode==0 else ''
                observed=line[len(initial):].lstrip() if line.startswith(initial) else ''
            else:
                lines=log.read_text(encoding='utf8').split('\n',1);observed=lines[1] if len(lines)>1 else ''
            if observed==common.EXPECTED:break
            time.sleep(.1)
        result.update(status='PASS_CONSOLE_READBACK' if observed==common.EXPECTED else 'FAIL_CONSOLE_READBACK',
                      expected=common.EXPECTED,observed=observed)
        last_test_tick=last_tick()
    except Exception as exc:result['error']=str(exc)
    finally:
        if probe and probe.poll() is None:
            ph=user.FindWindowW('HebENFixedInputProbeHiddenClass',None);owner=wintypes.DWORD()
            if ph:user.GetWindowThreadProcessId(ph,ctypes.byref(owner))
            if ph and owner.value==probe.pid:user.PostMessageW(ph,0x10,0,0)
            try:probe.wait(timeout=10)
            except subprocess.TimeoutExpired:result['probe_shutdown']='TIMEOUT_left_running'
        if not a.powershell:kernel.SetEvent(event)
        kernel.CloseHandle(event)
        if hwnd and user.GetForegroundWindow()==hwnd and before:user.SetForegroundWindow(before)
        if hwnd:
            if not a.powershell or user.GetForegroundWindow()!=hwnd:
                user.PostMessageW(hwnd,0x10,0,0)
            elif last_test_tick is not None and last_tick()==last_test_tick:
                # This window and prompt were created by this test; closing it
                # discards the unsubmitted string without sending Enter.
                user.PostMessageW(hwnd,0x10,0,0)
                result['owned_foreground_window_close_requested']=True
            else:result['owned_window_left_open_due_to_input']=True
        if a.powershell and 'console_pid' in result and not result.get('owned_window_left_open_due_to_input'):
            try:psutil.Process(result['console_pid']).wait(timeout=5);result['console_exit_observed']=True
            except (psutil.Error,TimeoutError):result['console_exit_observed']=False
        result['clipboard_sequence_unchanged']=user.GetClipboardSequenceNumber()==clip
        result['foreground_restored_or_changed_by_user']=user.GetForegroundWindow()!=hwnd
        (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(result,ensure_ascii=True))


if __name__=='__main__':main()

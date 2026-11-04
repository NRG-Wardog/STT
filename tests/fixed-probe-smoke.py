"""Run the fixed-input probe disarmed, without input injection or focus changes."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


def main():
    p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--trusted-terminal',action='store_true',help='Use opt-in terminal config while keeping the probe disarmed')
    p.add_argument('--clipboard',action='store_true',help='Use opt-in clipboard configuration while keeping the probe disarmed');a=p.parse_args()
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
    exe=out/'HebENFixedInputProbe.exe';shutil.copyfile(a.exe,exe)
    (out/'config.ini').write_text('[General]\nhotkey=CTRL+ALT+SHIFT+F12\ntrusted_terminal_input='+str(int(a.trusted_terminal))+
        '\ninput_method='+('clipboard' if a.clipboard else 'unicode')+'\n',encoding='ascii')
    trace=out/'trace.jsonl';env=dict(os.environ,HEBEN_DIAGNOSTICS_PATH=str(trace),HEBEN_PROBE_NO_HOTKEY='1')
    user=ctypes.WinDLL('user32',use_last_error=True)
    user.FindWindowW.argtypes=[wintypes.LPCWSTR,wintypes.LPCWSTR];user.FindWindowW.restype=wintypes.HWND
    user.GetForegroundWindow.restype=wintypes.HWND
    user.GetClipboardSequenceNumber.restype=wintypes.DWORD
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user.IsWindowVisible.argtypes=[wintypes.HWND];user.IsWindowVisible.restype=wintypes.BOOL
    user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM];user.PostMessageW.restype=wintypes.BOOL
    if user.FindWindowW('HebENFixedInputProbeHiddenClass',None):raise RuntimeError('Another probe is already running; no existing process was stopped')
    before_foreground=user.GetForegroundWindow();before_clipboard=user.GetClipboardSequenceNumber()
    process=subprocess.Popen([str(exe)],cwd=out,env=env,creationflags=subprocess.CREATE_NO_WINDOW)
    hwnd=None;result={'status':'FAIL','scope':'disarmed probe and synthetic message only; no SendInput, microphone, real hotkey or editor',
                      'exe_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'trusted_terminal_config':a.trusted_terminal,'clipboard_config':a.clipboard}
    try:
        for _ in range(100):
            hwnd=user.FindWindowW('HebENFixedInputProbeHiddenClass',None);owner=wintypes.DWORD()
            if hwnd:user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
            if hwnd and owner.value==process.pid:break
            if process.poll() is not None:raise RuntimeError(f'Probe exited: {process.returncode}')
            time.sleep(.05)
        else:raise TimeoutError('Probe window missing')
        result['hidden_window']=not user.IsWindowVisible(hwnd)
        if not user.PostMessageW(hwnd,0x0312,1201,0):raise OSError(ctypes.get_last_error(),'PostMessage failed')
        deadline=time.monotonic()+3;events=[]
        while time.monotonic()<deadline:
            if trace.exists():events=[json.loads(line) for line in trace.read_text(encoding='utf8').splitlines()]
            if any(e['stage']=='probe' and e['result']=='disarmed_hotkey_ignored' for e in events):break
            time.sleep(.05)
        result['registration_disabled']=any(e['stage']=='hotkey_register' and e['result']=='disabled_for_smoke' for e in events)
        result['synthetic_hotkey_ignored']=any(e['stage']=='probe' and e['result']=='disarmed_hotkey_ignored' for e in events)
        result['no_sendinput_attempt']=not any(e['stage']=='sendinput' for e in events)
        result['no_audio_start']=not any(e['stage'] in ('microphone','pcm','asr') for e in events)
        result['clipboard_sequence_unchanged']=user.GetClipboardSequenceNumber()==before_clipboard
        result['foreground_unchanged_at_observation']=user.GetForegroundWindow()==before_foreground
        required=('hidden_window','registration_disabled','synthetic_hotkey_ignored','no_sendinput_attempt','no_audio_start','clipboard_sequence_unchanged')
        result['status']='PASS_DISARMED_SMOKE' if all(result[x] for x in required) else 'FAIL'
    except Exception as exc:result['error']=str(exc)
    finally:
        if hwnd:
            owner=wintypes.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
            if owner.value==process.pid:user.PostMessageW(hwnd,0x10,0,0)
        try:process.wait(timeout=10)
        except subprocess.TimeoutExpired:result.update(shutdown='TIMEOUT_process_left_running',pid=process.pid)
        else:result['exit_code']=process.returncode
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print(json.dumps(result))
    if result['status']!='PASS_DISARMED_SMOKE':raise SystemExit(1)


if __name__=='__main__':main()

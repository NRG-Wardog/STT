"""Exercise input opt-ins in an isolated fixture; no hotkey, audio or clipboard writes."""
import argparse
import configparser
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
    p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
    exe=out/'HebENReplayFixture.exe';shutil.copyfile(a.exe,exe)
    ini=out/'config.ini';ini.write_text('[General]\ntrusted_terminal_input=0\ninput_method=unicode\nkeep_sentinel=unchanged\n',encoding='ascii')
    env=dict(os.environ,HEBEN_DIAGNOSTICS_PATH=str(out/'trace.jsonl'))
    user=ctypes.WinDLL('user32',use_last_error=True)
    user.FindWindowW.argtypes=[wintypes.LPCWSTR,wintypes.LPCWSTR];user.FindWindowW.restype=wintypes.HWND
    user.GetForegroundWindow.restype=wintypes.HWND;user.GetClipboardSequenceNumber.restype=wintypes.DWORD
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user.SendMessageTimeoutW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM,wintypes.UINT,wintypes.UINT,ctypes.POINTER(ctypes.c_size_t)];user.SendMessageTimeoutW.restype=ctypes.c_void_p
    user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
    if user.FindWindowW('HebENReplayFixtureHiddenClass',None):raise RuntimeError('Another replay fixture is active')
    beforeFg=user.GetForegroundWindow();beforeClip=user.GetClipboardSequenceNumber()
    proc=subprocess.Popen([str(exe)],cwd=out,env=env,creationflags=subprocess.CREATE_NO_WINDOW);hwnd=None
    result={'status':'FAIL','scope':'test-only configuration messages; no hotkey registration, microphone, SendInput or clipboard writes',
            'exe_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()}
    def send(command):
        answer=ctypes.c_size_t()
        if not user.SendMessageTimeoutW(hwnd,0x0111,command,0,2,10000,ctypes.byref(answer)):raise OSError(ctypes.get_last_error(),'WM_COMMAND failed')
    def settings():
        q=configparser.ConfigParser();q.read(ini,encoding='ascii');return q['General']
    try:
        for _ in range(100):
            hwnd=user.FindWindowW('HebENReplayFixtureHiddenClass',None);owner=wintypes.DWORD()
            if hwnd:user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
            if hwnd and owner.value==proc.pid:break
            if proc.poll() is not None:raise RuntimeError(f'Fixture exited: {proc.returncode}')
            time.sleep(.05)
        else:raise TimeoutError('Fixture window missing')
        send(2013);result['terminal_on']=settings().get('trusted_terminal_input')=='1'
        send(2015);result['clipboard_on']=settings().get('input_method')=='clipboard'
        send(2013);result['terminal_off']=settings().get('trusted_terminal_input')=='0'
        send(2014);result['unicode_on']=settings().get('input_method')=='unicode'
        result['unrelated_setting_preserved']=settings().get('keep_sentinel')=='unchanged'
        events=[json.loads(s) for s in (out/'trace.jsonl').read_text(encoding='utf8').splitlines()]
        result['no_input_or_clipboard_calls']=not any(e['stage'] in ('sendinput','clipboard_paste','microphone','pcm','asr') for e in events)
        result['clipboard_sequence_unchanged']=user.GetClipboardSequenceNumber()==beforeClip
        result['foreground_unchanged_at_observation']=user.GetForegroundWindow()==beforeFg
        checks=('terminal_on','clipboard_on','terminal_off','unicode_on','unrelated_setting_preserved','no_input_or_clipboard_calls','clipboard_sequence_unchanged')
        result['status']='PASS_TEST_ONLY_SETTINGS' if all(result[k] for k in checks) else 'FAIL'
    except Exception as exc:result['error']=str(exc)
    finally:
        if hwnd:
            owner=wintypes.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
            if owner.value==proc.pid:user.PostMessageW(hwnd,0x10,0,0)
        try:proc.wait(timeout=10)
        except subprocess.TimeoutExpired:result.update(shutdown='TIMEOUT_process_left_running',pid=proc.pid)
        else:result['exit_code']=proc.returncode
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print(json.dumps(result))
    if result['status']!='PASS_TEST_ONLY_SETTINGS':raise SystemExit(1)


if __name__=='__main__':main()

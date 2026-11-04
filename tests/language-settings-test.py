"""Test tray language commands in the isolated, no-microphone fixture."""
import argparse
import configparser
import ctypes
from ctypes import wintypes
import json
import os
import pathlib
import shutil
import subprocess
import time

def main():
    p=argparse.ArgumentParser();p.add_argument('--exe',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True);a=p.parse_args()
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);exe=out/'HebENReplayFixture.exe';shutil.copyfile(a.exe,exe)
    ini=out/'config.ini';ini.write_text('[General]\nlanguage=auto\nsentinel=keep\n',encoding='ascii')
    env=os.environ.copy();env['HEBEN_DIAGNOSTICS_PATH']=str(out/'trace.jsonl')
    user=ctypes.WinDLL('user32',use_last_error=True)
    user.FindWindowW.argtypes=[wintypes.LPCWSTR,wintypes.LPCWSTR];user.FindWindowW.restype=wintypes.HWND
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user.SendMessageTimeoutW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM,wintypes.UINT,wintypes.UINT,ctypes.POINTER(ctypes.c_size_t)];user.SendMessageTimeoutW.restype=ctypes.c_void_p
    user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
    proc=subprocess.Popen([str(exe)],cwd=str(out),env=env,creationflags=subprocess.CREATE_NO_WINDOW);hwnd=None
    result={'status':'FAIL','mode':'isolated settings commands; no microphone or desktop input','checks':{}}
    try:
        for _ in range(100):
            hwnd=user.FindWindowW('HebENReplayFixtureHiddenClass',None);owner=wintypes.DWORD()
            if hwnd:user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
            if hwnd and owner.value==proc.pid:break
            if proc.poll() is not None:raise RuntimeError('Fixture exited')
            time.sleep(.1)
        else:raise TimeoutError('Fixture window missing')
        for name,command in [('he',2011),('en',2012),('auto',2010)]:
            answer=ctypes.c_size_t()
            if not user.SendMessageTimeoutW(hwnd,0x111,command,0,2,10000,ctypes.byref(answer)):raise RuntimeError('Settings message failed')
            parser=configparser.ConfigParser();parser.read(ini,encoding='utf8')
            result['checks'][name]=(parser['General']['language']==name and parser['General']['sentinel']=='keep')
        result['status']='PASS' if all(result['checks'].values()) else 'FAIL'
    except Exception as exc:result['error']=str(exc)
    finally:
        if hwnd:user.PostMessageW(hwnd,0x10,0,0)
        try:proc.wait(timeout=10)
        except subprocess.TimeoutExpired:result['shutdown']='TIMEOUT_process_left_running'
        else:result['exit_code']=proc.returncode
        (out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    print(json.dumps(result))
    if result['status']!='PASS':raise SystemExit(1)

if __name__=='__main__':main()

"""Test session-bound stop messages in the isolated fixture, without a microphone."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
import pathlib
import shutil
import subprocess
import time

def main():
    p=argparse.ArgumentParser()
    for name in ('exe','model','vad','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
    exe=out/'HebENReplayFixture.exe';shutil.copyfile(a.exe,exe)
    (out/'config.ini').write_text('[General]\nmodel='+str(a.model.resolve())+'\nvad_model='+str(a.vad.resolve())+'\nvad_backend=silero\nbackend=cpu\n',encoding='utf8')
    env=os.environ.copy();env['HEBEN_DIAGNOSTICS_PATH']=str(out/'trace.jsonl')
    user=ctypes.WinDLL('user32',use_last_error=True)
    user.FindWindowW.argtypes=[wintypes.LPCWSTR,wintypes.LPCWSTR];user.FindWindowW.restype=wintypes.HWND
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user.SendMessageTimeoutW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM,wintypes.UINT,wintypes.UINT,ctypes.POINTER(ctypes.c_size_t)];user.SendMessageTimeoutW.restype=ctypes.c_void_p
    user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
    proc=subprocess.Popen([str(exe)],cwd=str(out),env=env,creationflags=subprocess.CREATE_NO_WINDOW);hwnd=None
    result={'status':'FAIL','mode':'isolated synthetic stop messages; no physical hotkey, microphone or editor'}
    def send(msg,wp=0):
        answer=ctypes.c_size_t()
        if not user.SendMessageTimeoutW(hwnd,msg,wp,0,2,15000,ctypes.byref(answer)):raise OSError(ctypes.get_last_error(),'message failed')
        return answer.value
    try:
        for _ in range(100):
            hwnd=user.FindWindowW('HebENReplayFixtureHiddenClass',None);owner=wintypes.DWORD()
            if hwnd:user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
            if hwnd and owner.value==proc.pid:break
            if proc.poll() is not None:raise RuntimeError('Fixture exited')
            time.sleep(.1)
        else:raise TimeoutError('Fixture window missing')
        if send(0x8000+40)!=1:raise RuntimeError('First start failed')
        first=send(0x8000+44)
        send(0x8000+4,first)
        result['focus_stop']=send(0x8000+42)==0
        if send(0x8000+40)!=1:raise RuntimeError('Second start failed')
        second=send(0x8000+44)
        send(0x8000+5,first);send(0x8000+4,first);send(0x8000+6,first)
        result['stale_messages_ignored']=second!=first and send(0x8000+42)==1
        send(0x0312,1201)
        result['synthetic_hotkey_stop']=send(0x8000+42)==0
        if send(0x8000+40)!=1:raise RuntimeError('Third start failed')
        third=send(0x8000+44);send(0x8000+6,third)
        result['current_vad_failure_stop']=send(0x8000+42)==0
        result['status']='PASS_SIMULATED_STOP_EVENTS' if all(result[x] for x in ('focus_stop','stale_messages_ignored','synthetic_hotkey_stop','current_vad_failure_stop')) else 'FAIL'
    except Exception as exc:result['error']=str(exc)
    finally:
        if hwnd:user.PostMessageW(hwnd,0x10,0,0)
        try:proc.wait(timeout=25)
        except subprocess.TimeoutExpired:result['shutdown']='TIMEOUT_process_left_running'
        else:result['exit_code']=proc.returncode
        (out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    print(json.dumps(result))
    if result['status']!='PASS_SIMULATED_STOP_EVENTS':raise SystemExit(1)

if __name__=='__main__':main()

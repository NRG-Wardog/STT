"""100 isolated app-worker cycles using synthetic silence, never microphone input."""
import argparse
import array
import ctypes
from ctypes import wintypes
import csv
import json
import os
import pathlib
import shutil
import subprocess
import time
import psutil

class COPYDATA(ctypes.Structure):
    _fields_=[('dwData',ctypes.c_size_t),('cbData',wintypes.DWORD),('lpData',ctypes.c_void_p)]

def main():
    p=argparse.ArgumentParser()
    for name in ('exe','model','vad','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--cycles',type=int,default=100);a=p.parse_args()
    if not 1<=a.cycles<=1000:raise ValueError('Invalid cycle count')
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
    exe=out/'HebENReplayFixture.exe';shutil.copyfile(a.exe,exe)
    (out/'config.ini').write_text('[General]\nmodel='+str(a.model.resolve())+'\nvad_model='+str(a.vad.resolve())+'\nvad_backend=silero\nbackend=cpu\nlanguage=auto\nthreads=4\nsilence_ms=1200\npreroll_ms=600\n',encoding='utf8')
    env=os.environ.copy();env['HEBEN_DIAGNOSTICS_PATH']=str(out/'trace.jsonl');env['HEBEN_TEST_TRANSCRIPT']=str(out/'unexpected-transcript.txt')
    user=ctypes.WinDLL('user32',use_last_error=True)
    user.FindWindowW.argtypes=[wintypes.LPCWSTR,wintypes.LPCWSTR];user.FindWindowW.restype=wintypes.HWND
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user.SendMessageTimeoutW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM,wintypes.UINT,wintypes.UINT,ctypes.POINTER(ctypes.c_size_t)];user.SendMessageTimeoutW.restype=ctypes.c_void_p
    user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
    process=subprocess.Popen([str(exe)],cwd=str(out),env=env,creationflags=subprocess.CREATE_NO_WINDOW);observed=psutil.Process(process.pid)
    hwnd=None;rows=[];result={'status':'FAIL','mode':'synthetic silence, isolated worker; no physical microphone, hotkey or editor','requested_cycles':a.cycles}
    def send(msg,lp=0):
        answer=ctypes.c_size_t()
        if not user.SendMessageTimeoutW(hwnd,msg,0,lp,2,15000,ctypes.byref(answer)):raise OSError(ctypes.get_last_error(),'message failed')
        return answer.value
    try:
        for _ in range(100):
            hwnd=user.FindWindowW('HebENReplayFixtureHiddenClass',None);owner=wintypes.DWORD()
            if hwnd:user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
            if hwnd and owner.value==process.pid:break
            if process.poll() is not None:raise RuntimeError('Fixture exited on launch')
            time.sleep(.1)
        else:raise TimeoutError('Fixture window missing')
        chunk=array.array('f',[0.0]*512).tobytes();buffer=ctypes.create_string_buffer(chunk)
        data=COPYDATA(0x48455043,len(chunk),ctypes.cast(buffer,ctypes.c_void_p))
        for cycle in range(1,a.cycles+1):
            if send(0x8000+40)!=1:raise RuntimeError(f'start failed at {cycle}')
            for _ in range(32):
                if send(0x004A,ctypes.addressof(data))!=1:raise RuntimeError(f'PCM rejected at {cycle}')
                time.sleep(.001)
            if send(0x8000+41)!=1:raise RuntimeError(f'stop failed at {cycle}')
            m=observed.memory_full_info();cpu=observed.cpu_times()
            rows.append({'cycle':cycle,'utc':time.time(),'private_bytes':m.private,'working_set_bytes':m.rss,
                         'handles':observed.num_handles(),'threads':observed.num_threads(),'cpu_seconds':cpu.user+cpu.system})
        time.sleep(1)
        trace=[json.loads(s) for s in (out/'trace.jsonl').read_text(encoding='utf8').splitlines()]
        unexpected=[x for x in trace if x['stage'] in ('segment','asr_text') and x['result'] in ('emitted','produced')]
        result.update(completed_cycles=len(rows),unexpected_speech_events=len(unexpected),
                      transcript_created=(out/'unexpected-transcript.txt').exists())
        result['status']='PASS_SIMULATED_CYCLES' if len(rows)==a.cycles and not unexpected and not result['transcript_created'] else 'FAIL'
    except Exception as exc:result['error']=str(exc)
    finally:
        if rows:
            with (out/'cycles.csv').open('w',newline='',encoding='utf8') as f:
                writer=csv.DictWriter(f,fieldnames=rows[0].keys());writer.writeheader();writer.writerows(rows)
        if hwnd:user.PostMessageW(hwnd,0x0010,0,0)
        try:process.wait(timeout=25)
        except subprocess.TimeoutExpired:result['shutdown']='TIMED_OUT_process_left_running';result['pid']=process.pid
        else:result['exit_code']=process.returncode
        (out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    print(json.dumps(result))
    if result['status']!='PASS_SIMULATED_CYCLES':raise SystemExit(1)

if __name__=='__main__':main()

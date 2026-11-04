"""Repeated prerecorded speech through the actual isolated app worker.

No microphone, physical hotkey, foreground input or editor is exercised.
"""
import argparse
import array
import csv
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import unicodedata
import wave
import psutil


class CopyData(ctypes.Structure):
    _fields_ = [('dwData', ctypes.c_size_t), ('cbData', wintypes.DWORD), ('lpData', ctypes.c_void_p)]


def normalize(s):
    return ' '.join(''.join(' ' if unicodedata.category(c).startswith('P') else c
                            for c in unicodedata.normalize('NFKC', s).casefold()).split())


def main():
    p = argparse.ArgumentParser()
    for name in ('exe', 'model', 'hebrew-model', 'vad', 'wav', 'reference', 'out'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--cycles', type=int, default=25)
    p.add_argument('--backend', choices=('cpu', 'cuda'), default='cuda')
    a = p.parse_args()
    if not 1 <= a.cycles <= 1000:
        raise ValueError('cycles must be 1..1000')
    out = a.out.resolve(); out.mkdir(parents=True, exist_ok=False)
    exe = out / 'HebENReplayFixture.exe'; shutil.copyfile(a.exe, exe)
    (out / 'config.ini').write_text('[General]\nmodel='+str(a.model.resolve())+'\nhebrew_model='+str(a.hebrew_model.resolve())+
        '\nhybrid=1\nvad_model='+str(a.vad.resolve())+'\nvad_backend=silero\nbackend='+a.backend+
        '\nlanguage=auto\nthreads=4\nsilence_ms=1200\npreroll_ms=600\nmodel_idle_seconds=10\n', encoding='utf8')
    with wave.open(str(a.wav), 'rb') as w:
        if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != (1, 2, 16000):
            raise ValueError('Expected mono PCM16 16 kHz WAV')
        pcm = array.array('h'); pcm.frombytes(w.readframes(w.getnframes()))
    expected = normalize(json.loads(a.reference.read_text(encoding='utf8'))['reference'])
    env = dict(os.environ, HEBEN_DIAGNOSTICS_PATH=str(out/'trace.jsonl'), HEBEN_TEST_TRANSCRIPT=str(out/'transcript.txt'))
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]; user.FindWindowW.restype = wintypes.HWND
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.SendMessageTimeoutW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM, wintypes.UINT, wintypes.UINT, ctypes.POINTER(ctypes.c_size_t)]
    user.SendMessageTimeoutW.restype = ctypes.c_void_p
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    process = subprocess.Popen([str(exe)], cwd=out, env=env, creationflags=subprocess.CREATE_NO_WINDOW)
    observed = psutil.Process(process.pid); hwnd = None; rows = []
    result = {'status':'FAIL', 'mode':'prerecorded speech through isolated worker; no microphone, physical hotkey or editor', 'requested_cycles':a.cycles}
    def send(msg, lp=0):
        value = ctypes.c_size_t()
        if not user.SendMessageTimeoutW(hwnd, msg, 0, lp, 2, 15000, ctypes.byref(value)):
            raise OSError(ctypes.get_last_error(), 'message failed')
        return value.value
    try:
        for _ in range(100):
            hwnd = user.FindWindowW('HebENReplayFixtureHiddenClass', None); owner = wintypes.DWORD()
            if hwnd: user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            if hwnd and owner.value == process.pid: break
            if process.poll() is not None: raise RuntimeError('Fixture exited on launch')
            time.sleep(.1)
        else: raise TimeoutError('Fixture window missing')
        for cycle in range(1, a.cycles+1):
            start = time.perf_counter()
            if send(0x8000+40) != 1: raise RuntimeError(f'start failed at {cycle}')
            for pos in range(0, len(pcm), 512):
                chunk = array.array('f', (v/32768.0 for v in pcm[pos:pos+512])).tobytes()
                buffer = ctypes.create_string_buffer(chunk)
                data = CopyData(0x48455043, len(chunk), ctypes.cast(buffer, ctypes.c_void_p))
                if send(0x004A, ctypes.addressof(data)) != 1: raise RuntimeError(f'PCM rejected at {cycle}')
                time.sleep(.002)
            if send(0x8000+41) != 1: raise RuntimeError(f'stop failed at {cycle}')
            target = out / ('transcript.txt' if cycle == 1 else f'transcript.txt.{cycle}')
            deadline = time.monotonic()+90
            while time.monotonic() < deadline and (not target.exists() or send(0x8000+43) != 0):
                if process.poll() is not None: raise RuntimeError('Fixture exited during inference')
                time.sleep(.05)
            if not target.exists() or send(0x8000+43) != 0: raise TimeoutError(f'cycle {cycle} not complete')
            mem = observed.memory_info(); cpu = observed.cpu_times()
            rows.append({'cycle':cycle, 'elapsed_seconds':time.perf_counter()-start,
                         'reference_match':normalize(target.read_text(encoding='utf8')) == expected,
                         'private_bytes':mem.private, 'working_set_bytes':mem.rss,
                         'handles':observed.num_handles(), 'threads':observed.num_threads(), 'cpu_seconds':cpu.user+cpu.system})
        result.update(completed_cycles=len(rows), matching_references=sum(r['reference_match'] for r in rows))
        result['status']='PASS_REPLAY_CYCLES' if len(rows)==a.cycles and all(r['reference_match'] for r in rows) else 'FAIL'
    except Exception as exc:
        result['error'] = str(exc)
    finally:
        if rows:
            with (out/'cycles.csv').open('x', newline='', encoding='utf8') as f:
                w=csv.DictWriter(f,fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
        if hwnd: user.PostMessageW(hwnd, 0x10, 0, 0)
        try: process.wait(timeout=25)
        except subprocess.TimeoutExpired: result.update(shutdown='TIMED_OUT_process_left_running', pid=process.pid)
        else: result['exit_code']=process.returncode
        (out/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf8')
    print(json.dumps(result))
    if result['status'] != 'PASS_REPLAY_CYCLES': raise SystemExit(1)


if __name__ == '__main__':
    main()

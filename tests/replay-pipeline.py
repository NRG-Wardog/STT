"""Exercise app VAD/queue/ASR with prerecorded PCM, without desktop input.

Requires the HEBEN_HEADLESS_TEST executable. This is not a microphone,
global-hotkey, Windows SendInput, or target-editor test.
"""
import argparse
import array
import ctypes
import csv
from ctypes import wintypes
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import threading
import time
import unicodedata
import wave

ROOT=pathlib.Path(__file__).resolve().parent.parent
class COPYDATA(ctypes.Structure):
    _fields_=[('dwData',ctypes.c_size_t),('cbData',wintypes.DWORD),('lpData',ctypes.c_void_p)]

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for data in iter(lambda:f.read(1048576),b''):h.update(data)
    return h.hexdigest()

def normalize(text):
    text=unicodedata.normalize('NFKC',text).casefold()
    return ' '.join(''.join(' ' if unicodedata.category(ch).startswith('P') else ch for ch in text).split())

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--exe',type=pathlib.Path,required=True)
    ap.add_argument('--wav',type=pathlib.Path,required=True)
    ap.add_argument('--model',type=pathlib.Path,required=True)
    ap.add_argument('--vad',type=pathlib.Path,required=True)
    ap.add_argument('--out',type=pathlib.Path,required=True)
    ap.add_argument('--backend',choices=['cpu','cuda'],default='cpu')
    ap.add_argument('--language',choices=['auto','he','en'],default='auto')
    ap.add_argument('--silence-ms',type=int,default=1200,choices=range(250,5001),metavar='250..5000')
    ap.add_argument('--hebrew-fallback',action='store_true',help='Experimentally retry non-Hebrew/non-English language detections in Hebrew')
    ap.add_argument('--hebrew-model',type=pathlib.Path,help='Enable experimental two-model routing using this model')
    ap.add_argument('--runtime-dir',type=pathlib.Path,help='Copy cuBLAS DLLs from this package and remove CUDA paths for the child')
    ap.add_argument('--observe-idle-seconds',type=int,default=0,help='Keep fixture alive after inference and sample its process (requires psutil)')
    ap.add_argument('--pace-ms',type=float,default=2,help='Delay after each 512-sample block; 32 ms is real-time pacing')
    ap.add_argument('--streaming-prototype',action='store_true',help='Enable experimental rolling-prefix inference')
    ap.add_argument('--streaming-agreement',type=int,default=2,choices=range(2,9))
    ap.add_argument('--streaming-hold-words',type=int,default=1,choices=range(1,6))
    ap.add_argument('--model-idle-seconds',type=int,default=5,choices=range(5,3601))
    ap.add_argument('--startup-wait-seconds',type=float,default=0,help='Observe fixture idle before capture begins')
    ap.add_argument('--skip-model-warm-ablation',action='store_true',help='Test-only silence ablation, not the packaged app')
    ap.add_argument('--warm-on-voice',action='store_true',help='Experimental candidate warm-up after voice onset')
    ap.add_argument('--vad-delay-ms',type=int,default=0,help='Test-only simulated initial VAD worker stall')
    ap.add_argument('--auto-silence',action='store_true',help='Feed synthetic silence and verify automatic stop, with no transcript expected')
    ap.add_argument('--auto-voice',action='store_true',help='Verify speech resets the silence deadline during simulated replay')
    ap.add_argument('--expect-overflow',action='store_true',help='Verify the fixture stops recording on a deliberately induced VAD buffer overflow')
    ap.add_argument('--expect-asr-error',action='store_true',help='Verify a broken Hebrew model reports an error and emits no transcript')
    ap.add_argument('--assert-active-before',type=int,default=0,help='Frame position at which an auto-voice replay must still be active')
    ap.add_argument('--reference-sequence',type=pathlib.Path,help='JSON array of expected transcripts from a controlled multi-phrase fixture')
    ap.add_argument('--reference-json',type=pathlib.Path,help='JSON with a reference field; use only a verified fixture reference')
    args=ap.parse_args()
    if not 0<=args.observe_idle_seconds<=60:raise ValueError('observe-idle-seconds must be 0..60')
    if not 0<=args.pace_ms<=100:raise ValueError('pace-ms must be 0..100')
    if not 0<=args.vad_delay_ms<=9000:raise ValueError('vad-delay-ms must be 0..9000')
    if not 0<=args.startup_wait_seconds<=30:raise ValueError('startup-wait-seconds must be 0..30')
    if args.observe_idle_seconds:
        import psutil
    out=args.out.resolve()
    out.mkdir(parents=True,exist_ok=False)
    exe=out/'HebENReplayFixture.exe';shutil.copyfile(args.exe,exe)
    if args.runtime_dir:
        for name in ('cublas64_13.dll','cublasLt64_13.dll'):
            shutil.copyfile(args.runtime_dir/name,out/name)
    (out/'config.ini').write_text('[General]\nmodel='+str(args.model.resolve())+'\nvad_model='+str(args.vad.resolve())+
        '\nvad_backend=silero\nbackend='+args.backend+'\nlanguage='+args.language+'\nthreads=4\nauto_hebrew_fallback='+str(int(args.hebrew_fallback))+
        '\nhybrid='+str(int(args.hebrew_model is not None))+'\nhebrew_model='+(str(args.hebrew_model.resolve()) if args.hebrew_model else '')+
        '\nsilence_ms='+str(args.silence_ms)+'\npreroll_ms=600\nmodel_idle_seconds='+str(args.model_idle_seconds)+
        '\nstreaming_prototype='+str(int(args.streaming_prototype))+'\nstreaming_interval_ms=1000\nstreaming_agreement='+str(args.streaming_agreement)+
        '\nstreaming_hold_words='+str(args.streaming_hold_words)+'\nmodel_warm_on_voice='+str(int(args.warm_on_voice))+'\n',encoding='utf8')
    with wave.open(str(args.wav),'rb') as w:
        if (w.getnchannels(),w.getsampwidth(),w.getframerate())!=(1,2,16000):raise ValueError('Expected mono 16 kHz PCM16 WAV')
        samples=array.array('h');samples.frombytes(w.readframes(w.getnframes()))
    if args.auto_silence and (len(samples)<6*16000 or any(samples)):
        raise ValueError('Auto-stop fixture must contain at least six seconds of zero PCM')
    if args.expect_overflow and (len(samples)<11*16000 or any(samples) or args.vad_delay_ms<1000):
        raise ValueError('Overflow fixture needs at least 11 seconds of zero PCM and a VAD delay')
    if args.auto_voice and not 0<args.assert_active_before<len(samples):raise ValueError('Provide a valid active-before frame')
    env=os.environ.copy();env['HEBEN_DIAGNOSTICS_PATH']=str(out/'trace.jsonl');env['HEBEN_TEST_TRANSCRIPT']=str(out/'transcript.txt')
    if args.skip_model_warm_ablation:env['HEBEN_TEST_SKIP_MODEL_WARM']='1'
    if args.vad_delay_ms:env['HEBEN_TEST_VAD_START_DELAY_MS']=str(args.vad_delay_ms)
    if args.runtime_dir:
        env['PATH']=str(pathlib.Path(os.environ['SystemRoot'])/'System32')
    result={'mode':'prerecorded PCM into isolated app worker; no microphone, hotkey, or editor','fixture_exe_sha256':sha(exe),'wav_sha256':sha(args.wav),
            'model_sha256':sha(args.model),'vad_sha256':sha(args.vad),'backend_requested':args.backend,'samples':len(samples),'status':'FAILED'}
    result.update(streaming_prototype=args.streaming_prototype,streaming_agreement=args.streaming_agreement,
                  skip_model_warm_ablation=args.skip_model_warm_ablation,model_warm_on_voice=args.warm_on_voice,model_idle_seconds=args.model_idle_seconds)
    result['pace_ms_per_512_samples']=args.pace_ms;result['simulated_vad_delay_ms']=args.vad_delay_ms
    if args.hebrew_model:result['hebrew_model_sha256']=sha(args.hebrew_model)
    if args.runtime_dir:result['runtime']='packaged cuBLAS DLLs in fixture directory; CUDA toolkit PATH removed for child'
    user32=ctypes.WinDLL('user32',use_last_error=True)
    user32.FindWindowW.argtypes=[wintypes.LPCWSTR,wintypes.LPCWSTR];user32.FindWindowW.restype=wintypes.HWND
    user32.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user32.SendMessageTimeoutW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM,wintypes.UINT,wintypes.UINT,ctypes.POINTER(ctypes.c_size_t)]
    user32.SendMessageTimeoutW.restype=ctypes.c_void_p
    user32.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
    process=subprocess.Popen([str(exe)],cwd=str(out),env=env,creationflags=subprocess.CREATE_NO_WINDOW)
    (out/'fixture-process.json').write_text(json.dumps({'pid':process.pid,'exe':str(exe)})+'\n',encoding='utf8')
    stop_sampling=threading.Event();resource_rows=[];sampler=None;runtime_modules={}
    if args.observe_idle_seconds:
        observed=psutil.Process(process.pid)
        def sample_resources():
            while not stop_sampling.is_set():
                try:
                    mem=observed.memory_info();cpu=observed.cpu_times()
                    resource_rows.append({'unix_time':time.time(),'pid':process.pid,'working_set_bytes':mem.rss,
                                          'private_bytes':getattr(mem,'private',None),
                                          'cpu_seconds':cpu.user+cpu.system,'threads':observed.num_threads(),
                                          'handles':observed.num_handles()})
                    if args.runtime_dir and len(runtime_modules)<2:
                        try:
                            for module in observed.memory_maps():
                                name=pathlib.Path(module.path).name.lower()
                                if name in ('cublas64_13.dll','cublaslt64_13.dll'):
                                    runtime_modules[name]='fixture_directory' if os.path.normcase(os.path.dirname(module.path))==os.path.normcase(str(out)) else 'other_location'
                        except (psutil.AccessDenied,NotImplementedError):
                            runtime_modules['inspection']='unavailable'
                except (psutil.NoSuchProcess,psutil.AccessDenied):break
                stop_sampling.wait(.5)
        sampler=threading.Thread(target=sample_resources,daemon=True);sampler.start()
    hwnd=None
    def send(msg,lp=0):
        answer=ctypes.c_size_t()
        if not user32.SendMessageTimeoutW(hwnd,msg,0,lp,2,15000,ctypes.byref(answer)):
            raise OSError(ctypes.get_last_error(),f'WM {msg} timed out or failed')
        return answer.value
    try:
        for _ in range(100):
            hwnd=user32.FindWindowW('HebENReplayFixtureHiddenClass',None)
            owner=wintypes.DWORD()
            if hwnd:user32.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
            if hwnd and owner.value==process.pid:break
            if process.poll() is not None:raise RuntimeError(f'Fixture exited: {process.returncode}')
            time.sleep(.1)
        else:raise RuntimeError('Fixture window unavailable')
        if args.startup_wait_seconds:time.sleep(args.startup_wait_seconds)
        if send(0x8000+40)!=1:raise RuntimeError('Replay start failed; see trace')
        start=time.perf_counter()
        delivered=0
        for offset in range(0,len(samples),512):
            chunk=array.array('f',(v/32768.0 for v in samples[offset:offset+512])).tobytes()
            buffer=ctypes.create_string_buffer(chunk)
            data=COPYDATA(0x48455043,len(chunk),ctypes.cast(buffer,ctypes.c_void_p))
            if send(0x004A,ctypes.addressof(data))!=1:
                if (args.auto_silence or args.auto_voice or args.expect_overflow) and send(0x8000+42)==0:break
                raise RuntimeError(f'PCM rejected at frame {offset}')
            delivered+=len(chunk)//4
            if args.auto_voice and 'active_before_threshold' not in result and delivered>=args.assert_active_before:
                time.sleep(.2)
                result['active_before_threshold']=send(0x8000+42)==1
                if not result['active_before_threshold']:raise RuntimeError('Replay stopped before the post-speech silence threshold')
            time.sleep(args.pace_ms/1000)  # no physical microphone involved
        if args.auto_silence or args.auto_voice or args.expect_overflow:
            deadline=time.monotonic()+10
            while time.monotonic()<deadline and send(0x8000+42)!=0:time.sleep(.05)
            stopped=send(0x8000+42)==0
            result.update(delivered_frames=delivered,auto_stopped=stopped,transcript_created=(out/'transcript.txt').exists())
            events=[json.loads(line) for line in (out/'trace.jsonl').read_text(encoding='utf8').splitlines()]
            result['stop_event_logged']=any(e['stage']=='capture' and e['result']=='stopped_after_5s_silence' for e in events)
            if args.expect_overflow:
                overflow=any(e['stage']=='vad' and e['result']=='audio_overflow' for e in events)
                failure_stop=any(e['stage']=='capture' and e['result']=='stopped_after_vad_failure' for e in events)
                result.update(overflow_logged=overflow,failure_stop_logged=failure_stop)
                result['status']='PASS_SIMULATED_OVERFLOW_STOP' if stopped and overflow and failure_stop and not result['transcript_created'] else 'FAIL_OVERFLOW_STOP'
            elif args.auto_silence:
                result['status']='PASS_SIMULATED_AUTO_STOP' if stopped and result['stop_event_logged'] and not result['transcript_created'] else 'FAIL_AUTO_STOP'
            else:
                result['status']='PASS_SIMULATED_VOICE_RESET' if stopped and result['stop_event_logged'] and result.get('active_before_threshold') else 'FAIL_VOICE_RESET'
                if args.reference_sequence and result['status']=='PASS_SIMULATED_VOICE_RESET':
                    expected=json.loads(args.reference_sequence.read_text(encoding='utf8'))
                    paths=[out/'transcript.txt']+[out/('transcript.txt.'+str(i)) for i in range(2,len(expected)+1)]
                    deadline=time.monotonic()+90
                    while time.monotonic()<deadline and not all(p.exists() for p in paths):
                        if process.poll() is not None:raise RuntimeError('Fixture exited before all transcripts')
                        time.sleep(.1)
                    while time.monotonic()<deadline and send(0x8000+43)!=0:time.sleep(.1)
                    if send(0x8000+43)!=0:raise TimeoutError('Inference did not finish')
                    actual=[p.read_text(encoding='utf8') for p in paths if p.exists()]
                    result['transcript_created']=(out/'transcript.txt').exists()
                    result['transcripts']=actual
                    extra=out/('transcript.txt.'+str(len(expected)+1))
                    result['unexpected_additional_segment']=extra.exists()
                    result['sequence_matches']=not extra.exists() and len(actual)==len(expected) and all(normalize(x)==normalize(y) for x,y in zip(actual,expected))
                    result['status']='PASS_SIMULATED_MULTI_PHRASE' if result['sequence_matches'] else 'FAIL_MULTI_PHRASE'
        elif args.expect_asr_error:
            if send(0x8000+41)!=1:raise RuntimeError('Replay stop failed')
            deadline=time.monotonic()+90
            while time.monotonic()<deadline and send(0x8000+43)!=0:time.sleep(.05)
            if send(0x8000+43)!=0:raise TimeoutError('Inference did not finish')
            events=[json.loads(line) for line in (out/'trace.jsonl').read_text(encoding='utf8').splitlines()]
            result['model_failure_logged']=any(e['stage']=='asr_hybrid_model' and e['result']=='failed' for e in events)
            result['error_reported']=any(e['stage']=='test_fixture' and e['result']=='error' for e in events)
            result['transcript_created']=(out/'transcript.txt').exists()
            result['status']='PASS_EXPECTED_ASR_ERROR' if result['model_failure_logged'] and result['error_reported'] and not result['transcript_created'] else 'FAIL_ASR_ERROR'
        else:
            if send(0x8000+41)!=1:raise RuntimeError('Replay stop failed')
            result['feed_seconds']=time.perf_counter()-start
            deadline=time.monotonic()+90
            transcript=out/'transcript.txt'
            while time.monotonic()<deadline and not transcript.exists():
                if process.poll() is not None:raise RuntimeError(f'Fixture exited: {process.returncode}')
                time.sleep(.2)
            if not transcript.exists():raise TimeoutError('No transcript produced; inspect trace')
            result['transcript']=transcript.read_text(encoding='utf8')
            if args.reference_json:
                reference=json.loads(args.reference_json.read_text(encoding='utf8'))['reference']
                result['reference_matches_after_normalization']=normalize(reference)==normalize(result['transcript'])
                result['status']='PASS_REPLAY_ONLY' if result['reference_matches_after_normalization'] else 'FAIL_REFERENCE_MISMATCH'
            else:result['status']='OUTPUT_ONLY_UNSCORED'
        if args.observe_idle_seconds:
            result['resource_sampling']={'interval_seconds':.5,'post_inference_hold_seconds':args.observe_idle_seconds,
                                         'model_idle_seconds':args.model_idle_seconds,'gpu_counters':'not measured by this sampler'}
            time.sleep(args.observe_idle_seconds)
    except Exception as exc:
        result['error']=str(exc)
    finally:
        stop_sampling.set()
        if sampler:
            sampler.join(timeout=3)
            if args.runtime_dir:result['runtime_modules_observed']=runtime_modules or {'status':'not_observed'}
            if resource_rows:
                with (out/'process-resources.csv').open('x',newline='',encoding='utf8') as f:
                    writer=csv.DictWriter(f,fieldnames=list(resource_rows[0]));writer.writeheader();writer.writerows(resource_rows)
        if hwnd:user32.PostMessageW(hwnd,0x0010,0,0)
        try:process.wait(timeout=25)
        except subprocess.TimeoutExpired:result['shutdown']='TIMED_OUT_process_left_running';result['pid']=process.pid
        else:result['exit_code']=process.returncode
        (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps(result,ensure_ascii=True))
    if result['status'] not in ('PASS_REPLAY_ONLY','OUTPUT_ONLY_UNSCORED','PASS_SIMULATED_AUTO_STOP','PASS_SIMULATED_VOICE_RESET','PASS_SIMULATED_MULTI_PHRASE','PASS_SIMULATED_OVERFLOW_STOP','PASS_EXPECTED_ASR_ERROR'):raise SystemExit(1)

if __name__=='__main__':main()

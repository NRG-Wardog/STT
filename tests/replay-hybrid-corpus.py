"""Score the real two-model app worker on prerecorded corpus WAVs.

No microphone, physical hotkey, editor, or real-time latency is tested.
"""
import argparse
import array
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import wave
import psutil
from importlib.util import module_from_spec, spec_from_file_location

spec = spec_from_file_location('compare_asr', Path(__file__).with_name('compare-asr-runs.py'))
comparison = module_from_spec(spec); spec.loader.exec_module(comparison)


class CopyData(ctypes.Structure):
    _fields_ = [('dwData', ctypes.c_size_t), ('cbData', wintypes.DWORD), ('lpData', ctypes.c_void_p)]


def main():
    p = argparse.ArgumentParser()
    for name in ('exe', 'model', 'hebrew-model', 'vad', 'corpus', 'audio-dir', 'out'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--pace-ms', type=float, default=5)
    p.add_argument('--backend', choices=('cpu','cuda'), default='cuda')
    p.add_argument('--language', choices=('auto','he','en'), default='auto')
    p.add_argument('--disable-hybrid',action='store_true',help='Use only the multilingual model for paired comparison')
    p.add_argument('--max-phrase-seconds',type=int,default=15,choices=range(3,26),help='Detector hard segment limit; shorter is not word-level streaming')
    p.add_argument('--silence-ms',type=int,default=1200,choices=range(250,5001),help='Detector pause needed to end a segment')
    p.add_argument('--streaming-prototype',action='store_true',help='Experimental repeated-prefix inference; no real editor or microphone')
    p.add_argument('--streaming-interval-ms',type=int,default=1000,choices=range(500,3001))
    p.add_argument('--streaming-agreement',type=int,default=4,choices=range(2,9))
    p.add_argument('--streaming-hold-words',type=int,default=2,choices=range(1,6))
    p.add_argument('--warm-on-voice',action='store_true',help='Experimental candidate: request model warm-up after voice onset')
    p.add_argument('--initial-quiet-ms',type=int,default=0,choices=range(0,4001),help='Prepend known silence to each clip')
    p.add_argument('--model-idle-seconds',type=int,default=10,choices=range(5,3601))
    p.add_argument('--cold-between-clips',action='store_true',help='Wait for model unload between clips')
    a = p.parse_args()
    if not 0 <= a.pace_ms <= 100: raise ValueError('pace-ms must be 0..100')
    corpus = json.loads(a.corpus.read_text(encoding='utf8'))
    if not corpus or len({r['id'] for r in corpus}) != len(corpus): raise ValueError('Empty or duplicate corpus')
    out = a.out.resolve(); out.mkdir(parents=True, exist_ok=False)
    exe = out/'HebENReplayFixture.exe'; shutil.copyfile(a.exe, exe)
    (out/'config.ini').write_text('[General]\nmodel='+str(a.model.resolve())+'\nhebrew_model='+str(a.hebrew_model.resolve())+
        '\nhybrid='+str(int(not a.disable_hybrid))+'\nvad_model='+str(a.vad.resolve())+'\nvad_backend=silero\nbackend='+a.backend+
        '\nlanguage='+a.language+'\nthreads=4\nsilence_ms='+str(a.silence_ms)+'\nmax_phrase_seconds='+str(a.max_phrase_seconds)+'\npreroll_ms=600\nmodel_idle_seconds='+str(a.model_idle_seconds)+'\n'+
        'streaming_prototype='+str(int(a.streaming_prototype))+'\nstreaming_interval_ms='+str(a.streaming_interval_ms)+
        '\nstreaming_agreement='+str(a.streaming_agreement)+'\nstreaming_hold_words='+str(a.streaming_hold_words)+
        '\nmodel_warm_on_voice='+str(int(a.warm_on_voice))+'\n', encoding='utf8')
    env = dict(os.environ, HEBEN_DIAGNOSTICS_PATH=str(out/'trace.jsonl'), HEBEN_TEST_TRANSCRIPT=str(out/'transcript.txt'))
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.FindWindowW.argtypes=[wintypes.LPCWSTR,wintypes.LPCWSTR]; user.FindWindowW.restype=wintypes.HWND
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user.SendMessageTimeoutW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM,wintypes.UINT,wintypes.UINT,ctypes.POINTER(ctypes.c_size_t)]
    user.SendMessageTimeoutW.restype=ctypes.c_void_p
    user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
    process=subprocess.Popen([str(exe)],cwd=out,env=env,creationflags=subprocess.CREATE_NO_WINDOW)
    observed=psutil.Process(process.pid); hwnd=None; rows=[]; transcript_count=0; trace_offset=0
    result={'status':'FAIL','mode':'prerecorded WAV into app worker; no microphone, physical hotkey or editor',
            'clips_requested':len(corpus),'pace_ms_per_512_samples':a.pace_ms,'hybrid_enabled':not a.disable_hybrid,'language_requested':a.language,
            'max_phrase_seconds':a.max_phrase_seconds,'silence_ms':a.silence_ms,
            'streaming_prototype':a.streaming_prototype,'streaming_interval_ms':a.streaming_interval_ms,
            'streaming_agreement':a.streaming_agreement,'streaming_hold_words':a.streaming_hold_words,
            'model_warm_on_voice':a.warm_on_voice,'initial_quiet_ms':a.initial_quiet_ms,
            'model_idle_seconds':a.model_idle_seconds,'cold_between_clips':a.cold_between_clips}
    def send(msg, lp=0):
        value=ctypes.c_size_t()
        if not user.SendMessageTimeoutW(hwnd,msg,0,lp,2,15000,ctypes.byref(value)): raise OSError(ctypes.get_last_error(),'message failed')
        return value.value
    try:
        for _ in range(100):
            hwnd=user.FindWindowW('HebENReplayFixtureHiddenClass',None); owner=wintypes.DWORD()
            if hwnd: user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
            if hwnd and owner.value==process.pid: break
            if process.poll() is not None: raise RuntimeError('Fixture exited on launch')
            time.sleep(.1)
        else: raise TimeoutError('Fixture window missing')
        with (out/'results.jsonl').open('x',encoding='utf8') as raw:
            for item in corpus:
                wav=a.audio_dir/(item['id']+'-original.wav')
                with wave.open(str(wav),'rb') as w:
                    if (w.getnchannels(),w.getsampwidth(),w.getframerate())!=(1,2,16000): raise ValueError('Expected mono PCM16 16 kHz WAV')
                    pcm=array.array('h');pcm.frombytes(w.readframes(w.getnframes()))
                if a.initial_quiet_ms:pcm=array.array('h',[0]*(16*a.initial_quiet_ms))+pcm
                start=time.perf_counter();cpu0=observed.cpu_times()
                if send(0x8000+40)!=1:raise RuntimeError('start failed: '+item['id'])
                delivered=0;auto_stopped_early=False
                for pos in range(0,len(pcm),512):
                    chunk=array.array('f',(v/32768 for v in pcm[pos:pos+512])).tobytes()
                    buf=ctypes.create_string_buffer(chunk);data=CopyData(0x48455043,len(chunk),ctypes.cast(buf,ctypes.c_void_p))
                    if send(0x004A,ctypes.addressof(data))!=1:
                        if send(0x8000+42)==0:
                            auto_stopped_early=True;break
                        raise RuntimeError('PCM rejected: '+item['id'])
                    delivered+=len(chunk)//4
                    time.sleep(a.pace_ms/1000)
                if send(0x8000+41)!=1:raise RuntimeError('stop failed: '+item['id'])
                deadline=time.monotonic()+90
                while time.monotonic()<deadline and send(0x8000+43)!=0:
                    if process.poll() is not None:raise RuntimeError('Fixture exited during inference')
                    time.sleep(.05)
                if send(0x8000+43)!=0:raise TimeoutError('inference unfinished: '+item['id'])
                parts=[]
                while True:
                    n=transcript_count+1
                    path=out/('transcript.txt' if n==1 else f'transcript.txt.{n}')
                    if not path.exists():break
                    parts.append(path.read_text(encoding='utf8'));transcript_count=n
                with (out/'trace.jsonl').open('r',encoding='utf8') as f:
                    f.seek(trace_offset);events=[json.loads(s) for s in f.read().splitlines()];trace_offset=f.tell()
                errors=[e['result'] for e in events if e['stage'] in ('vad','segment','test_fixture') and e['result'] in ('audio_overflow','processing_failed','queue_full','error')]
                text=' '.join(parts);ref=comparison.normalize(item['text']);hyp=comparison.normalize(text)
                mem=observed.memory_info();cpu1=observed.cpu_times()
                row={'sample':item['id'],'category':item['language'],'reference':item['text'],'text':text,
                     'language_requested':a.language,
                     'audio_sha256':hashlib.sha256(wav.read_bytes()).hexdigest(),'segments':len(parts),'errors':errors,
                     'delivered_frames':delivered,'total_frames':len(pcm),'auto_stopped_early':auto_stopped_early,
                     'word_edits':comparison.distance(ref.split(),hyp.split()),'reference_words':len(ref.split()),
                     'char_edits':comparison.distance(ref,hyp),'reference_chars':len(ref),
                     'worker_asr_ms':sum(e['value'] for e in events if (e['stage'],e['result']) in (('asr','finished'),('asr_hybrid','finished'))),
                     'elapsed_ms_simulated':(time.perf_counter()-start)*1000,
                     'cpu_ms':1000*((cpu1.user+cpu1.system)-(cpu0.user+cpu0.system)),
                     'private_bytes':mem.private,'working_set_bytes':mem.rss,'handles':observed.num_handles(),'threads':observed.num_threads(),
                     'detected_languages':[e['result'] for e in events if e['stage']=='asr_language']}
                row['streaming_conflicts']=sum((e['stage'],e['result'])==('streaming','final_prefix_conflict_retained') for e in events)
                row['streaming_commits']=[{'tick_ms':e['tick_ms'],'audio_samples':e['value'],'partial':bool(e['code'])}
                                          for e in events if (e['stage'],e['result'])==('streaming','commit_audio_samples')]
                voice=next((e['tick_ms'] for e in events if (e['stage'],e['result'])==('vad','voice_started')),None)
                first=next((e['tick_ms'] for e in events if (e['stage'],e['result'])==('streaming','append_committed') and e['code']==1),None)
                row['first_partial_after_vad_voice_ms']=first-voice if first is not None and voice is not None else None
                row['primary_model_load_ms']=[e['value'] for e in events if (e['stage'],e['result'])==('asr_model','loaded')]
                row['initial_quiet_ms']=a.initial_quiet_ms
                rows.append(row);raw.write(json.dumps(row,ensure_ascii=False)+'\n');raw.flush()
                print(item['id'], 'segments',len(parts),'errors',len(errors),flush=True)
                if a.cold_between_clips:
                    deadline=time.monotonic()+a.model_idle_seconds+20;unloaded=False
                    while time.monotonic()<deadline:
                        with (out/'trace.jsonl').open('r',encoding='utf8') as f:
                            f.seek(trace_offset);later=[json.loads(s) for s in f.read().splitlines()]
                        if any((e['stage'],e['result'])==('asr_models','unloaded_after_inactivity') for e in later):unloaded=True;break
                        if process.poll() is not None:raise RuntimeError('Fixture exited while awaiting unload')
                        time.sleep(.1)
                    if not unloaded:raise TimeoutError('Model did not unload between clips')
        groups={}
        for category in sorted({r['category'] for r in rows}):
            rr=[r for r in rows if r['category']==category]
            groups[category]={'clips':len(rr),'word_edits':sum(r['word_edits'] for r in rr),'reference_words':sum(r['reference_words'] for r in rr),
                              'char_edits':sum(r['char_edits'] for r in rr),'reference_chars':sum(r['reference_chars'] for r in rr),
                              'empty':sum(not r['text'] for r in rr),'errors':sum(bool(r['errors']) for r in rr),
                              'auto_stopped_early':sum(r['auto_stopped_early'] for r in rr)}
        result.update(completed=len(rows),groups=groups)
        result['status']='PASS_EXECUTION' if len(rows)==len(corpus) and not any(r['errors'] for r in rows) else 'FAIL_EXECUTION'
    except Exception as exc:
        result['error']=str(exc)
    finally:
        if hwnd:user.PostMessageW(hwnd,0x10,0,0)
        try:process.wait(timeout=25)
        except subprocess.TimeoutExpired:result.update(shutdown='TIMED_OUT_process_left_running',pid=process.pid)
        else:result['exit_code']=process.returncode
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print(json.dumps(result))
    if result['status']!='PASS_EXECUTION':raise SystemExit(1)


if __name__=='__main__':main()

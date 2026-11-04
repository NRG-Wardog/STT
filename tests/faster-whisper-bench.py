"""Optional offline comparison using faster-whisper/CTranslate2 (test-only)."""
import argparse
import ctypes
import importlib.machinery
import json
import pathlib
import time
from ctypes import wintypes
from faster_whisper import WhisperModel

common = importlib.machinery.SourceFileLoader('heben_bench', str(pathlib.Path(__file__).with_name('run-inference-bench.py'))).load_module()

class Memory(ctypes.Structure):
    _fields_ = [('cb',wintypes.DWORD),('PageFaultCount',wintypes.DWORD)] + [(name,ctypes.c_size_t) for name in (
        'PeakWorkingSetSize','WorkingSetSize','QuotaPeakPagedPoolUsage','QuotaPagedPoolUsage',
        'QuotaPeakNonPagedPoolUsage','QuotaNonPagedPoolUsage','PagefileUsage','PeakPagefileUsage','PrivateUsage')]

def memory():
    m=Memory(); m.cb=ctypes.sizeof(m)
    kernel=ctypes.WinDLL('kernel32',use_last_error=True); kernel.GetCurrentProcess.restype=wintypes.HANDLE
    psapi=ctypes.WinDLL('psapi',use_last_error=True)
    psapi.GetProcessMemoryInfo.argtypes=[wintypes.HANDLE,ctypes.POINTER(Memory),wintypes.DWORD]
    if not psapi.GetProcessMemoryInfo(kernel.GetCurrentProcess(),ctypes.byref(m),m.cb): raise ctypes.WinError(ctypes.get_last_error())
    return m

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--corpus',type=pathlib.Path,required=True); ap.add_argument('--audio-dir',type=pathlib.Path,required=True)
    ap.add_argument('--cache',type=pathlib.Path,required=True); ap.add_argument('--out-dir',type=pathlib.Path,required=True)
    ap.add_argument('--model',default='small'); args=ap.parse_args()
    args.out_dir.mkdir(parents=True,exist_ok=True)
    out=args.out_dir/'results.jsonl'
    if out.exists(): raise SystemExit(f'Refusing to overwrite {out}')
    samples=json.loads(args.corpus.read_text(encoding='utf8'))
    start=time.perf_counter()
    model=WhisperModel(args.model, device='cpu', compute_type='int8', cpu_threads=4, num_workers=1, download_root=str(args.cache.resolve()))
    load_ms=(time.perf_counter()-start)*1000
    with out.open('x',encoding='utf8') as f:
        for sample in samples:
            audio=args.audio_dir/(sample['id']+'-original.wav')
            start=time.perf_counter(); cpu=time.process_time()
            segments,info=model.transcribe(str(audio),language=None,beam_size=1,best_of=1,temperature=0,
                condition_on_previous_text=False,without_timestamps=True,vad_filter=False)
            text=''.join(seg.text for seg in segments)
            elapsed=(time.perf_counter()-start)*1000; cpu_ms=(time.process_time()-cpu)*1000
            m=memory(); ref=common.normalize(sample['text']); hyp=common.normalize(text)
            row={'model':'faster-whisper-'+args.model+'-int8','sample':sample['id'],'iteration':0,
                 'model_load_ms':load_ms,'inference_ms':elapsed,'cpu_ms':cpu_ms,'audio_s':info.duration,
                 'working_set_bytes':m.WorkingSetSize,'private_bytes':m.PrivateUsage,'peak_working_set_bytes':m.PeakWorkingSetSize,
                 'category':sample['language'],'reference':sample['text'],'text':text,'audio_sha256':common.digest(audio),
                 'normalized_reference':ref,'normalized_hypothesis':hyp,
                 'word_edits':common.distance(ref.split(),hyp.split()),'reference_words':len(ref.split()),
                 'char_edits':common.distance(ref,hyp),'reference_chars':len(ref),
                 'fixture':sample.get('fixture','synthetic TTS'),'detected_language':info.language,
                 'settings':'CPU int8; beam=1; best_of=1; temperature=0; no VAD; no previous context; no timestamps'}
            f.write(json.dumps(row,ensure_ascii=False)+'\n'); f.flush(); print(sample['id'],flush=True)
    print('Raw results:',out)

if __name__=='__main__': main()

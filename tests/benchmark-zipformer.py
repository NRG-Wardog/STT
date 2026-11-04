"""Offline sherpa-onnx Zipformer CPU benchmark. Python is test-only.

Input is prerecorded PCM16 WAV, not microphone, VAD, hotkey, or editor input.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import time
import wave
from importlib.util import module_from_spec, spec_from_file_location

import numpy as np
import psutil
import sherpa_onnx

spec=spec_from_file_location('compare_asr',Path(__file__).with_name('compare-asr-runs.py'))
comparison=module_from_spec(spec);spec.loader.exec_module(comparison)


def main():
    p=argparse.ArgumentParser()
    for name in ('model-dir','corpus','audio-dir','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--threads',type=int,default=4);a=p.parse_args()
    if not 1<=a.threads<=16:raise ValueError('threads must be 1..16')
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
    corpus=json.loads(a.corpus.read_text(encoding='utf8'));proc=psutil.Process(os.getpid())
    before=proc.memory_info(); start=time.perf_counter()
    recognizer=sherpa_onnx.OnlineRecognizer.from_transducer(
        tokens=str(a.model_dir/'tokens.txt'),encoder=str(a.model_dir/'encoder.int8.onnx'),
        decoder=str(a.model_dir/'decoder.int8.onnx'),joiner=str(a.model_dir/'joiner.int8.onnx'),
        model_type='zipformer2',num_threads=a.threads,provider='cpu',decoding_method='greedy_search',
        enable_endpoint_detection=False)
    load_ms=1000*(time.perf_counter()-start);loaded=proc.memory_info()
    rows=[]
    with (out/'results.jsonl').open('x',encoding='utf8') as f:
        for item in corpus:
            wav=a.audio_dir/(item['id']+'-original.wav')
            with wave.open(str(wav),'rb') as w:
                if (w.getnchannels(),w.getsampwidth(),w.getframerate())!=(1,2,16000):raise ValueError('Expected mono PCM16 16 kHz WAV')
                data=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(np.float32)/32768.0
            stream=recognizer.create_stream();start=time.perf_counter();cpu0=time.process_time();first=None
            for pos in range(0,len(data),3200):
                stream.accept_waveform(16000,data[pos:pos+3200])
                while recognizer.is_ready(stream):recognizer.decode_stream(stream)
                if first is None and recognizer.get_result(stream).strip():first=min(pos+3200,len(data))/16000
            stream.accept_waveform(16000,np.zeros(24000,dtype=np.float32));stream.input_finished()
            while recognizer.is_ready(stream):recognizer.decode_stream(stream)
            text=recognizer.get_result(stream);elapsed=1000*(time.perf_counter()-start);cpu=1000*(time.process_time()-cpu0)
            ref=comparison.normalize(item['text']);hyp=comparison.normalize(text);mem=proc.memory_info()
            row={'sample':item['id'],'category':item['language'],'reference':item['text'],'text':text,
                 'audio_sha256':hashlib.sha256(wav.read_bytes()).hexdigest(),'word_edits':comparison.distance(ref.split(),hyp.split()),
                 'reference_words':len(ref.split()),'char_edits':comparison.distance(ref,hyp),'reference_chars':len(ref),
                 'audio_seconds':len(data)/16000,'inference_ms':elapsed,'cpu_ms':cpu,'first_text_audio_seconds':first,
                 'private_bytes':mem.private,'working_set_bytes':mem.rss,'peak_working_set_bytes':mem.peak_wset,
                 'fixture':item.get('fixture','dataset reference not independently verified')}
            f.write(json.dumps(row,ensure_ascii=False)+'\n');f.flush();rows.append(row);print(item['id'],flush=True)
    groups={}
    for category in sorted({r['category'] for r in rows}):
        rr=[r for r in rows if r['category']==category];words=sum(r['reference_words'] for r in rr);chars=sum(r['reference_chars'] for r in rr)
        groups[category]={'clips':len(rr),'wer':sum(r['word_edits'] for r in rr)/words,
                          'cer':sum(r['char_edits'] for r in rr)/chars,'word_edits':sum(r['word_edits'] for r in rr),
                          'reference_words':words,'empty':sum(not r['text'] for r in rr)}
    summary={'status':'PASS_EXECUTION','mode':'prerecorded WAV; CPU greedy streaming decoder; no live microphone or editor',
             'sherpa_onnx':getattr(sherpa_onnx,'__version__','unknown'),'threads':a.threads,'chunk_ms':200,'flush_ms':1500,
             'load_ms':load_ms,'private_before_bytes':before.private,'private_loaded_bytes':loaded.private,
             'max_sampled_private_bytes':max(r['private_bytes'] for r in rows),'groups':groups}
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf8');print(json.dumps(summary))


if __name__=='__main__':main()

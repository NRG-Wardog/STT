"""Offline prefix-ASR stability experiment; never uses a microphone or injects text.

This is not the app's hybrid worker and is not a streaming decoder: each audio
prefix is decoded from the beginning. It measures how often previously agreed
words change when more of the same prerecorded utterance arrives.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
import unicodedata
import wave


def words(text):
    text=unicodedata.normalize('NFKD',text).lower()
    return ''.join(' ' if unicodedata.category(c).startswith('P') else c
                   for c in text if not unicodedata.category(c).startswith('M')).split()


def common(a,b):
    n=0
    while n<min(len(a),len(b)) and a[n]==b[n]:n+=1
    return a[:n]


def main():
    p=argparse.ArgumentParser()
    for name in ('exe','model','wav','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--language',choices=('he','en','auto'),default='auto')
    p.add_argument('--step-seconds',type=float,default=2)
    a=p.parse_args()
    if a.step_seconds<=0:raise ValueError('step must be positive')
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
    with wave.open(str(a.wav),'rb') as w:
        if (w.getnchannels(),w.getsampwidth(),w.getframerate())!=(1,2,16000):raise ValueError('Expected mono 16 kHz PCM16')
        samples=w.readframes(w.getnframes())
    frames=len(samples)//2; step=max(1,int(a.step_seconds*16000))
    points=list(range(step,frames,step))+[frames]
    previous=[];committed=[];rows=[];conflicts=0
    for index,count in enumerate(points):
        clip=out/f'prefix-{index+1:03d}.wav'
        with wave.open(str(clip),'wb') as w:
            w.setnchannels(1);w.setsampwidth(2);w.setframerate(16000);w.writeframes(samples[:count*2])
        started=time.perf_counter()
        proc=subprocess.run([str(a.exe.resolve()),str(a.model.resolve()),str(clip), '1','gpu',a.language],capture_output=True)
        (out/f'prefix-{index+1:03d}.stderr.txt').write_bytes(proc.stderr)
        if proc.returncode:raise RuntimeError(f'Inference failed for prefix {index+1}: {proc.returncode}')
        lines=proc.stdout.decode('utf8').splitlines()
        if len(lines)!=1:raise RuntimeError('Unexpected benchmark output')
        result=json.loads(lines[0]);current=words(result['text'])
        agreed=common(previous,current)[:-1] if previous else []
        conflict=current[:len(committed)]!=committed
        if conflict:conflicts+=1
        if not conflict and len(agreed)>len(committed) and agreed[:len(committed)]==committed:committed=agreed
        rows.append({'prefix_audio_seconds':count/16000,'hypothesis':result['text'],
                     'normalized_words':current,'agreed_committed_words':list(committed),
                     'committed_prefix_conflict':conflict,'invocation_wall_ms':(time.perf_counter()-started)*1000,
                     'bench_result':result})
        previous=current
        print(f'prefix {count/16000:.2f}s: words={len(current)}, committed={len(committed)}, conflict={conflict}',flush=True)
    final=rows[-1]['normalized_words']
    summary={'status':'DIAGNOSTIC_ONLY','audio':str(a.wav.resolve()),'model':str(a.model.resolve()),
             'language':a.language,'step_seconds':a.step_seconds,'snapshots':len(rows),
             'committed_words':len(committed),'committed_prefix_conflicts':conflicts,
             'final_agrees_with_committed_prefix':final[:len(committed)]==committed,
             'first_committed_audio_seconds':next((r['prefix_audio_seconds'] for r in rows if r['agreed_committed_words']),None),
             'limitations':'Repeated cold process per prefix; no microphone, VAD, hybrid language routing, Windows editor, accuracy ground truth, or user-facing live latency. Case and punctuation normalized for agreement; irreversible typing not exercised.'}
    (out/'results.json').write_text(json.dumps({'summary':summary,'rows':rows},ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(summary,ensure_ascii=False),flush=True)


if __name__=='__main__':main()

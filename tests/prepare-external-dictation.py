"""Extract an open human-recorded diagnostic set; references remain unreviewed.

The single speaker reads English text with romanized Hebrew loanwords and tech
vocabulary. This is not spontaneous Hebrew, Hebrew-script code switching, or
recorded with the intended user's microphone.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import wave
import pyarrow.parquet as pq

EXPECTED='822fa409140e42f7dba5bd1eaa2ff30b2e55ce081d1a3d002dc4952c1356b500'
REVISION='d523de0a8cab061d209a16edbbe4bf13241774d0'


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--parquet',required=True,type=Path);p.add_argument('--out',required=True,type=Path)
    a=p.parse_args()
    digest=hashlib.sha256(a.parquet.read_bytes()).hexdigest()
    if digest!=EXPECTED:raise ValueError('Parquet hash differs from pinned source')
    rows=pq.read_table(a.parquet).to_pylist()
    if len(rows)!=92:raise ValueError('Unexpected dataset size')
    a.out.mkdir(parents=True,exist_ok=False)
    corpus=[]
    for i,row in enumerate(rows,1):
        audio=row['audio']['bytes'];category=row['category'];reference=row['transcription']
        if not audio or not reference:raise ValueError(f'Incomplete sample {i}')
        with wave.open(io.BytesIO(audio),'rb') as w:
            if (w.getnchannels(),w.getsampwidth(),w.getframerate())!=(1,2,16000):raise ValueError(f'Unexpected WAV format {i}')
            duration=w.getnframes()/16000
        sample_id=f'external-{i:03}'
        (a.out/(sample_id+'-original.wav')).write_bytes(audio)
        corpus.append({'id':sample_id,'language':'mixed_latin' if category in ('hebrew_daily','hebrew_food') else 'en',
                       'text':reference,'fixture':'single-speaker read-aloud; source transcript NOT independently verified',
                       'category_source':category,'audio_sha256':hashlib.sha256(audio).hexdigest(),
                       'duration_seconds':duration,'source_row':i-1,'source_revision':REVISION,
                       'source':'https://huggingface.co/datasets/danielrosehill/Small-STT-Eval-Audio-Dataset',
                       'license':'CC-BY-4.0','reference_status':'DATASET_REFERENCE_UNREVIEWED',
                       'limitations':'English matrix; Hebrew words romanized; single speaker; read aloud; not intended microphone'})
    (a.out/'corpus.json').write_text(json.dumps(corpus,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    mixed=[r for r in corpus if r['language']=='mixed_latin']
    conversational=[r for r in corpus if r['category_source']=='conversational']
    technical=[r for r in corpus if r['category_source'].startswith('tech_') or r['category_source']=='ai_ml'][:17]
    subset=mixed+conversational+technical
    if (len(mixed),len(conversational),len(technical),len({r['id'] for r in subset}))!=(13,10,17,40):
        raise ValueError('Unexpected diagnostic subset composition')
    (a.out/'corpus-diagnostic-40.json').write_text(json.dumps(subset,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps({'clips':len(corpus),'mixed_latin':sum(x['language']=='mixed_latin' for x in corpus),
                      'reference_reviewed_by_us':0,'dataset_sha256':digest}))


if __name__=='__main__':main()

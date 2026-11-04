"""Extract a fixed, stratified FLEURS dev subset without unsafe tar extraction.

Obtain he_il/en_us dev.tsv and audio/dev.tar.gz from google/fleurs first.
FLEURS is CC-BY-4.0: https://huggingface.co/datasets/google/fleurs
Dataset transcripts are supplied references, not independently audited here.
"""
import csv
import argparse
import hashlib
import io
import json
import pathlib
import struct
import tarfile
import wave

parser=argparse.ArgumentParser()
parser.add_argument('--out',type=pathlib.Path,default=pathlib.Path('build/fleurs/subset-v4'))
parser.add_argument('--count',type=int,default=4,help='Samples per language; 4 retains the original fixed subset')
parser.add_argument('--exclude',type=pathlib.Path,help='Existing corpus.json whose dataset IDs must be excluded')
args=parser.parse_args()
if args.count<1 or args.count>100:raise SystemExit('Invalid count')
excluded={'he_il':set(),'en_us':set()}
if args.exclude:
    for item in json.loads(args.exclude.read_text(encoding='utf8')):
        lang='he_il' if item['language']=='he' else 'en_us'
        excluded[lang].add(str(item['dataset_id']))
source = pathlib.Path('build/fleurs')
out = args.out
if out.exists():
    raise SystemExit(f'Refusing to overwrite {out}')
out.mkdir(parents=True)
hashes = {'he_il':'3752e56b764bd0ed531afa805aa589cf4153d9585a3c034faafbf6c852663652',
          'en_us':'2658fda72f199e12676ecac9415094667a4e14e149b146e568ea00b2a2f0954c'}
corpus = []
for lang in ['he_il', 'en_us']:
    archive = source/(lang+'-dev.tar.gz')
    with archive.open('rb') as f:
        h=hashlib.sha256()
        for block in iter(lambda:f.read(1024*1024),b''): h.update(block)
    if h.hexdigest() != hashes[lang]: raise RuntimeError(f'Archive hash mismatch: {archive}')
    with (source/(lang+'-dev.tsv')).open(encoding='utf8',newline='') as f:
        rows=list(csv.reader(f,delimiter='\t'))
    selected=[]
    genders={r[6] for r in rows if len(r)>=7}
    # This Hebrew dev manifest has no female labels; do not invent balance.
    if args.count==4:
        selectors=['MALE','FEMALE'] if {'MALE','FEMALE'} <= genders else [None,None]
        for gender in selectors:
            bins = [(5.0,7.0),(7.0,12.0)] if lang == 'en_us' and gender == 'MALE' else [(2.5,5.0),(6.0,12.0)]
            for low,high in bins:
                row=next((r for r in rows if len(r)>=6 and r[0] not in excluded[lang] and (gender is None or (len(r)>=7 and r[6]==gender)) and low<=int(r[5])/16000<high and r[0] not in {s[0] for s in selected} and r[2] not in {s[2] for s in selected}),None)
                if row is None: raise RuntimeError(f'No sample for {lang} {gender} {low}-{high}')
                selected.append(row)
    else:
        # Deterministic, evenly spaced rows; no repeated sentence IDs/text.
        eligible=[];seen_ids=set();seen_text=set()
        for r in rows:
            if len(r)<6 or not r[5].isdigit() or r[0] in excluded[lang] or r[0] in seen_ids or r[2] in seen_text:continue
            if not 2.5<=int(r[5])/16000<=12.0:continue
            eligible.append(r);seen_ids.add(r[0]);seen_text.add(r[2])
        if len(eligible)<args.count:raise RuntimeError(f'Not enough unique samples for {lang}')
        selected=[eligible[(i*len(eligible))//args.count] for i in range(args.count)]
    with tarfile.open(archive,'r:gz') as tar:
        # gzip is sequential; read members in archive order to avoid repeated
        # decompression while preserving each row's stable identifier.
        ordered=sorted(enumerate(selected),key=lambda pair:tar.getmember('dev/'+pair[1][1]).offset_data)
        for idx,row in ordered:
            member=tar.getmember('dev/'+row[1]); inp=tar.extractfile(member)
            if inp is None or not member.isfile(): raise RuntimeError('Missing regular audio member')
            raw=inp.read(); stream=io.BytesIO(raw)
            if stream.read(4)!=b'RIFF': raise ValueError('Not RIFF')
            stream.read(4)
            if stream.read(4)!=b'WAVE': raise ValueError('Not WAVE')
            fmt=None; data=None
            while True:
                tag=stream.read(4)
                if not tag: break
                size_bytes=stream.read(4)
                if len(size_bytes)!=4: raise ValueError('Truncated chunk')
                size=struct.unpack('<I',size_bytes)[0]; content=stream.read(size)
                if len(content)!=size: raise ValueError('Truncated audio')
                if size%2: stream.read(1)
                if tag==b'fmt ': fmt=content
                if tag==b'data': data=content
            if fmt is None or data is None or len(fmt)<16: raise ValueError('Missing audio data')
            encoding,channels,rate,_,_,bits=struct.unpack_from('<HHIIHH',fmt)
            if (encoding,channels,rate,bits)!=(3,1,16000,32): raise ValueError('Unexpected audio format')
            samples=struct.iter_unpack('<f',data)
            pcm=b''.join(struct.pack('<h',max(-32768,min(32767,round(v*32767)))) for (v,) in samples)
            name=f'{lang}-{idx+1}' if args.count==4 else f'{lang}-validation-{idx+1:02d}'
            target=out/(name+'-original.wav')
            with wave.open(str(target),'wb') as w:
                w.setparams((1,2,16000,0,'NONE','not compressed')); w.writeframes(pcm)
            corpus.append({'id':name,'language':'he' if lang=='he_il' else 'en','text':row[2],
                           'fixture':'FLEURS dev human recording; reference supplied by dataset, not independently audited',
                           'speaker_gender':row[6] if len(row)>=7 else 'UNKNOWN','dataset_id':row[0], 'archive_member':member.name,
                           'original_audio_sha256':hashlib.sha256(raw).hexdigest(),
                           'source':'https://huggingface.co/datasets/google/fleurs', 'license':'CC-BY-4.0'})
(out/'corpus.json').write_text(json.dumps(corpus,ensure_ascii=False,indent=2),encoding='utf8')
print('Prepared',len(corpus),'FLEURS fixtures in',out)

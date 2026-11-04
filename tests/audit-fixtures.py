"""Audit FLEURS references and stitched fixtures without claiming human review.

The generated HTML is local-only, does not autoplay, and is never opened by
this script. Human listening and corrected references are a separate step.
"""
import csv
import hashlib
import html
import io
import json
import os
from pathlib import Path
import struct
import tarfile
import urllib.parse
import wave

ROOT=Path(__file__).resolve().parent.parent
SOURCE=ROOT/'build/fleurs'
SINGLE=SOURCE/'validation-30-each-v3'
MIXED=SOURCE/'stitched-mixed-30'
OUT=ROOT/'build/evidence/rootcause-reference-audit'
ARCHIVE_HASHES={'he_il':'3752e56b764bd0ed531afa805aa589cf4153d9585a3c034faafbf6c852663652',
                'en_us':'2658fda72f199e12676ecac9415094667a4e14e149b146e568ea00b2a2f0954c'}


def sha(data): return hashlib.sha256(data).hexdigest()
def file_sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()


def pcm(path):
    with wave.open(str(path),'rb') as w:
        if (w.getnchannels(),w.getsampwidth(),w.getframerate())!=(1,2,16000):raise ValueError(f'Unexpected PCM format: {path}')
        return w.readframes(w.getnframes())


def converted_source(raw):
    f=io.BytesIO(raw)
    if f.read(4)!=b'RIFF':raise ValueError('Not RIFF')
    f.read(4)
    if f.read(4)!=b'WAVE':raise ValueError('Not WAVE')
    fmt=None;data=None
    while True:
        tag=f.read(4)
        if not tag:break
        sz=struct.unpack('<I',f.read(4))[0];v=f.read(sz)
        if len(v)!=sz:raise ValueError('Truncated source WAV')
        if sz%2:f.read(1)
        if tag==b'fmt ':fmt=v
        if tag==b'data':data=v
    if fmt is None or data is None:raise ValueError('Missing WAV chunks')
    encoding,channels,rate,_,_,bits=struct.unpack_from('<HHIIHH',fmt)
    if (encoding,channels,rate,bits)!=(3,1,16000,32):raise ValueError('Unexpected original WAV format')
    return b''.join(struct.pack('<h',max(-32768,min(32767,round(v*32767)))) for (v,) in struct.iter_unpack('<f',data))


def main():
    if OUT.exists():raise FileExistsError(OUT)
    singles=json.loads((SINGLE/'corpus.json').read_text(encoding='utf8'))
    mixed=json.loads((MIXED/'corpus.json').read_text(encoding='utf8'))
    if len(singles)!=60 or len(mixed)!=30:raise ValueError('Expected 60 single clips and 30 stitched fixtures')
    by_id={r['id']:r for r in singles}
    if len(by_id)!=60:raise ValueError('Duplicate source clip IDs')
    checked={};rows=[]
    for lang in ('he_il','en_us'):
        archive=SOURCE/(lang+'-dev.tar.gz')
        if file_sha(archive)!=ARCHIVE_HASHES[lang]:raise ValueError(f'Archive hash mismatch: {archive}')
        with (SOURCE/(lang+'-dev.tsv')).open(encoding='utf8',newline='') as f:tsv=list(csv.reader(f,delimiter='\t'))
        index={(r[0],'dev/'+r[1]):r for r in tsv if len(r)>=7}
        selected=[r for r in singles if r['id'].startswith(lang+'-')]
        wanted={r['archive_member']:r for r in selected}
        if len(selected)!=30 or len(wanted)!=30:raise ValueError(f'Unexpected source fixture count for {lang}')
        with tarfile.open(archive,'r:gz') as t:
            for member in t:
                if member.name not in wanted:continue
                r=wanted[member.name];file=t.extractfile(member)
                if not member.isfile() or file is None:raise ValueError('Source member not a file')
                original=file.read();wav=SINGLE/(r['id']+'-original.wav');actual=pcm(wav)
                source_row=index.get((str(r['dataset_id']),member.name))
                if source_row is None or source_row[2]!=r['text']:raise ValueError('Reference differs from source TSV: '+r['id'])
                if sha(original)!=r['original_audio_sha256']:raise ValueError('Original source hash mismatch: '+r['id'])
                if converted_source(original)!=actual:raise ValueError('Converted PCM differs from source: '+r['id'])
                checked[r['id']]=actual
                rows.append({'id':r['id'],'category':r['language'],'reference':r['text'],'audio_sha256':file_sha(wav),
                             'frames':len(actual)//2,'reference_provenance':'exact match to source TSV; NOT listened to',
                             'review_status':'UNREVIEWED','path':str(wav.relative_to(ROOT)),'source_ids':r['id']})
    if len(checked)!=60:raise ValueError('Missing source audio')
    for r in mixed:
        ids=r['source_ids'];wav=MIXED/(r['id']+'-original.wav')
        if len(ids)!=2 or any(x not in checked for x in ids):raise ValueError('Unknown stitched source')
        expected=checked[ids[0]]+bytes(16000*2*350//1000)+checked[ids[1]]
        if pcm(wav)!=expected:raise ValueError('Stitched PCM mismatch: '+r['id'])
        if r['text']!=' '.join(by_id[i]['text'] for i in ids):raise ValueError('Stitched reference mismatch: '+r['id'])
        rows.append({'id':r['id'],'category':'mixed_stitched','reference':r['text'],'audio_sha256':file_sha(wav),
                     'frames':len(expected)//2,'reference_provenance':'joined source TSV references; NOT listened to',
                     'review_status':'UNREVIEWED','path':str(wav.relative_to(ROOT)),'source_ids':','.join(ids)})
    OUT.mkdir(parents=True,exist_ok=False)
    rows.sort(key=lambda r:r['id'])
    with (OUT/'fixtures.csv').open('x',encoding='utf8',newline='') as f:
        w=csv.DictWriter(f,fieldnames=rows[0]);w.writeheader();w.writerows(rows)
    summary={'status':'PASS_STRUCTURAL_AUDIT','fixtures':len(rows),'source_recordings':len(singles),
             'stitched_recordings':len(mixed),'human_listened_and_verified':0,'review_status':'ALL_UNREVIEWED',
             'limitation':'Archive identity and transcript provenance cannot establish what was actually spoken.'}
    (OUT/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf8')
    sections=[]
    for r in rows:
        href=urllib.parse.quote(os.path.relpath(ROOT/r['path'],OUT).replace(os.sep,'/'),safe='/')
        sections.append('<section data-id="'+html.escape(r['id'],quote=True)+'" data-hash="'+r['audio_sha256']+'">'
            '<h2>'+html.escape(r['id'])+' ('+html.escape(r['category'])+')</h2>'
            '<audio controls preload="none" src="'+href+'"></audio><p>Dataset reference (not verified):</p><pre>'+html.escape(r['reference'])+'</pre>'
            '<label>Review: <select><option value="UNREVIEWED">Unreviewed</option><option value="CONFIRMED">Confirmed by listening</option>'
            '<option value="CORRECTED">Corrected by listening</option><option value="UNINTELLIGIBLE">Unintelligible</option></select></label>'
            '<p><label>Heard transcript (required when corrected): <textarea rows="3"></textarea></label></p></section>')
    page='''<!doctype html><meta charset="utf-8"><title>HebEN offline reference review</title>
<style>body{font:16px sans-serif;max-width:1000px;margin:auto}section{border-top:1px solid #aaa;padding:1em}audio,textarea{width:100%}pre{white-space:pre-wrap}</style>
<h1>Offline reference review</h1><p>Listen to each clip before confirming or correcting its transcript. This page does not auto-play or send data anywhere. Saving is manual; unreviewed clips remain unreviewed.</p>
<label>Reviewer identifier (optional): <input id="reviewer"></label> <button id="save">Download review JSON</button>'''+''.join(sections)+'''
<script>document.getElementById('save').onclick=()=>{const rows=[...document.querySelectorAll('section')].map(e=>({id:e.dataset.id,audio_sha256:e.dataset.hash,status:e.querySelector('select').value,heard:e.querySelector('textarea').value.trim()}));if(rows.some(r=>r.status==='CORRECTED'&&!r.heard)){alert('A corrected clip needs the words heard.');return;}const d={reviewer:document.getElementById('reviewer').value.trim(),saved_utc:new Date().toISOString(),clips:rows};const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([JSON.stringify(d,null,2)],{type:'application/json'}));a.download='heben-reference-review.json';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000)};</script>'''
    (OUT/'listen-and-review.html').write_text(page,encoding='utf8')
    print(json.dumps(summary))


if __name__=='__main__':main()

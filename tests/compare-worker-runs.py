"""Compare matching app-worker corpus runs; these are not live microphone tests."""
import argparse
import json
from pathlib import Path
import statistics


def read(path):
    rows=[json.loads(s) for s in path.read_text(encoding='utf8').splitlines()]
    result={r['sample']:r for r in rows}
    if not rows or len(result)!=len(rows):raise ValueError('Empty or duplicate samples')
    return result


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--baseline',required=True,type=Path);p.add_argument('--candidate',required=True,type=Path)
    p.add_argument('--out',required=True,type=Path);a=p.parse_args()
    if a.out.exists():raise FileExistsError(a.out)
    b,c=read(a.baseline),read(a.candidate)
    if b.keys()!=c.keys():raise ValueError('Different clips')
    for k in b:
        if b[k]['audio_sha256']!=c[k]['audio_sha256'] or b[k]['reference']!=c[k]['reference']:
            raise ValueError('Audio/reference mismatch: '+k)
    result={'mode':'app workers receiving prerecorded WAV; no physical microphone, hotkey or editor',
            'normalization':'NFKD, casefold, remove Hebrew marks and format marks, punctuation to spaces; CER includes spaces',
            'groups':{},'pairing':{}}
    for category in sorted({x['category'] for x in b.values()}):
        keys=[k for k in b if b[k]['category']==category]
        group={}
        for name,source in [('baseline',b),('candidate',c)]:
            r=[source[k] for k in keys];words=sum(x['reference_words'] for x in r);chars=sum(x['reference_chars'] for x in r)
            early=sum(x['auto_stopped_early'] for x in r) if all('auto_stopped_early' in x for x in r) else None
            group[name]={'clips':len(r),'wer':sum(x['word_edits'] for x in r)/words,'word_edits':sum(x['word_edits'] for x in r),'reference_words':words,
                         'cer':sum(x['char_edits'] for x in r)/chars,'empty':sum(not x['text'] for x in r),
                         'errors':sum(bool(x['errors']) for x in r),'auto_stopped_early':early,
                         'worker_asr_ms_median':statistics.median(x['worker_asr_ms'] for x in r)}
        result['groups'][category]=group
        result['pairing'][category]={'improved':sum(c[k]['word_edits']<b[k]['word_edits'] for k in keys),
                                      'regressed':sum(c[k]['word_edits']>b[k]['word_edits'] for k in keys),
                                      'same':sum(c[k]['word_edits']==b[k]['word_edits'] for k in keys),
                                      'different_delivered_frames':sum(c[k]['delivered_frames']!=b[k]['delivered_frames'] for k in keys)
                                      if all('delivered_frames' in c[k] and 'delivered_frames' in b[k] for k in keys) else None}
    a.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8');print(json.dumps(result,indent=2))


if __name__=='__main__':main()

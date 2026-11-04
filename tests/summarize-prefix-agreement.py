"""Compare commit policies over already captured prefix hypotheses; no inference."""
import argparse
import json
from pathlib import Path


def common(a,b):
    n=0
    while n<min(len(a),len(b)) and a[n]==b[n]:n+=1
    return a[:n]


def run(rows,window,hold):
    committed=[];conflicts=0;first=None
    for i,row in enumerate(rows):
        current=row['normalized_words']
        if current[:len(committed)]!=committed:conflicts+=1;continue
        if i+1<window:continue
        agreed=current
        for old in rows[i-window+1:i]:agreed=common(agreed,old['normalized_words'])
        agreed=agreed[:-hold] if hold and len(agreed)>hold else ([] if hold else agreed)
        if agreed[:len(committed)]==committed and len(agreed)>len(committed):
            committed=agreed
            if first is None:first=row['prefix_audio_seconds']
    final=rows[-1]['normalized_words']
    return {'window':window,'hold_words':hold,'first_commit_audio_seconds':first,
            'committed_words':len(committed),'prefix_conflict_snapshots':conflicts,
            'final_prefix_matches':final[:len(committed)]==committed}


def main():
    p=argparse.ArgumentParser();p.add_argument('inputs',nargs='+',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    if a.out.exists():raise FileExistsError(a.out)
    result=[]
    for path in a.inputs:
        source=json.loads(path.read_text(encoding='utf8'));rows=source['rows']
        for window in (2,3,4):
            for hold in (1,2):result.append({'input':str(path),**run(rows,window,hold)})
    a.out.write_text(json.dumps({'status':'DIAGNOSTIC_ONLY','results':result,'limitations':'Normalized agreement to the same model final output is not accuracy or safety proof. No hybrid pipeline, microphone, editor, punctuation preservation, or real-time scheduling.'},indent=2)+'\n',encoding='utf8')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()

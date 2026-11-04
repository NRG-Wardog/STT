"""Per-utterance S/D/I and normalization sensitivity for local ASR outputs.

Dataset references are not ground truth until a human listens. Alignments are
deterministic edit-distance paths, not semantic or acoustic error labels.
"""
import argparse
import csv
import json
from pathlib import Path
import re
import unicodedata


def normalise(s,mode):
    s=unicodedata.normalize('NFKC',s).casefold()
    if mode=='surface':return ' '.join(s.split())
    if mode=='ivrit_like':
        s=''.join(c for c in unicodedata.normalize('NFKD',s)
                  if unicodedata.category(c)!='Cf' and not (0x0591<=ord(c)<=0x05c7 and unicodedata.category(c).startswith('M')))
        s=s.translate(str.maketrans('', '', '"\u05f4\u201c\u201d\u201e\u201f\u00ab\u00bb\uff02\''))
        s=re.sub(r'[<\[][^>\]]*[>\]]','',s)
        s=re.sub(r'\(([^)]+?)\)','',s)
        s=unicodedata.normalize('NFKC',s)
        return ' '.join(''.join(' ' if unicodedata.category(c)[0] in 'MSP' else c for c in s).split())
    if mode=='local':
        s=unicodedata.normalize('NFKD',s)
        s=''.join(c for c in s if unicodedata.category(c)!='Cf' and not (0x0591<=ord(c)<=0x05c7 and unicodedata.category(c).startswith('M')))
        return ' '.join(''.join(' ' if unicodedata.category(c).startswith('P') else c for c in s).split())
    raise ValueError(mode)


def align(ref,hyp):
    n,m=len(ref),len(hyp);d=[[0]*(m+1) for _ in range(n+1)]
    for i in range(n+1):d[i][0]=i
    for j in range(m+1):d[0][j]=j
    for i in range(1,n+1):
        for j in range(1,m+1):d[i][j]=min(d[i-1][j-1]+(ref[i-1]!=hyp[j-1]),d[i-1][j]+1,d[i][j-1]+1)
    i,j=n,m;ops=[]
    while i or j:
        if i and j and ref[i-1]==hyp[j-1] and d[i][j]==d[i-1][j-1]:ops.append(('match',ref[i-1],hyp[j-1]));i-=1;j-=1
        elif i and j and d[i][j]==d[i-1][j-1]+1:ops.append(('substitution',ref[i-1],hyp[j-1]));i-=1;j-=1
        elif i and d[i][j]==d[i-1][j]+1:ops.append(('deletion',ref[i-1],''));i-=1
        elif j and d[i][j]==d[i][j-1]+1:ops.append(('insertion','',hyp[j-1]));j-=1
        else:raise AssertionError('Invalid alignment')
    ops.reverse();return ops


def count_scripts(s):
    return {'hebrew':sum('\u0590'<=c<='\u05ff' and c.isalpha() for c in s),
            'arabic':sum('\u0600'<=c<='\u06ff' and c.isalpha() for c in s),
            'latin':sum(c.isascii() and c.isalpha() for c in s)}


def main():
    p=argparse.ArgumentParser();p.add_argument('--run',action='append',required=True,help='name=results.jsonl')
    p.add_argument('--pair',action='append',default=[],help='baseline:candidate, compare shared IDs only')
    p.add_argument('--reference-audit',type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
    if a.out.exists():raise FileExistsError(a.out)
    audit={}
    if a.reference_audit:
        with a.reference_audit.open(encoding='utf8',newline='') as f:audit={r['id']:r for r in csv.DictReader(f)}
    data={};rows=[];summaries=[]
    for spec in a.run:
        name,path=spec.split('=',1)
        if name in data:raise ValueError('Duplicate run name')
        inp=Path(path);records=[json.loads(s) for s in inp.read_text(encoding='utf-8-sig').splitlines()]
        by_id={r['sample']:r for r in records}
        if not records or len(by_id)!=len(records):raise ValueError('Empty or duplicate run')
        data[name]=by_id
        for cat in sorted({r['category'] for r in records}):
            for mode in ('surface','local','ivrit_like'):
                group=[r for r in records if r['category']==cat];total={'substitution':0,'deletion':0,'insertion':0};refwords=0;char_edits=0;refchars=0
                mismatch=0;unknown_route=0
                for r in group:
                    sample=r['sample'];review=audit.get(sample)
                    if review and review['audio_sha256']!=r['audio_sha256']:raise ValueError('Audited audio mismatch: '+sample)
                    ref=normalise(r['reference'],mode);hyp=normalise(r['text'],mode)
                    ops=align(ref.split(),hyp.split());counts={k:sum(o[0]==k for o in ops) for k in total}
                    for k in total:total[k]+=counts[k]
                    refwords+=len(ref.split());char_edits+=sum(o[0]!='match' for o in align(list(ref),list(hyp)));refchars+=len(ref)
                    detected=r.get('detected_languages')
                    requested=r.get('language_requested')
                    if detected is None:
                        detected=[r['language_detected']] if r.get('language_detected') else []
                    if cat in ('he','en') and requested in (None,'auto'):
                        if not detected:unknown_route+=1
                        elif any(x!=cat for x in detected):mismatch+=1
                    if mode=='local':
                        rows.append({'run':name,'sample':sample,'category':cat,'audio_sha256':r['audio_sha256'],
                                     'reference_status':review['review_status'] if review else 'UNREVIEWED_OR_EXTERNAL',
                                     'reference':r['reference'],'hypothesis':r['text'],'normalised_reference':ref,'normalised_hypothesis':hyp,
                                     'reference_words':len(ref.split()),'substitutions':counts['substitution'],'deletions':counts['deletion'],
                                     'insertions':counts['insertion'],'wer':sum(counts.values())/len(ref.split()) if ref.split() else None,
                                     'detected_languages':detected,'language_requested':requested,'output_scripts':count_scripts(r['text']),
                                     'segments':r.get('segments',r.get('phrases')),'auto_stopped_early':r.get('auto_stopped_early'),
                                     'delivered_frames':r.get('delivered_frames'),'total_frames':r.get('total_frames'),
                                     'alignment':[{'op':o,'reference':x,'hypothesis':y} for o,x,y in ops]})
                summaries.append({'run':name,'category':cat,'normalization':mode,'clips':len(group),'reference_words':refwords,
                                  'substitutions':total['substitution'],'deletions':total['deletion'],'insertions':total['insertion'],
                                  'wer':sum(total.values())/refwords if refwords else None,'cer_including_spaces':char_edits/refchars if refchars else None,
                                  'language_mismatch_clips':mismatch if cat in ('he','en') else None,
                                  'language_unknown_clips':unknown_route if cat in ('he','en') else None})
    pair_results=[];pair_rows=[];row_index={(r['run'],r['sample']):r for r in rows}
    for spec in a.pair:
        first,second=spec.split(':',1)
        if first not in data or second not in data:raise ValueError('Unknown paired run')
        common=sorted(set(data[first])&set(data[second]))
        if not common:raise ValueError('No paired samples')
        for k in common:
            x,y=data[first][k],data[second][k]
            if x['audio_sha256']!=y['audio_sha256'] or x['reference']!=y['reference']:raise ValueError('Paired input differs: '+k)
            u,v=row_index[(first,k)],row_index[(second,k)]
            ue=u['substitutions']+u['deletions']+u['insertions'];ve=v['substitutions']+v['deletions']+v['insertions']
            pair_rows.append({'baseline':first,'candidate':second,'sample':k,'category':u['category'],
                              'baseline_errors':ue,'candidate_errors':ve,'error_delta':ve-ue,
                              'same_normalised_output':u['normalised_hypothesis']==v['normalised_hypothesis'],
                              'baseline_hypothesis':u['hypothesis'],'candidate_hypothesis':v['hypothesis']})
        subset=[r for r in pair_rows if r['baseline']==first and r['candidate']==second]
        for cat in sorted({r['category'] for r in subset}):
            z=[r for r in subset if r['category']==cat]
            pair_results.append({'baseline':first,'candidate':second,'category':cat,'clips':len(z),
                'baseline_word_errors':sum(r['baseline_errors'] for r in z),'candidate_word_errors':sum(r['candidate_errors'] for r in z),
                'improved':sum(r['error_delta']<0 for r in z),'regressed':sum(r['error_delta']>0 for r in z),
                'same_error_count':sum(r['error_delta']==0 for r in z),'same_normalised_output':sum(r['same_normalised_output'] for r in z)})
    a.out.mkdir(parents=True,exist_ok=False)
    (a.out/'summary.json').write_text(json.dumps({'reference_warning':'No acoustic ground truth is verified by this tool',
        'ivrit_like_warning':'Approximation of published ivrit.ai normalization; not the official library or leaderboard reproduction',
        'alignment_tie_break':'match, substitution, deletion, insertion','groups':summaries,'pairs':pair_results},ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    with (a.out/'utterances.jsonl').open('x',encoding='utf8') as f:
        for row in rows:f.write(json.dumps(row,ensure_ascii=False)+'\n')
    keys=['run','sample','category','reference_status','reference','hypothesis','substitutions','deletions','insertions','reference_words','wer','detected_languages','language_requested','segments','auto_stopped_early','delivered_frames','total_frames']
    with (a.out/'utterances.csv').open('x',newline='',encoding='utf-8-sig') as f:
        w=csv.DictWriter(f,fieldnames=keys,extrasaction='ignore');w.writeheader();w.writerows(rows)
    if pair_rows:
        with (a.out/'pairs.csv').open('x',newline='',encoding='utf-8-sig') as f:
            w=csv.DictWriter(f,fieldnames=pair_rows[0]);w.writeheader();w.writerows(pair_rows)
    print(json.dumps({'status':'PASS_ANALYSIS','runs':len(data),'utterance_rows':len(rows),'reference_warning':'not human verified'}))


if __name__=='__main__':main()

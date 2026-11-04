"""Record a specific supervised observation without inferring other passes."""
import argparse
import csv
import json
from pathlib import Path


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--matrix',required=True,type=Path);p.add_argument('--result',required=True,type=Path)
    p.add_argument('--out',required=True,type=Path);a=p.parse_args()
    if a.out.exists():raise FileExistsError(a.out)
    with a.matrix.open(encoding='utf-8-sig',newline='') as f:rows=list(csv.DictReader(f));fields=list(rows[0])
    result=json.loads(a.result.read_text(encoding='utf8'))
    targets={'Notepad','ChatGPT web composer','VS Code editor','Codex CLI in Windows Terminal'}
    if result.get('status')!='PASS_USER_VISUAL_ONE_INSERTION' or result.get('target') not in targets or result.get('observed_user_report')!=result.get('reference'):
        raise ValueError('Expected a user-confirmed fixed-string observation for a supported target')
    trace=Path(result['trace'])
    if not trace.is_file():raise ValueError('Trace evidence is missing')
    matches=[r for r in rows if r['target']==result['target'] and r['scenario']=='fixed_unicode_string']
    if len(matches)!=1:raise ValueError('Expected one fixed-string row')
    row=matches[0];row.update(status='PASS',reason='One exact insertion visually confirmed by user; repeat reliability not established.',
                              editor_readback='USER VISUAL CONFIRMATION')
    if 'evidence' not in fields:
        fields+=['evidence']
        for r in rows:r['evidence']=''
    row['evidence']=str(a.result)
    with a.out.open('x',encoding='utf-8-sig',newline='') as f:
        w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rows)
    print(json.dumps({'status':'PASS_RECORDED','updated_rows':1,'total_rows':len(rows)}))


if __name__=='__main__':main()

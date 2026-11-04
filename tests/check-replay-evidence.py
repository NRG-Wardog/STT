"""Verify stage ordering and privacy properties of one isolated replay trace."""
import argparse
import json
import pathlib

ap=argparse.ArgumentParser();ap.add_argument('directory',type=pathlib.Path);args=ap.parse_args()
raw=(args.directory/'trace.jsonl').read_text(encoding='utf8')
events=[json.loads(line) for line in raw.splitlines()]
result=json.loads((args.directory/'result.json').read_text(encoding='utf8'))
def index(stage,status):
    return next(i for i,event in enumerate(events) if event['stage']==stage and event['result']==status)
required=[('test_fixture','replay_started'),('vad_model','loaded'),('pcm','received'),('vad','speech_detected'),
          ('segment','emitted'),('segment','queued'),('asr','started'),('asr','finished'),('asr_text','produced'),('test_sink','written')]
positions={f'{stage}/{status}':index(stage,status) for stage,status in required}
sequence=[positions['segment/emitted'],positions['segment/queued'],positions['asr/started'],positions['asr/finished'],positions['asr_text/produced'],positions['test_sink/written']]
checks={'required_stages_present':True,'segment_to_output_order':sequence==sorted(sequence),
        'reference_matches':result.get('reference_matches_after_normalization') is True,
        'trace_ascii_only':all(ord(ch)<128 for ch in raw),
        'trace_does_not_contain_fixture_words':all(x not in raw.lower() for x in ('hello world','detection test')),
        'actual_microphone_tested':False,'actual_editor_acceptance_tested':False}
output={'checks':checks,'positions':positions,'status':'PASS_REPLAY_EVIDENCE' if all(v for k,v in checks.items() if not k.startswith('actual_')) else 'FAIL'}
(args.directory/'evidence-check.json').write_text(json.dumps(output,indent=2),encoding='utf8')
print(json.dumps(output))
if output['status']!='PASS_REPLAY_EVIDENCE':raise SystemExit(1)

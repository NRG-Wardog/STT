"""Summarize timestamped test-fixture silence runs; no inference is performed."""
import argparse
import csv
import datetime as dt
import json
from pathlib import Path
import statistics


def ts(value):return dt.datetime.fromisoformat(value.replace('Z','+00:00')).timestamp()


def summarize(directory):
    result=json.loads((directory/'result.json').read_text(encoding='utf8'))
    events=[json.loads(x) for x in (directory/'trace.jsonl').read_text(encoding='utf8').splitlines()]
    rows=list(csv.DictReader((directory/'process-resources.csv').open(encoding='utf8')))
    def when(stage,event):
        return next((ts(x['utc']) for x in events if x['stage']==stage and x['result']==event),None)
    start=when('test_fixture','replay_started');stop=when('capture','stopped_after_5s_silence')
    loaded=when('asr_model','loaded');unloaded=when('asr_models','unloaded_after_inactivity')
    if start is None or stop is None:raise ValueError('Expected test phase timestamps missing')
    def group(values):
        if not values:return {'samples':0}
        return {'samples':len(values),'private_median_mib':round(statistics.median(float(x['private_bytes']) for x in values)/1048576,2),
                'private_max_sampled_mib':round(max(float(x['private_bytes']) for x in values)/1048576,2),
                'working_set_median_mib':round(statistics.median(float(x['working_set_bytes']) for x in values)/1048576,2)}
    before=[x for x in rows if float(x['unix_time'])<start]
    active=[x for x in rows if start<float(x['unix_time'])<stop]
    steady=[x for x in active if loaded is None or float(x['unix_time'])>loaded+.2]
    end=float(rows[-1]['unix_time']);after=[x for x in rows if float(x['unix_time'])>max(stop+1,unloaded+.5 if unloaded else end-5)]
    load_ms=next((x['value'] for x in events if (x['stage'],x['result'])==('asr_model','loaded')),None)
    return {'directory':str(directory),'fixture_sha256':result['fixture_exe_sha256'],'skip_model_warm_ablation':result['skip_model_warm_ablation'],
            'model_warm_on_voice':result.get('model_warm_on_voice',False),
            'status':result['status'],'startup_idle':group(before),'capture_after_warm_if_applicable':group(steady),
            'after_unload_or_final_idle':group(after),'model_load_ms':load_ms,
            'stop_to_unload_s':round(unloaded-stop,3) if unloaded else None,
            'limitations':'Headless fixture, synthetic silence, 0.5 s process sampling; not the exact packaged executable, physical microphone, or first-word latency.'}


def main():
    p=argparse.ArgumentParser();p.add_argument('runs',nargs='+',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    if a.out.exists():raise FileExistsError(a.out)
    result={'status':'DESCRIPTIVE_FIXTURE_MEASUREMENT','runs':[summarize(x) for x in a.runs],
            'comparison_note':'Results are fixture-only. Skip-warm is an ablation; warm-on-voice is an experimental candidate. Memory differences include ASR initialization, backend and allocator effects; they do not isolate each allocation or prove speech latency.'}
    a.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8');print(json.dumps(result,indent=2))


if __name__=='__main__':main()

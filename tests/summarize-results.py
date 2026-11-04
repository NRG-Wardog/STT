"""Summarize the CSV/JSONL evidence without treating missing measurements as zero."""
import argparse
import collections
import csv
import json
import math
import pathlib
import statistics

def percentile(values, p):
    values = sorted(values)
    if not values:
        return None
    index = (len(values)-1)*p
    low = math.floor(index); high = math.ceil(index)
    return values[low] + (values[high]-values[low])*(index-low)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--idle', type=pathlib.Path, nargs='*', default=[])
    parser.add_argument('--inference', type=pathlib.Path, nargs='*', default=[])
    parser.add_argument('--out', type=pathlib.Path, required=True)
    args = parser.parse_args()
    output = {'idle': {}, 'inference': {}, 'notes': [
        'Idle CPU percent is relative to one logical CPU; first sample excluded.',
        'Check each inference fixture field: synthetic TTS input text and dataset-provided references are different populations.',
        'Percentiles on small samples are descriptive, not population estimates.'
    ]}
    for path in args.idle:
        with path.open(encoding='utf-8-sig', newline='') as f:
            rows = list(csv.DictReader(f))
        if len(rows) < 2:
            raise ValueError(f'{path}: need at least two samples')
        cpus = [float(r['cpu_percent_one_core']) for r in rows[1:]]
        output['idle'][str(path)] = {
            'sample_count':len(rows), 'duration_s':float(rows[-1]['elapsed_s'])-float(rows[0]['elapsed_s']),
            'cpu_percent_mean_one_core':statistics.mean(cpus), 'cpu_percent_p95_one_core':percentile(cpus,.95),
            'cpu_percent_max_one_core':max(cpus), 'cpu_seconds_delta':float(rows[-1]['cpu_seconds'])-float(rows[0]['cpu_seconds']),
            'private_bytes_median':statistics.median(int(r['private_bytes']) for r in rows),
            'working_set_bytes_median':statistics.median(int(r['working_set_bytes']) for r in rows),
            'peak_working_set_bytes':max(int(r['peak_working_set_bytes']) for r in rows),
            'handles_first_last':[int(rows[0]['handles']),int(rows[-1]['handles'])],
            'threads_first_last':[int(rows[0]['threads']),int(rows[-1]['threads'])]
        }
    groups = collections.defaultdict(list)
    for path in args.inference:
        with path.open(encoding='utf-8') as f:
            for line in f:
                row = json.loads(line)
                groups[(row['model'], row['category'])].append(row)
    for (model, category), rows in groups.items():
        # Repetitions do not create independent reference utterances.
        first = [r for r in rows if r['iteration'] == 0]
        times = [r['inference_ms'] for r in rows]
        output['inference'][model+' / '+category] = {
            'utterances':len(first), 'runs':len(rows),
            'wer':sum(r['word_edits'] for r in first)/sum(r['reference_words'] for r in first),
            'cer':sum(r['char_edits'] for r in first)/sum(r['reference_chars'] for r in first),
            'inference_ms_p50':percentile(times,.5), 'inference_ms_p95':percentile(times,.95),
            'rtf_median':statistics.median(r['inference_ms']/(1000*r['audio_s']) for r in rows),
            'private_bytes_peak_observed':max(r['private_bytes'] for r in rows),
            'model_load_ms_p50':percentile([r['model_load_ms'] for r in first],.5)
        }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(output, ensure_ascii=False, indent=2),encoding='utf-8')

if __name__ == '__main__':
    main()

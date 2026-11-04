"""Compare paired offline ASR runs with explicit Hebrew-aware normalization."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import unicodedata


def normalize(text):
    text = unicodedata.normalize('NFKD', text).casefold()
    out = []
    for c in text:
        category = unicodedata.category(c)
        if 0x0591 <= ord(c) <= 0x05c7 and category.startswith('M'):
            continue  # Hebrew niqqud and cantillation
        if category == 'Cf':
            continue  # bidi and format marks
        out.append(' ' if category.startswith('P') else c)
    return ' '.join(''.join(out).split())


def distance(a, b):
    prev = list(range(len(b) + 1))
    for i, x in enumerate(a, 1):
        cur = [i]
        for j, y in enumerate(b, 1):
            cur.append(min(cur[-1] + 1, prev[j] + 1, prev[j - 1] + (x != y)))
        prev = cur
    return prev[-1]


def percentile(values, p):
    values = sorted(values)
    return values[math.ceil(len(values) * p) - 1]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--run', action='append', required=True, help='name=results.jsonl')
    p.add_argument('--out', required=True, type=Path)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    runs = {}
    for spec in args.run:
        name, path = spec.split('=', 1)
        rows = [json.loads(s) for s in Path(path).read_text(encoding='utf-8-sig').splitlines()]
        by_id = {r['sample']: r for r in rows}
        if not rows or len(by_id) != len(rows):
            raise ValueError(f'{name}: empty or duplicate sample IDs')
        runs[name] = by_id
    base = next(iter(runs.values()))
    for name, rows in runs.items():
        if rows.keys() != base.keys():
            raise ValueError(f'{name}: nonidentical clip sets')
        for key in base:
            if rows[key]['audio_sha256'] != base[key]['audio_sha256'] or rows[key]['reference'] != base[key]['reference']:
                raise ValueError(f'{name}: mismatched audio/reference for {key}')
    result, per_clip = [], []
    for name, rows in runs.items():
        for category in sorted({r['category'] for r in rows.values()}):
            selected = [r for r in rows.values() if r['category'] == category]
            werr = wref = cerr = cref = 0
            for r in selected:
                ref, hyp = normalize(r['reference']), normalize(r['text'])
                we, ce = distance(ref.split(), hyp.split()), distance(ref, hyp)
                werr += we; wref += len(ref.split()); cerr += ce; cref += len(ref)
                per_clip.append({'run': name, 'sample': r['sample'], 'category': category,
                                 'word_edits': we, 'reference_words': len(ref.split()),
                                 'char_edits': ce, 'reference_chars': len(ref),
                                 'language_detected': r['language_detected'], 'inference_ms': r['inference_ms']})
            latency = [r['inference_ms'] for r in selected]
            loads = [r['model_load_ms'] for r in selected if r['model_load_ms'] is not None]
            memory = [r['private_bytes'] for r in selected if r['private_bytes'] is not None]
            result.append({'run': name, 'category': category, 'clips': len(selected), 'word_edits': werr,
                           'reference_words': wref, 'wer': werr / wref if wref else None,
                           'char_edits': cerr, 'reference_chars': cref, 'cer': cerr / cref if cref else None,
                           'inference_ms_median': statistics.median(latency), 'inference_ms_p95': percentile(latency, .95),
                           'model_load_ms_median': statistics.median(loads) if len(loads)==len(selected) else None,
                           'cpu_ms_median': statistics.median(r['cpu_ms'] for r in selected),
                           'real_time_factor_median': statistics.median(r['inference_ms'] / (1000 * r['audio_s']) for r in selected),
                           'private_bytes_max': max(memory) if len(memory)==len(selected) else None})
    (args.out / 'summary.json').write_text(json.dumps({'normalization': 'NFKD, casefold, remove Hebrew marks and format marks, punctuation to spaces; CER includes spaces',
                                                       'mode': 'paired full-recording offline inference; no microphone, VAD, hotkey or editor',
                                                       'groups': result}, indent=2) + '\n', encoding='utf8')
    with (args.out / 'per-clip.csv').open('x', newline='', encoding='utf8') as f:
        w = csv.DictWriter(f, fieldnames=list(per_clip[0])); w.writeheader(); w.writerows(per_clip)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()

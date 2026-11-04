"""Compare an offline fallback policy on already measured matching clips.

This does not simulate VAD, sessions or actual microphone/editor behavior.
"""
import argparse
import json
from pathlib import Path


def read(path):
    return {r['sample']: r for r in map(json.loads, path.read_text(encoding='utf8').splitlines())}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--auto', required=True, type=Path)
    parser.add_argument('--he', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    auto, he = read(args.auto), read(args.he)
    if auto.keys() != he.keys():
        raise ValueError('The clip sets differ')
    totals = {}
    for key, a in auto.items():
        h = he[key]
        if a['audio_sha256'] != h['audio_sha256'] or a['model_sha256'] != h['model_sha256']:
            raise ValueError(f'Input mismatch: {key}')
        stats = totals.setdefault(a['category'], dict(clips=0, fallback=0, baseline_edits=0,
                                                       fallback_edits=0, improvements=0, regressions=0))
        use_he = a['language_detected'] not in ('he', 'en')
        b = h if use_he else a
        stats['clips'] += 1
        stats['fallback'] += use_he
        stats['baseline_edits'] += a['word_edits']
        stats['fallback_edits'] += b['word_edits']
        stats['improvements'] += b['word_edits'] < a['word_edits']
        stats['regressions'] += b['word_edits'] > a['word_edits']
    result = {'mode': 'offline policy simulation; no live microphone or editor', 'categories': totals}
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()

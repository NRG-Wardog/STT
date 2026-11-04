"""Simulate routing from matching completed ASR runs; never executes a model."""
import argparse
import json
from pathlib import Path


def read(path):
    rows = [json.loads(s) for s in path.read_text(encoding='utf8').splitlines()]
    result = {x['sample']: x for x in rows}
    if len(result) != len(rows):
        raise ValueError('Duplicate sample IDs')
    return result


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--multilingual', type=Path, required=True)
    p.add_argument('--hebrew', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    multilingual, hebrew = read(a.multilingual), read(a.hebrew)
    if multilingual.keys() != hebrew.keys():
        raise ValueError('Different clip sets')
    a.out.parent.mkdir(parents=True, exist_ok=True)
    with a.out.open('x', encoding='utf8') as out:
        for sample, m in multilingual.items():
            h = hebrew[sample]
            if m['audio_sha256'] != h['audio_sha256'] or m['reference'] != h['reference']:
                raise ValueError('Mismatched audio/reference for ' + sample)
            selected = dict(m if m['language_detected'] == 'en' else h)
            selected['routing_detector_language'] = m['language_detected']
            selected['language_detected'] = m['language_detected']
            selected['selected_model'] = 'multilingual' if m['language_detected'] == 'en' else 'hebrew'
            selected['fixture'] = 'SIMULATED routing from separate offline runs, not a real hybrid execution'
            selected['inference_ms'] = m['inference_ms'] + (0 if m['language_detected'] == 'en' else h['inference_ms'])
            selected['cpu_ms'] = m['cpu_ms'] + (0 if m['language_detected'] == 'en' else h['cpu_ms'])
            selected['audio_s'] = m['audio_s']
            # These cannot be derived from separate runs, regardless of which
            # constituent was selected. Never report them as hybrid measures.
            selected['model_load_ms'] = None
            selected['private_bytes'] = None
            selected['working_set_bytes'] = None
            selected['peak_working_set_bytes'] = None
            out.write(json.dumps(selected, ensure_ascii=False) + '\n')
    print(f'Simulated {len(multilingual)} paired clips; no hybrid process was run.')


if __name__ == '__main__':
    main()

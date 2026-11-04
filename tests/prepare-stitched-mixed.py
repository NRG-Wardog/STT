"""Create artificial mixed-language pairs from separately recorded FLEURS clips.

These are not naturally code-switched sentences: they join two speakers and
references. They are useful for testing dropped clauses, not population WER.
"""
import argparse
import hashlib
import json
from pathlib import Path
import wave


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--input', required=True, type=Path)
    p.add_argument('--out', required=True, type=Path)
    args = p.parse_args()
    records = json.loads((args.input / 'corpus.json').read_text(encoding='utf8'))
    he = [r for r in records if r['language'] == 'he']
    en = [r for r in records if r['language'] == 'en']
    if len(he) != len(en) or not he:
        raise ValueError('Expected equal non-empty Hebrew and English sets')
    args.out.mkdir(parents=True, exist_ok=False)
    samples = []
    for i, (h, e) in enumerate(zip(he, en)):
        pair = (h, e) if i % 2 == 0 else (e, h)
        pcm = []
        for r in pair:
            path = args.input / (r['id'] + '-original.wav')
            with wave.open(str(path), 'rb') as w:
                if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != (1, 2, 16000):
                    raise ValueError('Expected mono 16 kHz PCM16')
                pcm.append(w.readframes(w.getnframes()))
        sample_id = f'stitched-{i+1:02}'
        path = args.out / (sample_id + '-original.wav')
        with wave.open(str(path), 'wb') as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000)
            w.writeframes(pcm[0] + b'\0' * (16000 * 2 * 350 // 1000) + pcm[1])
        samples.append({'id': sample_id, 'language': 'mixed', 'text': ' '.join(r['text'] for r in pair),
                        'fixture': 'artificially stitched FLEURS human recordings; separate speakers and sentences, dataset labels not audited',
                        'source_ids': [r['id'] for r in pair], 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    (args.out / 'corpus.json').write_text(json.dumps(samples, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
    print(json.dumps({'clips': len(samples), 'pause_ms': 350, 'fixture': 'stitched, not natural code-switching'}))


if __name__ == '__main__':
    main()

"""Compare streaming Silero thresholds on named WAV fixtures, offline only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    p = argparse.ArgumentParser()
    for arg in ('exe', 'model', 'speech-dir', 'noise-dir', 'out'):
        p.add_argument('--' + arg, type=Path, required=True)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    inputs = [('speech', p) for p in sorted(args.speech_dir.glob('*-original.wav'))]
    inputs += [('synthetic_noise', p) for p in sorted(args.noise_dir.glob('*.wav'))]
    if not inputs:
        raise ValueError('No audio fixtures')
    counts = {}
    with (args.out / 'results.jsonl').open('x', encoding='utf8') as out:
        for threshold in (0.5, 0.3):
            env = dict(os.environ, HEBEN_VAD_TEST_THRESHOLD=str(threshold))
            for label, wav in inputs:
                run = subprocess.run([str(args.exe.resolve()), str(args.model.resolve()), str(wav.resolve()), 'stream600'],
                                     env=env, capture_output=True, check=True, text=True)
                row = json.loads(run.stdout)
                row.update(category=label, sample=wav.name, sha256=hashlib.sha256(wav.read_bytes()).hexdigest())
                out.write(json.dumps(row) + '\n')
                k = f'{threshold}:{label}'
                v = counts.setdefault(k, {'files': 0, 'files_with_segments': 0})
                v['files'] += 1
                v['files_with_segments'] += row['segments'] > 0
    summary = {'mode': 'prerecorded WAV; no live microphone or editor', 'counts': counts,
               'limitations': 'synthetic noise only; segments do not prove transcript accuracy or absence of false triggers'}
    (args.out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf8')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()

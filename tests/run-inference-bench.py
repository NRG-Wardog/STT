"""Run local controlled Whisper experiments; Python is needed only for testing.

Synthetic fixtures are not human-verified ground truth. WER/CER here compare
against TTS input text and are diagnostic, not an estimate of real-world error.
"""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import unicodedata

ROOT = pathlib.Path(__file__).resolve().parent.parent

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def normalize(text):
    text = unicodedata.normalize('NFKC', text).lower()
    text = ''.join(' ' if unicodedata.category(ch).startswith('P') else ch for ch in text)
    return ' '.join(text.split())

def distance(a, b):
    previous = list(range(len(b) + 1))
    for i, x in enumerate(a, 1):
        current = [i]
        for j, y in enumerate(b, 1):
            current.append(min(current[-1] + 1, previous[j] + 1, previous[j-1] + (x != y)))
        previous = current
    return previous[-1]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', type=pathlib.Path, required=True)
    ap.add_argument('--models', type=pathlib.Path, nargs='+', required=True)
    ap.add_argument('--corpus', type=pathlib.Path, default=ROOT/'tests/synthetic-corpus.json')
    ap.add_argument('--audio-dir', type=pathlib.Path, default=ROOT/'build/synthetic')
    ap.add_argument('--out-dir', type=pathlib.Path, default=ROOT/'build/evidence/inference')
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--gpu', action='store_true', help='Request GPU; inspect stderr to verify backend actually used')
    ap.add_argument('--language', choices=['auto','he','en'], default='auto')
    ap.add_argument('--timestamps',action='store_true',help='Experimental multi-segment decoding (timestamps are not inserted as text)')
    args = ap.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    samples = json.loads(args.corpus.read_text(encoding='utf-8'))
    raw = args.out_dir/'results.jsonl'
    if raw.exists():
        raise SystemExit(f'Refusing to overwrite {raw}; choose a new output directory')
    with raw.open('x', encoding='utf-8', newline='\n') as out:
        for model in args.models:
            model_hash = digest(model)
            for sample in samples:
                audio = args.audio_dir/(sample['id']+'-original.wav')
                command = [str(args.exe.resolve()), str(model.resolve()), str(audio.resolve()), str(args.repeats)]
                if args.gpu:
                    command.append('gpu')
                elif args.language!='auto':
                    command.append('cpu')
                if args.language!='auto':command.append(args.language)
                env=os.environ.copy();env['HEBEN_BENCH_TIMESTAMPS']='1' if args.timestamps else '0'
                result = subprocess.run(command, capture_output=True,env=env)
                (args.out_dir/(model.stem+'-'+sample['id']+'.stderr.txt')).write_bytes(result.stderr)
                if result.returncode:
                    raise RuntimeError(f'{model} {sample["id"]} failed: {result.returncode}')
                lines = result.stdout.decode('utf-8').splitlines()
                if len(lines) != args.repeats:
                    raise RuntimeError(f'Unexpected output: {lines!r}')
                reference = normalize(sample['text'])
                for line in lines:
                    row = json.loads(line)
                    hypothesis = normalize(row['text'])
                    row.update(model=model.name, model_sha256=model_hash, sample=sample['id'],
                               audio_sha256=digest(audio), category=sample['language'],
                               reference=sample['text'], normalized_reference=reference,
                               normalized_hypothesis=hypothesis,
                               word_edits=distance(reference.split(), hypothesis.split()),
                               reference_words=len(reference.split()),
                               char_edits=distance(reference, hypothesis), reference_chars=len(reference),
                               fixture=sample.get('fixture', 'synthetic TTS; reference is synthesis input, not independently verified'),
                               decoding='timestamps_multi_segment' if args.timestamps else 'single_segment_no_timestamps')
                    out.write(json.dumps(row, ensure_ascii=False)+'\n'); out.flush()
                print(model.name, sample['id'], flush=True)
    print('Raw results:', raw)

if __name__ == '__main__':
    main()

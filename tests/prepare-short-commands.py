"""Select a reproducible diagnostic subset from TensorFlow's mini Speech Commands.

The labels are the dataset's single-word labels, not independently verified
transcripts. No archive paths are extracted directly to the filesystem.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile

WORDS = ('yes', 'no', 'up', 'down', 'left', 'right', 'stop', 'go')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--zip', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--per-word', type=int, default=5)
    args = parser.parse_args()
    if args.per_word < 1:
        raise ValueError('per-word must be positive')
    args.out.mkdir(parents=True, exist_ok=False)
    corpus = []
    used_speakers = set()
    with zipfile.ZipFile(args.zip) as archive:
        for word in WORDS:
            names = sorted(n for n in archive.namelist()
                           if n.startswith(f'mini_speech_commands/{word}/')
                           and n.endswith('.wav') and '_nohash_' in n)
            count = 0
            for name in names:
                basename = PurePosixPath(name).name
                speaker = basename.split('_nohash_', 1)[0]
                if speaker in used_speakers:
                    continue
                audio = archive.read(name)
                sample_id = f'{word}-{count + 1:02d}'
                (args.out / f'{sample_id}-original.wav').write_bytes(audio)
                corpus.append({'id': sample_id, 'text': word, 'language': 'en',
                               'archive_entry': name, 'speaker': speaker,
                               'fixture': 'human Speech Commands audio; dataset word label, not independently verified',
                               'sha256': hashlib.sha256(audio).hexdigest()})
                used_speakers.add(speaker)
                count += 1
                if count == args.per_word:
                    break
            if count != args.per_word:
                raise ValueError(f'Insufficient distinct speakers for {word}')
    (args.out / 'corpus.json').write_text(json.dumps(corpus, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'samples': len(corpus), 'distinct_speakers': len(used_speakers),
                      'reference': 'dataset labels, not independently verified'}))


if __name__ == '__main__':
    main()

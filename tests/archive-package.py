"""Create and verify a ZIP using the package's SHA256SUMS.txt manifest."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile


def digest_stream(f):
    h = hashlib.sha256()
    for block in iter(lambda: f.read(1024 * 1024), b''):
        h.update(block)
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--package', required=True, type=Path)
    p.add_argument('--out', required=True, type=Path)
    a = p.parse_args()
    root = a.package.resolve(); output = a.out.resolve()
    if output.exists() or output.with_suffix(output.suffix+'.sha256').exists():
        raise FileExistsError('Refusing to overwrite an existing archive or checksum')
    if root in output.parents:
        raise ValueError('Archive must be outside package directory')
    manifest = {}
    for line in (root/'SHA256SUMS.txt').read_text(encoding='ascii').splitlines():
        digest, name = line.split('  ', 1)
        if name in manifest or name.startswith('/') or '..' in Path(name).parts:
            raise ValueError('Invalid manifest path')
        manifest[name] = digest
    files = sorted(p for p in root.rglob('*') if p.is_file())
    names = {p.relative_to(root).as_posix() for p in files}
    if names != set(manifest) | {'SHA256SUMS.txt'}:
        raise ValueError('Manifest does not match package contents')
    for path in files:
        name = path.relative_to(root).as_posix()
        if name in manifest:
            with path.open('rb') as f:
                if digest_stream(f) != manifest[name]: raise ValueError(f'Package hash mismatch: {name}')
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=3, allowZip64=True) as z:
        for path in files:
            name = path.relative_to(root).as_posix()
            z.write(path, name)
            print('archived', name, flush=True)
    with zipfile.ZipFile(output) as z:
        if set(z.namelist()) != names or len(z.namelist()) != len(names):
            raise ValueError('Archive entries do not match package')
        for name, expected in manifest.items():
            with z.open(name) as f:
                if digest_stream(f) != expected: raise ValueError(f'Archive hash mismatch: {name}')
        if z.read('SHA256SUMS.txt') != (root/'SHA256SUMS.txt').read_bytes():
            raise ValueError('Archive manifest mismatch')
    with output.open('rb') as f: archive_hash = digest_stream(f)
    output.with_suffix(output.suffix+'.sha256').write_text(archive_hash+'  '+output.name+'\n', encoding='ascii')
    print(json.dumps({'status':'PASS_ARCHIVE_INTEGRITY', 'entries':len(names), 'bytes':output.stat().st_size,
                      'sha256':archive_hash, 'note':'File integrity only; does not prove the application works.'}))


if __name__ == '__main__':
    main()

"""Package and read back the CPU trial distribution; refuses overwrites."""
import hashlib
import json
import pathlib
import zipfile
import argparse

parser=argparse.ArgumentParser()
parser.add_argument('--source',type=pathlib.Path,default=pathlib.Path('dist'))
parser.add_argument('--archive',type=pathlib.Path,default=pathlib.Path('build/packaging/HebEN-CPU-trial.zip'))
parser.add_argument('--receipt',type=pathlib.Path,default=pathlib.Path('build/evidence/release-zip.json'))
args=parser.parse_args()
root=args.source
destination=args.archive
receipt=args.receipt
if destination.exists() or receipt.exists(): raise SystemExit('Refusing to overwrite existing package or receipt')
destination.parent.mkdir(parents=True,exist_ok=True);receipt.parent.mkdir(parents=True,exist_ok=True)
files=sorted(p for p in root.rglob('*') if p.is_file())
def digest(stream):
    h=hashlib.sha256()
    for block in iter(lambda:stream.read(1024*1024),b''): h.update(block)
    return h.hexdigest()
manifest={}
with zipfile.ZipFile(destination,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=True) as z:
    for p in files:
        name=p.relative_to(root).as_posix()
        with p.open('rb') as f: manifest[name]=digest(f)
        z.write(p,name)
with zipfile.ZipFile(destination,'r') as z:
    if set(z.namelist())!=set(manifest): raise RuntimeError('ZIP entries differ from distribution')
    for name,expected in manifest.items():
        with z.open(name) as f: actual=digest(f)
        if actual!=expected: raise RuntimeError('ZIP read-back hash mismatch: '+name)
with destination.open('rb') as f: archive_hash=digest(f)
receipt.write_text(json.dumps({'archive':str(destination),'archive_sha256':archive_hash,'archive_bytes':destination.stat().st_size,
                               'entry_sha256':manifest,'status':'VERIFIED read-back of every ZIP entry'},indent=2),encoding='utf8')
print('Verified:',destination,'bytes:',destination.stat().st_size)

"""Create deterministic synthetic 16-kHz noise and silence fixtures; test-only."""
import json
import pathlib
import random
import struct
import wave

out = pathlib.Path('build/synthetic-noise')
out.mkdir(parents=True, exist_ok=True)
random.seed(20261103)
specs = [('silence', 0.0), ('low-noise', 0.004), ('loud-noise', 0.02)]
for name, rms in specs:
    path = out/(name+'.wav')
    if path.exists():
        raise SystemExit(f'Refusing to overwrite {path}')
    data = bytearray()
    for _ in range(5*16000):
        sample = max(-32768,min(32767,round(random.gauss(0,rms)*32767)))
        data.extend(struct.pack('<h',sample))
    with wave.open(str(path),'wb') as w:
        w.setparams((1,2,16000,0,'NONE','not compressed'))
        w.writeframes(data)
    print(path)
(out/'metadata.json').write_text(json.dumps({'seed':20261103,'midpoint_seed':20261104,'duration_seconds':5,'type':'Gaussian synthetic noise, not a real acoustic environment','requested_rms':dict(specs + [('mid-noise',0.008)])},indent=2),encoding='utf8')
# Separate seed for the midpoint fixture, so it can be recreated independently.
path = out/'mid-noise.wav'
if path.exists():
    raise SystemExit(f'Refusing to overwrite {path}')
rng = random.Random(20261104)
data = b''.join(struct.pack('<h', max(-32768, min(32767, round(rng.gauss(0,.008)*32767)))) for _ in range(5*16000))
with wave.open(str(path),'wb') as w:
    w.setparams((1,2,16000,0,'NONE','not compressed')); w.writeframes(data)

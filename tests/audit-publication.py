"""Fail on common sensitive artifacts or credentials in the Git index.

This is a safety aid, not a substitute for reviewing staged changes.
"""
import re
import subprocess
import sys

paths=subprocess.check_output(['git','diff','--cached','--name-only','-z']).decode('utf-8').split('\0')
paths=[p for p in paths if p]
forbidden=re.compile(r'(^|/)(build|dist|models|__pycache__)/|\.(wav|flac|mp3|ogg|pyc|exe|dll|zip|bin|onnx|pem|key|pfx|etl|dmp)$',re.I)
patterns=[re.compile(x,re.I) for x in (
    r'github_pat_[A-Za-z0-9_]{30,}',r'gh[pousr]_[A-Za-z0-9]{30,}',
    r'sk-[A-Za-z0-9_-]{30,}',r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',
    r'[A-Z]:[\\/]Users[\\/][^\\/\s]+',
)]
bad=[]
for path in paths:
    if forbidden.search(path):bad.append((path,'forbidden artifact'));continue
    content=subprocess.check_output(['git','show',':'+path])
    if b'\0' in content:continue
    text=content.decode('utf-8',errors='replace')
    if any(pattern.search(text) for pattern in patterns):bad.append((path,'sensitive pattern; content omitted'))
if bad:
    for path,reason in bad:print(f'{path}: {reason}')
    sys.exit(1)
print(f'PASS: scanned {len(paths)} staged paths; no selected secrets or generated artifacts found.')

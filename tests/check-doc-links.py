"""Check simple repository-relative Markdown links in the public documentation."""
from pathlib import Path
import re
import sys
from urllib.parse import unquote

root=Path(__file__).resolve().parent.parent
documents=[root/'README.md',root/'README.he.md',*(root/'docs').glob('*.md')]
public={'BENCHMARKS.md','DEVELOPMENT.md','LICENSING.md','RELEASE-NOTES.md'}
broken=[]
for document in documents:
    if document.parent.name=='docs' and document.name not in public:continue
    for target in re.findall(r'\]\(([^)]+)\)',document.read_text(encoding='utf8')):
        if target.startswith(('http://','https://','mailto:','#')):continue
        path=unquote(target.split('#',1)[0].split(' ',1)[0])
        if path and not (document.parent/path).exists():broken.append((str(document.relative_to(root)),target))
if broken:
    for document,target in broken:print(f'{document}: missing {target}')
    sys.exit(1)
print('PASS: public Markdown relative links resolve locally.')

import importlib.util
from pathlib import Path

spec=importlib.util.spec_from_file_location('errors',Path(__file__).with_name('rootcause-errors.py'))
errors=importlib.util.module_from_spec(spec);spec.loader.exec_module(errors)

ops=errors.align(['hello','there'],['hello','world','again'])
assert [x[0] for x in ops].count('substitution')==1
assert [x[0] for x in ops].count('insertion')==1
assert [x[0] for x in errors.align(['one','two'],['one'])].count('deletion')==1
assert errors.normalise('שָׁלוֹם\u200f,  world!', 'local')=='שלום world'
assert errors.normalise('צה"ל', 'local')=='צה ל'
assert errors.normalise('צה"ל', 'ivrit_like')=='צהל'
assert errors.normalise("don't",'ivrit_like')=='dont'
assert errors.normalise('Text (noise) [music] remains','ivrit_like')=='text remains'
print('Error decomposition and normalization checks passed.')

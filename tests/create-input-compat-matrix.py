"""Create an honest pending matrix; never marks a destination as passed."""
import argparse
import csv
import json
from pathlib import Path


TARGETS=[
 ('Notepad','present','BLOCKED','Requires controlled foreground editor and actual text readback.'),
 ('Chrome ordinary input','present','BLOCKED','Requires controlled Chrome field and actual text readback.'),
 ('Chrome textarea','present','BLOCKED','Requires controlled Chrome field and actual text readback.'),
 ('ChatGPT web composer','previously observed','BLOCKED','Prior UIA metadata saw a writable Edit, but fixed-string insertion has not been tested.'),
 ('ChatGPT desktop','installation not established','NOT TESTED','No installed ChatGPT desktop target has been established by this inventory.'),
 ('VS Code editor','present','BLOCKED','Requires a controlled editor and actual text readback.'),
 ('VS Code integrated terminal','terminal safety unknown','BLOCKED','A terminal prompt cannot be assumed non-sensitive from generic accessibility metadata.'),
 ('Codex CLI in Windows Terminal','previously observed host','BLOCKED','The observed Windows Terminal Text control is refused; its password state cannot be established safely.'),
 ('Codex VS Code extension','extension process observed','BLOCKED','The active composer and safe target require controlled identification and readback.'),
 ('Codex desktop','app package observed','BLOCKED','The desktop composer has not been identified or tested with a controlled field.'),
 ('Codex web','availability not established','NOT TESTED','A controlled Codex web composer has not been selected or tested.'),
 ('Windows Terminal','present','BLOCKED','The terminal surface can host password prompts and is not safely classified by its UIA Text provider.'),
 ('PowerShell terminal','present','BLOCKED','A terminal prompt cannot be assumed non-sensitive; no command may be executed.'),
 ('Electron text editor','candidate not established','NOT TESTED','An independent controlled Electron text editor has not been selected.'),
 ('Multiline RTL editor','candidate not established','BLOCKED','Requires a controlled RTL editing field and visual or programmatic readback.'),
]
SCENARIOS=('fixed_unicode_string','hebrew','english','mixed_direction','numbers_punctuation','insert_beginning',
           'insert_middle','insert_end','existing_selection','long_text','rapid_repetition','focus_change',
           'non_bmp','clipboard_unchanged','no_enter_or_submission','password_refusal','elevated_target_refusal')


def main():
    p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path);a=p.parse_args()
    if a.out.exists():raise FileExistsError(a.out)
    a.out.parent.mkdir(parents=True,exist_ok=True)
    rows=[{'target':t,'availability':availability,'scenario':scenario,'status':status,'reason':reason,
           'editor_readback':'NOT PERFORMED'} for t,availability,status,reason in TARGETS for scenario in SCENARIOS]
    with a.out.open('x',encoding='utf-8-sig',newline='') as f:
        w=csv.DictWriter(f,fieldnames=rows[0]);w.writeheader();w.writerows(rows)
    print(json.dumps({'targets':len(TARGETS),'scenarios_per_target':len(SCENARIOS),'rows':len(rows),
                      'pass':0,'note':'Pending matrix; no per-target test is claimed as passed.'}))


if __name__=='__main__':main()

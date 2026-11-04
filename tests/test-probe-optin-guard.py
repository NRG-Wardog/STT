"""Check launcher consent gates using validate-only mode; never launch a probe."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    p=argparse.ArgumentParser();p.add_argument('--clipboard',required=True,type=Path);p.add_argument('--trusted',required=True,type=Path)
    p.add_argument('--unicode',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
    if a.out.exists():raise FileExistsError(a.out)
    base=['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File','tests/launch-fixed-input-probe.ps1']
    def run(directory,*flags):
        r=subprocess.run(base+['-Directory',str(directory),'-ValidateOnly',*flags],capture_output=True,text=True)
        return r.returncode,r.stdout+r.stderr
    c,ct=run(a.clipboard);t,tt=run(a.trusted);ca,cat=run(a.clipboard,'-AllowClipboard')
    ta,tat=run(a.trusted,'-AllowTrustedTerminal');u,ut=run(a.unicode)
    checks={'clipboard_refused_without_optin':c!=0 and '-AllowClipboard' in ct,
            'terminal_refused_without_optin':t!=0 and '-AllowTrustedTerminal' in tt,
            'clipboard_validate_only_with_optin':ca==0 and 'VALIDATED_WITHOUT_LAUNCH' in cat,
            'terminal_validate_only_with_optin':ta==0 and 'VALIDATED_WITHOUT_LAUNCH' in tat,
            'unicode_validate_only':u==0 and 'VALIDATED_WITHOUT_LAUNCH' in ut}
    result={'status':'PASS_OPTIN_GUARDS' if all(checks.values()) else 'FAIL',
            'scope':'validate-only; no process, hotkey, clipboard or input events',**checks}
    a.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8');print(json.dumps(result))
    if result['status']!='PASS_OPTIN_GUARDS':raise SystemExit(1)


if __name__=='__main__':main()

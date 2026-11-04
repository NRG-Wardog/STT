"""Verify clipboard transaction in a child private window station, not the user's clipboard."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
import time


def main():
    p=argparse.ArgumentParser();p.add_argument('--exe',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
    if a.out.exists():raise FileExistsError(a.out)
    user=ctypes.WinDLL('user32',use_last_error=True)
    user.GetClipboardSequenceNumber.restype=ctypes.c_uint
    user.GetForegroundWindow.restype=ctypes.c_void_p
    before=user.GetClipboardSequenceNumber();foreground=user.GetForegroundWindow()
    started=time.perf_counter()
    run=subprocess.run([str(a.exe.resolve())],capture_output=True,text=True,timeout=20,creationflags=subprocess.CREATE_NO_WINDOW)
    elapsed=time.perf_counter()-started;after=user.GetClipboardSequenceNumber()
    try:detail=json.loads(run.stdout.strip())
    except json.JSONDecodeError:detail={'status':'NO_VALID_CHILD_RESULT'}
    checks=('isolated','snapshot_and_set','privacy_marker','restore_original','newer_content_preserved','unsupported_format_refused','newline_refused')
    ok=run.returncode==0 and all(detail.get(k) is True for k in checks) and before==after
    result={'status':'PASS_PRIVATE_CLIPBOARD' if ok else 'FAIL_OR_BLOCKED','scope':'private noninteractive child station; no user clipboard content read, no paste, no editor',
            'exe_sha256':hashlib.sha256(a.exe.read_bytes()).hexdigest(),'elapsed_seconds':elapsed,'child_exit_code':run.returncode,
            'interactive_clipboard_sequence_unchanged':before==after,'foreground_unchanged_at_observation':foreground==user.GetForegroundWindow(),
            'child':detail,'stderr':run.stderr[-2000:]}
    a.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8');print(json.dumps(result))
    if not ok:raise SystemExit(1)


if __name__=='__main__':main()

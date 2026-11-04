"""Actual OS hotkey/input into an isolated VS Code editor, with file readback.

Runs only after the desktop is idle, uses an isolated VS Code profile and a
test-owned file, and aborts if foreground changes. Never sends Enter or uses
the clipboard. This is the fixed-string probe, not microphone or ASR.
"""
import argparse
import array
import ctypes
from ctypes import wintypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import psutil
import subprocess
import time
import uuid
import wave

ROOT=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('desktop_common',ROOT/'tests/desktop-chrome-unicode.py')
common=importlib.util.module_from_spec(spec);spec.loader.exec_module(common)


def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--probe-dir',type=Path,required=True)
    p.add_argument('--min-idle-seconds',type=int,default=90);p.add_argument('--wait-idle-seconds',type=int,default=0)
    p.add_argument('--app',choices=('vscode','notepad'),default='vscode')
    p.add_argument('--scenario',choices=('blank','middle','selection'),default='blank')
    p.add_argument('--live-pid',type=int);p.add_argument('--live-cycles',type=int,default=1)
    p.add_argument('--expected-sha256',default='')
    p.add_argument('--audio-wav',type=Path,help='Use the test-only synthetic PCM fixture; no physical microphone')
    p.add_argument('--physical-fixture',action='store_true',help='Use a separate test-only physical-microphone instance')
    p.add_argument('--human-speech',action='store_true',help='User is present; wait for explicit test signal and use the physical fixture')
    p.add_argument('--supervised',action='store_true',help='Explicitly authorized supervised human test; permits focus during recent input')
    p.add_argument('--reference',default='');a=p.parse_args()
    if a.min_idle_seconds<60 or not 0<=a.wait_idle_seconds<=600:raise ValueError('Invalid idle guard')
    if a.live_pid and (a.app!='notepad' or a.scenario!='blank' or not a.expected_sha256 or not 1<=a.live_cycles<=100):raise ValueError('Live cycles require blank Notepad, exact hash, and 1..100 cycles')
    if a.audio_wav and (a.live_pid or a.app!='notepad' or a.scenario!='blank' or not a.reference):raise ValueError('Synthetic audio needs a blank owned Notepad and a reference')
    if a.physical_fixture and (a.audio_wav or a.live_pid or a.app!='notepad' or a.scenario!='blank'):raise ValueError('Physical fixture needs a blank owned Notepad and no other capture mode')
    if a.human_speech and (not a.physical_fixture or not a.supervised or not a.reference):raise ValueError('Human speech requires supervised physical fixture and reference')
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
    live=psutil.Process(a.live_pid) if a.live_pid else None
    exe=Path(live.exe()).resolve() if live else (a.probe_dir/('HebENDesktopMicFixture.exe' if a.physical_fixture else 'HebENDesktopAudioFixture.exe' if a.audio_wav else 'HebENFixedInputProbe.exe')).resolve()
    meta={'sha256':a.expected_sha256.lower()} if live else json.loads((a.probe_dir/'metadata.json').read_text(encoding='utf-8-sig'))
    if hashlib.sha256(exe.read_bytes()).hexdigest()!=meta['sha256']:raise ValueError('Probe hash mismatch')
    ini=(a.probe_dir/'config.ini').read_text(encoding='utf-8-sig').lower()
    if (('hotkey=ctrl+shift+r' if live else 'hotkey=ctrl+alt+shift+f12') not in ini or 'input_method=unicode' not in ini or (not live and 'trusted_terminal_input=0' not in ini)):raise ValueError('Unexpected test settings')
    code=(Path(os.environ['LOCALAPPDATA'])/'Programs/Microsoft VS Code/Code.exe' if a.app=='vscode'
          else Path(os.environ['WINDIR'])/'System32/notepad.exe')
    if not code.is_file():raise FileNotFoundError(code)
    user=ctypes.WinDLL('user32',use_last_error=True);kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    user.GetForegroundWindow.restype=wintypes.HWND;user.GetClipboardSequenceNumber.restype=wintypes.DWORD
    user.GetLastInputInfo.argtypes=[ctypes.POINTER(common.LASTINPUTINFO)];kernel.GetTickCount64.restype=ctypes.c_ulonglong
    kernel.CreateEventW.argtypes=[ctypes.c_void_p,wintypes.BOOL,wintypes.BOOL,wintypes.LPCWSTR];kernel.CreateEventW.restype=wintypes.HANDLE
    kernel.SetEvent.argtypes=[wintypes.HANDLE];kernel.WaitForSingleObject.argtypes=[wintypes.HANDLE,wintypes.DWORD]
    kernel.WaitForSingleObject.restype=wintypes.DWORD;kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user.GetClassNameW.argtypes=[wintypes.HWND,wintypes.LPWSTR,ctypes.c_int]
    user.GetWindowTextW.argtypes=[wintypes.HWND,wintypes.LPWSTR,ctypes.c_int]
    user.SetWindowTextW.argtypes=[wintypes.HWND,wintypes.LPCWSTR];user.SetWindowTextW.restype=wintypes.BOOL
    user.ShowWindow.argtypes=[wintypes.HWND,ctypes.c_int];user.SetForegroundWindow.argtypes=[wintypes.HWND]
    user.SendInput.argtypes=[wintypes.UINT,ctypes.POINTER(common.INPUT),ctypes.c_int];user.SendInput.restype=wintypes.UINT
    user.FindWindowW.argtypes=[wintypes.LPCWSTR,wintypes.LPCWSTR];user.FindWindowW.restype=wintypes.HWND
    user.GetPropW.argtypes=[wintypes.HWND,wintypes.LPCWSTR];user.GetPropW.restype=wintypes.HANDLE
    user.GetWindowThreadProcessId.restype=wintypes.DWORD
    user.SendMessageTimeoutW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM,wintypes.UINT,wintypes.UINT,ctypes.POINTER(ctypes.c_size_t)]
    user.SendMessageTimeoutW.restype=ctypes.c_void_p
    user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
    callback=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM);user.EnumWindows.argtypes=[callback,wintypes.LPARAM]
    def idle_seconds():
        x=common.LASTINPUTINFO(ctypes.sizeof(common.LASTINPUTINFO),0)
        if not user.GetLastInputInfo(ctypes.byref(x)):raise OSError(ctypes.get_last_error(),'GetLastInputInfo')
        return ((kernel.GetTickCount64()&0xffffffff)-x.dwTime&0xffffffff)/1000
    def last_tick():
        x=common.LASTINPUTINFO(ctypes.sizeof(common.LASTINPUTINFO),0)
        if not user.GetLastInputInfo(ctypes.byref(x)):raise OSError(ctypes.get_last_error(),'GetLastInputInfo')
        return x.dwTime
    deadline=time.monotonic()+a.wait_idle_seconds
    while not a.human_speech and idle_seconds()<a.min_idle_seconds and time.monotonic()<deadline:time.sleep(2)
    before=user.GetForegroundWindow();clip=user.GetClipboardSequenceNumber();owned=probe=cue=None;hwnd=owned_log=app=None;started_live=False;cue_handles=[]
    result={'status':'BLOCKED','mode':('real running-app hotkey and physical microphone cycles in owned Notepad' if live else
            'test-only parallel physical microphone, real hotkey and owned Notepad' if a.physical_fixture else
            'test-only synthetic PCM through real hotkey, VAD, ASR and Windows input to owned Notepad; no microphone' if a.audio_wav else
            'real Windows fixed-probe hotkey to owned '+a.app+' file; no microphone or ASR'),
            'idle_seconds_before':idle_seconds(),'probe_sha256':meta['sha256']}
    try:
        if not a.human_speech and idle_seconds()<a.min_idle_seconds:raise RuntimeError('Recent user input; no window launched')
        document=out/'test-document.txt'
        initial={'blank':'','middle':'LEFT RIGHT','selection':'LEFT REPLACE RIGHT'}[a.scenario]
        expected={'blank':a.reference if a.audio_wav else common.EXPECTED,'middle':'LEFT '+common.EXPECTED+'RIGHT',
                  'selection':'LEFT '+common.EXPECTED+' RIGHT'}[a.scenario]
        document.write_text(initial,encoding='utf8')
        if a.app=='vscode':
            profile=out/'profile';settings=profile/'User';settings.mkdir(parents=True)
            (settings/'settings.json').write_text(json.dumps({'files.autoSave':'afterDelay','files.autoSaveDelay':250,
                'workbench.startupEditor':'none','telemetry.telemetryLevel':'off','update.mode':'none'}),encoding='utf8')
            args=[str(code),'--new-window','--disable-extensions','--user-data-dir',str(profile),
                  '--extensions-dir',str(out/'extensions'),str(document)]
        else:
            if any((p.info.get('name') or '').lower()=='notepad.exe' for p in psutil.process_iter(['name'])):
                raise RuntimeError('Existing Notepad process could receive the file; test did not launch')
            args=[str(code),str(document)]
        si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=7
        owned_log=(out/'owned-app.log').open('wb')
        owned=subprocess.Popen(args,cwd=out,startupinfo=si,stdout=owned_log,stderr=subprocess.STDOUT)
        result['owned_launcher_pid']=owned.pid
        handles=[];owned_pids={owned.pid};observed_windows=[];window_pid=owned.pid
        @callback
        def visit(h,unused):
            owner=wintypes.DWORD();user.GetWindowThreadProcessId(h,ctypes.byref(owner))
            name=ctypes.create_unicode_buffer(128);user.GetClassNameW(h,name,128)
            if owner.value in owned_pids:
                observed_windows.append({'pid':owner.value,'class':name.value})
                if name.value==('Chrome_WidgetWin_1' if a.app=='vscode' else 'Notepad'):handles.append((h,owner.value))
            return True
        for _ in range(300):
            try:owned_pids={owned.pid,*[p.pid for p in psutil.Process(owned.pid).children(recursive=True)]}
            except psutil.Error:pass
            handles.clear();user.EnumWindows(visit,0)
            if handles:break
            if owned.poll() is not None:
                result['owned_exit_code']=owned.returncode
                raise RuntimeError('Isolated test application exited before window')
            time.sleep(.1)
        if not handles:
            result['owned_window_metadata']=observed_windows[-20:]
            raise TimeoutError('No window owned by isolated test process tree')
        hwnd,window_pid=handles[0]
        result['owned_window_pid']=window_pid;result['owned_window_handle']=int(hwnd)
        if a.app=='notepad':
            title=ctypes.create_unicode_buffer(512)
            for _ in range(50):
                user.GetWindowTextW(hwnd,title,512)
                if document.name in title.value:break
                time.sleep(.1)
            if document.name not in title.value:raise RuntimeError('Notepad title does not identify the owned test file')
        if user.GetForegroundWindow() not in (before,hwnd) or (not a.human_speech and idle_seconds()<a.min_idle_seconds):raise RuntimeError('User input or focus change before test')
        user.ShowWindow(hwnd,9)
        if user.GetForegroundWindow()!=hwnd:user.SetForegroundWindow(hwnd)
        if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Windows denied foreground to isolated VS Code')
        focused=None
        for _ in range(40):
            if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed while preparing editor')
            helper=([str(ROOT/'build/candidate/Release/HebENFocusOwnedClass.exe'),str(hwnd),str(window_pid),'native-edit-context'] if a.app=='vscode'
                    else [str(ROOT/'build/candidate/Release/HebENFocusOwnedNotepad.exe'),str(hwnd),str(window_pid)])
            focused=subprocess.run(helper,capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=10)
            if focused.returncode==0:break
            time.sleep(.25)
        if focused:result['focus_metadata']=focused.stdout.decode('utf8',errors='replace').strip()
        if not focused or focused.returncode:raise RuntimeError('Could not confirm focus on the owned editor')
        if a.scenario!='blank':
            if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed before test cursor setup')
            if a.app=='notepad':
                end='12' if a.scenario=='selection' else '5'
                selection=subprocess.run([str(ROOT/'build/candidate/Release/HebENReadOwnedSelection.exe'),str(hwnd),str(window_pid),'5',end],
                                         capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=5)
                result['selection_metadata']=selection.stdout.decode('utf8',errors='replace').strip()
                if selection.returncode:raise RuntimeError('Cannot independently verify the owned Notepad selection')
                selected=json.loads(result['selection_metadata'])
                required=(5,12) if a.scenario=='selection' else (5,5)
                if (selected['start'],selected['end'])!=required:raise RuntimeError('Cursor or selection setup differs from the requested offsets')
            else:
                raise RuntimeError('Selection setup is implemented only for the owned Notepad fixture')
        trace=out/'probe-trace.jsonl'
        if live:
            app=user.FindWindowW('HebENDictationHiddenClass',None);owner=wintypes.DWORD()
            if app:user.GetWindowThreadProcessId(app,ctypes.byref(owner))
            if not app or owner.value!=a.live_pid or user.GetPropW(app,'HebEN.Capturing'):raise RuntimeError('Live app identity mismatch or capture already active')
        else:
            env=dict(os.environ,HEBEN_DIAGNOSTICS_PATH=str(trace))
            if a.audio_wav or a.physical_fixture:env['PATH']=str(ROOT/'build/recovery-input-live-v2')+os.pathsep+env.get('PATH','')
            probe=subprocess.Popen([str(exe)],cwd=exe.parent,env=env,creationflags=subprocess.CREATE_NO_WINDOW)
            for _ in range(100):
                if trace.exists() and '"hotkey_register","result":"ok"' in trace.read_text(encoding='utf8'):break
                if probe.poll() is not None:raise RuntimeError('Probe exited')
                time.sleep(.05)
            else:raise RuntimeError('Probe shortcut not registered')
        if a.human_speech:
            (out/'ready.json').write_text(json.dumps({'status':'READY_FOR_SIGNAL','owned_window_pid':window_pid,'fixture_pid':probe.pid})+'\n',encoding='utf8')
            deadline=time.monotonic()+90
            while not (out/'go.signal').exists() and time.monotonic()<deadline:
                time.sleep(.05)
            if not (out/'go.signal').exists():raise TimeoutError('Human speech signal not received')
            user.ShowWindow(hwnd,9)
            if user.GetForegroundWindow()!=hwnd:user.SetForegroundWindow(hwnd)
            if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Could not activate the supervised test window')
            focused=subprocess.run([str(ROOT/'build/candidate/Release/HebENFocusOwnedNotepad.exe'),str(hwnd),str(window_pid)],
                                   capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=10)
            if focused.returncode:raise RuntimeError('Could not refocus the supervised test document')
            nonce=uuid.uuid4().hex;names=['Local\\HebENCue-'+nonce+'-'+x for x in ('begin','ready','finish')]
            cue_handles=[kernel.CreateEventW(None,True,False,name) for name in names]
            if not all(cue_handles):raise RuntimeError('Could not create supervised cue events')
            cue=subprocess.Popen([str(ROOT/'build/candidate/Release/HebENHumanSpeechCue.exe'),*names,str(hwnd)],
                                 cwd=out,creationflags=subprocess.CREATE_NO_WINDOW)
            deadline=time.monotonic()+15
            while kernel.WaitForSingleObject(cue_handles[0],100)!=0:
                if cue.poll() is not None:raise RuntimeError('Visual cue exited before countdown')
                if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed during countdown')
                if time.monotonic()>deadline:raise TimeoutError('Visual cue countdown timed out')
        if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed before hotkey')
        keys=([(0x11,0),(0x10,0),(0x52,0),(0x52,2),(0x10,2),(0x11,2)] if live else
              [(0x11,0),(0x12,0),(0x10,0),(0x7b,0),(0x7b,2),(0x10,2),(0x12,2),(0x11,2)])
        inputs=(common.INPUT*len(keys))(*[common.INPUT(type=1,ki=common.KEYBDINPUT(wVk=k,dwFlags=f)) for k,f in keys])
        sent=user.SendInput(len(keys),inputs,ctypes.sizeof(common.INPUT))
        if sent!=len(keys):raise OSError(ctypes.get_last_error(),'Hotkey send incomplete')
        if live:
            cycles=[];interrupted=False
            for _ in range(100):
                if user.GetPropW(app,'HebEN.Capturing'):started_live=True;break
                if user.GetForegroundWindow()!=hwnd:break
                time.sleep(.02)
            if not started_live:raise RuntimeError('Initial hotkey did not start capture')
            time.sleep(.05);expected_tick=last_tick()
            for cycle in range(1,a.live_cycles+1):
                time.sleep(.12)
                if user.GetForegroundWindow()!=hwnd or last_tick()!=expected_tick:interrupted=True;break
                if user.SendInput(len(keys),inputs,ctypes.sizeof(common.INPUT))!=len(keys):raise RuntimeError('Cycle stop hotkey incomplete')
                for _ in range(100):
                    if not user.GetPropW(app,'HebEN.Capturing'):break
                    time.sleep(.02)
                if user.GetPropW(app,'HebEN.Capturing'):raise RuntimeError('Cycle did not stop')
                started_live=False;time.sleep(.05);expected_tick=last_tick();mem=live.memory_info();cpu=live.cpu_times()
                cycles.append({'cycle':cycle,'utc':time.time(),'private_bytes':mem.private,'working_set_bytes':mem.rss,
                               'cpu_seconds':cpu.user+cpu.system,'handles':live.num_handles(),'threads':live.num_threads()})
                with (out/'cycles.jsonl').open('a',encoding='utf8') as raw:raw.write(json.dumps(cycles[-1])+'\n')
                if cycle<a.live_cycles:
                    if user.GetForegroundWindow()!=hwnd or last_tick()!=expected_tick:interrupted=True;break
                    if user.SendInput(len(keys),inputs,ctypes.sizeof(common.INPUT))!=len(keys):raise RuntimeError('Cycle start hotkey incomplete')
                    for _ in range(100):
                        if user.GetPropW(app,'HebEN.Capturing'):started_live=True;break
                        if user.GetForegroundWindow()!=hwnd:break
                        time.sleep(.02)
                    if not started_live:raise RuntimeError('Cycle did not start')
                    time.sleep(.05);expected_tick=last_tick()
            result.update(status='PASS_REAL_HOTKEY_MIC_CYCLES' if len(cycles)==a.live_cycles and not interrupted else 'BLOCKED_OR_INTERRUPTED_CYCLES',
                          requested_cycles=a.live_cycles,completed_cycles=len(cycles),interrupted=interrupted,cycles=cycles,
                          note='Actual registered hotkey and physical microphone; no audio saved; no speech-recognition accuracy claim.')
            return
        if a.physical_fixture:
            app=user.FindWindowW('HebENDesktopMicTestHiddenClass',None);owner=wintypes.DWORD()
            if app:user.GetWindowThreadProcessId(app,ctypes.byref(owner))
            if not app or owner.value!=probe.pid:raise RuntimeError('Microphone fixture identity mismatch')
            for _ in range(100):
                if user.GetPropW(app,'HebEN.Capturing'):break
                if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed while starting physical microphone')
                time.sleep(.05)
            else:raise RuntimeError('Hotkey did not start physical microphone fixture')
            if a.human_speech:kernel.SetEvent(cue_handles[1])
            start=time.monotonic()
            while time.monotonic()-start<(20 if a.human_speech else 4) and user.GetPropW(app,'HebEN.Capturing'):
                if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed during physical microphone test')
                time.sleep(.1)
            if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed before stopping physical microphone')
            if user.GetPropW(app,'HebEN.Capturing'):
                if user.SendInput(len(keys),inputs,ctypes.sizeof(common.INPUT))!=len(keys):raise RuntimeError('Stopping hotkey incomplete')
            for _ in range(100):
                if not user.GetPropW(app,'HebEN.Capturing'):break
                time.sleep(.05)
            if user.GetPropW(app,'HebEN.Capturing'):raise RuntimeError('Physical microphone did not stop')
            if a.human_speech:kernel.SetEvent(cue_handles[2])
            events=[json.loads(x) for x in trace.read_text(encoding='utf8').splitlines() if x.strip()]
            frames=[x for x in events if (x['stage'],x['result'])==('pcm','capture_stopped_frames_peak_millionths')]
            voice=any((x['stage'],x['result'])==('vad','speech_detected') for x in events)
            passed=bool(frames and frames[-1]['value']>0)
            if a.human_speech:
                # Wait for the final ASR job, then save only the test-owned file.
                for _ in range(150):
                    if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed while awaiting speech result')
                    current=[json.loads(x) for x in trace.read_text(encoding='utf8').splitlines() if x.strip()]
                    pending=ctypes.c_size_t()
                    queried=user.SendMessageTimeoutW(app,0x8000+43,0,0,2,2000,ctypes.byref(pending))
                    if queried and pending.value==0 and any((x['stage'],x['result'])==('asr','finished') for x in current) and any(x['stage']=='sendinput' and x['result']=='events_requested' for x in current):break
                    time.sleep(.1)
                time.sleep(.5)
                if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed before saving test file')
                savekeys=[(0x11,0),(0x53,0),(0x53,2),(0x11,2)]
                save=(common.INPUT*len(savekeys))(*[common.INPUT(type=1,ki=common.KEYBDINPUT(wVk=k,dwFlags=f)) for k,f in savekeys])
                if user.SendInput(len(savekeys),save,ctypes.sizeof(common.INPUT))!=len(savekeys):raise RuntimeError('Test file save incomplete')
                text=''
                for _ in range(100):
                    text=document.read_text(encoding='utf-8-sig')
                    if text:break
                    time.sleep(.1)
                result.update(status='PASS_PHYSICAL_SPEECH_EDITOR' if passed and voice and common.normalized(text)==common.normalized(a.reference) else 'FAIL_PHYSICAL_SPEECH_EDITOR',
                              captured_frames=frames[-1]['value'] if frames else 0,voice_detected=voice,observed=text,expected=a.reference,
                              note='Separate physical-microphone streaming fixture and owned Notepad; no audio saved; not packaged production binary.')
                return
            result.update(status='PASS_FIXTURE_PHYSICAL_PCM' if passed else 'FAIL_FIXTURE_PHYSICAL_PCM',
                          captured_frames=frames[-1]['value'] if frames else 0,voice_detected=voice,
                          note='Separate microphone-enabled streaming fixture; no audio saved; not a speech-to-editor accuracy test.')
            return
        if a.audio_wav:
            app=user.FindWindowW('HebENDesktopAudioTestHiddenClass',None);owner=wintypes.DWORD()
            if app:user.GetWindowThreadProcessId(app,ctypes.byref(owner))
            if not app or owner.value!=probe.pid:raise RuntimeError('Audio fixture identity mismatch')
            for _ in range(100):
                if user.GetPropW(app,'HebEN.Capturing'):break
                if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed while starting capture')
                time.sleep(.05)
            else:raise RuntimeError('Hotkey did not start fixture capture')
            with wave.open(str(a.audio_wav),'rb') as w:
                if (w.getnchannels(),w.getsampwidth(),w.getframerate())!=(1,2,16000):raise ValueError('Expected mono PCM16 16 kHz WAV')
                pcm=array.array('h');pcm.frombytes(w.readframes(w.getnframes()))
            class RECT(ctypes.Structure):_fields_=[('left',wintypes.LONG),('top',wintypes.LONG),('right',wintypes.LONG),('bottom',wintypes.LONG)]
            class GUIINFO(ctypes.Structure):
                _fields_=[('cbSize',wintypes.DWORD),('flags',wintypes.DWORD),('hwndActive',wintypes.HWND),('hwndFocus',wintypes.HWND),
                          ('hwndCapture',wintypes.HWND),('hwndMenuOwner',wintypes.HWND),('hwndMoveSize',wintypes.HWND),
                          ('hwndCaret',wintypes.HWND),('rcCaret',RECT)]
            user.GetGUIThreadInfo.argtypes=[wintypes.DWORD,ctypes.POINTER(GUIINFO)]
            thread=user.GetWindowThreadProcessId(hwnd,None);info=GUIINFO();info.cbSize=ctypes.sizeof(info)
            if not user.GetGUIThreadInfo(thread,ctypes.byref(info)) or not info.hwndFocus:raise RuntimeError('Owned editor focus unavailable')
            focus_pid=wintypes.DWORD();user.GetWindowThreadProcessId(info.hwndFocus,ctypes.byref(focus_pid))
            if focus_pid.value!=window_pid:raise RuntimeError('Focused editor owner mismatch')
            def text_length():
                answer=ctypes.c_size_t()
                if not user.SendMessageTimeoutW(info.hwndFocus,0x000e,0,0,2,2000,ctypes.byref(answer)):raise RuntimeError('Cannot read owned editor length')
                return answer.value
            result['audio_sha256']=hashlib.sha256(a.audio_wav.read_bytes()).hexdigest();result['audio_duration_s']=len(pcm)/16000
            start=time.monotonic();observations=[];last_length=0
            for pos in range(0,len(pcm),512):
                if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed during synthetic PCM')
                chunk=array.array('f',(v/32768 for v in pcm[pos:pos+512])).tobytes()
                buffer=ctypes.create_string_buffer(chunk);data=common.COPYDATA(0x48455043,len(chunk),ctypes.cast(buffer,ctypes.c_void_p));answer=ctypes.c_size_t()
                if not user.SendMessageTimeoutW(app,0x004a,0,ctypes.addressof(data),2,15000,ctypes.byref(answer)) or answer.value!=1:raise RuntimeError('Synthetic PCM rejected')
                if pos%(512*8)==0:
                    length=text_length()
                    if length!=last_length:observations.append({'elapsed_s':round(time.monotonic()-start,3),'audio_s':pos/16000,'characters':length});last_length=length
                target=start+(pos+min(512,len(pcm)-pos))/16000
                if target>time.monotonic():time.sleep(target-time.monotonic())
            if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed before stopping capture')
            if user.GetPropW(app,'HebEN.Capturing'):
                if user.SendInput(len(keys),inputs,ctypes.sizeof(common.INPUT))!=len(keys):raise RuntimeError('Stopping hotkey incomplete')
            for _ in range(100):
                if not user.GetPropW(app,'HebEN.Capturing'):break
                time.sleep(.05)
            last_change=time.monotonic()
            for _ in range(150):
                if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed while awaiting ASR')
                length=text_length()
                if length!=last_length:
                    observations.append({'elapsed_s':round(time.monotonic()-start,3),'audio_s':len(pcm)/16000,'characters':length});last_length=length;last_change=time.monotonic()
                if trace.exists() and '"final_hypothesis"' in trace.read_text(encoding='utf8') and time.monotonic()-last_change>1:break
                time.sleep(.1)
            result['audio_observations']=observations
        if a.app=='notepad':
            # The file already exists and is owned by this test. Save only
            # after the target remained foreground and the probe completed.
            for _ in range(120):
                if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed before Notepad save')
                if trace.exists() and '"events_accepted_editor_unverified"' in trace.read_text(encoding='utf8'):break
                time.sleep(.1)
            else:raise TimeoutError('Probe did not complete insertion')
            keys2=[(0x11,0),(0x53,0),(0x53,2),(0x11,2)]
            save=(common.INPUT*len(keys2))(*[common.INPUT(type=1,ki=common.KEYBDINPUT(wVk=k,dwFlags=f)) for k,f in keys2])
            if user.GetForegroundWindow()!=hwnd or user.SendInput(len(keys2),save,ctypes.sizeof(common.INPUT))!=len(keys2):raise RuntimeError('Guarded save was not fully queued')
        text=''
        for _ in range(150):
            if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed while waiting for save')
            text=document.read_text(encoding='utf-8-sig')
            if (common.normalized(text)==common.normalized(expected) if a.audio_wav else text==expected):break
            time.sleep(.1)
        matches=common.normalized(text)==common.normalized(expected) if a.audio_wav else text==expected
        result.update(status='PASS_FILE_READBACK' if matches else 'FAIL_FILE_READBACK',observed=text,
                      expected=expected,scenario=a.scenario,readback='test-owned file via '+('VS Code autosave' if a.app=='vscode' else 'guarded Notepad Ctrl+S'),enter_sent=False)
    except Exception as exc:result['error']=str(exc)
    finally:
        if len(cue_handles)==3 and cue_handles[2]:kernel.SetEvent(cue_handles[2])
        if cue:
            try:cue.wait(timeout=5)
            except subprocess.TimeoutExpired:result['cue_shutdown']='TIMEOUT_left_running'
        for handle in cue_handles:
            if handle:kernel.CloseHandle(handle)
        if started_live and app and hwnd and user.GetForegroundWindow()==hwnd and user.GetPropW(app,'HebEN.Capturing'):
            user.PostMessageW(app,0x0312,1201,0)
        if probe and probe.poll() is None:
            ph=user.FindWindowW('HebENDesktopMicTestHiddenClass' if a.physical_fixture else 'HebENDesktopAudioTestHiddenClass' if a.audio_wav else 'HebENFixedInputProbeHiddenClass',None);owner=wintypes.DWORD()
            if ph:user.GetWindowThreadProcessId(ph,ctypes.byref(owner))
            if ph and owner.value==probe.pid:user.PostMessageW(ph,0x10,0,0)
            try:probe.wait(timeout=10)
            except subprocess.TimeoutExpired:result['probe_shutdown']='TIMEOUT_left_running'
        if hwnd and user.GetForegroundWindow()==hwnd and before:user.SetForegroundWindow(before)
        if owned and owned.poll() is None:
            if hwnd and user.GetForegroundWindow()!=hwnd:user.PostMessageW(hwnd,0x10,0,0)
            try:owned.wait(timeout=10)
            except subprocess.TimeoutExpired:result['vscode_shutdown']='TIMEOUT_left_running'
        if owned_log:owned_log.close()
        result['clipboard_sequence_unchanged']=user.GetClipboardSequenceNumber()==clip
        result['foreground_restored_or_changed_by_user']=user.GetForegroundWindow()!=hwnd
        (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(result,ensure_ascii=True))


if __name__=='__main__':main()

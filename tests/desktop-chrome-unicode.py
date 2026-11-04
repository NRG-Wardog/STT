"""Supervised-by-guard local Chrome OS-input test; never uses the user's profile.

Requires at least 90 seconds since desktop input, an unchanged foreground window,
and a test-owned Chrome foreground target. It sends a real global probe hotkey;
the fixed-input probe uses the same Windows Unicode insertion backend as HebEN.
CDP is used only to load, focus, and read back a local test page.
"""
import argparse
import array
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import psutil
import subprocess
import time
import unicodedata
import urllib.request
import wave
import websocket
import winsound

ROOT=Path(__file__).resolve().parent.parent
CHROME=Path(r'C:\Program Files\Google\Chrome\Application\chrome.exe')
EXPECTED='שלום עולם Hello World 123'


class LASTINPUTINFO(ctypes.Structure):
    _fields_=[('cbSize',wintypes.UINT),('dwTime',wintypes.DWORD)]
class MOUSEINPUT(ctypes.Structure):
    _fields_=[('dx',wintypes.LONG),('dy',wintypes.LONG),('mouseData',wintypes.DWORD),('dwFlags',wintypes.DWORD),('time',wintypes.DWORD),('dwExtraInfo',ctypes.c_size_t)]
class KEYBDINPUT(ctypes.Structure):
    _fields_=[('wVk',wintypes.WORD),('wScan',wintypes.WORD),('dwFlags',wintypes.DWORD),('time',wintypes.DWORD),('dwExtraInfo',ctypes.c_size_t)]
class HARDWAREINPUT(ctypes.Structure):
    _fields_=[('uMsg',wintypes.DWORD),('wParamL',wintypes.WORD),('wParamH',wintypes.WORD)]
class INPUT_UNION(ctypes.Union):
    _fields_=[('mi',MOUSEINPUT),('ki',KEYBDINPUT),('hi',HARDWAREINPUT)]
class INPUT(ctypes.Structure):
    _anonymous_=('u',);_fields_=[('type',wintypes.DWORD),('u',INPUT_UNION)]
class COPYDATA(ctypes.Structure):
    _fields_=[('dwData',ctypes.c_size_t),('cbData',wintypes.DWORD),('lpData',ctypes.c_void_p)]


def normalized(text):
    text=unicodedata.normalize('NFKD',text).lower()
    return ' '.join(''.join(' ' if unicodedata.category(c).startswith('P') else c
                            for c in text if not unicodedata.category(c).startswith('M')).split())


def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--probe-dir',type=Path,required=True)
    p.add_argument('--min-idle-seconds',type=int,default=90)
    p.add_argument('--wait-idle-seconds',type=int,default=0)
    p.add_argument('--audio-wav',type=Path,help='Test-only synthetic audio mode; never uses microphone')
    p.add_argument('--reference',default='',help='Expected transcription for controlled synthetic audio')
    p.add_argument('--live-pid',type=int,help='Sample an existing exact app using its physical microphone; no audio is saved')
    p.add_argument('--live-trace',type=Path,help='Existing diagnostics file for the exact running app')
    p.add_argument('--expected-sha256',default='',help='Required executable SHA-256 in live mode')
    p.add_argument('--live-playback-wav',type=Path,help='Play a quiet local WAV through the default output while the real microphone captures')
    p.add_argument('--playback-gain',type=float,default=.2,help='Local test WAV gain, capped at 0.6; system volume is not changed')
    p.add_argument('--live-cycles',type=int,default=1,help='Controlled real-hotkey/physical-microphone cycles; no audio saved')
    a=p.parse_args()
    if a.min_idle_seconds<60:raise ValueError('minimum desktop idle is 60 seconds')
    if not 0<=a.wait_idle_seconds<=600:raise ValueError('wait must be between zero and 600 seconds')
    if not .05<=a.playback_gain<=.6:raise ValueError('playback gain must be between 0.05 and 0.6')
    if not 1<=a.live_cycles<=100 or (a.live_cycles>1 and (not a.live_pid or a.live_playback_wav)):
        raise ValueError('live cycles require the live app, no playback, and a count of 1..100')
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
    if a.live_playback_wav and not a.live_pid:raise ValueError('live playback requires the live application')
    if a.live_pid and (a.audio_wav or not a.live_trace or not a.expected_sha256):raise ValueError('live mode needs a trace and exact hash and cannot accept synthetic audio')
    live=psutil.Process(a.live_pid) if a.live_pid else None
    exe=Path(live.exe()).resolve() if live else (a.probe_dir/('HebENDesktopAudioFixture.exe' if a.audio_wav else 'HebENFixedInputProbe.exe')).resolve()
    meta={'sha256':a.expected_sha256.lower()} if live else json.loads((a.probe_dir/'metadata.json').read_text(encoding='utf-8-sig'))
    if hashlib.sha256(exe.read_bytes()).hexdigest()!=meta['sha256']:raise ValueError('executable hash mismatch')
    ini=(a.probe_dir/'config.ini').read_text(encoding='utf-8-sig').lower()
    if (('hotkey=ctrl+shift+r' if live else 'hotkey=ctrl+alt+shift+f12') not in ini or 'input_method=unicode' not in ini or (not live and 'trusted_terminal_input=0' not in ini)):
        raise ValueError('unsafe or unexpected probe settings')
    user=ctypes.WinDLL('user32',use_last_error=True);kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    user.GetForegroundWindow.restype=wintypes.HWND;user.GetClipboardSequenceNumber.restype=wintypes.DWORD
    user.GetLastInputInfo.argtypes=[ctypes.POINTER(LASTINPUTINFO)];kernel.GetTickCount64.restype=ctypes.c_ulonglong
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)];user.GetWindowThreadProcessId.restype=wintypes.DWORD
    user.GetClassNameW.argtypes=[wintypes.HWND,wintypes.LPWSTR,ctypes.c_int];user.GetClassNameW.restype=ctypes.c_int
    user.SetForegroundWindow.argtypes=[wintypes.HWND];user.SetForegroundWindow.restype=wintypes.BOOL
    user.IsWindowVisible.argtypes=[wintypes.HWND];user.IsWindowVisible.restype=wintypes.BOOL
    user.ShowWindow.argtypes=[wintypes.HWND,ctypes.c_int]
    user.SendInput.argtypes=[wintypes.UINT,ctypes.POINTER(INPUT),ctypes.c_int];user.SendInput.restype=wintypes.UINT
    user.FindWindowW.argtypes=[wintypes.LPCWSTR,wintypes.LPCWSTR];user.FindWindowW.restype=wintypes.HWND
    user.GetPropW.argtypes=[wintypes.HWND,wintypes.LPCWSTR];user.GetPropW.restype=wintypes.HANDLE
    user.SendMessageTimeoutW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM,wintypes.UINT,wintypes.UINT,ctypes.POINTER(ctypes.c_size_t)]
    user.SendMessageTimeoutW.restype=ctypes.c_void_p
    user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
    callback=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
    user.EnumWindows.argtypes=[callback,wintypes.LPARAM]
    def idle_seconds():
        value=LASTINPUTINFO(ctypes.sizeof(LASTINPUTINFO),0)
        if not user.GetLastInputInfo(ctypes.byref(value)):raise OSError(ctypes.get_last_error(),'GetLastInputInfo')
        return ((kernel.GetTickCount64() & 0xffffffff)-value.dwTime & 0xffffffff)/1000
    def last_input_tick():
        value=LASTINPUTINFO(ctypes.sizeof(LASTINPUTINFO),0)
        if not user.GetLastInputInfo(ctypes.byref(value)):raise OSError(ctypes.get_last_error(),'GetLastInputInfo')
        return value.dwTime
    deadline=time.monotonic()+a.wait_idle_seconds
    while idle_seconds()<a.min_idle_seconds and time.monotonic()<deadline:time.sleep(2)
    before=user.GetForegroundWindow();clip_before=user.GetClipboardSequenceNumber()
    result={'status':'BLOCKED','mode':('exact running application and physical microphone; owned Chrome; no audio or transcript saved' if live else 'owned Chrome; actual Windows hotkey, synthetic PCM into test fixture, VAD/ASR and actual SendInput; no microphone' if a.audio_wav else 'owned visible Chrome window and actual Windows probe hotkey/SendInput; local page; user profile untouched'),
            'probe_sha256':meta['sha256'],'idle_seconds_before':idle_seconds(),'checks':[]}
    browser=probe=ws=browser_ws=None;hwnd=app=None;started_live=False;phase='idle_guard';playback_file=None
    try:
        if result['idle_seconds_before']<a.min_idle_seconds:raise RuntimeError('Recent user input; no window launched')
        phase='browser_launch';profile=out/'profile';profile.mkdir();trace=a.live_trace.resolve() if live else out/'probe-trace.jsonl'
        trace_offset=trace.stat().st_size if live else 0
        flags=['--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-component-update',
               '--disable-sync','--disable-extensions','--host-resolver-rules=MAP * ~NOTFOUND, EXCLUDE localhost',
               '--force-renderer-accessibility','--remote-debugging-port=0','--user-data-dir='+str(profile),'--new-window','about:blank']
        si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=7
        browser=subprocess.Popen([str(CHROME),*flags],cwd=out,startupinfo=si)
        active=profile/'DevToolsActivePort'
        for _ in range(200):
            if active.exists() and len(active.read_text().splitlines())>=2:break
            if browser.poll() is not None:raise RuntimeError('Owned browser exited')
            time.sleep(.1)
        else:raise TimeoutError('Owned browser CDP unavailable')
        phase='cdp_connect';port,browser_path=active.read_text().splitlines()[:2]
        page=next(x for x in json.load(urllib.request.urlopen('http://127.0.0.1:'+port+'/json/list',timeout=5)) if x['type']=='page')
        ws=websocket.create_connection(page['webSocketDebuggerUrl'],timeout=10,suppress_origin=True);seq=0
        def call(method,params=None):
            nonlocal seq
            seq+=1;current=seq;ws.send(json.dumps({'id':current,'method':method,'params':params or {}}))
            while True:
                item=json.loads(ws.recv())
                if item.get('id')!=current:continue
                if 'error' in item:raise RuntimeError(str(item['error']))
                return item.get('result',{})
        def evaluate(expr):return call('Runtime.evaluate',{'expression':expr,'returnByValue':True})['result'].get('value')
        phase='controlled_page_load';call('Page.enable');call('Runtime.enable');call('Page.navigate',{'url':(ROOT/'tests/fixtures/desktop-controlled-editor.html').resolve().as_uri()})
        for _ in range(100):
            if evaluate('document.readyState')=='complete' and evaluate("!!document.getElementById('editable')"):break
            time.sleep(.05)
        else:raise TimeoutError('Controlled editor did not load')
        phase='owned_window_focus';handles=[]
        @callback
        def visit(h,unused):
            pid=wintypes.DWORD();user.GetWindowThreadProcessId(h,ctypes.byref(pid))
            name=ctypes.create_unicode_buffer(128);user.GetClassNameW(h,name,128)
            if pid.value==browser.pid and name.value=='Chrome_WidgetWin_1':handles.append(h)
            return True
        for _ in range(100):
            handles.clear();user.EnumWindows(visit,0)
            if handles:break
            time.sleep(.1)
        if not handles:raise RuntimeError('No owned Chrome window')
        hwnd=handles[0]
        if user.GetForegroundWindow() not in (before,hwnd) or idle_seconds()<a.min_idle_seconds:
            raise RuntimeError('User input or foreground change before test; aborted')
        # The owned browser was initially minimized. Restore it only after the
        # idle and original-foreground checks; never activate a user's window.
        user.ShowWindow(hwnd,9)
        if user.GetForegroundWindow()!=hwnd:user.SetForegroundWindow(hwnd)
        if user.GetForegroundWindow()!=hwnd:
            raise RuntimeError('Windows did not grant foreground to owned Chrome')
        phase='app_identity_or_launch'
        if live:
            app=user.FindWindowW('HebENDictationHiddenClass',None);owner=wintypes.DWORD()
            if app:user.GetWindowThreadProcessId(app,ctypes.byref(owner))
            if not app or owner.value!=a.live_pid or user.GetPropW(app,'HebEN.Capturing'):raise RuntimeError('Live app identity mismatch or capture already active')
        else:
            env=dict(os.environ,HEBEN_DIAGNOSTICS_PATH=str(trace))
            if a.audio_wav:env['PATH']=str(ROOT/'build/recovery-input-live-v2')+os.pathsep+env.get('PATH','')
            probe=subprocess.Popen([str(exe)],cwd=exe.parent,env=env,creationflags=subprocess.CREATE_NO_WINDOW)
            for _ in range(100):
                if trace.exists() and '"hotkey_register","result":"ok"' in trace.read_text(encoding='utf8'):break
                if probe.poll() is not None:raise RuntimeError('Probe exited before registration')
                time.sleep(.05)
            else:raise RuntimeError('Probe shortcut not registered')
        phase='controlled_editor_test'
        for field in (('editable',) if a.audio_wav or live else ('input','textarea','editable')):
            if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed; stopped before hotkey')
            evaluate("document.getElementById('"+field+"').focus()")
            if evaluate('document.activeElement.id')!=field:raise RuntimeError('Controlled editor did not focus')
            label={'input':'Local input','textarea':'Local textarea','editable':'Local contenteditable'}[field]
            focused=None
            for _ in range(12):
                if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed while waiting for editor accessibility')
                focused=subprocess.run([str(ROOT/'build/candidate/Release/HebENFocusOwnedEditor.exe'),str(hwnd),str(browser.pid),label],
                                       capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=10)
                if focused.returncode==0:break
                time.sleep(.25)
            result.setdefault('focus_checks',[]).append({'field':field,'exit_code':focused.returncode,
                                                        'metadata':focused.stdout.decode('utf8',errors='replace').strip()})
            if focused.returncode:raise RuntimeError('UI Automation could not focus the owned test editor')
            if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed before injection')
            keys=([(0x11,0),(0x10,0),(0x52,0),(0x52,2),(0x10,2),(0x11,2)] if live else
                  [(0x11,0),(0x12,0),(0x10,0),(0x7b,0),(0x7b,2),(0x10,2),(0x12,2),(0x11,2)])
            inputs=(INPUT*len(keys))(*[INPUT(type=1,ki=KEYBDINPUT(wVk=k,dwFlags=flags)) for k,flags in keys])
            sent=user.SendInput(len(keys),inputs,ctypes.sizeof(INPUT))
            if sent!=len(keys):raise OSError(ctypes.get_last_error(),f'hotkey events accepted {sent}/{len(keys)}')
            if live:
                for _ in range(100):
                    if user.GetPropW(app,'HebEN.Capturing'):started_live=True;break
                    if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed while starting live capture')
                    time.sleep(.05)
                else:raise RuntimeError('Hotkey did not start the running application')
                if a.live_cycles>1:
                    cycles=[];interrupted=False;time.sleep(.05);expected_tick=last_input_tick()
                    for cycle in range(1,a.live_cycles+1):
                        time.sleep(.12)
                        if user.GetForegroundWindow()!=hwnd or last_input_tick()!=expected_tick:
                            interrupted=True;break
                        if user.SendInput(len(keys),inputs,ctypes.sizeof(INPUT))!=len(keys):raise RuntimeError('Cycle stop hotkey was not fully queued')
                        for _ in range(100):
                            if not user.GetPropW(app,'HebEN.Capturing'):break
                            time.sleep(.02)
                        if user.GetPropW(app,'HebEN.Capturing'):raise RuntimeError('Cycle capture did not stop')
                        time.sleep(.05);expected_tick=last_input_tick();started_live=False
                        mem=live.memory_info();cpu=live.cpu_times()
                        cycles.append({'cycle':cycle,'utc':time.time(),'private_bytes':mem.private,'working_set_bytes':mem.rss,
                                       'cpu_seconds':cpu.user+cpu.system,'handles':live.num_handles(),'threads':live.num_threads()})
                        if evaluate("document.getElementById('editable').textContent.length")!=0:
                            interrupted=True;break
                        if cycle<a.live_cycles:
                            if user.GetForegroundWindow()!=hwnd or last_input_tick()!=expected_tick:
                                interrupted=True;break
                            if user.SendInput(len(keys),inputs,ctypes.sizeof(INPUT))!=len(keys):raise RuntimeError('Cycle start hotkey was not fully queued')
                            for _ in range(100):
                                if user.GetPropW(app,'HebEN.Capturing'):started_live=True;break
                                if user.GetForegroundWindow()!=hwnd:break
                                time.sleep(.02)
                            if not started_live:raise RuntimeError('Cycle capture did not start')
                            time.sleep(.05);expected_tick=last_input_tick()
                    result['checks'].append({'field':field,'status':'PASS' if len(cycles)==a.live_cycles and not interrupted else 'FAIL_OR_INTERRUPTED',
                                             'requested_cycles':a.live_cycles,'completed_cycles':len(cycles),'interrupted':interrupted,
                                             'submissions':evaluate('window.testSubmissions'),'cycles':cycles,
                                             'note':'Actual registered hotkey and real microphone; PCM not saved; no speech or ASR accuracy claim.'})
                    result['status']='PASS_REAL_HOTKEY_MIC_CYCLES' if len(cycles)==a.live_cycles and not interrupted else 'BLOCKED_OR_INTERRUPTED_CYCLES'
                    break
                samples=[];start=time.monotonic();playback_duration=0;observations=[];last_text=''
                if a.live_playback_wav:
                    with wave.open(str(a.live_playback_wav),'rb') as w:
                        if (w.getnchannels(),w.getsampwidth(),w.getframerate())!=(1,2,16000):raise ValueError('Expected mono PCM16 16 kHz WAV')
                        sound=array.array('h');sound.frombytes(w.readframes(w.getnframes()))
                    playback_duration=len(sound)/16000
                    quiet=array.array('h',(int(v*a.playback_gain) for v in sound))
                    playback_file=out/'owned-temporary-playback.wav'
                    with wave.open(str(playback_file),'wb') as w:
                        w.setnchannels(1);w.setsampwidth(2);w.setframerate(16000);w.writeframes(quiet.tobytes())
                    result['playback_audio_sha256']=hashlib.sha256(a.live_playback_wav.read_bytes()).hexdigest()
                    result['playback_gain']=a.playback_gain;result['playback_duration_s']=playback_duration
                    winsound.PlaySound(str(playback_file),winsound.SND_FILENAME|winsound.SND_ASYNC|winsound.SND_NODEFAULT)
                limit=playback_duration+1 if a.live_playback_wav else 4
                while time.monotonic()-start<limit and user.GetPropW(app,'HebEN.Capturing'):
                    if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed during live capture')
                    mem=live.memory_info();cpu=live.cpu_times()
                    samples.append({'elapsed_s':round(time.monotonic()-start,3),'private_bytes':mem.private,'working_set_bytes':mem.rss,
                                    'cpu_seconds':cpu.user+cpu.system,'handles':live.num_handles(),'threads':live.num_threads()})
                    if a.live_playback_wav:
                        value=evaluate("document.getElementById('editable').textContent")
                        if value!=last_text:observations.append({'elapsed_s':round(time.monotonic()-start,3),'character_count':len(value)});last_text=value
                    time.sleep(.25)
                if a.live_playback_wav:winsound.PlaySound(None,0)
                if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed before stopping live capture')
                if user.GetPropW(app,'HebEN.Capturing'):
                    if user.SendInput(len(keys),inputs,ctypes.sizeof(INPUT))!=len(keys):raise RuntimeError('Stop hotkey was not fully queued')
                for _ in range(80):
                    if not user.GetPropW(app,'HebEN.Capturing'):break
                    time.sleep(.05)
                if user.GetPropW(app,'HebEN.Capturing'):raise RuntimeError('Live capture did not stop; requesting guarded cleanup')
                started_live=False
                if a.live_playback_wav:
                    for _ in range(100):
                        if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed while awaiting actual transcription')
                        value=evaluate("document.getElementById('editable').textContent")
                        if value!=last_text:observations.append({'elapsed_s':round(time.monotonic()-start,3),'character_count':len(value)});last_text=value
                        if a.reference and normalized(value)==normalized(a.reference):break
                        time.sleep(.1)
                with trace.open('rb') as f:f.seek(trace_offset);events=[json.loads(line) for line in f.read().decode('utf8').splitlines() if line.strip()]
                observed={(e['stage'],e['result']) for e in events}
                frames=[e for e in events if (e['stage'],e['result'])==('pcm','capture_stopped_frames_peak_millionths')]
                voice=('vad','speech_detected') in observed
                dom_length=evaluate("document.getElementById('editable').textContent.length")
                passed=bool(frames and frames[-1]['value']>0 and ('hotkey','received') in observed and ('microphone','started') in observed)
                match=bool(a.reference) and normalized(last_text)==normalized(a.reference) if a.live_playback_wav else None
                result['checks'].append({'field':field,'status':'PASS_CAPTURE_PCM' if passed else 'FAIL_CAPTURE_PCM',
                    'voice_detected':voice,'silence_assessment':'not silent' if voice else 'no voice detected; not proof of absolute silence',
                    'captured_frames':frames[-1]['value'] if frames else 0,'peak_millionths':frames[-1]['code'] if frames else None,
                    'submissions':evaluate('window.testSubmissions'),'editor_length_only':dom_length,'resource_samples':samples,
                    'transcription_normalized_match':match,'insertion_observations':observations,
                    'note':'Exact running binary and real physical microphone; no captured audio or transcription saved.'})
                result['status']=('PASS_PHYSICAL_AUDIO_TO_EDITOR' if passed and match else 'FAIL_PHYSICAL_AUDIO_TO_EDITOR') if a.live_playback_wav else ('PASS_PHYSICAL_MIC_PCM' if passed else 'FAIL_PHYSICAL_MIC_PCM')
                break
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
                result['audio_sha256']=hashlib.sha256(a.audio_wav.read_bytes()).hexdigest()
                result['audio_duration_s']=len(pcm)/16000;observations=[];start=time.monotonic();last_value=''
                for pos in range(0,len(pcm),512):
                    if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed during audio playback')
                    chunk=array.array('f',(v/32768 for v in pcm[pos:pos+512])).tobytes()
                    buffer=ctypes.create_string_buffer(chunk);data=COPYDATA(0x48455043,len(chunk),ctypes.cast(buffer,ctypes.c_void_p))
                    answer=ctypes.c_size_t()
                    if not user.SendMessageTimeoutW(app,0x004a,0,ctypes.addressof(data),2,15000,ctypes.byref(answer)) or answer.value!=1:
                        raise RuntimeError('Synthetic PCM was rejected')
                    if pos%(512*8)==0:
                        value=evaluate("document.getElementById('editable').textContent")
                        if value!=last_value:observations.append({'elapsed_s':round(time.monotonic()-start,3),'audio_s':pos/16000,'text':value});last_value=value
                    target=start+(pos+min(512,len(pcm)-pos))/16000
                    if target>time.monotonic():time.sleep(target-time.monotonic())
                if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed before stopping capture')
                if user.GetPropW(app,'HebEN.Capturing'):
                    if user.SendInput(len(keys),inputs,ctypes.sizeof(INPUT))!=len(keys):raise RuntimeError('Stopping hotkey was not fully queued')
                value=last_value
                for _ in range(200):
                    if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed while waiting for output')
                    value=evaluate("document.getElementById('editable').textContent")
                    if value!=last_value:observations.append({'elapsed_s':round(time.monotonic()-start,3),'audio_s':len(pcm)/16000,'text':value});last_value=value
                    if a.reference and normalized(value)==normalized(a.reference) and not user.GetPropW(app,'HebEN.Capturing'):break
                    time.sleep(.1)
                match=bool(a.reference) and normalized(value)==normalized(a.reference)
                result['checks'].append({'field':field,'expected':a.reference,'observed':value,'normalized_match':match,
                    'status':'PASS' if match else 'FAIL','submissions':evaluate('window.testSubmissions'),'observations':observations,
                    'note':'Real hotkey and editor readback; audio supplied to test-only callback over IPC, not microphone.'})
                break
            value=''
            for _ in range(100):
                if user.GetForegroundWindow()!=hwnd:raise RuntimeError('Foreground changed while probe was active')
                value=evaluate("document.getElementById('"+field+"')."+('textContent' if field=='editable' else 'value'))
                if value==EXPECTED:break
                time.sleep(.1)
            result['checks'].append({'field':field,'expected':EXPECTED,'observed':value,'status':'PASS' if value==EXPECTED else 'FAIL',
                                     'submissions':evaluate('window.testSubmissions')})
            if value!=EXPECTED:break
        if not live:
            expected_count=1 if a.audio_wav else 3
            result['status']='PASS_CONTROLLED_EDITOR_READBACK' if len(result['checks'])==expected_count and all(x['status']=='PASS' and x['submissions']==0 for x in result['checks']) else 'FAIL_CONTROLLED_EDITOR_READBACK'
    except Exception as exc:result.update(error=str(exc),failure_phase=phase)
    finally:
        if playback_file:
            winsound.PlaySound(None,0)
            try:playback_file.unlink(missing_ok=True)
            except OSError:result['temporary_playback_cleanup']='FAILED'
        if started_live and app and hwnd and user.GetForegroundWindow()==hwnd and user.GetPropW(app,'HebEN.Capturing'):
            user.PostMessageW(app,0x0312,1201,0)
        if probe and probe.poll() is None:
            ph=user.FindWindowW('HebENDesktopAudioTestHiddenClass' if a.audio_wav else 'HebENFixedInputProbeHiddenClass',None);owner=wintypes.DWORD()
            if ph:user.GetWindowThreadProcessId(ph,ctypes.byref(owner))
            if ph and owner.value==probe.pid:user.PostMessageW(ph,0x10,0,0)
            try:probe.wait(timeout=10)
            except subprocess.TimeoutExpired:result['probe_shutdown']='TIMEOUT_left_running'
        if ws:ws.close()
        if hwnd and user.GetForegroundWindow()==hwnd and before:user.SetForegroundWindow(before)
        if browser:
            try:
                if 'browser_path' in locals():
                    browser_ws=websocket.create_connection('ws://127.0.0.1:'+port+browser_path,timeout=3,suppress_origin=True)
                    browser_ws.send(json.dumps({'id':1,'method':'Browser.close'}));browser_ws.close()
            except Exception:pass
            try:browser.wait(timeout=10)
            except subprocess.TimeoutExpired:
                owned=[]
                @callback
                def close_visit(h,unused):
                    owner=wintypes.DWORD();user.GetWindowThreadProcessId(h,ctypes.byref(owner))
                    if owner.value==browser.pid:owned.append(h)
                    return True
                user.EnumWindows(close_visit,0)
                if user.GetForegroundWindow() not in owned:
                    for h in owned:user.PostMessageW(h,0x10,0,0)
                    try:browser.wait(timeout=5);result['browser_shutdown']='graceful_wm_close'
                    except subprocess.TimeoutExpired:result['browser_shutdown']='TIMEOUT_left_running'
                else:result['browser_shutdown']='foreground_owned_browser_left_running'
        result['clipboard_sequence_unchanged']=user.GetClipboardSequenceNumber()==clip_before
        result['foreground_restored_or_changed_by_user']=user.GetForegroundWindow()!=hwnd
        (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(result,ensure_ascii=True))


if __name__=='__main__':main()

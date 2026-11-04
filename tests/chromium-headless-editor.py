"""Local headless Chromium CDP editor check; never uses Windows SendInput."""
import argparse
import ctypes
from ctypes import wintypes
import json
import pathlib
import subprocess
import time
import urllib.request
import websocket

ROOT=pathlib.Path(__file__).resolve().parent.parent
CHROME=pathlib.Path(r'C:\Program Files\Google\Chrome\Application\chrome.exe')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',type=pathlib.Path,required=True);args=ap.parse_args()
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    profile=out/'profile';profile.mkdir()
    user=ctypes.WinDLL('user32',use_last_error=True);user.GetForegroundWindow.restype=wintypes.HWND
    foreground_before=user.GetForegroundWindow()
    cmd=[str(CHROME),'--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check',
         '--disable-background-networking','--disable-component-update','--disable-sync','--disable-extensions',
         '--host-resolver-rules=MAP * ~NOTFOUND, EXCLUDE localhost','--remote-debugging-port=0',
         '--user-data-dir='+str(profile),'about:blank']
    proc=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=subprocess.CREATE_NO_WINDOW)
    ws=None;browser=None;result={'mode':'isolated headless Chromium CDP insertion; not Windows SendInput','status':'FAIL'}
    try:
        active=profile/'DevToolsActivePort'
        for _ in range(150):
            if active.exists() and len(active.read_text().splitlines())>=2:break
            if proc.poll() is not None:raise RuntimeError('Chromium exited before CDP was ready')
            time.sleep(.1)
        else:raise TimeoutError('Chromium CDP did not start')
        port,browser_path=active.read_text().splitlines()[:2]
        pages=json.load(urllib.request.urlopen('http://127.0.0.1:'+port+'/json/list',timeout=5))
        page=next(x for x in pages if x['type']=='page')
        ws=websocket.create_connection(page['webSocketDebuggerUrl'],timeout=10,suppress_origin=True)
        message_id=0
        def call(method,params=None):
            nonlocal message_id
            message_id+=1;wanted=message_id;ws.send(json.dumps({'id':wanted,'method':method,'params':params or {}}))
            while True:
                item=json.loads(ws.recv())
                if item.get('id')!=wanted:continue
                if 'error' in item:raise RuntimeError(str(item['error']))
                return item.get('result',{})
        def evaluate(expression):
            response=call('Runtime.evaluate',{'expression':expression,'returnByValue':True,'awaitPromise':True})
            if 'exceptionDetails' in response:raise RuntimeError(str(response['exceptionDetails']))
            return response['result'].get('value')
        call('Page.enable');call('Runtime.enable')
        call('Page.navigate',{'url':(ROOT/'tests/fixtures/chromium-editor.html').resolve().as_uri()})
        for _ in range(100):
            if evaluate('document.readyState')=='complete' and evaluate("!!document.getElementById('textarea')"):break
            time.sleep(.05)
        else:raise TimeoutError('Fixture page did not load')
        text='שלום hello, 42!'
        checks={}
        for name in ('textarea','editable'):
            evaluate("document.getElementById("+json.dumps(name)+").focus()")
            call('Input.insertText',{'text':text})
            observed=evaluate("document.getElementById("+json.dumps(name)+")."+('value' if name=='textarea' else 'innerText'))
            checks[name+'_exact']=observed==text
            checks[name+'_focus_preserved']=evaluate('document.activeElement.id')==name
        evaluate("document.getElementById('textarea').focus();document.getElementById('textarea').value=''")
        call('Input.insertText',{'text':'first\nsecond'})
        checks['multiline_exact']=evaluate("document.getElementById('textarea').value")=='first\nsecond'
        checks['submissions_zero']=evaluate('window.testSubmissions')==0
        result.update(checks);result['events']=evaluate('window.testEvents');result['status']='PASS' if all(checks.values()) else 'FAIL'
    except Exception as exc:result['error']=str(exc)
    finally:
        if ws:ws.close()
        try:
            if 'browser_path' in locals():
                browser=websocket.create_connection('ws://127.0.0.1:'+port+browser_path,timeout=3,suppress_origin=True)
                browser.send(json.dumps({'id':1,'method':'Browser.close'}));browser.close()
        except Exception:pass
        try:proc.wait(timeout=10)
        except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=10)
        result['foreground_same_at_end']=user.GetForegroundWindow()==foreground_before
        result['process_exit']=proc.returncode
        (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps(result,ensure_ascii=True))
    if result['status']!='PASS':raise SystemExit(1)

if __name__=='__main__':main()

param([Parameter(Mandatory=$true)][string]$FixtureExe,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class IntegrationNative {
    [StructLayout(LayoutKind.Sequential)] public struct COPYDATA { public UIntPtr dwData; public int cbData; public IntPtr lpData; }
    [StructLayout(LayoutKind.Sequential)] public struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c,string n);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
    [DllImport("user32.dll")] public static extern uint GetClipboardSequenceNumber();
    [DllImport("user32.dll")] public static extern bool GetLastInputInfo(ref LASTINPUTINFO x);
    [DllImport("kernel32.dll")] public static extern uint GetTickCount();
    public static double IdleSeconds(uint last) { return unchecked(GetTickCount()-last)/1000.0; }
    [DllImport("user32.dll",SetLastError=true)] public static extern IntPtr SendMessageTimeout(IntPtr h,uint msg,IntPtr w,IntPtr l,uint flags,uint timeout,out IntPtr result);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
}
'@
$last=[IntegrationNative+LASTINPUTINFO]::new(); $last.cbSize=[Runtime.InteropServices.Marshal]::SizeOf($last)
if (-not [IntegrationNative]::GetLastInputInfo([ref]$last)) { throw 'Could not establish user idle state.' }
$idle=[IntegrationNative]::IdleSeconds($last.dwTime)
if ($idle -lt 30) {
    [pscustomobject]@{status='BLOCKED';reason='Recent user input; test will not take foreground';user_idle_seconds=$idle;timestamp_utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $Output -Encoding UTF8
    throw "User input was seen $idle seconds ago; refusing to take foreground for the automated test."
}
if(Get-Process HebENDictation,HebENIntegrationFixture -ErrorAction SilentlyContinue) { throw 'A dictation process already exists.' }
$previous=[IntegrationNative]::GetForegroundWindow()
$exe=(Resolve-Path -LiteralPath $FixtureExe).Path
$app=$null; $editor=$null; $appWindow=[IntPtr]::Zero; $editorWindow=[IntPtr]::Zero
$results=[ordered]@{fixture='controlled WinForms editor; test-only executable';timestamp_utc=[DateTime]::UtcNow.ToString('o');passed=$false}
function Send-Probe([IntPtr]$h,[uint32]$message,[IntPtr]$payload) {
    $answer=[IntPtr]::Zero
    $ok=[IntegrationNative]::SendMessageTimeout($h,$message,[IntPtr]::Zero,$payload,2,10000,[ref]$answer)
    if($ok -eq [IntPtr]::Zero) { throw "Timed out sending message $message" }
    return $answer.ToInt64()
}
function Send-Text([IntPtr]$h,[string]$text) {
    $chars=[Runtime.InteropServices.Marshal]::StringToHGlobalUni($text)
    $cd=[IntegrationNative+COPYDATA]::new(); $cd.dwData=[UIntPtr]0x48454245; $cd.cbData=($text.Length+1)*2; $cd.lpData=$chars
    $ptr=[Runtime.InteropServices.Marshal]::AllocHGlobal([Runtime.InteropServices.Marshal]::SizeOf($cd))
    try { [Runtime.InteropServices.Marshal]::StructureToPtr($cd,$ptr,$false); return Send-Probe $h 0x004a $ptr }
    finally { [Runtime.InteropServices.Marshal]::FreeHGlobal($ptr); [Runtime.InteropServices.Marshal]::FreeHGlobal($chars) }
}
try {
    $app=Start-Process -FilePath $exe -WorkingDirectory ([IO.Path]::GetDirectoryName($exe)) -PassThru
    $editorScript=Join-Path $PSScriptRoot 'integration-editor.ps1'
    $editor=Start-Process -FilePath powershell.exe -ArgumentList @('-NoProfile','-STA','-ExecutionPolicy','Bypass','-File',('"'+$editorScript+'"')) -WindowStyle Normal -PassThru
    for($i=0;$i -lt 100;$i++) {
        $appWindow=[IntegrationNative]::FindWindow('HebENDictationHiddenClass',$null)
        $editorWindow=[IntegrationNative]::FindWindow($null,'HebEN controlled test editor')
        if($appWindow -ne [IntPtr]::Zero -and $editorWindow -ne [IntPtr]::Zero) {break}
        Start-Sleep -Milliseconds 100
    }
    if($appWindow -eq [IntPtr]::Zero -or $editorWindow -eq [IntPtr]::Zero) { throw 'Test windows did not appear.' }
    $owner=0; [void][IntegrationNative]::GetWindowThreadProcessId($appWindow,[ref]$owner)
    if($owner -ne $app.Id) { throw 'Unexpected app window owner.' }
    [void][IntegrationNative]::SetForegroundWindow($editorWindow)
    $root=[Windows.Automation.AutomationElement]::FromHandle($editorWindow)
    function Find-Control([string]$name) {
        $condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$name)
        $el=$root.FindFirst([Windows.Automation.TreeScope]::Descendants,$condition)
        if(-not $el) { throw "Test control unavailable: $name" }; return $el
    }
    $first=Find-Control 'HebEN test field one'; $second=Find-Control 'HebEN test field two'; $password=Find-Control 'HebEN test password'
    $value1=$first.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern)
    $value2=$second.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern)
    $phrase= -join @([char]0x05e9,[char]0x05dc,[char]0x05d5,[char]0x05dd,' hello 123.')
    $clipBefore=[IntegrationNative]::GetClipboardSequenceNumber()
    $first.SetFocus(); Start-Sleep -Milliseconds 200
    if([IntegrationNative]::GetForegroundWindow() -ne $editorWindow) {throw 'Test editor is not foreground.'}
    $capture=Send-Probe $appWindow (0x8000+30) ([IntPtr]::Zero)
    $insert=Send-Text $appWindow $phrase
    Start-Sleep -Milliseconds 300
    $results.unicode_capture=($capture -eq 1); $results.unicode_send=($insert -eq 1)
    $results.unicode_exact=($value1.Current.Value -ceq $phrase)
    $results.focus_preserved=([IntegrationNative]::GetForegroundWindow() -eq $editorWindow -and $first.Current.HasKeyboardFocus)
    $first.SetFocus(); [void](Send-Probe $appWindow (0x8000+30) ([IntPtr]::Zero)); $second.SetFocus(); Start-Sleep -Milliseconds 100
    $wrong=Send-Text $appWindow 'wrong field'; Start-Sleep -Milliseconds 100
    $results.focus_change_refused=($wrong -eq 0 -and $value2.Current.Value -eq '')
    $password.SetFocus(); Start-Sleep -Milliseconds 100
    $results.password_refused=((Send-Probe $appWindow (0x8000+30) ([IntPtr]::Zero)) -eq 0)
    $results.clipboard_unchanged=([IntegrationNative]::GetClipboardSequenceNumber() -eq $clipBefore)
    $value1.SetValue(''); $first.SetFocus(); Start-Sleep -Milliseconds 100
    $capture=Send-Probe $appWindow (0x8000+30) ([IntPtr]::Zero)
    $segment1=Send-Text $appWindow '  hello   '
    $segment2=Send-Text $appWindow '  world  '
    Start-Sleep -Milliseconds 300
    $results.segments_single_space=($capture -eq 1 -and $segment1 -eq 1 -and $segment2 -eq 1 -and $value1.Current.Value -ceq 'hello world')
    $cases=@(
        @{name='english';input='Hello, world! 123.';expected='Hello, world! 123.'},
        @{name='hebrew';input=(-join @([char]0x05e9,[char]0x05dc,[char]0x05d5,[char]0x05dd,'!'));expected=(-join @([char]0x05e9,[char]0x05dc,[char]0x05d5,[char]0x05dd,'!'))},
        @{name='multiline_safe';input="first`r`nsecond";expected='first second'}
    )
    foreach($case in $cases) {
        $value1.SetValue('');$first.SetFocus();Start-Sleep -Milliseconds 100
        $accepted=Send-Probe $appWindow (0x8000+30) ([IntPtr]::Zero)
        $sent=Send-Text $appWindow $case.input;Start-Sleep -Milliseconds 300
        $results[$case.name]=($accepted -eq 1 -and $sent -eq 1 -and $value1.Current.Value -ceq $case.expected)
    }
    $results.passed=$results.unicode_capture -and $results.unicode_send -and $results.unicode_exact -and $results.focus_preserved -and $results.focus_change_refused -and $results.password_refused -and $results.clipboard_unchanged -and $results.segments_single_space -and $results.english -and $results.hebrew -and $results.multiline_safe
} catch { $results.error=$_.Exception.Message }
finally {
    $results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Output -Encoding UTF8
    if($appWindow -ne [IntPtr]::Zero) { [void][IntegrationNative]::PostMessage($appWindow,0x10,[IntPtr]::Zero,[IntPtr]::Zero) }
    if([IntegrationNative]::GetForegroundWindow() -eq $editorWindow -and $previous -ne [IntPtr]::Zero) { [void][IntegrationNative]::SetForegroundWindow($previous) }
    if($editorWindow -ne [IntPtr]::Zero) { [void][IntegrationNative]::PostMessage($editorWindow,0x10,[IntPtr]::Zero,[IntPtr]::Zero) }
}
if(-not $results.passed) { throw "Integration fixture failed; see $Output" }

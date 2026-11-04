param([Parameter(Mandatory=$true)][string]$Directory,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $Output){throw 'Output already exists'}
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class ProbeLauncherSmokeNative {
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint GetClipboardSequenceNumber();
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c,string n);
}
'@
$beforeForeground=[ProbeLauncherSmokeNative]::GetForegroundWindow()
$beforeClipboard=[ProbeLauncherSmokeNative]::GetClipboardSequenceNumber()
$result=[ordered]@{status='FAIL';scope='disarmed probe launcher and safe stop; no microphone, hotkey registration or insertion'}
$probe=$null
try {
    $probe=& (Join-Path $PSScriptRoot 'launch-fixed-input-probe.ps1') -Directory $Directory -Disarmed
    if($probe.armed){throw 'Probe unexpectedly armed'}
    $result.pid=$probe.pid
    for($i=0;$i -lt 100 -and [ProbeLauncherSmokeNative]::FindWindow('HebENFixedInputProbeHiddenClass',$null) -eq [IntPtr]::Zero;$i++) {Start-Sleep -Milliseconds 50}
    if([ProbeLauncherSmokeNative]::FindWindow('HebENFixedInputProbeHiddenClass',$null) -eq [IntPtr]::Zero){throw 'Probe window not created'}
    $existing=Get-Process HebENDictation -ErrorAction SilentlyContinue | Select-Object -First 1
    if($existing) {
        try {& (Join-Path $PSScriptRoot 'stop-fixed-input-probe.ps1') -ProbeProcessId $existing.Id -Directory $Directory;throw 'Wrong process was unexpectedly accepted'}
        catch {if($_.Exception.Message -notmatch 'Process path mismatch'){throw};$result.wrong_process_refused=$true}
        $result.existing_app_still_running=[bool](Get-Process -Id $existing.Id -ErrorAction SilentlyContinue)
    } else {$result.wrong_process_refused=$null;$result.existing_app_still_running=$null}
    & (Join-Path $PSScriptRoot 'stop-fixed-input-probe.ps1') -ProbeProcessId $probe.pid -Directory $Directory | Out-Null
    $result.probe_stopped=-not [bool](Get-Process -Id $probe.pid -ErrorAction SilentlyContinue)
    $result.foreground_unchanged_at_observation=([ProbeLauncherSmokeNative]::GetForegroundWindow() -eq $beforeForeground)
    $result.clipboard_sequence_unchanged=([ProbeLauncherSmokeNative]::GetClipboardSequenceNumber() -eq $beforeClipboard)
    if($result.probe_stopped -and $result.clipboard_sequence_unchanged -and (!$existing -or ($result.wrong_process_refused -and $result.existing_app_still_running))) {$result.status='PASS_DISARMED_LIFECYCLE'}
} catch {$result.error=$_.Exception.Message}
finally {
    if($probe -and (Get-Process -Id $probe.pid -ErrorAction SilentlyContinue)) {
        try {& (Join-Path $PSScriptRoot 'stop-fixed-input-probe.ps1') -ProbeProcessId $probe.pid -Directory $Directory | Out-Null}
        catch {$result.cleanup_error=$_.Exception.Message}
    }
    $result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $Output -Encoding UTF8
}
if($result.status -ne 'PASS_DISARMED_LIFECYCLE'){throw "Probe lifecycle test failed; see $Output"}
$result | ConvertTo-Json -Depth 4

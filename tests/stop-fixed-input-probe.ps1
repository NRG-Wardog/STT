param([Parameter(Mandatory=$true)][int]$ProbeProcessId,[Parameter(Mandatory=$true)][string]$Directory)
$ErrorActionPreference='Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FixedProbeStopNative {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,string name);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd,uint message,IntPtr w,IntPtr l);
}
'@
$dir=(Resolve-Path -LiteralPath $Directory).Path
$expected=(Resolve-Path -LiteralPath (Join-Path $dir 'HebENFixedInputProbe.exe')).Path
$target=Get-Process -Id $ProbeProcessId -ErrorAction Stop
if($target.Path -ne $expected){throw 'Process path mismatch; no process was stopped'}
$hwnd=[FixedProbeStopNative]::FindWindow('HebENFixedInputProbeHiddenClass',$null)
$owner=0;[void][FixedProbeStopNative]::GetWindowThreadProcessId($hwnd,[ref]$owner)
if($hwnd -eq [IntPtr]::Zero -or $owner -ne $ProbeProcessId){throw 'Probe window identity mismatch; no process was stopped'}
if(-not [FixedProbeStopNative]::PostMessage($hwnd,0x10,[IntPtr]::Zero,[IntPtr]::Zero)){throw 'Could not request probe shutdown'}
if(-not $target.WaitForExit(10000)){throw 'Probe did not exit; it was not force-terminated'}
Write-Output "Stopped fixed input probe $ProbeProcessId"

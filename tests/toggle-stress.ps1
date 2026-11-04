param(
    [Parameter(Mandatory=$true)][int]$AppPid,
    [Parameter(Mandatory=$true)][long]$TargetWindow,
    [Parameter(Mandatory=$true)][string]$Output,
    [int]$Cycles = 100
)
$ErrorActionPreference='Stop'
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ToggleProbe {
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,string name);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h,string key);
    [DllImport("user32.dll",SetLastError=true)] public static extern IntPtr SendMessageTimeout(IntPtr h,uint msg,IntPtr w,IntPtr l,uint flags,uint timeout,out IntPtr result);
}
'@
if ($Cycles -lt 1 -or $Cycles -gt 1000) { throw 'Invalid cycle count' }
$p=Get-Process -Id $AppPid
$app=[ToggleProbe]::FindWindow('HebENDictationHiddenClass',$null)
$owner=0; [void][ToggleProbe]::GetWindowThreadProcessId($app,[ref]$owner)
if ($app -eq [IntPtr]::Zero -or $owner -ne $AppPid) { throw 'Application window or PID mismatch' }
$target=[IntPtr]$TargetWindow
if ([ToggleProbe]::GetForegroundWindow() -ne $target) { throw 'Target window is not foreground; test did not start.' }
if ([ToggleProbe]::GetProp($app,'HebEN.Capturing') -ne [IntPtr]::Zero) { throw 'App is already capturing.' }
$rows=[Collections.Generic.List[object]]::new()
try {
    for($i=1;$i -le $Cycles;$i++) {
        if ([ToggleProbe]::GetForegroundWindow() -ne $target) { throw 'Foreground changed; stopped the test.' }
        $nullResult=[IntPtr]::Zero
        $started=[ToggleProbe]::SendMessageTimeout($app,0x0312,[IntPtr]1201,[IntPtr]::Zero,2,10000,[ref]$nullResult)
        $active=[ToggleProbe]::GetProp($app,'HebEN.Capturing') -ne [IntPtr]::Zero
        if ($started -eq [IntPtr]::Zero -or -not $active) { throw "Activation failed at cycle $i" }
        Start-Sleep -Milliseconds 100
        $stopped=[ToggleProbe]::SendMessageTimeout($app,0x0312,[IntPtr]1201,[IntPtr]::Zero,2,10000,[ref]$nullResult)
        $inactive=[ToggleProbe]::GetProp($app,'HebEN.Capturing') -eq [IntPtr]::Zero
        if ($stopped -eq [IntPtr]::Zero -or -not $inactive) { throw "Deactivation failed at cycle $i" }
        $p.Refresh()
        $rows.Add([pscustomobject]@{cycle=$i;utc=[DateTime]::UtcNow.ToString('o');active_seen=$active;inactive_seen=$inactive;private_bytes=$p.PrivateMemorySize64;working_set_bytes=$p.WorkingSet64;handles=$p.HandleCount;threads=$p.Threads.Count;cpu_seconds=$p.TotalProcessorTime.TotalSeconds})
    }
} finally {
    $rows | Export-Csv -LiteralPath $Output -NoTypeInformation -Encoding UTF8
}
Write-Host "Completed $($rows.Count) cycles. WM_HOTKEY was sent directly: this does not test Windows shortcut registration."

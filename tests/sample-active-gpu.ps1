param([Parameter(Mandatory=$true)][int]$AppPid,[Parameter(Mandatory=$true)][string]$Output,[int]$WaitSeconds=300)
$ErrorActionPreference='Stop'
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class HebENGpuSampleWindow {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,string title);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h,string key);
}
'@
$app=[HebENGpuSampleWindow]::FindWindow('HebENDictationHiddenClass',$null);$owner=[uint32]0
[void][HebENGpuSampleWindow]::GetWindowThreadProcessId($app,[ref]$owner)
if($app -eq [IntPtr]::Zero -or $owner -ne $AppPid){throw 'Application identity mismatch'}
$timer=[Diagnostics.Stopwatch]::StartNew();$rows=[Collections.Generic.List[object]]::new();$seen=$false
while($timer.Elapsed.TotalSeconds -lt $WaitSeconds) {
 if([HebENGpuSampleWindow]::GetProp($app,'HebEN.Capturing') -ne [IntPtr]::Zero){$seen=$true;break}
 Start-Sleep -Milliseconds 200
}
if($seen) {
 for($i=0;$i -lt 10;$i++) {
  $capturing=[HebENGpuSampleWindow]::GetProp($app,'HebEN.Capturing') -ne [IntPtr]::Zero
  $stamp=[DateTime]::UtcNow.ToString('o')
  try {
   $c=Get-Counter -Counter '\GPU Engine(*)\Utilization Percentage','\GPU Process Memory(*)\Dedicated Usage','\GPU Process Memory(*)\Shared Usage' -ErrorAction SilentlyContinue
   $after=[HebENGpuSampleWindow]::GetProp($app,'HebEN.Capturing') -ne [IntPtr]::Zero
   if($null -eq $c){$rows.Add([pscustomobject]@{utc=$stamp;capturing=$capturing;capturing_after=$after;path='ERROR';instance='';value='No data';status=-1})}
   else {foreach($v in $c.CounterSamples){if($v.InstanceName -match ('pid_'+$AppPid+'_')){$rows.Add([pscustomobject]@{utc=$stamp;capturing=$capturing;capturing_after=$after;path=$v.Path;instance=$v.InstanceName;value=$v.CookedValue;status=$v.Status})}}}
  } catch {$rows.Add([pscustomobject]@{utc=$stamp;capturing=$capturing;path='ERROR';instance='';value=$_.Exception.Message})}
  if(-not $capturing){break};Start-Sleep -Milliseconds 500
 }
}
$rows | Export-Csv -LiteralPath $Output -NoTypeInformation -Encoding UTF8
[pscustomobject]@{capture_seen=$seen;rows=$rows.Count;method='Windows per-process GPU performance counters; capture flag sampled before counter request; do not infer full-interval GPU use'} |
 ConvertTo-Json | Set-Content -LiteralPath ($Output+'.json') -Encoding UTF8

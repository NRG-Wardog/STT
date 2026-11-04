param([Parameter(Mandatory=$true)][string]$Output,[int]$Seconds=15)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class FocusMetaNative {
 [StructLayout(LayoutKind.Sequential)] public struct RECT {public int left,top,right,bottom;}
 [StructLayout(LayoutKind.Sequential)] public struct GUITHREADINFO {public int cbSize;public int flags;public IntPtr hwndActive,hwndFocus,hwndCapture,hwndMenuOwner,hwndMoveSize,hwndCaret;public RECT rcCaret;}
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint t,ref GUITHREADINFO g);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,System.Text.StringBuilder s,int n);
}
'@
$rows=[Collections.Generic.List[object]]::new();$end=[DateTime]::UtcNow.AddSeconds($Seconds)
while([DateTime]::UtcNow -lt $end) {
 $h=[FocusMetaNative]::GetForegroundWindow();$pidValue=0;$tid=[FocusMetaNative]::GetWindowThreadProcessId($h,[ref]$pidValue)
 $p=Get-Process -Id $pidValue -ErrorAction SilentlyContinue
 $row=[ordered]@{utc=[DateTime]::UtcNow.ToString('o');foreground_process=if($p){$p.ProcessName}else{'unknown'};metadata_only=$true;contents_read=$false}
 if($p -and $p.ProcessName -match '(?i)^(chrome|msedge|chatgpt|codex|code|windowsterminal|notepad)$') {
  try {
   $g=[FocusMetaNative+GUITHREADINFO]::new();$g.cbSize=[Runtime.InteropServices.Marshal]::SizeOf($g)
   $row.gui_info=[FocusMetaNative]::GetGUIThreadInfo($tid,[ref]$g);$row.native_focus_present=($g.hwndFocus -ne [IntPtr]::Zero);$row.native_caret_present=($g.hwndCaret -ne [IntPtr]::Zero)
   if($g.hwndFocus -ne [IntPtr]::Zero){$class=New-Object Text.StringBuilder 128;[void][FocusMetaNative]::GetClassName($g.hwndFocus,$class,128);$row.native_focus_class=$class.ToString()}
   $e=[Windows.Automation.AutomationElement]::FocusedElement;$c=$e.Current
   $row.type=$c.ControlType.ProgrammaticName;$row.has_focus=$c.HasKeyboardFocus;$row.password=$c.IsPassword;$row.enabled=$c.IsEnabled
   $v=$null;$row.value_pattern=$e.TryGetCurrentPattern([Windows.Automation.ValuePattern]::Pattern,[ref]$v);if($v){$row.readonly=$v.Current.IsReadOnly}
   $t=$null;$row.text_pattern=$e.TryGetCurrentPattern([Windows.Automation.TextPattern]::Pattern,[ref]$t)
   $children=$e.FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.Condition]::TrueCondition);$list=@()
   for($i=0;$i -lt [Math]::Min($children.Count,20);$i++){ $q=$children.Item($i).Current;$list+= [pscustomobject]@{type=$q.ControlType.ProgrammaticName;has_focus=$q.HasKeyboardFocus;password=$q.IsPassword;enabled=$q.IsEnabled} }
   $row.children=$list
  } catch {$row.error=$_.Exception.Message}
 } else {$row.status='skipped_other_foreground'}
 $rows.Add([pscustomobject]$row);Start-Sleep -Milliseconds 1000
}
$rows | ForEach-Object { $_ | ConvertTo-Json -Depth 5 -Compress } | Set-Content -LiteralPath $Output -Encoding UTF8
Write-Host "Captured $($rows.Count) metadata-only samples."

param([Parameter(Mandatory=$true)][ValidateSet('Set','Check')][string]$Action)
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class HebENClipboardSentinel {
 [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern IntPtr CreateWindowEx(uint ex,string cls,string name,uint style,int x,int y,int w,int h,IntPtr parent,IntPtr menu,IntPtr inst,IntPtr param);
 [DllImport("user32.dll")] public static extern bool DestroyWindow(IntPtr h);
 [DllImport("user32.dll",SetLastError=true)] public static extern bool OpenClipboard(IntPtr h);
 [DllImport("user32.dll")] public static extern bool CloseClipboard();
 [DllImport("user32.dll",SetLastError=true)] public static extern bool EmptyClipboard();
 [DllImport("user32.dll",SetLastError=true)] public static extern IntPtr SetClipboardData(uint format,IntPtr data);
 [DllImport("user32.dll",SetLastError=true)] public static extern IntPtr GetClipboardData(uint format);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern IntPtr GlobalAlloc(uint flags,UIntPtr bytes);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern IntPtr GlobalLock(IntPtr memory);
 [DllImport("kernel32.dll")] public static extern bool GlobalUnlock(IntPtr memory);
 [DllImport("kernel32.dll")] public static extern IntPtr GlobalFree(IntPtr memory);
}
'@
$sentinel='HEBEN-CLIPBOARD-CHECK'
if($Action -eq 'Check') {
 if(-not [HebENClipboardSentinel]::OpenClipboard([IntPtr]::Zero)){throw 'Clipboard busy'}
 try {
  $handle=[HebENClipboardSentinel]::GetClipboardData(13)
  if($handle -eq [IntPtr]::Zero){'NO_UNICODE_TEXT';return}
  $ptr=[HebENClipboardSentinel]::GlobalLock($handle)
  if($ptr -eq [IntPtr]::Zero){throw 'Clipboard data unavailable'}
  try {
   $value=[Runtime.InteropServices.Marshal]::PtrToStringUni($ptr)
   if($value -ceq $sentinel){'MATCHES_MARKER'}else{'DOES_NOT_MATCH_MARKER'}
  } finally {[void][HebENClipboardSentinel]::GlobalUnlock($handle)}
 } finally {[void][HebENClipboardSentinel]::CloseClipboard()}
 return
}
# This intentionally replaces the existing clipboard. Call only after explicit consent.
$h=[HebENClipboardSentinel]::CreateWindowEx(0,'STATIC','HebEN clipboard fixture',0,0,0,0,0,[IntPtr]::Zero,[IntPtr]::Zero,[IntPtr]::Zero,[IntPtr]::Zero)
if($h -eq [IntPtr]::Zero){throw 'Cannot create hidden clipboard fixture window'}
$mem=[IntPtr]::Zero
try {
 $bytes=[Text.Encoding]::Unicode.GetBytes($sentinel+"`0")
 $mem=[HebENClipboardSentinel]::GlobalAlloc(2,[UIntPtr]::new([uint32]$bytes.Length))
 if($mem -eq [IntPtr]::Zero){throw 'Cannot allocate clipboard memory'}
 $ptr=[HebENClipboardSentinel]::GlobalLock($mem)
 if($ptr -eq [IntPtr]::Zero){throw 'Cannot lock clipboard memory'}
 [Runtime.InteropServices.Marshal]::Copy($bytes,0,$ptr,$bytes.Length)
 [void][HebENClipboardSentinel]::GlobalUnlock($mem)
 if(-not [HebENClipboardSentinel]::OpenClipboard($h)){throw 'Clipboard busy; unchanged'}
 try {
  if(-not [HebENClipboardSentinel]::EmptyClipboard()){throw 'Unable to replace clipboard'}
  if([HebENClipboardSentinel]::SetClipboardData(13,$mem) -eq [IntPtr]::Zero){throw 'Setting plain clipboard text failed after replacement'}
  $mem=[IntPtr]::Zero
 } finally {[void][HebENClipboardSentinel]::CloseClipboard()}
} finally {if($mem -ne [IntPtr]::Zero){[void][HebENClipboardSentinel]::GlobalFree($mem)};[void][HebENClipboardSentinel]::DestroyWindow($h)}
'MARKER_SET'

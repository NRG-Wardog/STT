param([Parameter(Mandatory=$true)][string]$Exe)
$ErrorActionPreference='Stop'
$path=(Resolve-Path -LiteralPath $Exe).Path
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class HebENResourceCheck {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern IntPtr LoadLibraryEx(string path,IntPtr file,uint flags);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern IntPtr FindResource(IntPtr module,IntPtr name,IntPtr type);
 [DllImport("kernel32.dll")] public static extern bool FreeLibrary(IntPtr module);
}
'@
$module=[HebENResourceCheck]::LoadLibraryEx($path,[IntPtr]::Zero,0x22)
if($module -eq [IntPtr]::Zero){throw 'Could not inspect executable resources.'}
try {
 if([HebENResourceCheck]::FindResource($module,[IntPtr]101,[IntPtr]14) -eq [IntPtr]::Zero){throw 'HebEN icon group 101 is missing.'}
} finally {[void][HebENResourceCheck]::FreeLibrary($module)}
Write-Output 'PASS: executable contains HebEN icon resource 101.'

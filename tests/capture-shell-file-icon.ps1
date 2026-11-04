param([Parameter(Mandatory=$true)][string]$Exe,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$exePath=(Resolve-Path -LiteralPath $Exe).Path
$outPath=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $outPath){throw 'Refusing to overwrite existing evidence.'}
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
[StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
public struct SHFILEINFO { public IntPtr hIcon; public int iIcon; public uint dwAttributes; [MarshalAs(UnmanagedType.ByValTStr, SizeConst=260)] public string szDisplayName; [MarshalAs(UnmanagedType.ByValTStr, SizeConst=80)] public string szTypeName; }
public static class HebENShellIcon {
 [DllImport("shell32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr SHGetFileInfo(string path,uint attrs,ref SHFILEINFO info,uint size,uint flags);
 [DllImport("user32.dll")] public static extern bool DestroyIcon(IntPtr h);
}
'@
$info=[SHFILEINFO]::new()
$got=[HebENShellIcon]::SHGetFileInfo($exePath,0,[ref]$info,[uint32][Runtime.InteropServices.Marshal]::SizeOf([type][SHFILEINFO]),0x100)
if($got -eq [IntPtr]::Zero -or $info.hIcon -eq [IntPtr]::Zero){throw 'SHGetFileInfo failed.'}
try {
 $icon=[Drawing.Icon]::FromHandle($info.hIcon)
 try { $bitmap=$icon.ToBitmap(); try {$bitmap.Save($outPath,[Drawing.Imaging.ImageFormat]::Png)} finally {$bitmap.Dispose()} } finally {$icon.Dispose()}
} finally {[void][HebENShellIcon]::DestroyIcon($info.hIcon)}
Write-Output "Windows shell file icon captured to $outPath"

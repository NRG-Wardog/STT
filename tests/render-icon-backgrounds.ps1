param([Parameter(Mandatory=$true)][string]$Exe,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$exePath=(Resolve-Path -LiteralPath $Exe).Path
$outPath=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $outPath){throw 'Refusing to overwrite existing evidence.'}
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class HebENIconRender {
 [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
 public static extern uint PrivateExtractIcons(string path,int index,int cx,int cy,[Out] IntPtr[] icons,[Out] uint[] ids,uint count,uint flags);
 [DllImport("user32.dll")] public static extern bool DestroyIcon(IntPtr h);
}
'@
$sizes=@(16,24,32,48,256)
$canvas=[Drawing.Bitmap]::new(440,400,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
try {
 $g=[Drawing.Graphics]::FromImage($canvas)
 try {
  $g.Clear([Drawing.Color]::White)
  $dark=[Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(28,28,28))
  try {$g.FillRectangle($dark,220,0,220,400)} finally {$dark.Dispose()}
  foreach($size in $sizes){
   [IntPtr[]]$icons=@([IntPtr]::Zero);[uint32[]]$ids=@(0)
   if([HebENIconRender]::PrivateExtractIcons($exePath,0,$size,$size,$icons,$ids,1,0) -ne 1 -or $icons[0] -eq [IntPtr]::Zero){throw "Icon extraction failed at $size"}
   try {
    $icon=[Drawing.Icon]::FromHandle($icons[0])
    try {
     $y=if($size -eq 256){130}else{10+([Array]::IndexOf($sizes,$size)*30)}
     $drawSize=if($size -eq 256){180}else{$size}
     $g.DrawIcon($icon,[Drawing.Rectangle]::new(12,$y,$drawSize,$drawSize))
     $g.DrawIcon($icon,[Drawing.Rectangle]::new(232,$y,$drawSize,$drawSize))
    } finally {$icon.Dispose()}
   } finally {[void][HebENIconRender]::DestroyIcon($icons[0])}
  }
 } finally {$g.Dispose()}
 $canvas.Save($outPath,[Drawing.Imaging.ImageFormat]::Png)
} finally {$canvas.Dispose()}
Write-Output "Rendered extracted Windows icon frames on light and dark backgrounds: $outPath"

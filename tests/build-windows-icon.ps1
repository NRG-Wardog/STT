param([Parameter(Mandatory=$true)][string]$Png,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$source=(Resolve-Path -LiteralPath $Png).Path
$dest=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $dest){throw 'Refusing to overwrite icon'}
Add-Type -AssemblyName System.Drawing
$image=[Drawing.Image]::FromFile($source)
$frames=[Collections.Generic.List[byte[]]]::new();$sizes=@(16,24,32,48,64,256)
try {
 foreach($size in $sizes){
  $bitmap=[Drawing.Bitmap]::new($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
  try {
   $g=[Drawing.Graphics]::FromImage($bitmap)
   try {$g.Clear([Drawing.Color]::Transparent);$g.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic;
        $g.SmoothingMode=[Drawing.Drawing2D.SmoothingMode]::HighQuality;$g.PixelOffsetMode=[Drawing.Drawing2D.PixelOffsetMode]::HighQuality;
        # A light rounded backplate keeps the original dark outline visible on
        # dark taskbars; transparent corners remain in every ICO frame.
        $margin=[single]([Math]::Max(0.5,$size*0.025));$side=[single]($size-2*$margin);$radius=[single]($side*0.28)
        $path=[Drawing.Drawing2D.GraphicsPath]::new();$brush=[Drawing.SolidBrush]::new([Drawing.Color]::White)
        try {
          $path.AddArc($margin,$margin,$radius,$radius,180,90)
          $path.AddArc(($margin+$side-$radius),$margin,$radius,$radius,270,90)
          $path.AddArc(($margin+$side-$radius),($margin+$side-$radius),$radius,$radius,0,90)
          $path.AddArc($margin,($margin+$side-$radius),$radius,$radius,90,90)
          $path.CloseFigure();$g.FillPath($brush,$path)
        } finally {$path.Dispose();$brush.Dispose()}
        $padding=[single]($size*0.045);$extent=[single]($size-2*$padding)
        $g.DrawImage($image,$padding,$padding,$extent,$extent)} finally {$g.Dispose()}
   $stream=[IO.MemoryStream]::new()
   try {$bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png);$frames.Add($stream.ToArray())} finally {$stream.Dispose()}
  } finally {$bitmap.Dispose()}
 }
 $file=[IO.File]::Open($dest,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
 try {
  $writer=[IO.BinaryWriter]::new($file)
  try {
   $writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]$sizes.Count)
   $offset=6+16*$sizes.Count
   for($i=0;$i -lt $sizes.Count;$i++){
    $dim=if($sizes[$i] -eq 256){0}else{$sizes[$i]}
    $writer.Write([byte]$dim);$writer.Write([byte]$dim);$writer.Write([byte]0);$writer.Write([byte]0)
    $writer.Write([uint16]1);$writer.Write([uint16]32);$writer.Write([uint32]$frames[$i].Length);$writer.Write([uint32]$offset)
    $offset+=$frames[$i].Length
   }
   foreach($frame in $frames){$writer.Write([byte[]]$frame)}
  } finally {$writer.Dispose()}
 } finally {$file.Dispose()}
} finally {$image.Dispose()}
Write-Output "Created multi-resolution Windows icon: $dest"

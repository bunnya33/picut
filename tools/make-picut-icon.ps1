param([string]$SourcePng)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$picutAssetDirectory=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../assets'))
if (-not $SourcePng) { $SourcePng=Join-Path $picutAssetDirectory 'picut.png' }
$picutSource=[Drawing.Image]::FromFile([IO.Path]::GetFullPath($SourcePng))
$picutFrames=@()
foreach ($picutSize in @(16,20,24,32,40,48,64,96,128,256)) {
    $picutBitmap=[Drawing.Bitmap]::new($picutSize,$picutSize)
    $picutGraphics=[Drawing.Graphics]::FromImage($picutBitmap)
    $picutGraphics.Clear([Drawing.Color]::Transparent)
    $picutGraphics.CompositingMode=[Drawing.Drawing2D.CompositingMode]::SourceCopy
    $picutGraphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $picutGraphics.PixelOffsetMode=[Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $picutGraphics.DrawImage($picutSource,[Drawing.Rectangle]::new(0,0,$picutSize,$picutSize))
    $picutStream=[IO.MemoryStream]::new()
    $picutBitmap.Save($picutStream,[Drawing.Imaging.ImageFormat]::Png)
    $picutFrames+=,@{ Size=$picutSize; Bytes=$picutStream.ToArray() }
    $picutGraphics.Dispose(); $picutBitmap.Dispose(); $picutStream.Dispose()
}
$picutSource.Dispose()
$picutOutput=[IO.File]::Create((Join-Path $picutAssetDirectory 'picut.ico'))
$picutWriter=[IO.BinaryWriter]::new($picutOutput)
$picutWriter.Write([UInt16]0); $picutWriter.Write([UInt16]1); $picutWriter.Write([UInt16]$picutFrames.Count)
$picutOffset=6+16*$picutFrames.Count
foreach ($picutFrame in $picutFrames) {
    $picutDimension=if($picutFrame.Size -eq 256){0}else{$picutFrame.Size}
    $picutWriter.Write([byte]$picutDimension); $picutWriter.Write([byte]$picutDimension)
    $picutWriter.Write([byte]0); $picutWriter.Write([byte]0)
    $picutWriter.Write([UInt16]1); $picutWriter.Write([UInt16]32)
    $picutWriter.Write([UInt32]$picutFrame.Bytes.Length); $picutWriter.Write([UInt32]$picutOffset)
    $picutOffset+=$picutFrame.Bytes.Length
}
foreach ($picutFrame in $picutFrames) { $picutWriter.Write([byte[]]$picutFrame.Bytes) }
$picutWriter.Dispose(); $picutOutput.Dispose()

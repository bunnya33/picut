$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$picutAssetDirectory=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../assets'))
$picutFrames=@()
foreach ($picutSize in @(16,24,32,48,64,128,256)) {
    $picutBitmap=[Drawing.Bitmap]::new($picutSize,$picutSize)
    $picutGraphics=[Drawing.Graphics]::FromImage($picutBitmap)
    $picutGraphics.Clear([Drawing.Color]::Transparent)
    $picutMargin=[Math]::Max(1,[Math]::Floor($picutSize/16))
    $picutGap=$picutMargin
    $picutTile=[Math]::Floor(($picutSize-2*$picutMargin-$picutGap)/2)
    $picutColors=@('#4d90fe','#ffc700','#e94242','#80c342')
    for ($picutIndex=0;$picutIndex -lt 4;$picutIndex++) {
        $picutBrush=[Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($picutColors[$picutIndex]))
        $picutGraphics.FillRectangle($picutBrush,[int]($picutMargin+($picutIndex%2)*($picutTile+$picutGap)),[int]($picutMargin+[Math]::Floor($picutIndex/2)*($picutTile+$picutGap)),[int]$picutTile,[int]$picutTile)
        $picutBrush.Dispose()
    }
    $picutStream=[IO.MemoryStream]::new()
    $picutBitmap.Save($picutStream,[Drawing.Imaging.ImageFormat]::Png)
    $picutFrames+=,@{ Size=$picutSize; Bytes=$picutStream.ToArray() }
    if ($picutSize -eq 256) { $picutBitmap.Save((Join-Path $picutAssetDirectory 'picut.png'),[Drawing.Imaging.ImageFormat]::Png) }
    $picutGraphics.Dispose(); $picutBitmap.Dispose(); $picutStream.Dispose()
}
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

$ErrorActionPreference = 'Stop'
$floatshotExecutable = Join-Path $PSScriptRoot 'dist/FloatShot-Native-0.5.11-image-text-trial/FloatShotNative.exe'
if (-not (Test-Path -LiteralPath $floatshotExecutable)) {
    throw 'Image text trial missing. Run build.ps1 on codex/image-text-trial first.'
}
Start-Process -FilePath $floatshotExecutable -ArgumentList $(if ($args.Count) { $args } else { '--tray' })

$ErrorActionPreference = 'Stop'
$picutExecutable = Join-Path $PSScriptRoot 'dist/picut-0.5.27-scroll-trial/picut.exe'
if (-not (Test-Path -LiteralPath $picutExecutable)) {
    throw 'Scroll trial missing. Build and package codex/scroll-capture-v2 first.'
}
Start-Process -FilePath $picutExecutable -ArgumentList $(if ($args.Count) { $args } else { '--tray' }) -WindowStyle Hidden

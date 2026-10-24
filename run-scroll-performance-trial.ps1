$ErrorActionPreference = 'Stop'
$picutExecutable = Join-Path $PSScriptRoot 'dist/picut-0.5.29-scroll-trial/picut.exe'
if (-not (Test-Path -LiteralPath $picutExecutable)) {
    throw 'Scroll performance trial missing. Run build.ps1 on codex/scroll-performance-trial first.'
}
Start-Process -FilePath $picutExecutable -ArgumentList $(if ($args.Count) { $args } else { '--tray' }) -WindowStyle Hidden

$ErrorActionPreference = 'Stop'
$floatshotExecutable = Join-Path $PSScriptRoot 'dist/picut-0.5.18/picut.exe'
if (-not (Test-Path -LiteralPath $floatshotExecutable)) {
    throw 'Image text trial missing. Run build.ps1 on codex/image-text-trial first.'
}
Start-Process -FilePath $floatshotExecutable -ArgumentList $(if ($args.Count) { $args } else { '--tray' })

$ErrorActionPreference = 'Stop'
$floatshotExecutable = Join-Path $PSScriptRoot 'dist/picut-0.5.52/picut.exe'
if (-not (Test-Path -LiteralPath $floatshotExecutable)) {
    throw 'Native release missing. Run build.ps1 first.'
}
Start-Process -FilePath $floatshotExecutable -ArgumentList $(if ($args.Count) { $args } else { '--tray' }) -WindowStyle Hidden

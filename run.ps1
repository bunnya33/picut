$ErrorActionPreference = 'Stop'
$floatshotExecutable = Join-Path $PSScriptRoot 'dist/FloatShot-Native-0.5.10/FloatShotNative.exe'
if (-not (Test-Path -LiteralPath $floatshotExecutable)) {
    throw 'Native release missing. Run build.ps1 first.'
}
Start-Process -FilePath $floatshotExecutable -ArgumentList $(if ($args.Count) { $args } else { '--tray' })

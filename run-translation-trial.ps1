$ErrorActionPreference = 'Stop'
$picutExecutable = Join-Path $PSScriptRoot 'dist/picut-0.5.23-translation-trial/picut.exe'
if (-not (Test-Path -LiteralPath $picutExecutable)) { throw 'Translation trial missing. Build and package codex/picut-local-translation first.' }
# The executable also selects this isolated profile when launched directly.
Start-Process -FilePath $picutExecutable -ArgumentList $(if ($args.Count) { $args } else { '--tray' }) -WindowStyle Hidden

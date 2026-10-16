[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$picutRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$picutTools = Join-Path $picutRoot 'build/installer-tools'
$picutDownloads = Join-Path $picutTools 'downloads'
$picutInno = Join-Path $picutTools 'inno-6.7.3'
New-Item -ItemType Directory -Path $picutDownloads -Force | Out-Null

function Get-InstallerDownload([string]$Url, [string]$File, [string]$Sha256) {
    if (-not (Test-Path -LiteralPath $File)) {
        Invoke-WebRequest -Uri $Url -OutFile $File -UseBasicParsing
    }
    if ((Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash -ne $Sha256) {
        throw "Installer SDK checksum mismatch: $File"
    }
}

$picutBootstrap = Join-Path $picutDownloads 'innosetup-6.7.3.exe'
Get-InstallerDownload 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe' $picutBootstrap '9C73C3BAE7ED48D44112A0F48E66742C00090BDB5BEF71D9D3C056C66E97B732'
$picutSignature = Get-AuthenticodeSignature -LiteralPath $picutBootstrap
if ($picutSignature.Status -ne 'Valid' -or $picutSignature.SignerCertificate.Subject -notmatch 'O=Pyrsys B\.V\.') {
    throw 'The official Inno Setup bootstrap must have a valid Pyrsys B.V. signature.'
}
if (-not (Test-Path -LiteralPath (Join-Path $picutInno 'ISCC.exe'))) {
    # Portable mode creates no registry entries, shortcuts or file associations.
    $picutBootstrapLog = Join-Path $picutTools 'compiler-setup.log'
    $picutProcess = Start-Process -FilePath $picutBootstrap -ArgumentList @(
        '/PORTABLE=1', '/CURRENTUSER', '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
        ('/DIR="' + $picutInno + '"'), ('/LOG="' + $picutBootstrapLog + '"')
    ) -PassThru -WindowStyle Hidden -Wait
    if ($picutProcess.ExitCode -ne 0) { throw "Installer SDK setup failed: $($picutProcess.ExitCode)" }
}
# Chinese is a user-contributed translation distributed in the official source tag.
$picutChinese = Join-Path $picutTools 'ChineseSimplified.isl'
Get-InstallerDownload 'https://raw.githubusercontent.com/jrsoftware/issrc/is-6_7_3/Files/Languages/Unofficial/ChineseSimplified.isl' $picutChinese '7D544B9BB1D142CFA11F2E5D3CC8ABE2E55F8E066C5124E3772675AA236E1278'
Write-Output "Installer SDK ready: $picutInno"

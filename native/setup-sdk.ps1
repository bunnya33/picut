[CmdletBinding()]
param([switch]$SkipFfmpeg)
$ErrorActionPreference = 'Stop'
$floatshotRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$floatshotSdk = Join-Path $floatshotRoot 'build/native-sdk'
$floatshotDownloads = Join-Path $floatshotSdk 'downloads'
$floatshotQt = Join-Path $floatshotSdk 'qt'
New-Item -ItemType Directory -Path $floatshotDownloads,$floatshotQt -Force | Out-Null
$floatshotRepository = 'https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_683/qt6_683/'
$floatshotArchiveSuffix = '-Windows-Windows_11_23H2-MSVC2022-Windows-Windows_11_23H2-X86_64.7z'
$floatshotArchives = @(
    @{ Module='qtbase'; Package='qt.qt6.683.win64_msvc2022_64'; Hash='41688269FAC0565DB956C66D9EECAE777D16197E0C02CD81B88640C1F5D73D3F' },
    @{ Module='qttools'; Package='qt.qt6.683.win64_msvc2022_64'; Hash='76AC7B8C86CA72B6608F1D41EE848AEAB19663536990B4968CE52DB8EC980BC3' },
    @{ Module='qtmultimedia'; Package='qt.qt6.683.addons.qtmultimedia.win64_msvc2022_64'; Hash='607AFD11B22079BC8D6BB1B6DF08A9B44F5D15D1B87089A1CB00902DFA0A4340' },
    @{ Module='qtimageformats'; Package='qt.qt6.683.addons.qtimageformats.win64_msvc2022_64'; Hash='55F4D6AEB3270DA0D3F88318E704AADF73DBD4C64B6DD17811CC5988062A5CD0' }
)
function Get-VerifiedDownload([string]$Url, [string]$File, [string]$Hash) {
    if (-not (Test-Path -LiteralPath $File)) { Invoke-WebRequest -Uri $Url -OutFile $File }
    if ((Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash -ne $Hash) { throw "Checksum mismatch: $File" }
}
foreach ($floatshotArchive in $floatshotArchives) {
    $floatshotName = $floatshotArchive.Module + $floatshotArchiveSuffix
    $floatshotFile = Join-Path $floatshotDownloads $floatshotName
    $floatshotUrl = $floatshotRepository + $floatshotArchive.Package + '/6.8.3-0-202503201308' + $floatshotName
    Get-VerifiedDownload $floatshotUrl $floatshotFile $floatshotArchive.Hash
    & (Join-Path $env:WINDIR 'System32/tar.exe') -xf $floatshotFile -C $floatshotQt
    if ($LASTEXITCODE -ne 0) { throw "SDK extraction failed: $floatshotName" }
}
[IO.File]::WriteAllText((Join-Path $floatshotQt 'bin/qt.conf'), "[Paths]`r`nPrefix=..`r`n", [Text.Encoding]::ASCII)
$floatshotZxingZip = Join-Path $floatshotDownloads 'zxing-cpp-2.3.0.zip'
Get-VerifiedDownload 'https://codeload.github.com/zxing-cpp/zxing-cpp/zip/refs/tags/v2.3.0' $floatshotZxingZip '89B70F6175C6347D72BDC72722D643C0F461DC8A0D9BBC4C927A240B32B706F2'
Expand-Archive -LiteralPath $floatshotZxingZip -DestinationPath $floatshotSdk -Force
if (-not $SkipFfmpeg) {
    Get-VerifiedDownload 'https://raw.githubusercontent.com/imageio/imageio-binaries/master/ffmpeg/ffmpeg-win-x86_64-v7.1.exe' (Join-Path $floatshotSdk 'ffmpeg.exe') '2CE797A0F88D7F067180338FB227F7B1928EA727BD9A4D7A1D022F7C52AF71A3'
}
Write-Output "SDK ready: $floatshotSdk"
& (Join-Path $PSScriptRoot 'setup-ocr-sdk.ps1')

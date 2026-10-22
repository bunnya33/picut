[CmdletBinding()]
param([string]$VisualStudioDir)
$ErrorActionPreference = 'Stop'
$picutRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$picutSdk = Join-Path $picutRoot 'build/translation-sdk'
$picutDownloads = Join-Path $picutSdk 'downloads'
New-Item -ItemType Directory -Path $picutDownloads -Force | Out-Null

function Get-TranslationSource([string]$Name, [string]$Repository, [string]$Revision, [string]$Destination) {
    $archive = Join-Path $picutDownloads ($Name + '.zip')
    if (-not (Test-Path -LiteralPath $archive)) {
        Invoke-WebRequest -UseBasicParsing -Uri ('https://codeload.github.com/' + $Repository + '/zip/' + $Revision) -OutFile $archive -TimeoutSec 180
    }
    $expectedHashes = @{
        'ctranslate2-4.6.0'='745A21E728535691CC800791D4D501D90422CE43911B56D5A5DE366B3DD7339F'
        'cpu-features'='4CCDED88992089EFFA9852740B192E404E7BA26541B185AD2A4A0E8AA95A109C'
        'spdlog'='9A00DD50318B9467148ADC5E822E55221C65D8D8794C6890BA034EED222DCF64'
        'ruy'='6CE5D6B89DEF2C4F2772074FE89451A3DE69C9BFE30A3887A6F2749111824094'
        'cpuinfo'='CD4D4A7659581260F57F821C151826190813F2E3741E7EDAE4AEF336D50D6496'
        'sentencepiece-0.2.0'='67C34082DEB3D89F589D8614B1AE58AA4AF250C79E18737D8103242EC62A28F9'
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHashes[$Name]) { throw ('Translation source checksum mismatch: ' + $Name) }
    if (-not (Test-Path -LiteralPath (Join-Path $Destination 'CMakeLists.txt'))) {
        $expanded = Join-Path $picutDownloads ($Name + '-expanded')
        Expand-Archive -LiteralPath $archive -DestinationPath $expanded -Force
        $source = Get-ChildItem -LiteralPath $expanded -Directory | Select-Object -First 1
        New-Item -ItemType Directory -Path $Destination -Force | Out-Null
        Get-ChildItem -LiteralPath $source.FullName -Force | Copy-Item -Destination $Destination -Recurse -Force
    }
    [ordered]@{ name=$Name; repository=$Repository; revision=$Revision; sha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash }
}

$picutCt2 = Join-Path $picutSdk 'ctranslate2'
$picutSources = @(
    Get-TranslationSource 'ctranslate2-4.6.0' 'OpenNMT/CTranslate2' 'v4.6.0' $picutCt2
    Get-TranslationSource 'cpu-features' 'google/cpu_features' '8a494eb1e158ec2050e5f699a504fbc9b896a43b' (Join-Path $picutCt2 'third_party/cpu_features')
    Get-TranslationSource 'spdlog' 'gabime/spdlog' '76fb40d95455f249bd70824ecfcae7a8f0930fa3' (Join-Path $picutCt2 'third_party/spdlog')
    Get-TranslationSource 'ruy' 'google/ruy' '363f252289fb7a1fba1703d99196524698cb884d' (Join-Path $picutCt2 'third_party/ruy')
    Get-TranslationSource 'cpuinfo' 'pytorch/cpuinfo' '082deffc80ce517f81dc2f3aebe6ba671fcd09c9' (Join-Path $picutCt2 'third_party/ruy/third_party/cpuinfo')
    Get-TranslationSource 'sentencepiece-0.2.0' 'google/sentencepiece' 'v0.2.0' (Join-Path $picutSdk 'sentencepiece')
)
[IO.File]::WriteAllText((Join-Path $picutSdk 'sources.json'), ($picutSources | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding($false)))

if (-not $VisualStudioDir) {
    $picutVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioDir = & $picutVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $VisualStudioDir) { throw 'Visual Studio C++ desktop tools are required.' }
$picutVcvars = Join-Path $VisualStudioDir 'VC/Auxiliary/Build/vcvars64.bat'
$picutNinja = Join-Path $VisualStudioDir 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
$picutCmake = (Get-Command cmake.exe).Source
$picutBuild = Join-Path $picutSdk 'build'
$picutToolset = ''
if (Get-ChildItem -LiteralPath (Join-Path $VisualStudioDir 'VC/Tools/MSVC') -Directory | Where-Object Name -Like '14.44.*') { $picutToolset = '-vcvars_ver=14.44' }
$picutCommands = @"
@echo off
call "$picutVcvars" $picutToolset >nul
if errorlevel 1 exit /b %errorlevel%
set "PATH=$picutNinja;%PATH%"
"$picutCmake" -S "$PSScriptRoot/translation-sdk" -B "$picutBuild" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.10
if errorlevel 1 exit /b %errorlevel%
"$picutCmake" --build "$picutBuild" --parallel 4
exit /b %errorlevel%
"@
$picutCommandFile = Join-Path $picutSdk 'build-sdk.cmd'
[IO.File]::WriteAllText($picutCommandFile, $picutCommands, [Text.Encoding]::Default)
& $env:ComSpec /d /c $picutCommandFile
if ($LASTEXITCODE -ne 0) { throw 'Translation SDK build failed.' }
Write-Output ('Native translation SDK ready: ' + $picutBuild)

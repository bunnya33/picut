[CmdletBinding()]
param([string]$QtDir, [string]$BuildDir, [string]$VisualStudioDir, [switch]$NoTests)
$ErrorActionPreference = 'Stop'
$floatshotRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $QtDir) { $QtDir = Join-Path $floatshotRoot 'build/native-sdk/qt' }
if (-not $BuildDir) { $BuildDir = Join-Path $floatshotRoot 'build/native' }
$QtDir = [IO.Path]::GetFullPath($QtDir)
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
if (-not (Test-Path -LiteralPath (Join-Path $QtDir 'lib/cmake/Qt6/Qt6Config.cmake'))) {
    throw 'Qt SDK missing. Install Qt 6.8 MSVC x64 or run native/setup-sdk.ps1.'
}
if (-not $VisualStudioDir) {
    $floatshotVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VisualStudioDir = & $floatshotVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $VisualStudioDir) { throw 'Install Visual Studio C++ desktop tools and a Windows SDK.' }
$floatshotVcvars = Join-Path $VisualStudioDir 'VC/Auxiliary/Build/vcvars64.bat'
$floatshotNinja = Join-Path $VisualStudioDir 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
$floatshotCmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
if (-not $floatshotCmake) { $floatshotCmake = Join-Path $env:ProgramFiles 'CMake/bin/cmake.exe' }
if (-not (Test-Path -LiteralPath $floatshotCmake)) { throw 'Install CMake 3.24 or newer.' }
$floatshotToolset = ''
if (Get-ChildItem -LiteralPath (Join-Path $VisualStudioDir 'VC/Tools/MSVC') -Directory | Where-Object Name -Like '14.44.*') {
    $floatshotToolset = '-vcvars_ver=14.44'
}
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
$floatshotLaunch = Join-Path $BuildDir 'build-with-msvc.cmd'
$floatshotTests = if ($NoTests) { 'OFF' } else { 'ON' }
# A saved command file preserves Windows quoting and the vcvars environment.
$floatshotCommands = @"
@echo off
call "$floatshotVcvars" $floatshotToolset >nul
if errorlevel 1 exit /b %errorlevel%
set "PATH=$floatshotNinja;%PATH%"
"$floatshotCmake" -S "$PSScriptRoot" -B "$BuildDir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=$floatshotTests -DCMAKE_PREFIX_PATH="$QtDir"
if errorlevel 1 exit /b %errorlevel%
"$floatshotCmake" --build "$BuildDir" --parallel 4
exit /b %errorlevel%
"@
[IO.File]::WriteAllText($floatshotLaunch, $floatshotCommands, [Text.Encoding]::Default)
& $env:ComSpec /d /c $floatshotLaunch
if ($LASTEXITCODE -ne 0) { throw "Native build failed with exit code $LASTEXITCODE." }


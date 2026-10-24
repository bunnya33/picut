[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$picutRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$picutSdk = Join-Path $picutRoot 'build/native-sdk'
$picutCvSource = Join-Path $picutSdk 'opencv-4.11.0'
$picutCvInstall = Join-Path $picutSdk 'opencv-scroll'
if (Test-Path -LiteralPath (Join-Path $picutCvInstall 'x64/vc17/staticlib/opencv_video4110.lib')) {
    Write-Output "Scroll image processor ready: $picutCvInstall"
    return
}
if (-not (Test-Path -LiteralPath (Join-Path $picutCvSource 'CMakeLists.txt'))) {
    $picutDownload = Join-Path $picutSdk 'downloads/ocr/opencv-4.11.0.zip'
    New-Item -ItemType Directory -Path (Split-Path -Parent $picutDownload) -Force | Out-Null
    if (-not (Test-Path -LiteralPath $picutDownload)) {
        Invoke-WebRequest -Uri 'https://codeload.github.com/opencv/opencv/zip/refs/tags/4.11.0' -OutFile $picutDownload
    }
    if ((Get-FileHash -LiteralPath $picutDownload -Algorithm SHA256).Hash -ne '11DBD2C8D248FA97AC7D20F33C4BAB8559EF32835D1C9274009150BB2CF5218A') {
        throw 'Scroll OpenCV source checksum mismatch.'
    }
    Expand-Archive -LiteralPath $picutDownload -DestinationPath $picutSdk -Force
}
$picutVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$picutVs = & $picutVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $picutVs) { throw 'Visual Studio C++ tools are required.' }
$picutVcvars = Join-Path $picutVs 'VC/Auxiliary/Build/vcvars64.bat'
$picutNinja = Join-Path $picutVs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
$picutCmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
if (-not $picutCmake) { $picutCmake = Join-Path $env:ProgramFiles 'CMake/bin/cmake.exe' }
$picutToolset = ''
if (Get-ChildItem -LiteralPath (Join-Path $picutVs 'VC/Tools/MSVC') -Directory | Where-Object Name -Like '14.44.*') {
    $picutToolset = '-vcvars_ver=14.44'
}
$picutCvBuild = Join-Path $picutRoot 'build/scroll-opencv'
New-Item -ItemType Directory -Path $picutCvBuild -Force | Out-Null
$picutCommand = Join-Path $picutCvBuild 'build-opencv.cmd'
$picutCommands = @"
@echo off
call "$picutVcvars" $picutToolset >nul
if errorlevel 1 exit /b %errorlevel%
set "PATH=$picutNinja;%PATH%"
"$picutCmake" -S "$picutCvSource" -B "$picutCvBuild" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$picutCvInstall" -DBUILD_LIST=core,imgproc,video -DBUILD_SHARED_LIBS=OFF -DBUILD_WITH_STATIC_CRT=OFF -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOCS=OFF -DBUILD_opencv_apps=OFF -DBUILD_opencv_python2=OFF -DBUILD_opencv_python3=OFF -DBUILD_JAVA=OFF -DWITH_IPP=OFF -DWITH_ITT=OFF -DWITH_OPENCL=OFF -DWITH_LAPACK=OFF -DWITH_EIGEN=OFF -DWITH_TBB=OFF -DWITH_FFMPEG=OFF -DWITH_OPENEXR=OFF
if errorlevel 1 exit /b %errorlevel%
"$picutCmake" --build "$picutCvBuild" --parallel 4
if errorlevel 1 exit /b %errorlevel%
"$picutCmake" --install "$picutCvBuild" --config Release
exit /b %errorlevel%
"@
[IO.File]::WriteAllText($picutCommand, $picutCommands, [Text.Encoding]::Default)
& $env:ComSpec /d /c $picutCommand
if ($LASTEXITCODE -ne 0) { throw 'Scroll OpenCV C++ build failed.' }
Write-Output "Scroll image processor ready: $picutCvInstall"

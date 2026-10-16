[CmdletBinding()]
param([switch]$SkipOpenCvBuild)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$floatshotRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$floatshotSdk = Join-Path $floatshotRoot 'build/native-sdk'
$floatshotDownloads = Join-Path $floatshotSdk 'downloads/ocr'
$floatshotModels = Join-Path $floatshotSdk 'paddleocr-small'
New-Item -ItemType Directory -Path $floatshotDownloads,$floatshotModels -Force | Out-Null
function Get-OcrDownload([string]$Url, [string]$File, [string]$Hash) {
    if (-not (Test-Path -LiteralPath $File)) { Invoke-WebRequest -Uri $Url -OutFile $File }
    if ((Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash -ne $Hash) { throw "OCR dependency checksum mismatch: $File" }
}
$floatshotOrtArchive = Join-Path $floatshotDownloads 'onnxruntime-win-x64-1.22.0.zip'
Get-OcrDownload 'https://github.com/microsoft/onnxruntime/releases/download/v1.22.0/onnxruntime-win-x64-1.22.0.zip' $floatshotOrtArchive '174C616EFC0271194488642A72F1A514E01487DA4DFE84C49296D66E40EBE0DA'
if (-not (Test-Path -LiteralPath (Join-Path $floatshotSdk 'onnxruntime-win-x64-1.22.0/include/onnxruntime_cxx_api.h'))) {
    Expand-Archive -LiteralPath $floatshotOrtArchive -DestinationPath $floatshotSdk -Force
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$floatshotGpuArchive=Join-Path $floatshotDownloads 'onnxruntime-directml-1.22.0.nupkg'
Get-OcrDownload 'https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/1.22.0/microsoft.ml.onnxruntime.directml.1.22.0.nupkg' $floatshotGpuArchive '29F9872D786236B79AA83F94482F3A17C14297E4833768D6D0ED4883EE732E60'
$floatshotGpuSdk=Join-Path $floatshotSdk 'onnxruntime-directml-1.22.0'
if (-not(Test-Path -LiteralPath $floatshotGpuSdk)) { [IO.Compression.ZipFile]::ExtractToDirectory($floatshotGpuArchive,$floatshotGpuSdk) }
$floatshotDmlArchive=Join-Path $floatshotDownloads 'directml-1.15.4.nupkg'
Get-OcrDownload 'https://api.nuget.org/v3-flatcontainer/microsoft.ai.directml/1.15.4/microsoft.ai.directml.1.15.4.nupkg' $floatshotDmlArchive '4E7CB7DDCE8CF837A7A75DC029209B520CA0101470FCDF275C1F49736A3615B9'
$floatshotDmlSdk=Join-Path $floatshotSdk 'directml-1.15.4'
if (-not(Test-Path -LiteralPath $floatshotDmlSdk)) { [IO.Compression.ZipFile]::ExtractToDirectory($floatshotDmlArchive,$floatshotDmlSdk) }
$floatshotLicenseFolder=Join-Path $PSScriptRoot 'licenses'
New-Item -ItemType Directory -Path $floatshotLicenseFolder -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $floatshotGpuSdk 'LICENSE') -Destination (Join-Path $floatshotLicenseFolder 'ONNXRuntime-DirectML-MIT.txt') -Force
Copy-Item -LiteralPath (Join-Path $floatshotGpuSdk 'ThirdPartyNotices.txt') -Destination (Join-Path $floatshotLicenseFolder 'ONNXRuntime-DirectML-ThirdPartyNotices.txt') -Force
foreach ($floatshotDmlNotice in @('LICENSE.txt','LICENSE-CODE.txt','ThirdPartyNotices.txt')) {
    Copy-Item -LiteralPath (Join-Path $floatshotDmlSdk $floatshotDmlNotice) -Destination (Join-Path $floatshotLicenseFolder ('DirectML-' + $floatshotDmlNotice)) -Force
}
$floatshotCvArchive = Join-Path $floatshotDownloads 'opencv-4.11.0.zip'
Get-OcrDownload 'https://codeload.github.com/opencv/opencv/zip/refs/tags/4.11.0' $floatshotCvArchive '11DBD2C8D248FA97AC7D20F33C4BAB8559EF32835D1C9274009150BB2CF5218A'
if (-not (Test-Path -LiteralPath (Join-Path $floatshotSdk 'opencv-4.11.0/CMakeLists.txt'))) {
    Expand-Archive -LiteralPath $floatshotCvArchive -DestinationPath $floatshotSdk -Force
}
$floatshotModelSource = 'https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.10.0/'
Get-OcrDownload ($floatshotModelSource + 'onnx/PP-OCRv6/det/PP-OCRv6_det_small.onnx') (Join-Path $floatshotModels 'det.onnx') '090F04ABCD9D9A7498BC4EBF677E4CB9BDCE1FE4197DDB7E529F1EF44E1FF94F'
Get-OcrDownload ($floatshotModelSource + 'onnx/PP-OCRv6/rec/PP-OCRv6_rec_small.onnx') (Join-Path $floatshotModels 'rec.onnx') '6F327246B50388F3C176AE304BD95767EA6DC0C9AE92153EF8CBE210B3C14884'
Get-OcrDownload ($floatshotModelSource + 'onnx/PP-OCRv5/cls/ch_PP-LCNet_x1_0_textline_ori_cls_server.onnx') (Join-Path $floatshotModels 'cls.onnx') '7D3C02EF6C7DA8AE08B4347CC7695B2081AAE68C325D64375724ECF39C99E743'
Get-OcrDownload ($floatshotModelSource + 'paddle/PP-OCRv6/rec/PP-OCRv6_rec_small/ppocrv6_dict.txt') (Join-Path $floatshotModels 'dict.txt') 'B5F2BFE2BDD9448429E3E82B51C789775D9B42F2403D082B00662EB77E401C5D'
$floatshotModelManifest = [ordered]@{
    family='PaddleOCR PP-OCRv6 small (desktop/mobile tier, original precision)'; conversion='RapidAI/RapidOCR v3.10.0 ONNX'; license='Apache-2.0'
    source=$floatshotModelSource
    det='det.onnx'; cls='cls.onnx'
    profiles=[ordered]@{common=[ordered]@{label='Small · 中简繁 / 日文 / 拉丁字母（50 语种）'; model='PP-OCRv6 small'; rec='rec.onnx'; dict='dict.txt'}}
    checksums=[ordered]@{
        'det.onnx'='090f04abcd9d9a7498bc4ebf677e4cb9bdce1fe4197ddb7e529f1ef44e1ff94f'
        'rec.onnx'='6f327246b50388f3c176ae304bd95767ea6dc0c9ae92153ef8cbe210b3c14884'
        'cls.onnx'='7d3c02ef6c7da8ae08b4347cc7695b2081aae68c325d64375724ecf39c99e743'
        'dict.txt'='b5f2bfe2bdd9448429e3e82b51c789775d9b42f2403d082b00662eb77e401c5d'
    }
}
[IO.File]::WriteAllText((Join-Path $floatshotModels 'models.json'), ($floatshotModelManifest | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
if (-not $SkipOpenCvBuild) {
    $floatshotCvInstall = Join-Path $floatshotSdk 'opencv-min'
    $floatshotCvCache = Join-Path $floatshotRoot 'build/ocr-opencv/CMakeCache.txt'
    $floatshotDynamicCrt = (Test-Path -LiteralPath $floatshotCvCache) -and (Select-String -LiteralPath $floatshotCvCache -Pattern '^BUILD_WITH_STATIC_CRT:BOOL=OFF$' -Quiet)
    if (-not (Test-Path -LiteralPath (Join-Path $floatshotCvInstall 'OpenCVConfig.cmake')) -or -not $floatshotDynamicCrt) {
        $floatshotVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $floatshotVs = & $floatshotVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not $floatshotVs) { throw 'Visual Studio C++ tools are required for the OCR image processor.' }
        $floatshotVcvars = Join-Path $floatshotVs 'VC/Auxiliary/Build/vcvars64.bat'
        $floatshotNinja = Join-Path $floatshotVs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
        $floatshotCmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
        if (-not $floatshotCmake) { $floatshotCmake = Join-Path $env:ProgramFiles 'CMake/bin/cmake.exe' }
        $floatshotCvBuild = Join-Path $floatshotRoot 'build/ocr-opencv'
        New-Item -ItemType Directory -Path $floatshotCvBuild -Force | Out-Null
        $floatshotToolset = ''
        if (Get-ChildItem -LiteralPath (Join-Path $floatshotVs 'VC/Tools/MSVC') -Directory | Where-Object Name -Like '14.44.*') { $floatshotToolset = '-vcvars_ver=14.44' }
        $floatshotCommandFile = Join-Path $floatshotCvBuild 'build-opencv.cmd'
        $floatshotCommands = @"
@echo off
call "$floatshotVcvars" $floatshotToolset >nul
if errorlevel 1 exit /b %errorlevel%
set "PATH=$floatshotNinja;%PATH%"
"$floatshotCmake" -S "$floatshotSdk/opencv-4.11.0" -B "$floatshotCvBuild" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$floatshotCvInstall" -DBUILD_LIST=core,imgproc -DBUILD_SHARED_LIBS=OFF -DBUILD_WITH_STATIC_CRT=OFF -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOCS=OFF -DBUILD_opencv_apps=OFF -DBUILD_opencv_python2=OFF -DBUILD_opencv_python3=OFF -DBUILD_JAVA=OFF -DWITH_IPP=OFF -DWITH_ITT=OFF -DWITH_OPENCL=OFF -DWITH_LAPACK=OFF -DWITH_EIGEN=OFF -DWITH_TBB=OFF -DWITH_FFMPEG=OFF -DWITH_OPENEXR=OFF
if errorlevel 1 exit /b %errorlevel%
"$floatshotCmake" --build "$floatshotCvBuild" --parallel 4
if errorlevel 1 exit /b %errorlevel%
"$floatshotCmake" --install "$floatshotCvBuild" --config Release
exit /b %errorlevel%
"@
        [IO.File]::WriteAllText($floatshotCommandFile,$floatshotCommands,[Text.Encoding]::Default)
        & $env:ComSpec /d /c $floatshotCommandFile
        if ($LASTEXITCODE -ne 0) { throw 'Minimal OpenCV C++ build failed.' }
    }
}
Write-Output "Native PaddleOCR dependencies ready: $floatshotModels"

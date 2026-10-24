[CmdletBinding()]
param([string]$QtDir, [string]$Ffmpeg, [string]$PackageDir, [string]$BuildDir, [switch]$SkipBuild, [switch]$NoZip)
$ErrorActionPreference = 'Stop'
$floatshotRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $BuildDir) { $BuildDir = Join-Path $floatshotRoot 'build/native-scroll-performance' }
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
if (-not $QtDir) { $QtDir = Join-Path $floatshotRoot 'build/native-sdk/qt' }
if (-not $Ffmpeg) { $Ffmpeg = Join-Path $floatshotRoot 'build/native-sdk/ffmpeg.exe' }
if (-not (Test-Path -LiteralPath $Ffmpeg)) { throw 'FFmpeg missing. Run native/setup-sdk.ps1 or pass -Ffmpeg.' }
if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') -QtDir $QtDir -BuildDir $BuildDir }
$floatshotPackage = if ($PackageDir) { [IO.Path]::GetFullPath($PackageDir) } else { Join-Path $floatshotRoot 'dist/picut-0.5.32-scroll-trial' }
if ($floatshotPackage -notmatch 'scroll-trial') { throw 'Use a separate scroll-trial package directory.' }
New-Item -ItemType Directory -Path $floatshotPackage -Force | Out-Null
foreach ($floatshotBinary in @('picut.exe', 'NativeVideoPreview.dll', 'FloatShotPaddleOcr.exe', 'onnxruntime.dll', 'FloatShotTranslate.exe', 'ctranslate2.dll')) {
    Copy-Item -LiteralPath (Join-Path $BuildDir $floatshotBinary) -Destination $floatshotPackage -Force
}
$picutTranslationSource = Join-Path $floatshotRoot 'build/translation-sdk/models'
$picutTranslationManifest = Get-Content -LiteralPath (Join-Path $picutTranslationSource 'models.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$picutTranslationTarget = Join-Path $floatshotPackage 'translation'
New-Item -ItemType Directory -Path $picutTranslationTarget -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $picutTranslationSource 'models.json') -Destination $picutTranslationTarget -Force
foreach ($picutDirection in $picutTranslationManifest.models.PSObject.Properties) {
    $picutDirectionTarget = Join-Path $picutTranslationTarget $picutDirection.Name
    New-Item -ItemType Directory -Path $picutDirectionTarget -Force | Out-Null
    foreach ($picutModelFile in $picutDirection.Value.checksums.PSObject.Properties) {
        $picutSourceFile = Join-Path $picutTranslationSource ($picutDirection.Name + '/' + $picutModelFile.Name)
        Copy-Item -LiteralPath $picutSourceFile -Destination $picutDirectionTarget -Force
        if ((Get-FileHash -LiteralPath (Join-Path $picutDirectionTarget $picutModelFile.Name)).Hash -ne $picutModelFile.Value) { throw 'Translation model checksum mismatch.' }
    }
}
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/试验版-v0.5.32-流畅滚动.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/试验版-v0.5.31-流畅滚动.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/试验版-v0.5.30-流畅滚动.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/试验版-v0.5.29-流畅滚动.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.26-正式翻译与Shift画笔.md') -Destination $floatshotPackage -Force
foreach ($picutReport in @('native-v0.5.32-tests.txt','native-v0.5.32-scroll-v2-tests.txt','native-v0.5.32-translation-tests.txt','native-v0.5.32-ocr-tests.txt')) {
    $picutReportSource = Join-Path $floatshotRoot ('.test-artifacts/' + $picutReport)
    if (Test-Path -LiteralPath $picutReportSource) { Copy-Item -LiteralPath $picutReportSource -Destination $floatshotPackage -Force }
}
$floatshotGpuFolder = Join-Path $floatshotPackage 'gpu'
New-Item -ItemType Directory -Path $floatshotGpuFolder -Force | Out-Null
foreach ($floatshotGpuFile in @('FloatShotPaddleOcr.exe','onnxruntime.dll','onnxruntime_providers_shared.dll','DirectML.dll')) {
    Copy-Item -LiteralPath (Join-Path $BuildDir ('gpu/' + $floatshotGpuFile)) -Destination $floatshotGpuFolder -Force
}
$floatshotOcrFolder = Join-Path $floatshotPackage 'ocr'
New-Item -ItemType Directory -Path $floatshotOcrFolder -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDir 'ocr/models.json') -Destination $floatshotOcrFolder -Force
$floatshotModelManifest = Get-Content -LiteralPath (Join-Path $floatshotOcrFolder 'models.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($floatshotChecksum in $floatshotModelManifest.checksums.PSObject.Properties) {
    Copy-Item -LiteralPath (Join-Path $BuildDir ('ocr/' + $floatshotChecksum.Name)) -Destination $floatshotOcrFolder -Force
    $floatshotModelFile = Join-Path $floatshotPackage ('ocr/' + $floatshotChecksum.Name)
    if ((Get-FileHash -LiteralPath $floatshotModelFile).Hash -ne $floatshotChecksum.Value) { throw "Packaged OCR model checksum mismatch: $floatshotModelFile" }
}
Copy-Item -LiteralPath $Ffmpeg -Destination (Join-Path $floatshotPackage 'ffmpeg.exe') -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'assets/picut.ico') -Destination $floatshotPackage -Force
$env:PATH = (Join-Path $QtDir 'bin') + ';' + $env:PATH
& (Join-Path $QtDir 'bin/windeployqt.exe') --release --no-translations --no-opengl-sw --no-compiler-runtime --exclude-plugins qdds,qicns,qjp2,qmng,qtga,qtiff,qwbmp --dir $floatshotPackage (Join-Path $floatshotPackage 'picut.exe') (Join-Path $floatshotPackage 'NativeVideoPreview.dll')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
$floatshotVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$floatshotVs = & $floatshotVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$floatshotRuntime = Get-ChildItem -Path (Join-Path $floatshotVs 'VC/Redist/MSVC/*/x64/Microsoft.VC14*.CRT') -Directory | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $floatshotRuntime) { throw 'Visual C++ redistributable DLLs missing.' }
Copy-Item -Path (Join-Path $floatshotRuntime.FullName '*.dll') -Destination $floatshotPackage -Force
[IO.File]::WriteAllText((Join-Path $floatshotPackage 'qt.conf'), "[Paths]`r`nPrefix=.`r`nPlugins=.`r`n", [Text.Encoding]::ASCII)
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'licenses') -Destination $floatshotPackage -Recurse -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'THIRD_PARTY_NOTICES.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '使用说明.txt') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'diagnose.ps1') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'diagnose.cmd') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/原生迁移-v0.5.0.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.1.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.2.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.3.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.10.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.11-图片选字试用.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.12-同步文字侧栏.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.13-picut录屏与序号.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.14-热键浅色与兼容.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.15-滚动截屏.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.16-F3贴图修复.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.18-滚动拼接重构.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.19-动态表情滚动截屏.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.20-长图动态刷新.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.21-长图内容保护.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/版本回滚.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.9.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.8.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.7.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.6.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.5.md') -Destination $floatshotPackage -Force
Copy-Item -LiteralPath (Join-Path $floatshotRoot 'docs/更新记录-v0.5.4.md') -Destination $floatshotPackage -Force
foreach ($floatshotPinReport in @('native-v0.5.5-pin-comparison.json', 'native-v0.5.21-pin-after/pin-latency-report.json')) {
    $floatshotPinSource = Join-Path $floatshotRoot ('.test-artifacts/' + $floatshotPinReport)
    if (Test-Path -LiteralPath $floatshotPinSource) { Copy-Item -LiteralPath $floatshotPinSource -Destination $floatshotPackage -Force }
}
$floatshotPortablePinReport = Join-Path $floatshotRoot '.test-artifacts/native-v0.5.21-portable-pin/pin-latency-report.json'
if (Test-Path -LiteralPath $floatshotPortablePinReport) {
    Copy-Item -LiteralPath $floatshotPortablePinReport -Destination (Join-Path $floatshotPackage 'portable-pin-latency-report.json') -Force
}
foreach ($floatshotReport in @('native-v0.5.21-tests.txt', 'native-v0.5.16-f3-tests.txt', 'native-v0.5.15-recording-tests.txt', 'native-v0.5.15-ocr-tests.txt', 'native-v0.5.15-ocr-sidebar-tests.txt', 'native-v0.5.15-compatibility-recording-tests.txt', 'native-v0.5.15-portable-tests.txt', 'native-v0.5.15-portable-ocr-tests.txt', 'native-v0.5.15-windows-selection-tests.txt', 'native-v0.5.15-windows-ocr-tests.txt', 'native-v0.5.15-windows-recording-tests.txt')) {
    $floatshotReportSource = Join-Path $floatshotRoot ('.test-artifacts/' + $floatshotReport)
    if (Test-Path -LiteralPath $floatshotReportSource) {
        Copy-Item -LiteralPath $floatshotReportSource -Destination $floatshotPackage -Force
    }
}
$floatshotImageTextTarget = Join-Path $floatshotPackage 'image-text-previews'
foreach ($floatshotScrollReport in @('native-v0.5.21-scroll-tests.txt','native-v0.5.21-scroll-windows-tests.txt')) {
    $floatshotScrollSource = Join-Path $floatshotRoot ('.test-artifacts/' + $floatshotScrollReport)
    if (Test-Path -LiteralPath $floatshotScrollSource) { Copy-Item -LiteralPath $floatshotScrollSource -Destination $floatshotPackage -Force }
}
New-Item -ItemType Directory -Path $floatshotImageTextTarget -Force | Out-Null
foreach ($floatshotPreview in @('image-text-source.png','image-text-partial-selection.png','image-text-all-selection.png','image-text-fullscreen.png','image-text-fullscreen-zoomed.png','recognition-workspace.png')) {
    $floatshotPreviewSource = Join-Path $floatshotRoot ('.test-artifacts/native-v0.5.15-ocr-windows/' + $floatshotPreview)
    if (-not (Test-Path -LiteralPath $floatshotPreviewSource)) {
        $floatshotPreviewSource = Join-Path $floatshotRoot ('.test-artifacts/native-v0.5.15-ocr/' + $floatshotPreview)
    }
    if (Test-Path -LiteralPath $floatshotPreviewSource) { Copy-Item -LiteralPath $floatshotPreviewSource -Destination $floatshotImageTextTarget -Force }
}
foreach ($floatshotOcrReport in @('quality-report.json','cold-process.json','gpu-quality-report.json','dense-timing.json','dense-gpu-timing.json')) {
    $floatshotOcrSource = Join-Path $floatshotRoot ('.test-artifacts/native-v0.5.15-ocr/' + $floatshotOcrReport)
    if (Test-Path -LiteralPath $floatshotOcrSource) { Copy-Item -LiteralPath $floatshotOcrSource -Destination $floatshotPackage -Force }
}
$floatshotMemorySource = Join-Path $floatshotRoot '.test-artifacts/native-v0.5.15-ocr-memory/ocr-memory-report.json'
if (Test-Path -LiteralPath $floatshotMemorySource) { Copy-Item -LiteralPath $floatshotMemorySource -Destination $floatshotPackage -Force }
$floatshotPortableMemorySource = Join-Path $floatshotRoot '.test-artifacts/native-v0.5.15-portable-memory/ocr-memory-report.json'
if (Test-Path -LiteralPath $floatshotPortableMemorySource) { Copy-Item -LiteralPath $floatshotPortableMemorySource -Destination (Join-Path $floatshotPackage 'portable-ocr-memory-report.json') -Force }
$floatshotComparison = Join-Path $floatshotRoot '.test-artifacts/native-v0.5.15-model-comparison/comparison-report.json'
if (Test-Path -LiteralPath $floatshotComparison) { Copy-Item -LiteralPath $floatshotComparison -Destination $floatshotPackage -Force }
$floatshotControlPreview = Join-Path $floatshotRoot '.test-artifacts/native-v0.5.15-recording-ui/region-controls.png'
if (Test-Path -LiteralPath $floatshotControlPreview) {
    Copy-Item -LiteralPath $floatshotControlPreview -Destination (Join-Path $floatshotPackage 'recording-controls-preview.png') -Force
}
$floatshotAnnotationPreviews = Join-Path $floatshotRoot '.test-artifacts/native-v0.5.15-annotation-ui'
foreach ($floatshotRecordingPreview in @('region-controls.png','countdown-controls.png','region-paused.png','fullscreen-controls.png')) {
    $floatshotRecordingSource=Join-Path $floatshotRoot ('.test-artifacts/native-v0.5.15-recording-ui/'+$floatshotRecordingPreview)
    if (Test-Path -LiteralPath $floatshotRecordingSource) { Copy-Item -LiteralPath $floatshotRecordingSource -Destination (Join-Path $floatshotPackage ('recording-'+$floatshotRecordingPreview)) -Force }
}
if (Test-Path -LiteralPath $floatshotAnnotationPreviews) {
    $floatshotAnnotationTarget = Join-Path $floatshotPackage 'annotation-previews'
    New-Item -ItemType Directory -Path $floatshotAnnotationTarget -Force | Out-Null
    Get-ChildItem -LiteralPath $floatshotAnnotationPreviews -File | Copy-Item -Destination $floatshotAnnotationTarget -Force
}
$floatshotForbidden = Get-ChildItem -LiteralPath $floatshotPackage -Recurse -File | Where-Object { $_.Name -match '(?i)python|pyside|shiboken|\.pyd$|Qt6Test|\.pdb$' }
if ($floatshotForbidden) { throw 'Unexpected Python or test runtime found in the release.' }
if (-not $NoZip) {
    $floatshotZip = $floatshotPackage + '-win-x64.zip'
    Compress-Archive -LiteralPath $floatshotPackage -DestinationPath $floatshotZip -CompressionLevel Optimal -Force
    $floatshotZipHash = Get-FileHash -LiteralPath $floatshotZip -Algorithm SHA256
    [IO.File]::WriteAllText(($floatshotZip + '.sha256'), ($floatshotZipHash.Hash + '  ' + [IO.Path]::GetFileName($floatshotZip) + [Environment]::NewLine), [Text.Encoding]::ASCII)
    $floatshotZipHash
}
Write-Output $floatshotPackage

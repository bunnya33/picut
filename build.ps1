[CmdletBinding()]
param([string]$QtDir, [string]$Ffmpeg, [switch]$SkipTests, [switch]$NoZip)
$ErrorActionPreference = 'Stop'
if (-not $QtDir) { $QtDir = Join-Path $PSScriptRoot 'build/native-sdk/qt' }
if (-not $Ffmpeg) { $Ffmpeg = Join-Path $PSScriptRoot 'build/native-sdk/ffmpeg.exe' }
& (Join-Path $PSScriptRoot 'native/build.ps1') -QtDir $QtDir -BuildDir (Join-Path $PSScriptRoot 'build/native-scroll-performance')
if (-not $SkipTests) {
    $floatshotOriginalPath = $env:PATH
    $floatshotOriginalPlatform = $env:QT_QPA_PLATFORM
    $floatshotOriginalPlugins = $env:QT_PLUGIN_PATH
    $floatshotOriginalFfmpeg = $env:FLOATSHOT_FFMPEG
    $floatshotOriginalRecordingArtifacts = $env:FLOATSHOT_RECORDING_TEST_DIR
    $floatshotOriginalOcrArtifacts = $env:FLOATSHOT_OCR_TEST_DIR
    $floatshotOriginalDataDirectory = $env:FLOATSHOT_DATA_DIR
    $floatshotOriginalAnnotationArtifacts = $env:FLOATSHOT_ANNOTATION_TEST_DIR
    $picutOriginalTranslationArtifacts = $env:PICUT_TRANSLATION_TEST_DIR
    $picutOriginalScrollArtifacts = $env:PICUT_SCROLL_V2_TEST_DIR
    try {
        $env:PATH = (Join-Path $QtDir 'bin') + ';' + $env:PATH
        $env:QT_PLUGIN_PATH = Join-Path $QtDir 'plugins'
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:FLOATSHOT_FFMPEG = $Ffmpeg
        $floatshotReportDirectory = Join-Path $PSScriptRoot '.test-artifacts'
        New-Item -ItemType Directory -Path $floatshotReportDirectory -Force | Out-Null
        $env:FLOATSHOT_DATA_DIR = Join-Path $floatshotReportDirectory 'native-v0.5.34-regression-data'
        $env:FLOATSHOT_ANNOTATION_TEST_DIR = Join-Path $floatshotReportDirectory 'native-v0.5.34-annotation-ui'
        $floatshotTestReport = Join-Path $floatshotReportDirectory 'native-v0.5.34-tests.txt'
        $floatshotTests = Start-Process -FilePath (Join-Path $PSScriptRoot 'build/native-scroll-performance/FloatShotNativeTests.exe') -ArgumentList '-o', ('"' + $floatshotTestReport + ',txt"') -WindowStyle Hidden -PassThru -Wait
        Get-Content -LiteralPath $floatshotTestReport -Tail 4
        if ($floatshotTests.ExitCode -ne 0) { throw 'Native regression tests failed.' }
        $env:PICUT_SCROLL_V2_TEST_DIR = Join-Path $floatshotReportDirectory 'native-v0.5.34-scroll-v2'
        $picutScrollReport = Join-Path $floatshotReportDirectory 'native-v0.5.34-scroll-v2-tests.txt'
        $picutScrollTests = Start-Process -FilePath (Join-Path $PSScriptRoot 'build/native-scroll-performance/FloatShotScrollV2Tests.exe') -ArgumentList '-o', ('"' + $picutScrollReport + ',txt"') -WindowStyle Hidden -PassThru -Wait
        Get-Content -LiteralPath $picutScrollReport -Tail 4
        if ($picutScrollTests.ExitCode -ne 0) { throw 'Scroll V2 tests failed.' }
        $env:QT_QPA_PLATFORM = 'windows'
        $env:FLOATSHOT_DATA_DIR = Join-Path $floatshotReportDirectory 'native-v0.5.34-pin-after'
        $floatshotPinTest = Start-Process -FilePath (Join-Path $PSScriptRoot 'build/native-scroll-performance/picut.exe') -ArgumentList '--verify-pin-latency' -WindowStyle Hidden -PassThru -Wait
        if ($floatshotPinTest.ExitCode -ne 0) { throw 'Synthetic F3 latency and persistence checks failed.' }
        $env:FLOATSHOT_DATA_DIR = Join-Path $floatshotReportDirectory 'native-v0.5.34-regression-data'
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:FLOATSHOT_OCR_TEST_DIR = Join-Path $floatshotReportDirectory 'native-v0.5.34-ocr'
        $floatshotOcrReport = Join-Path $floatshotReportDirectory 'native-v0.5.34-ocr-tests.txt'
        $floatshotOcrTests = Start-Process -FilePath (Join-Path $PSScriptRoot 'build/native-scroll-performance/FloatShotOcrTests.exe') -ArgumentList '-o', ('"' + $floatshotOcrReport + ',txt"') -WindowStyle Hidden -PassThru -Wait
        Get-Content -LiteralPath $floatshotOcrReport -Tail 4
        if ($floatshotOcrTests.ExitCode -ne 0) { throw 'Offline OCR tests failed.' }
        $env:PICUT_TRANSLATION_TEST_DIR = Join-Path $floatshotReportDirectory 'native-v0.5.34-translation'
        $picutTranslationReport = Join-Path $floatshotReportDirectory 'native-v0.5.34-translation-tests.txt'
        $picutTranslationTests = Start-Process -FilePath (Join-Path $PSScriptRoot 'build/native-scroll-performance/FloatShotTranslationTests.exe') -ArgumentList '-o', ('"' + $picutTranslationReport + ',txt"') -WindowStyle Hidden -PassThru -Wait
        Get-Content -LiteralPath $picutTranslationReport -Tail 4
        if ($picutTranslationTests.ExitCode -ne 0) { throw 'Local translation tests failed.' }
        $env:QT_QPA_PLATFORM = 'windows'
        $env:FLOATSHOT_RECORDING_TEST_DIR = Join-Path $floatshotReportDirectory 'native-v0.5.34-recording-ui'
        $floatshotRecordingReport = Join-Path $floatshotReportDirectory 'native-v0.5.34-recording-tests.txt'
        Write-Output 'Recording UI checks briefly show test windows on the Windows desktop.'
        $floatshotRecordingTests = Start-Process -FilePath (Join-Path $PSScriptRoot 'build/native-scroll-performance/FloatShotRecordingOverlayTests.exe') -ArgumentList '-o', ('"' + $floatshotRecordingReport + ',txt"') -WindowStyle Hidden -PassThru -Wait
        Get-Content -LiteralPath $floatshotRecordingReport -Tail 4
        if ($floatshotRecordingTests.ExitCode -ne 0) { throw 'Recording UI tests failed.' }
        if (Select-String -LiteralPath $floatshotRecordingReport -Pattern '^SKIP' -Quiet) {
            Write-Warning 'Live desktop capture checks were skipped; see native-v0.5.34-recording-tests.txt. Capture exclusion in actual video remains unverified.'
        }
    } finally {
        $env:PATH = $floatshotOriginalPath
        $env:QT_QPA_PLATFORM = $floatshotOriginalPlatform
        $env:QT_PLUGIN_PATH = $floatshotOriginalPlugins
        $env:FLOATSHOT_FFMPEG = $floatshotOriginalFfmpeg
        $env:FLOATSHOT_RECORDING_TEST_DIR = $floatshotOriginalRecordingArtifacts
        $env:FLOATSHOT_OCR_TEST_DIR = $floatshotOriginalOcrArtifacts
        $env:FLOATSHOT_DATA_DIR = $floatshotOriginalDataDirectory
        $env:FLOATSHOT_ANNOTATION_TEST_DIR = $floatshotOriginalAnnotationArtifacts
        $env:PICUT_TRANSLATION_TEST_DIR = $picutOriginalTranslationArtifacts
        $env:PICUT_SCROLL_V2_TEST_DIR = $picutOriginalScrollArtifacts
    }
}
& (Join-Path $PSScriptRoot 'native/package.ps1') -QtDir $QtDir -Ffmpeg $Ffmpeg -SkipBuild -NoZip:$NoZip

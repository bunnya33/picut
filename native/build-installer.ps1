[CmdletBinding()]
param([string]$PackageDir, [string]$Iscc, [switch]$TranslationTrial, [switch]$ScrollTrial)
$ErrorActionPreference = 'Stop'
if ($TranslationTrial -and $ScrollTrial) { throw 'Choose one installer identity.' }
$picutRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $PackageDir) { $PackageDir = Join-Path $picutRoot 'dist/picut-0.5.29-scroll-trial' }
$PackageDir = (Resolve-Path -LiteralPath $PackageDir).Path
$picutMain = Join-Path $PackageDir 'picut.exe'
$picutVersion = (Get-Item -LiteralPath $picutMain).VersionInfo.ProductVersion
if ($picutVersion -notmatch '^\d+\.\d+\.\d+$') { throw 'picut.exe has no valid three-part product version.' }
if ((Get-Item -LiteralPath $picutMain).VersionInfo.ProductName -like '*翻译试用版*' -and -not $TranslationTrial) { throw 'Pass -TranslationTrial to use the separate installer identity and directory.' }

if ((Get-Item -LiteralPath $picutMain).VersionInfo.ProductName -like '*长截图*试验版*' -and -not $ScrollTrial) { throw 'Pass -ScrollTrial for the separate installer identity.' }

$picutRequired = @(
    'picut.exe', 'picut.ico', 'NativeVideoPreview.dll', 'ffmpeg.exe',
    'FloatShotPaddleOcr.exe', 'onnxruntime.dll', 'qt.conf',
    'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'vcruntime140.dll', 'msvcp140.dll',
    'platforms/qwindows.dll', 'gpu/FloatShotPaddleOcr.exe', 'gpu/onnxruntime.dll',
    'gpu/DirectML.dll', 'ocr/models.json', 'licenses', 'THIRD_PARTY_NOTICES.md', '使用说明.txt', 'diagnose.ps1', 'diagnose.cmd'
)
$picutRequired += @('FloatShotTranslate.exe','ctranslate2.dll','translation/models.json')
foreach ($picutRelative in $picutRequired) {
    if (-not (Test-Path -LiteralPath (Join-Path $PackageDir $picutRelative))) {
        throw "Runtime incomplete: $picutRelative. Run native/package.ps1 first."
    }
}
$picutTranslation = Get-Content -LiteralPath (Join-Path $PackageDir 'translation/models.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($picutDirection in $picutTranslation.models.PSObject.Properties) {
    foreach ($picutHash in $picutDirection.Value.checksums.PSObject.Properties) {
        $picutModel = Join-Path $PackageDir ('translation/' + $picutDirection.Name + '/' + $picutHash.Name)
        if ((Get-FileHash -LiteralPath $picutModel).Hash -ne $picutHash.Value) { throw 'Translation model checksum mismatch.' }
    }
}
$picutModelManifest = Get-Content -LiteralPath (Join-Path $PackageDir 'ocr/models.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($picutChecksum in $picutModelManifest.checksums.PSObject.Properties) {
    $picutModel = Join-Path $PackageDir ('ocr/' + $picutChecksum.Name)
    if ((Get-FileHash -LiteralPath $picutModel -Algorithm SHA256).Hash -ne $picutChecksum.Value) {
        throw "OCR model checksum mismatch: $picutModel"
    }
}

$picutTools = Join-Path $picutRoot 'build/installer-tools'
$picutChinese = Join-Path $picutTools 'ChineseSimplified.isl'
if (-not $Iscc) { $Iscc = Join-Path $picutTools 'inno-6.7.3/ISCC.exe' }
if (-not (Test-Path -LiteralPath $Iscc) -or -not (Test-Path -LiteralPath $picutChinese)) {
    & (Join-Path $PSScriptRoot 'setup-installer-sdk.ps1')
}
if (-not (Test-Path -LiteralPath $Iscc)) { throw "ISCC missing: $Iscc" }
if ((Get-FileHash -LiteralPath $picutChinese -Algorithm SHA256).Hash -ne '7D544B9BB1D142CFA11F2E5D3CC8ABE2E55F8E066C5124E3772675AA236E1278') {
    throw 'Chinese installer messages checksum mismatch.'
}

# Use a fresh staging folder. Keep reports/previews in the portable archive only.
$picutBuildBase = Join-Path $picutRoot 'build/installer'
$picutStage = Join-Path $picutBuildBase ('picut-' + $picutVersion + '-' + [guid]::NewGuid().ToString('N'))
$picutPayload = Join-Path $picutStage 'payload'
New-Item -ItemType Directory -Path $picutPayload -Force | Out-Null
Get-ChildItem -LiteralPath $PackageDir -File | Where-Object {
    $_.Extension -in @('.exe', '.dll', '.ico', '.conf') -or
    $_.Name -in @('THIRD_PARTY_NOTICES.md', '使用说明.txt', 'diagnose.ps1', 'diagnose.cmd', '试验版-v0.5.29-流畅滚动.md', '更新记录-v0.5.26-正式翻译与Shift画笔.md')
} | Copy-Item -Destination $picutPayload
Get-ChildItem -LiteralPath $PackageDir -Directory | Where-Object {
    $_.Name -notin @('annotation-previews', 'image-text-previews', 'translation-previews')
} | Copy-Item -Destination $picutPayload -Recurse

$picutFiles = @(Get-ChildItem -LiteralPath $picutPayload -Recurse -File)
if ($picutFiles | Where-Object { $_.Name -match '(?i)python|pyside|shiboken|\.pyd$|\.pdb$|Qt6Test|Tests\.exe$' }) {
    throw 'Unexpected development or Python runtime files in installer payload.'
}
$picutManifest = @($picutFiles | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path = $_.FullName.Substring($picutPayload.Length + 1).Replace('\', '/')
        size = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
})
$picutEncoding = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path $picutStage 'payload-manifest.json'), ($picutManifest | ConvertTo-Json -Depth 5), $picutEncoding)
$picutOutputDir = Join-Path $picutRoot 'dist'
[string[]]$picutTrialArguments = if ($ScrollTrial) { @('/DScrollTrial=1') } elseif ($TranslationTrial) { @('/DTranslationTrial=1') } else { @() }
& $Iscc @picutTrialArguments ('/DAppVersion=' + $picutVersion) ('/DPayloadDir=' + $picutPayload) ('/DInstallerOutputDir=' + $picutOutputDir) ('/DChineseMessages=' + $picutChinese) (Join-Path $PSScriptRoot 'picut.iss')
if ($LASTEXITCODE -ne 0) { throw "Installer compilation failed: $LASTEXITCODE" }
$picutSuffix = if ($ScrollTrial) { '-scroll-trial-setup-x64.exe' } elseif ($TranslationTrial) { '-translation-trial-setup-x64.exe' } else { '-setup-x64.exe' }
$picutInstaller = Join-Path $picutOutputDir ('picut-' + $picutVersion + $picutSuffix)
$picutInstallerHash = (Get-FileHash -LiteralPath $picutInstaller -Algorithm SHA256).Hash
[IO.File]::WriteAllText(($picutInstaller + '.sha256'), ($picutInstallerHash + '  ' + [IO.Path]::GetFileName($picutInstaller) + "`r`n"), [Text.Encoding]::ASCII)
Write-Output "Installer: $picutInstaller"
Write-Output "SHA256: $picutInstallerHash"
Write-Output "Validation manifest: $(Join-Path $picutStage 'payload-manifest.json')"

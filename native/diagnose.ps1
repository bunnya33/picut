$ErrorActionPreference = 'Stop'
$picutReportRoot = Join-Path $env:TEMP ('picut-diagnostics-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,6))
New-Item -ItemType Directory -Path $picutReportRoot -Force | Out-Null
$picutReport = Join-Path $picutReportRoot 'report.txt'
$picutLines = [Collections.Generic.List[string]]::new()
$picutWindows = Get-ItemProperty 'HKLM:/SOFTWARE/Microsoft/Windows NT/CurrentVersion'
$picutLines.Add("picut compatibility diagnostics: $(Get-Date -Format o)")
$picutSystemName = if ([int]$picutWindows.CurrentBuildNumber -ge 22000) { 'Windows 11' } else { $picutWindows.ProductName }
$picutLines.Add("Windows: $picutSystemName $($picutWindows.DisplayVersion) build $($picutWindows.CurrentBuildNumber).$($picutWindows.UBR)")
$picutLines.Add("OS64: $([Environment]::Is64BitOperatingSystem); Process64: $([Environment]::Is64BitProcess)")
if ([int]$picutWindows.CurrentBuildNumber -lt 17763) { $picutLines.Add('UNSUPPORTED: Qt 6.8 requires Windows 10 1809 (17763) or later.') }
foreach ($picutRelative in @('picut.exe','Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll','platforms/qwindows.dll','ffmpeg.exe','NativeVideoPreview.dll')) {
    $picutFile = Join-Path $PSScriptRoot $picutRelative
    if (Test-Path -LiteralPath $picutFile) { $picutLines.Add("OK $picutRelative $((Get-Item -LiteralPath $picutFile).Length) bytes") }
    else { $picutLines.Add("MISSING $picutRelative") }
}
$picutSavedEnv = @{}
foreach ($picutName in @('FLOATSHOT_DATA_DIR','QT_DEBUG_PLUGINS','QT_QPA_PLATFORM','QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH')) {
    $picutSavedEnv[$picutName] = [Environment]::GetEnvironmentVariable($picutName,'Process')
}
try {
    $env:FLOATSHOT_DATA_DIR = Join-Path $picutReportRoot 'isolated-data'
    $env:QT_DEBUG_PLUGINS = '1'
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_PLUGIN_PATH = ''
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = ''
    $picutChecks = @(
        @{Name='startup'; File='picut.exe'; Args=@('--smoke-test')},
        @{Name='encoder'; File='ffmpeg.exe'; Args=@('-hide_banner','-loglevel','error','-f','lavfi','-i','color=c=black:s=320x240:r=5','-t','1','-c:v','libx264','-pix_fmt','yuv420p',('"'+(Join-Path $picutReportRoot 'synthetic-video.mp4')+'"'))}
    )
    foreach ($picutCheck in $picutChecks) {
        try {
            $picutCheckOut=Join-Path $picutReportRoot ($picutCheck.Name+'-stdout.txt')
            $picutCheckError=Join-Path $picutReportRoot ($picutCheck.Name+'-stderr.txt')
            $picutProcess=New-Object Diagnostics.Process
            $picutProcess.StartInfo.FileName=Join-Path $PSScriptRoot $picutCheck.File
            $picutProcess.StartInfo.Arguments=$picutCheck.Args -join ' '
            $picutProcess.StartInfo.WorkingDirectory=$PSScriptRoot
            $picutProcess.StartInfo.UseShellExecute=$false
            $picutProcess.StartInfo.CreateNoWindow=$true
            $picutProcess.StartInfo.RedirectStandardOutput=$true
            $picutProcess.StartInfo.RedirectStandardError=$true
            [void]$picutProcess.Start()
            $picutOutputTask=$picutProcess.StandardOutput.ReadToEndAsync()
            $picutErrorTask=$picutProcess.StandardError.ReadToEndAsync()
            if (-not $picutProcess.WaitForExit(20000)) {
                Stop-Process -Id $picutProcess.Id -Force -ErrorAction SilentlyContinue
                $picutLines.Add("$($picutCheck.Name): timed out (20 seconds)")
            } else {
                $picutProcess.WaitForExit()
                $picutExit=$picutProcess.ExitCode
                [IO.File]::WriteAllText($picutCheckOut,$picutOutputTask.Result,[Text.Encoding]::UTF8)
                [IO.File]::WriteAllText($picutCheckError,$picutErrorTask.Result,[Text.Encoding]::UTF8)
                $picutLines.Add("$($picutCheck.Name): exit $picutExit")
                if ($picutExit -eq -1073741515) { $picutLines.Add('Loader: missing DLL (0xC0000135).') }
                if ($picutExit -eq -1073741701) { $picutLines.Add('Loader: invalid image / architecture mismatch (0xC000007B).') }
                if ($picutExit -eq -1073741795) { $picutLines.Add('CPU: unsupported instruction (0xC000001D).') }
            }
            $picutProcess.Dispose()
        } catch { $picutLines.Add("$($picutCheck.Name): $($_.Exception.Message)") }
    }
} finally {
    foreach ($picutName in $picutSavedEnv.Keys) { [Environment]::SetEnvironmentVariable($picutName,$picutSavedEnv[$picutName],'Process') }
}
[IO.File]::WriteAllLines($picutReport,$picutLines,(New-Object Text.UTF8Encoding($true)))
Get-Content -LiteralPath $picutReport
Write-Host "诊断报告：$picutReport"

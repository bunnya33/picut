[CmdletBinding()]
param([string]$Browser, [string]$OutputDir)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $OutputDir) { $OutputDir = Join-Path $root '.test-artifacts/native-v0.5.27-browser' }
if (-not $Browser) {
    $Browser = @('C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe') | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $Browser) { throw 'Specify -Browser with an installed Chromium browser executable.' }
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$html = @"
<!doctype html><meta charset="UTF-8"><title>picut browser rendering fixture</title>
<style>*{box-sizing:border-box}html{scroll-behavior:auto}body{margin:0;width:720px;height:100vh;overflow:hidden;background:#f9fafc;color:#242932;font:16px/24px Arial,"Microsoft YaHei",sans-serif}main{height:100vh;overflow:auto}main::-webkit-scrollbar{display:none}section{height:64px;padding:6px 20px;border-bottom:1px solid #e7e9ed}section:nth-child(7n){background:#edf3fa}b{color:#27629a}code{font-family:Consolas;color:#704283}</style>
<main id="content"></main><script>for(let i=0;i<100;i++){let s=document.createElement('section');s.innerHTML='<b>Paragraph '+(i+1)+'</b> — 连续文档与实际浏览器渲染<br><code>capture('+i+'); verify(pixel_'+(i*79+13)+');</code> Keep original text and coordinates.';content.append(s)}content.scrollTop=Number(location.hash.slice(1)||0)</script>
"@
$pagePath = Join-Path $OutputDir 'fixture.html'
[IO.File]::WriteAllText($pagePath, $html, [Text.UTF8Encoding]::new($false))
$page = [Uri]::new($pagePath).AbsoluteUri
foreach ($offset in @(0, 173, 487, 909, 1331, 1753, -1)) {
    $size = if ($offset -lt 0) { '720,6400' } else { '720,720' }
    $name = if ($offset -lt 0) { 'reference' } else { 'frame-' + $offset }
    $fragment = if ($offset -lt 0) { '' } else { '#' + $offset }
    $arguments = @('--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check', '--hide-scrollbars', '--force-device-scale-factor=1', ('--window-size=' + $size), '--virtual-time-budget=1000', ('--user-data-dir="' + $OutputDir + '/profile"'), ('--screenshot="' + $OutputDir + '/' + $name + '.png"'), ('"' + $page + $fragment + '"'))
    $process = Start-Process -FilePath $Browser -ArgumentList $arguments -WindowStyle Hidden -PassThru -Wait -RedirectStandardError (Join-Path $OutputDir ($name + '.log'))
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $OutputDir ($name + '.png')))) { throw 'Browser rendering failed.' }
}
Write-Output "Set PICUT_SCROLL_V2_BROWSER_DIR=$OutputDir and run FloatShotScrollV2Tests browserRenderedFrames."
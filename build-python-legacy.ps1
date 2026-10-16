$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$pythonPath = Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $pythonPath)) { throw '请先运行 run-python-legacy.ps1 安装开发环境。' }
& $pythonPath -m pip install -e '.[dev]'
if ($LASTEXITCODE -ne 0) { throw '依赖安装失败。' }
& $pythonPath tools/make_icon.py
& $pythonPath tools/collect_licenses.py
& $pythonPath tools/render_feature_guide.py
if ($LASTEXITCODE -ne 0) { throw '功能文档生成失败。' }
& $pythonPath -m pytest -q
if ($LASTEXITCODE -ne 0) { throw '验证未通过，已停止打包。' }
& $pythonPath -m PyInstaller --noconfirm FloatShot.spec
if ($LASTEXITCODE -ne 0) { throw '打包失败。' }
$version = & $pythonPath -c 'import floatshot; print(floatshot.__version__)'
$packageDirectory = Join-Path $PSScriptRoot "dist\FloatShot-$version"
$archivePath = Join-Path $PSScriptRoot "dist\FloatShot-$version-win-x64.zip"
Copy-Item -LiteralPath 'README.md','THIRD_PARTY_NOTICES.md' -Destination $packageDirectory
Copy-Item -LiteralPath 'docs' -Destination $packageDirectory -Recurse -Force
Compress-Archive -LiteralPath $packageDirectory -DestinationPath $archivePath -Force
Write-Output "完成：$packageDirectory\FloatShot.exe 与 $archivePath"

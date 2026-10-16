from pathlib import Path
import tomllib
import os
import sys

from PyInstaller.utils.hooks import collect_data_files, collect_submodules, collect_dynamic_libs

# Only resolve DLLs from this Python installation and Windows. Other programs'
# PATH entries can supply an incompatible ICU DLL with the same filename.
windows_directory = Path(os.environ.get('SystemRoot', 'C:/Windows'))
os.environ['PATH'] = os.pathsep.join(str(path) for path in [
    Path(sys.prefix) / 'Scripts', Path(sys.base_prefix), Path(sys.base_prefix) / 'DLLs',
    windows_directory / 'System32', windows_directory])

version = tomllib.loads(Path(SPECPATH, 'pyproject.toml').read_text(encoding='utf-8'))['project']['version']
datas = collect_data_files('soundcard') + collect_data_files('imageio_ffmpeg')
datas += [('THIRD_PARTY_NOTICES.md', '.'), ('licenses', 'licenses')]

a = Analysis(
    ['run.py'],
    pathex=[SPECPATH],
    binaries=collect_dynamic_libs('winrt'),
    datas=datas,
    hiddenimports=['_cffi_backend', 'soundcard.mediafoundation', 'zxingcpp',
                   'winrt.windows.media.ocr', 'winrt.windows.graphics.imaging', 'winrt.windows.storage',
                   'winrt.windows.storage.streams', 'winrt.windows.globalization',
                   'winrt.windows.foundation', 'winrt.windows.foundation.collections',
                   'comtypes.gen.UIAutomationClient'] + collect_submodules('winrt') + collect_submodules('comtypes.gen'),
    hookspath=[],
    excludes=['tkinter', 'pytest', 'IPython', 'PySide6.QtWebEngineCore', 'PySide6.QtWebEngineWidgets'],
    noarchive=False,
)
# Windows 10/11 provides these system DLLs. Never ship PATH-sourced substitutes.
a.binaries = [entry for entry in a.binaries
              if not Path(entry[0]).name.lower().startswith(('api-ms-win-', 'ext-ms-win-'))
              and Path(entry[0]).name.lower() not in ('ucrtbase.dll', 'icuuc.dll', 'icuin.dll')]
pyz = PYZ(a.pure)
exe = EXE(
    pyz, a.scripts, [], exclude_binaries=True, name='FloatShot',
    debug=False, bootloader_ignore_signals=False, strip=False, upx=False,
    console=False, icon='assets/floatshot.ico',
)
coll = COLLECT(exe, a.binaries, a.datas, strip=False, upx=False, name=f'FloatShot-{version}')

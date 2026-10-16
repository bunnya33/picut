"""Retain the license notices shipped in the installed Windows wheels."""
import shutil
import subprocess
from importlib import metadata
from pathlib import Path

import imageio_ffmpeg

root = Path("licenses")
root.mkdir(exist_ok=True)
packages = ["PySide6", "PySide6_Essentials", "PySide6_Addons", "shiboken6", "mss", "SoundCard",
            "numpy", "Pillow", "cffi", "pycparser", "imageio-ffmpeg", "pyinstaller"]
packages += ["zxing-cpp", "winrt-runtime", "typing_extensions", "winrt-Windows.Media.Ocr",
             "winrt-Windows.Graphics.Imaging", "winrt-Windows.Storage", "winrt-Windows.Storage.Streams",
             "winrt-Windows.Globalization", "winrt-Windows.Foundation", "winrt-Windows.Foundation.Collections"]
packages.append("comtypes")
index = []
for name in packages:
    dist = metadata.distribution(name)
    count = 0
    for filename in dist.files or []:
        lower = str(filename).lower()
        base = Path(str(filename)).name.lower()
        if "/licenses/" in lower or base.startswith(("license", "copying", "notice")):
            source = Path(dist.locate_file(filename))
            if source.is_file() and source.suffix.lower() not in (".py", ".pyc", ".dll", ".pyd"):
                target = root / name / str(filename)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
                count += 1
    index.append(f"{name} {dist.version}: {count} license files")
executable = imageio_ffmpeg.get_ffmpeg_exe()
for option, filename in (("-L", "FFmpeg-license.txt"), ("-version", "FFmpeg-build.txt")):
    result = subprocess.run([executable, option], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    (root / filename).write_bytes(result.stdout)
(root / "INDEX.txt").write_text("\n".join(index)+"\n", encoding="utf-8")
print("\n".join(index))

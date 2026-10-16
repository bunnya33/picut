# Third-party components

FloatShot v0.3.0 uses dynamically packaged third-party libraries. Their licenses remain applicable to those components. The application's source is retained in this workspace.

| Component | Version in this build | License / source |
| --- | --- | --- |
| Qt / PySide6 / Shiboken6 | 6.10.3 | LGPL-3.0 / GPL-3.0 or commercial; https://www.qt.io/licensing/ and https://code.qt.io/cgit/pyside/pyside-setup.git/ |
| Qt source | 6.10.3 | https://download.qt.io/archive/qt/6.10/6.10.3/ |
| mss | 10.2.0 | MIT; https://github.com/BoboTiG/python-mss |
| SoundCard | 0.4.6 | BSD-3-Clause; https://github.com/bastibe/SoundCard |
| NumPy | 2.4.6 | BSD-3-Clause with bundled-library notices; https://numpy.org/ |
| Pillow | 12.3.0 | MIT-CMU and bundled codec licenses; https://python-pillow.github.io/ |
| CFFI / pycparser | 2.1.1 / 3.0 | MIT / BSD-3-Clause; https://cffi.readthedocs.io/ and https://github.com/eliben/pycparser |
| imageio-ffmpeg | 0.6.0 | BSD-2-Clause wrapper; https://github.com/imageio/imageio-ffmpeg |
| Bundled FFmpeg / x264 | FFmpeg 7.1 | GPL-enabled build; https://ffmpeg.org/ and https://www.videolan.org/developers/x264.html |
| PyInstaller runtime | 6.22.3 | GPL-2.0 with bootloader exception; https://pyinstaller.org/ |
| ZXing-C++ | 2.3.0 | Apache-2.0; https://github.com/zxing-cpp/zxing-cpp/tree/v2.3.0 |
| comtypes | 1.4.17 | MIT; https://github.com/enthought/comtypes |
| PyWinRT runtime / Windows projections | 3.2.1 | MIT; https://github.com/pywinrt/pywinrt |
| typing_extensions | 4.16.0 | PSF-2.0; https://github.com/python/typing_extensions |

License texts collected from the installed distributions are included under `licenses/` in the source workspace and `_internal/licenses/` in the portable package. The exact FFmpeg build configuration and license report are included there as well. This FFmpeg executable is the Gyan 7.1 essentials build; its build and source information is at https://www.gyan.dev/ffmpeg/builds/ . Imageio's binary packaging is at https://github.com/imageio/imageio-binaries .

Qt libraries are kept as separate files in the portable package's `_internal` folder. Fonts are loaded from the user's Windows installation and are not redistributed. Qt multimedia may also include FFmpeg libraries and their corresponding Qt distribution license notices.

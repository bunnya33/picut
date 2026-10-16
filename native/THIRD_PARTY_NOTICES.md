# FloatShot Native v0.5.4 third-party notices

This release uses native C++ and dynamically linked Qt. Python, PySide6, Shiboken, NumPy, and PyInstaller are not part of this release.

| Component | Version | License and source |
| --- | --- | --- |
| Qt Core, Gui, Widgets, Network, Multimedia, MultimediaWidgets, ImageFormats | 6.8.3 | LGPL-3.0 / GPL-3.0 or commercial. https://download.qt.io/archive/qt/6.8/6.8.3/submodules/ |
| Qt's FFmpeg playback libraries | 7.1 | LGPL-2.1-or-later and individual bundled-component licenses. https://ffmpeg.org/releases/ |
| ZXing-C++, statically linked | 2.3.0 | Apache-2.0. https://github.com/zxing-cpp/zxing-cpp/tree/v2.3.0 |
| Separate recording FFmpeg / x264 executable | 7.1, Gyan essentials | GPL-enabled build. https://www.gyan.dev/ffmpeg/builds/ and https://github.com/imageio/imageio-binaries/tree/master/ffmpeg |
| Microsoft Visual C++ runtime | 14.x | Microsoft Visual Studio redistributable license. https://visualstudio.microsoft.com/license-terms/ |
| Windows SDK / C++/WinRT APIs | Windows SDK | System APIs; no Python projections included. https://github.com/microsoft/cppwinrt |
| ONNX Runtime CPU, dynamically linked in OCR worker only | 1.22.0 | MIT and bundled notices. https://github.com/microsoft/onnxruntime/tree/v1.22.0 |
| ONNX Runtime DirectML, separate GPU OCR worker runtime | 1.22.0 | MIT and bundled notices. https://www.nuget.org/packages/Microsoft.ML.OnnxRuntime.DirectML/1.22.0 |
| DirectML, loaded by GPU OCR worker only | 1.15.4 | Microsoft DirectML runtime license; LICENSE-CODE applies to accompanying code. https://www.nuget.org/packages/Microsoft.AI.DirectML/1.15.4 |
| OpenCV core / imgproc, statically linked in OCR worker only | 4.11.0 | Apache-2.0. https://github.com/opencv/opencv/tree/4.11.0 |
| PP-OCRv6 small detector / recognizer and PP-LCNet x1.0 text-line orientation | PaddleOCR models converted by RapidAI/RapidOCR v3.10.0 | Apache-2.0. https://github.com/PaddlePaddle/PaddleOCR and https://www.modelscope.cn/models/RapidAI/RapidOCR/files |

License texts, Qt bundled-component notices, and recording FFmpeg build configuration are in `licenses/`. Qt DLLs and plugins remain separate and can be replaced with compatible builds. Application C++ sources and CMake/PowerShell build scripts are in `native/` in the accompanying workspace. Qt source archives contain the corresponding module sources and additional third-party notices. System fonts are not redistributed.

Qt Multimedia is loaded only when opening a video preview. The recording FFmpeg executable runs only during recording and final audio muxing; it is not an idle background process.

PaddleOCR runs locally in the separate C++ executable `FloatShotPaddleOcr.exe`. No server connection or Python interpreter is required. The optional GPU path uses the runtimes under `gpu/`; CPU inference remains available without a compatible GPU. DirectML runtime/code licenses and third-party notices, plus ONNX Runtime DirectML notices, are included under `licenses/`. All original-precision model files are bundled under `ocr/`; their fixed source paths and SHA-256 checksums are recorded in `ocr/models.json` and `native/setup-ocr-sdk.ps1`. This release uses PP-OCRv6 small detection/recognition with the PP-LCNet x1.0 server orientation classifier. Weights are not quantized or edited. Medium/tiny/v5 experiment weights are not bundled. The official Small main model supports 50 languages, not every writing system. ONNX Runtime telemetry events are disabled by the worker. OpenCV codec/video/GUI modules are not linked; Qt decodes input images.

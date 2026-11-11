# picut Native v0.5.37 third-party notices

This release uses native C++ and dynamically linked Qt. Python, PySide6, Shiboken, NumPy, and PyInstaller are not part of this release.

The optional translation trial includes model weights published by Helsinki-NLP / OPUS-MT, Language Technology Research Group at the University of Helsinki. English-to-Chinese uses Apache-2.0; Chinese-to-English uses CC-BY-4.0 (https://creativecommons.org/licenses/by/4.0/). Original model cards, attribution, fixed upstream revisions and checksums accompany the language packs. The weights were converted to CTranslate2 INT8; tokenizers were retained. These are modified distributions, with no endorsement by the original authors implied. License texts and dependency notices are under `licenses/translation/`. CTranslate2's bundled thread-pool library is by Barak Shoshany: https://github.com/bshoshany/thread-pool.

`FloatShotTranslate.exe` starts only after OCR, uses CPU inference, and exits on completion or cancellation. It makes no network requests and does not require a Python interpreter, CUDA or a discrete GPU. Build-only conversion tools and original PyTorch weights are not included in the release.

CTranslate2 4.6.0 is locally modified by `native/translation-sdk/windows-thread-cleanup.cmake`: its non-OpenMP CPU pool and Ruy context are explicitly destroyed before worker threads return, and on the caller after joining them. This avoids joining their internal threads during Windows DLL TLS teardown under the loader lock. The changes preserve normal RAII process exit. MSVC compiles the SDK sources as UTF-8.

| Component | Version | License and source |
| --- | --- | --- |
| CTranslate2, dynamically linked only in translation worker | 4.6.0 | MIT. https://github.com/OpenNMT/CTranslate2/tree/v4.6.0 |
| Ruy / cpu_features / cpuinfo / spdlog | Fixed commits in native/setup-translation-sdk.ps1 | Apache-2.0 / BSD-2-Clause / MIT; original texts in licenses/translation/ |
| SentencePiece, native static inference library | 0.2.0 | Apache-2.0 and bundled third-party notices. https://github.com/google/sentencepiece/tree/v0.2.0 |
| OPUS-MT English to Chinese | Helsinki-NLP/opus-mt-en-zh, revision 408d9bc410a388e1d9aef112a2daba955b945255 | Apache-2.0. https://huggingface.co/Helsinki-NLP/opus-mt-en-zh |
| OPUS-MT Chinese to English | Helsinki-NLP/opus-mt-zh-en, revision cf109095479db38d6df799875e34039d4938aaa6 | CC-BY-4.0. Helsinki-NLP / OPUS-MT / Language Technology Research Group at the University of Helsinki. https://huggingface.co/Helsinki-NLP/opus-mt-zh-en |
| Qt Core, Gui, Widgets, Network, Multimedia, MultimediaWidgets, ImageFormats | 6.8.3 | LGPL-3.0 / GPL-3.0 or commercial. https://download.qt.io/archive/qt/6.8/6.8.3/submodules/ |
| Qt's FFmpeg playback libraries | 7.1 | LGPL-2.1-or-later and individual bundled-component licenses. https://ffmpeg.org/releases/ |
| ZXing-C++, statically linked | 2.3.0 | Apache-2.0. https://github.com/zxing-cpp/zxing-cpp/tree/v2.3.0 |
| Separate recording FFmpeg / x264 executable | 7.1, Gyan essentials | GPL-enabled build. https://www.gyan.dev/ffmpeg/builds/ and https://github.com/imageio/imageio-binaries/tree/master/ffmpeg |
| Microsoft Visual C++ runtime | 14.x | Microsoft Visual Studio redistributable license. https://visualstudio.microsoft.com/license-terms/ |
| Windows SDK / C++/WinRT APIs | Windows SDK | System APIs; no Python projections included. https://github.com/microsoft/cppwinrt |
| ONNX Runtime CPU, dynamically linked in OCR worker only | 1.22.0 | MIT and bundled notices. https://github.com/microsoft/onnxruntime/tree/v1.22.0 |
| ONNX Runtime DirectML, separate GPU OCR worker runtime | 1.22.0 | MIT and bundled notices. https://www.nuget.org/packages/Microsoft.ML.OnnxRuntime.DirectML/1.22.0 |
| DirectML, loaded by GPU OCR worker only | 1.15.4 | Microsoft DirectML runtime license; LICENSE-CODE applies to accompanying code. https://www.nuget.org/packages/Microsoft.AI.DirectML/1.15.4 |
| OpenCV core / imgproc in OCR worker; core / imgproc / video in the scrolling capture application, statically linked | 4.11.0 | Apache-2.0. https://github.com/opencv/opencv/tree/4.11.0 |
| zlib, OpenCV's static dependency | 1.3.1 | zlib license. Copyright Jean-loup Gailly and Mark Adler. https://zlib.net/ |
| SoftFloat routines in OpenCV core | Bundled in OpenCV 4.11.0 | BSD-3-Clause. John R. Hauser / The Regents of the University of California. License text: licenses/OpenCV-SoftFloat-BSD.txt |
| PP-OCRv6 small detector / recognizer and PP-LCNet x1.0 text-line orientation | PaddleOCR models converted by RapidAI/RapidOCR v3.10.0 | Apache-2.0. https://github.com/PaddlePaddle/PaddleOCR and https://www.modelscope.cn/models/RapidAI/RapidOCR/files |

License texts, Qt bundled-component notices, and recording FFmpeg build configuration are in `licenses/`. Qt DLLs and plugins remain separate and can be replaced with compatible builds. Application C++ sources and CMake/PowerShell build scripts are in `native/` in the accompanying workspace. Qt source archives contain the corresponding module sources and additional third-party notices. System fonts are not redistributed.

Qt Multimedia is loaded only when opening a video preview. The recording FFmpeg executable runs only during recording and final audio muxing; it is not an idle background process.

PaddleOCR runs locally in the separate C++ executable `FloatShotPaddleOcr.exe`. No server connection or Python interpreter is required. The optional GPU path uses the runtimes under `gpu/`; CPU inference remains available without a compatible GPU. DirectML runtime/code licenses and third-party notices, plus ONNX Runtime DirectML notices, are included under `licenses/`. All original-precision model files are bundled under `ocr/`; their fixed source paths and SHA-256 checksums are recorded in `ocr/models.json` and `native/setup-ocr-sdk.ps1`. This release uses PP-OCRv6 small detection/recognition with the PP-LCNet x1.0 server orientation classifier. Weights are not quantized or edited. Medium/tiny/v5 experiment weights are not bundled. The official Small main model supports 50 languages, not every writing system. ONNX Runtime telemetry events are disabled by the worker. The OCR worker does not link OpenCV video, codecs or GUI modules; Qt decodes input images.

Scrolling capture uses OpenCV's sparse pyramidal Lucas–Kanade optical flow locally on CPU. The application links only core, imgproc and video, without opencv_world, DNN models, OpenCL, IPP or a Python runtime. Tracking resources are created when a scrolling capture session starts. OpenCV's video module supplies motion tracking, not recording or video decoding. Recording continues to use the separate FFmpeg process.

# picut v0.5.15

Select a screenshot region with F1, then click Scroll Capture next to OCR. The capture hole passes mouse input to the original application. Manual scrolling and optional automatic downward wheel input append verified overlapping pixels; boundary checks reject interior scrolling surrounded by static content. A fixed sidebar is allowed when scrolling columns reach both vertical edges; full-width strips preserve the sidebar, including its repetitions. A complete side preview scales both dimensions to fit the screen as the image grows. Original strips are stored in a temporary directory, with small thumbnails in memory; final output preserves captured pixels. See docs/更新记录-v0.5.15-滚动截屏.md.

Shift+F1 selects a recording region. Settings are optional for region capture, persisted locally, and summarized beside the selection. Fullscreen recording still opens settings. Countdown and recording share an excluded boundary and compact controls. Numbered circles have no adjustment handles; Tab adds an inline underlined label, committed as part of the same mark. See docs/更新记录-v0.5.13-picut录屏与序号.md.

Hotkeys are recorded with QKeySequenceEdit (one chord), with global hooks suspended while settings are open and duplicate/native-key validation before saving. Panels use a fixed light palette. Default and repeated launches stay in the tray; --show-home explicitly opens the workbench. See docs/更新记录-v0.5.14-热键浅色与兼容.md. Bundled diagnose.cmd runs isolated startup and synthetic encoding checks.

The OCR panel contains only a read-only text box and a bottom Cancel button. Titles, gray hints, select-all/close/zoom/copy buttons have been removed; keyboard shortcuts and bidirectional selection remain. Cancel stops active recognition.

## Synchronized OCR sidebar

F4, the toolbar and text recognition menus open one frameless workspace with the original image and a read-only text sidebar. Image and text share UTF-16 selection offsets; both retain highlights across focus changes. Selecting in the image scrolls the text to the caret; selecting in the sidebar pans an enlarged image to the corresponding glyph. Partial and multiline selection, Ctrl+C, Ctrl+A and Esc/F4 work from both sides. The old OCR text dialog has been replaced; QR/barcode results keep their separate dialog.

Adjacent layouts preserve the image's global rectangle and put the panel on a fitting side. Full-screen, tall and overflowing regions use a split workspace within one screen's available geometry, with a draggable divider, wheel zoom, space/middle-button pan, fit/100% and collapse actions in the image context menu. A view transform maps all interaction back into the immutable original image; parent capture/pin geometry and pixels stay intact. Completed OCR results are cached per surface until pixels/profile/acceleration change. Recognition always receives the full-resolution original and uses PaddleOCR Small. Windows OCR remains in benchmark/helper code rather than a selectable sidebar backend.

The data directory remains `%LOCALAPPDATA%/FloatShot/FloatShotNative-ImageTextTrial`, preserving trial settings and history without migrating stable data. Packages are in `dist/picut-0.5.15*`; both `run.ps1` and `run-image-text-trial.ps1` start this version. Earlier packages remain available for rollback. Git `main` and tag `v0.5.10` retain the stable source baseline, and tags `v0.5.11-image-text-trial` and `v0.5.12-ocr-sidebar` preserve the prior OCR interfaces. See `docs/版本回滚.md`.

Windows C++20 / Qt 6.8.3 Widgets application. No Python interpreter, bindings, build scripts, or runtime are required. Qt Multimedia is an optional DLL loaded on first video preview. Recording uses a separately deployed FFmpeg executable; audio capture uses Windows WASAPI.

## Build

Install Visual Studio 2022 or newer with C++ desktop tools and a Windows 10/11 SDK, plus CMake 3.24 or newer. Run from the repository root in PowerShell:

```powershell
./native/setup-sdk.ps1
./native/build.ps1
./native/package.ps1 -SkipBuild
```

`setup-sdk.ps1` fetches the official Qt MSVC x64 SDK, pinned ZXing-C++ source, and standalone FFmpeg, verifies SHA-256 hashes, and uses Windows tar / Expand-Archive. It does not install anything globally. An existing Qt installation can be used with `build.ps1 -QtDir C:/Qt/6.8.3/msvc2022_64`. `build-native.cmd` is a convenience wrapper. Packaging supports `-QtDir`, `-Ffmpeg`, and `-NoZip`.

Executables are built in `build/native/`. The complete runtime folder and ZIP are in `dist/picut-0.5.15*`. Do not distribute the EXE alone. Release packaging rejects Python, PySide, Shiboken, `.pyd`, PDB, and QtTest files.

To build the Windows installer from this deployed runtime, run `./native/build-installer.ps1`. It prepares a local portable Inno Setup 6.7.3 compiler on first use, verifying the official bootstrap's SHA-256 and Authenticode signature and pinning the Chinese translation to the same source tag. No global compiler installation is required. Options `-PackageDir` and `-Iscc` accept an existing runtime/compiler. The app version comes from `picut.exe` metadata. The output is `dist/picut-<version>-setup-x64.exe` plus its SHA-256 file; payload hashes are saved under `build/installer/` for verification.

The installer targets Windows 10 1809+ x64-compatible systems, runs without elevation, defaults to `%LOCALAPPDATA%/Programs/picut`, and supports Chinese and English. It includes deployed runtimes, recording components, offline OCR models, usage notes and licenses, while leaving development reports/previews in the portable archive. It adds Start menu shortcuts and an optional desktop shortcut, registers normal Windows uninstallation, and retains settings/history. Silent setup never launches the app. The stable AppId in `picut.iss` supports future upgrades; exit the existing app from the tray before upgrading.

v0.5.10 makes the width/dot button a direct stroke/outline selector for tools that have fill/area modes. Left-click switches back without a popup; repeated selection is a no-op. Right-click opens the size slider while preserving the current mode. Screenshot and pin editing share this behavior. Pen, arrow and number tools keep their size-button behavior.

v0.5.9 replaces nontext option ComboBoxes with compact icon buttons (arrow styles use an icon menu). Shape fill is rendered, hit-tested and persisted. Area buttons toggle back to stroke mode when clicked again. Numbered circles are the eighth toolbar tool (Ctrl+8), with size/color options, per-image numbering and undo/redo support. Text retains its native font/point-size controls.

`setup-sdk.ps1` also prepares ONNX Runtime 1.22.0, static OpenCV 4.11 core/imgproc, PP-OCRv6 small detector/recognizer and the PP-LCNet x1.0 server orientation classifier. Run `setup-ocr-sdk.ps1` separately to add these to an existing SDK. Small weights live in `build/native-sdk/paddleocr-small/`; the older medium SDK and v0.5.3 release are retained separately for comparison. All models are checksum verified and retain original precision. No Python is used for setup, building or inference.

`FloatShotPaddleOcr.exe` is an isolated C++ worker. The root worker uses CPU ONNX Runtime; `gpu/` contains the same worker with ONNX Runtime DirectML 1.22.0 and DirectML 1.15.4. Automatic mode uses GPU for images at least 200 px in each dimension and 100,000 pixels total, otherwise CPU; an unavailable or failing GPU automatically retries on CPU. No CUDA is required. The worker selects a DirectX 12 hardware adapter, preferring dedicated memory, and processes similar-width text lines in batches of four on GPU. Orientation remains on CPU in batches of 16; CPU recognition stays single-line after benchmarking. Both use identical full-precision models.

The GUI starts the worker with a temporary PNG and receives newline JSON progress on stdout and the result through an output file; completion, cancellation and timeout remove the temporary directory and terminate model residency. The main EXE does not link OpenCV, ONNX Runtime or DirectML. GPU DLLs stay separate from CPU DLLs; the child working directory is the package root to locate Qt without SDK PATH entries. Windows OCR is retained for benchmark comparison. The Small main model supports the official 50-language range: Chinese simplified/traditional, English, Japanese and Latin-script languages. Korean, Arabic, Cyrillic, Thai and Indic-script recognizers are not included. See `docs/更新记录-v0.5.4.md` for current benchmarks, language limitations and comparison with v0.5.3 medium.

`FloatShotOcrTests.exe` uses identical synthetic images for Windows/Paddle comparison and verifies cancellation, cleanup, Unicode paths, transparency, orientation, large-image tile boundaries and the result dialog. It also compares CPU/GPU output on 120 text lines, tests GPU accuracy/cancellation, and independently tests actual backend failure and automatic CPU fallback. Only explicitly unsupported GPU hardware causes GPU cases to skip; deployment errors fail. Set `FLOATSHOT_OCR_TEST_DIR` to save images/reports. The root build runs these tests. `picut.exe --verify-ocr-memory` measures three isolated OCR runs using synthetic text; set `FLOATSHOT_DATA_DIR` to a test folder first.

## Checks

```powershell
$env:PATH = "$PWD/build/native-sdk/qt/bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$PWD/build/native-sdk/qt/plugins"
$env:FLOATSHOT_FFMPEG = "$PWD/build/native-sdk/ffmpeg.exe"
$env:QT_QPA_PLATFORM = 'offscreen'
./build/native/FloatShotNativeTests.exe
```

The tests cover native OCR and QR recognition, mark editing, live selection changes, original-position pins, save cancellation, settings/data migration, recording pause/restart, video decode, and WASAPI loopback audio.

The root `build.ps1` also runs `FloatShotRecordingOverlayTests.exe` with `QT_QPA_PLATFORM=windows`. It checks controller placement, pause/time/hotkey synchronization, stopping/failure cleanup, dragging, tiny regions, and both windows' `WDA_EXCLUDEFROMCAPTURE` flags. It briefly shows test windows. Live capture tests only record a solid-color fixture after confirming that GDI can see it. They decode the MP4 to check that the border and controls leave the underlying pixels visible. An inactive desktop/compositor is reported as a skip, never as successful exclusion. Set `FLOATSHOT_RECORDING_TEST_DIR` to save generated previews and JSON reports. Previews rendered from widgets and synthetic video sources do not establish real capture exclusion.

The v0.5.1 recording frame is transparent to mouse input. The controller stays inside the recorded region when it fits; for very small regions it stays on the recording monitor. Both are allocated only during recording, and failure to set capture exclusion uses exterior-only borders and controls outside the recording region; fullscreen hides controls if no other screen has space. Windows 10 1809+ is required; in-region capture exclusion requires 2004+.

The application also supports `--verify-features`, `--verify-memory` (synthetic), `--verify-desktop-memory` (current desktop), `--verify-ui`, and `--smoke-test`. Always set `FLOATSHOT_DATA_DIR` to a new test folder. Desktop memory checks do not save screen pixels and do not alter the clipboard. Memory results describe Windows working set and private commit, not a guaranteed minimum or the Task Manager default column.

See `docs/原生迁移-v0.5.0.md` for product scope and measured results. The original Python source and releases are retained separately for comparison and recovery; the native build does not use them.

F3 image/history persistence uses a single lazy `Store` IO queue; normal shutdown flushes jobs and GUI completions. Hidden workbenches defer history refresh, thumbnails are bounded to 128 entries, and pin layout changes do not re-encode unchanged images. `--verify-pin-latency` uses ninety synthetic records and holds IO until first paint to verify that display does not await disk writes. Always set `FLOATSHOT_DATA_DIR` to an isolated test directory before running it. See `docs/更新记录-v0.5.5.md`.

v0.5.6 fixes freehand samples in place and commits each completed pen stroke. Width input is locked while drawing a pen stroke and adjusts only future strokes afterward. Marker supports a two-endpoint line and a rectangle; line width and endpoints remain editable until confirmation. Menus show shortcut hints in a separate column, and tray hints read the configured hotkeys without registering duplicate shortcuts. Set FLOATSHOT_ANNOTATION_TEST_DIR for synthetic widget previews from the annotation/menu regression tests. See docs/更新记录-v0.5.6.md.

v0.5.7 updates screenshot and recording selection toolbar placement on each drag/resize event. Shared placement keeps the controls beside the selection and within the active screen. The annotation toolbar embeds glyph masks from the user reference and matches its 428x34 layout, gray background, blue border and 16 px icons. Regression checks compare rendered pixels with the bundled reference and assert positions before mouse release. See docs/更新记录-v0.5.7.md.

v0.5.8 adds transparent inline text editing in the capture/pin surface, with corner scaling, rotation, edge dragging and deletion. The text toolbar follows the supplied reference: bold, italic, outline, font, fractional point size, custom color and palette. A shared QTextDocument format keeps editing and exports aligned; pending text commits before F3 or export. See docs/更新记录-v0.5.8.md.

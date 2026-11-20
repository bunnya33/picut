# picut for macOS

双击 `.dmg`，将 picut 拖到「应用程序」，再从「应用程序」打开。之后可以从菜单栏的青蛙图标进入截图、贴图、录屏和设置。发行包适用于 Apple 芯片 Mac，系统要求 macOS 15 或更新版本。

- **截图和标注**。按 `F1` 框选，`Enter` 复制，`⌘⌥⇧P` 在原位置贴图，`F4` 识别文字。在功能键兼作亮度等系统控制键时，使用 `Fn + F1` 等组合。快捷键可在设置中修改。
- **滚动截屏**。框选单块显示器上的滚动内容，点工具栏的滚动截屏按钮，再手动滚动页面。把固定栏和滚动条留在框外。
- **录屏**。从截图选区切换到录屏工具栏，可录制区域或窗口。整屏录制也可从菜单栏进入。默认录制为 MP4，可在录屏设置中打开系统声音和麦克风。
- **文字识别和翻译**。使用 Apple Vision 离线识别，支持原图选字和双栏复制。应用内置中英文翻译模型，可直接离线翻译图片。

首次截图或录屏时，在「系统设置 → 隐私与安全性 → 屏幕与系统音频录制」允许 picut 后重新打开。开启麦克风录制时，macOS 会另外请求麦克风权限。

系统会将本地构建标识为临时签名。若打开从 ZIP 下载的副本时出现开发者验证提示，可在确认来源后通过「系统设置 → 隐私与安全性」中的「仍要打开」批准本次启动。

## 从源码构建

需要 macOS、CMake 3.24 或更新版本、Qt 6.8 或更新版本、OpenCV 4 的 core、imgproc 和 video 模块，以及带 macOS 15 或更新 SDK 的 Xcode 或命令行工具。构建时会下载 ZXing-C++、CTranslate2、SentencePiece 及其依赖。可用环境变量 `QT_PREFIX` 和 `OPENCV_PREFIX` 指定依赖目录。

首次打包先用以下命令准备模型。可通过 `PICUT_TRANSLATION_MODELS` 指定已转换的模型目录。

```sh
python3.11 -m venv build/translation-tools
build/translation-tools/bin/pip install 'ctranslate2==4.6.0' 'torch>=2.6,<2.10' 'transformers>=4.48,<5' 'sentencepiece==0.2.0' sacremoses
build/translation-tools/bin/python tools/convert_translation_models.py
python3 -m venv build/macos-dmg-tools
build/macos-dmg-tools/bin/pip install 'dmgbuild==1.6.5'
./native/macos/build.sh
```

脚本默认构建当前机器的架构，执行截图、贴图、OCR、双向图片翻译、长图拼接及录屏结束检查，并将应用、拖拽安装的 DMG 和 ZIP 写到 `dist/`。如需构建 Intel 版本，将 `PICUT_MAC_ARCH` 设为 `x86_64`，并提供同架构的依赖。

应用数据默认写入 `~/Library/Application Support/FloatShot/FloatShotNative-ScrollPerformanceTrial/`。构建检查使用独立目录。

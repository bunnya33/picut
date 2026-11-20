#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${PICUT_MAC_BUILD_DIR:-$root/build/macos}"
dist_dir="${PICUT_MAC_DIST_DIR:-$root/dist}"
sdk="$root/build/macos-sdk"
cmake="${CMAKE:-cmake}"
if ! command -v "$cmake" >/dev/null && [[ -x "$root/build/macos-tools/bin/cmake" ]]; then
    cmake="$root/build/macos-tools/bin/cmake"
    export PATH="$root/build/macos-tools/bin:$PATH"
fi
qt="${QT_PREFIX:-$sdk/qt/6.8.3/macos}"
opencv="${OPENCV_PREFIX:-$sdk/opencv}"
models="${PICUT_TRANSLATION_MODELS:-$root/build/translation-sdk/models}"
dmgbuild="${PICUT_DMGBUILD:-dmgbuild}"
if ! command -v "$dmgbuild" >/dev/null && [[ -x "$root/build/macos-dmg-tools/bin/dmgbuild" ]]; then
    dmgbuild="$root/build/macos-dmg-tools/bin/dmgbuild"
fi
if command -v brew >/dev/null; then
    [[ -d "$qt" ]] || qt="$(brew --prefix qt)"
    [[ -d "$opencv" ]] || opencv="$(brew --prefix opencv)"
fi
deploy="$qt/bin/macdeployqt"
if [[ ! -x "$deploy" ]]; then
    printf '%s\n' '找不到 macdeployqt，请将 QT_PREFIX 指向 Qt 6.8 或更新版本的 macOS 安装目录。' >&2
    exit 1
fi

if [[ ! -f "$models/models.json" ]]; then
    printf '%s\n' '找不到离线翻译模型，请按 native/macos/README.md 准备或设置 PICUT_TRANSLATION_MODELS。' >&2
    exit 1
fi
if ! command -v "$dmgbuild" >/dev/null; then
    printf '%s\n' '找不到 dmgbuild，请按 native/macos/README.md 安装。' >&2
    exit 1
fi

arch="${PICUT_MAC_ARCH:-$(uname -m)}"
jobs="${PICUT_MAC_JOBS:-$(sysctl -n hw.logicalcpu)}"
generator=()
if [[ ! -f "$build_dir/CMakeCache.txt" ]] && command -v ninja >/dev/null; then
    generator=(-G Ninja)
fi
"$cmake" -S "$root/native" -B "$build_dir" ${generator[@]+"${generator[@]}"} \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DCMAKE_PREFIX_PATH="$qt;$opencv" -DPICUT_TRANSLATION_MODELS="$models"
"$cmake" --build "$build_dir" --parallel "$jobs"
"$(dirname "$(command -v "$cmake")")/ctest" --test-dir "$build_dir" --output-on-failure

name="picut-macos-$arch"
mkdir -p "$dist_dir"
stage="$(mktemp -d "$dist_dir/.picut-stage.XXXXXX")"
archive="$stage/../.${name}.$$.zip"
trap 'rm -rf "$stage" "$archive"' EXIT
ditto "$build_dir/picut.app" "$stage/picut.app"
"$deploy" "$stage/picut.app" -always-overwrite -no-strip \
    -libpath="$opencv/lib" -libpath="$build_dir/ctranslate2" \
    -executable="$stage/picut.app/Contents/MacOS/picut-translate"

license_dir="$stage/picut.app/Contents/Resources/licenses"
mkdir -p "$license_dir"
for file in Qt Qt-Bundled OpenCV-Apache-2.0.txt OpenCV-SoftFloat-BSD.txt ZXing-Apache-2.0.txt zlib.txt translation; do
    ditto "$root/native/licenses/$file" "$license_dir/$file"
done
ditto "$root/native/THIRD_PARTY_NOTICES.md" "$license_dir/THIRD_PARTY_NOTICES.md"
ditto "$root/native/macos/README.md" "$stage/使用说明.md"
codesign --force --deep --sign "${PICUT_MAC_SIGN_IDENTITY:--}" "$stage/picut.app"
codesign --verify --deep --strict "$stage/picut.app"
ditto -c -k --sequesterRsrc "$stage" "$archive"
xcrun swift "$root/native/macos/dmg-background.swift" "$stage/background.png"
"$dmgbuild" -s "$root/native/macos/dmg_settings.py" \
    -D "app=$stage/picut.app" -D "background=$stage/background.png" picut "$stage/$name.dmg"
mv -f "$stage/$name.dmg" "$dist_dir/$name.dmg"
mv -f "$archive" "$dist_dir/$name.zip"
destination="$dist_dir/$name"
mkdir -p "$destination"
if [[ -d "$destination/picut.app" ]]; then
    mv "$destination/picut.app" "$stage/previous.app"
fi
mv "$stage/picut.app" "$destination/picut.app"
cp "$stage/使用说明.md" "$destination/使用说明.md"
printf '%s\n' "$destination/picut.app" "$dist_dir/$name.dmg" "$dist_dir/$name.zip"

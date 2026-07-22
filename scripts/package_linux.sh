#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build-release"
DIST_DIR="$ROOT_DIR/dist/linux"
APPDIR="$BUILD_DIR/AppDir"
ARCH="${ARCH:-$(uname -m)}"
export ARCH

for tool in cmake ctest curl unzip; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "❌ Required tool not found: $tool"
        exit 1
    fi
done

ONNXRUNTIME_ROOT="${ONNXRUNTIME_ROOT:-}"
CMAKE_ONNX_ARGS=()
if [[ -n "$ONNXRUNTIME_ROOT" ]]; then
    CMAKE_ONNX_ARGS+=("-DONNXRUNTIME_ROOT=$ONNXRUNTIME_ROOT")
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    "${CMAKE_ONNX_ARGS[@]}"
cmake --build "$BUILD_DIR" --config Release

rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR"

VERSION="$(sed -n 's/^project(CloakFrame VERSION \([0-9.]*\).*/\1/p' "$ROOT_DIR/CMakeLists.txt")"
VERSION="${VERSION:-0.0.0}"

install -Dm644 "$ROOT_DIR/THIRD_PARTY_NOTICES.txt" "$APPDIR/usr/share/doc/cloakframe/THIRD_PARTY_NOTICES.txt"
install -Dm644 "$ROOT_DIR/LICENSE" "$APPDIR/usr/share/doc/cloakframe/LICENSE.txt"

ICON_STAGE="$BUILD_DIR/cloakframe.png"
cp "$ROOT_DIR/assets/cloakframe-512.png" "$ICON_STAGE"

TOOLS_DIR="$BUILD_DIR/appimage-tools"
mkdir -p "$TOOLS_DIR"
LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-${ARCH}.AppImage"
LINUXDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-${ARCH}.AppImage"

LINUXDEPLOY_SHA256_x86_64="e87ee0815d109282fdda73e34c2361d64d02b0ffaea3674b18f1fd1f6a687dcf"
LINUXDEPLOY_QT_SHA256_x86_64="be1b7e166bf9975cfb694ebe6759ba40502ffc6196440d3e64aa90c4dbd67e9f"

verify_sha256() {
    local file="$1" expected="$2"
    if [[ -z "$expected" ]]; then
        echo "⚠️  No pinned SHA256 for $(basename "$file") on $ARCH; skipping verification (dev build)."
        return 0
    fi
    local actual
    actual="$(sha256sum "$file" | awk '{print $1}')"
    if [[ "$actual" != "$expected" ]]; then
        echo "❌ SHA256 mismatch for $(basename "$file"): got $actual, expected $expected"
        exit 1
    fi
}

fetch() {
    local url="$1" out="$2" expected="${3:-}"
    if [[ ! -f "$out" ]]; then
        echo "⬇️  $url"
        curl -fL "$url" -o "$out"
    fi
    verify_sha256 "$out" "$expected"
    chmod +x "$out"
}

linuxdeploy_expected=""
linuxdeploy_qt_expected=""
if [[ "$ARCH" == "x86_64" ]]; then
    linuxdeploy_expected="$LINUXDEPLOY_SHA256_x86_64"
    linuxdeploy_qt_expected="$LINUXDEPLOY_QT_SHA256_x86_64"
fi

fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage" "$LINUXDEPLOY" "$linuxdeploy_expected"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage" "$LINUXDEPLOY_QT" "$linuxdeploy_qt_expected"

FFMPEG_BASE_URL="https://ffmpeg.martin-riedl.de/download/linux/amd64/1783011670_8.1.2"
FFMPEG_ZIP_SHA256_x86_64="56452c0bfc4ee0325cd615d62f46ba8264f62eed34f727c2224c6c84fa7b8719"
FFPROBE_ZIP_SHA256_x86_64="c6f2d36e98f9a4445fad0b0be539f4c4faf13fd502116bf131becd53f56cd390"

bundle_ffmpeg_tool() {
    local name="$1" expected="$2"
    local archive="$TOOLS_DIR/$name.zip"
    if [[ ! -f "$archive" ]]; then
        echo "⬇️  $FFMPEG_BASE_URL/$name.zip"
        curl -fL "$FFMPEG_BASE_URL/$name.zip" -o "$archive"
    fi
    verify_sha256 "$archive" "$expected"
    unzip -o -q "$archive" "$name" -d "$APPDIR/usr/bin/ffmpeg"
    chmod 755 "$APPDIR/usr/bin/ffmpeg/$name"
    sha256sum "$APPDIR/usr/bin/ffmpeg/$name" | awk '{print $1}' \
        > "$APPDIR/usr/bin/ffmpeg/$name.sha256"
}

if [[ "$ARCH" == "x86_64" ]]; then
    mkdir -p "$APPDIR/usr/bin/ffmpeg"
    bundle_ffmpeg_tool ffmpeg "$FFMPEG_ZIP_SHA256_x86_64"
    bundle_ffmpeg_tool ffprobe "$FFPROBE_ZIP_SHA256_x86_64"
else
    echo "⚠️  No pinned FFmpeg build for $ARCH; the AppImage will rely on a system FFmpeg."
fi

echo "🧪 Running release tests…"
PATH="$APPDIR/usr/bin/ffmpeg:$PATH" \
    ctest --test-dir "$BUILD_DIR" --output-on-failure

export PATH="$TOOLS_DIR:$PATH"
if [[ -n "${QMAKE:-}" ]]; then
    export QMAKE
fi

DEPLOY_ARGS=()
if [[ -n "$ONNXRUNTIME_ROOT" && -d "$ONNXRUNTIME_ROOT/lib" ]]; then
    export LD_LIBRARY_PATH="$ONNXRUNTIME_ROOT/lib:${LD_LIBRARY_PATH:-}"
    while IFS= read -r lib; do
        DEPLOY_ARGS+=("--library" "$lib")
    done < <(find "$ONNXRUNTIME_ROOT/lib" -name 'libonnxruntime.so*' -type f)
fi

rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

OUTPUT="CloakFrame-${VERSION}-${ARCH}.AppImage"
(
    cd "$DIST_DIR"
    OUTPUT="$OUTPUT" "$LINUXDEPLOY" \
        --appdir "$APPDIR" \
        --executable "$APPDIR/usr/bin/CloakFrame" \
        --desktop-file "$APPDIR/usr/share/applications/cloakframe.desktop" \
        --icon-file "$ICON_STAGE" \
        --plugin qt \
        "${DEPLOY_ARGS[@]}" \
        --output appimage
)

if find "$APPDIR" -name '*.onnx' -print -quit | grep -q .; then
    echo "❌ ONNX model files found in the AppDir; models must not be bundled."
    exit 1
fi

if [[ "$ARCH" == "x86_64" ]]; then
    for tool in ffmpeg ffprobe; do
        expected="$(cat "$APPDIR/usr/bin/ffmpeg/$tool.sha256")"
        actual="$(sha256sum "$APPDIR/usr/bin/ffmpeg/$tool" | awk '{print $1}')"
        if [[ "$actual" != "$expected" ]]; then
            echo "❌ Bundled $tool was modified during packaging (sidecar mismatch)."
            exit 1
        fi
    done
fi

echo "✅ AppImage created: $DIST_DIR/$OUTPUT"

#!/usr/bin/env bash
set -euo pipefail

# ── Config ─────────────────────────────────────────────────────────
# Builds FaceVeil and packages it as a self-contained AppImage.
#
# Required environment variables:
#   ONNXRUNTIME_ROOT — path to the ONNX Runtime release (containing include/ and
#                      lib/), unless a system pkg-config libonnxruntime is present.
# Optional:
#   QMAKE            — path to qmake for Qt6 (linuxdeploy-plugin-qt uses it).
#   ARCH             — target architecture for the AppImage (default: uname -m).

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build-release"
DIST_DIR="$ROOT_DIR/dist/linux"
APPDIR="$BUILD_DIR/AppDir"
ARCH="${ARCH:-$(uname -m)}"
export ARCH

for tool in cmake curl; do
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

# ── Build ──────────────────────────────────────────────────────────
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    "${CMAKE_ONNX_ARGS[@]}"
cmake --build "$BUILD_DIR" --config Release

rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR"

VERSION="$(sed -n 's/^project(FaceVeil VERSION \([0-9.]*\).*/\1/p' "$ROOT_DIR/CMakeLists.txt")"
VERSION="${VERSION:-0.0.0}"

# Ship the license and third-party notices next to the binary.
install -Dm644 "$ROOT_DIR/THIRD_PARTY_NOTICES.txt" "$APPDIR/usr/share/doc/faceveil/THIRD_PARTY_NOTICES.txt"
install -Dm644 "$ROOT_DIR/LICENSE" "$APPDIR/usr/share/doc/faceveil/LICENSE.txt"

# linuxdeploy resolves the icon by the desktop file's Icon= name and only
# accepts standard icon resolutions, so use the 512x512 asset named faceveil.png.
ICON_STAGE="$BUILD_DIR/faceveil.png"
cp "$ROOT_DIR/assets/faceveil-512.png" "$ICON_STAGE"

# ── Fetch linuxdeploy + Qt plugin ──────────────────────────────────
TOOLS_DIR="$BUILD_DIR/appimage-tools"
mkdir -p "$TOOLS_DIR"
LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-${ARCH}.AppImage"
LINUXDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-${ARCH}.AppImage"

fetch() {
    local url="$1" out="$2"
    if [[ ! -f "$out" ]]; then
        echo "⬇️  $url"
        curl -fL "$url" -o "$out"
        chmod +x "$out"
    fi
}

fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage" "$LINUXDEPLOY"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage" "$LINUXDEPLOY_QT"

export PATH="$TOOLS_DIR:$PATH"
if [[ -n "${QMAKE:-}" ]]; then
    export QMAKE
fi

# Help linuxdeploy locate the ONNX Runtime shared library for bundling.
DEPLOY_ARGS=()
if [[ -n "$ONNXRUNTIME_ROOT" && -d "$ONNXRUNTIME_ROOT/lib" ]]; then
    export LD_LIBRARY_PATH="$ONNXRUNTIME_ROOT/lib:${LD_LIBRARY_PATH:-}"
    while IFS= read -r lib; do
        DEPLOY_ARGS+=("--library" "$lib")
    done < <(find "$ONNXRUNTIME_ROOT/lib" -name 'libonnxruntime.so*' -type f)
fi

# ── Package AppImage ───────────────────────────────────────────────
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

OUTPUT="FaceVeil-${VERSION}-${ARCH}.AppImage"
(
    cd "$DIST_DIR"
    OUTPUT="$OUTPUT" "$LINUXDEPLOY" \
        --appdir "$APPDIR" \
        --executable "$APPDIR/usr/bin/FaceVeil" \
        --desktop-file "$APPDIR/usr/share/applications/faceveil.desktop" \
        --icon-file "$ICON_STAGE" \
        --plugin qt \
        "${DEPLOY_ARGS[@]}" \
        --output appimage
)

# Guard: SCRFD models are downloaded at runtime and must never be bundled
# (InsightFace's models are non-commercial and are not redistributed here).
if find "$APPDIR" -name '*.onnx' -print -quit | grep -q .; then
    echo "❌ ONNX model files found in the AppDir; models must not be bundled."
    exit 1
fi

echo "✅ AppImage created: $DIST_DIR/$OUTPUT"

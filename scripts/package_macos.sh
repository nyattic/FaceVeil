#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build-release"
DIST_DIR="$ROOT_DIR/dist/macos"
APP_NAME="CloakFrame.app"
APP_PATH="$BUILD_DIR/$APP_NAME"
DIST_APP="$DIST_DIR/$APP_NAME"
FRAMEWORKS_DIR="$DIST_APP/Contents/Frameworks"
MACOS_DIR="$DIST_APP/Contents/MacOS"
EXECUTABLE="$MACOS_DIR/CloakFrame"
ENTITLEMENTS="$ROOT_DIR/scripts/entitlements.plist"
BUNDLE_ID="${BUNDLE_ID:-com.cloakframe.app}"
CLOAKFRAME_MACOS_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-15.0}"

for tool in cmake ctest codesign macdeployqt install_name_tool otool vtool hdiutil ditto brew; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "❌ Required tool not found: $tool"
        exit 1
    fi
done

HOMEBREW_PREFIX="$(brew --prefix)"

DEVELOPER_ID="${DEVELOPER_ID:-}"
if [[ -z "$DEVELOPER_ID" ]]; then
    echo "⚠️  DEVELOPER_ID is not set — falling back to ad-hoc signing (local only)."
    SIGN_IDENTITY="-"
    DISTRIBUTABLE=0
else
    if ! security find-identity -p codesigning -v 2>/dev/null | grep -q -F "$DEVELOPER_ID"; then
        echo "❌ Developer signing identity not found in keychain: $DEVELOPER_ID"
        echo "   Available codesigning identities:"
        security find-identity -p codesigning -v || true
        exit 1
    fi
    SIGN_IDENTITY="$DEVELOPER_ID"
    DISTRIBUTABLE=1
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$CLOAKFRAME_MACOS_DEPLOYMENT_TARGET" \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$HOMEBREW_PREFIX"
cmake --build "$BUILD_DIR" --config Release

rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"
ditto "$APP_PATH" "$DIST_APP"

VERSION="$(plutil -extract CFBundleShortVersionString raw "$DIST_APP/Contents/Info.plist" 2>/dev/null || echo "0.0.0")"

macdeployqt "$DIST_APP" \
  -verbose=1 \
  -libpath="$HOMEBREW_PREFIX/lib" \
  -libpath="$HOMEBREW_PREFIX/Frameworks"

mkdir -p "$FRAMEWORKS_DIR"

is_bundle_candidate() {
  local dependency="$1"
  [[ "$dependency" == /opt/homebrew/* \
     || "$dependency" == /usr/local/* \
     || "$dependency" == "$HOMEBREW_PREFIX"/* ]]
}

copy_dependency() {
  local dependency="$1"
  local name
  name="$(basename "$dependency")"
  if [[ ! -f "$FRAMEWORKS_DIR/$name" ]]; then
    cp "$dependency" "$FRAMEWORKS_DIR/$name"
    chmod u+w "$FRAMEWORKS_DIR/$name"
  fi
}

rewrite_dependency() {
  local binary="$1"
  local dependency="$2"
  local name
  name="$(basename "$dependency")"
  if [[ "$binary" == "$EXECUTABLE" ]]; then
    install_name_tool -change "$dependency" "@executable_path/../Frameworks/$name" "$binary" 2>/dev/null || true
  else
    install_name_tool -change "$dependency" "@loader_path/$name" "$binary" 2>/dev/null || true
  fi
}

rewrite_rpath_dependency() {
  local binary="$1"
  local dependency="$2"
  local name
  name="$(basename "$dependency")"
  if [[ ! -f "$FRAMEWORKS_DIR/$name" ]]; then
    return
  fi
  if [[ "$binary" == "$EXECUTABLE" ]]; then
    install_name_tool -change "$dependency" "@executable_path/../Frameworks/$name" "$binary" 2>/dev/null || true
  else
    install_name_tool -change "$dependency" "@loader_path/$name" "$binary" 2>/dev/null || true
  fi
}

rewrite_rpath_dependencies_for() {
  local binary="$1"
  while IFS= read -r dependency; do
    if [[ "$dependency" == @rpath/* ]]; then
      rewrite_rpath_dependency "$binary" "$dependency"
    fi
  done < <(otool -L "$binary" | awk 'NR > 1 {print $1}')
}

bundle_dependencies_for() {
  local binary="$1"
  while IFS= read -r dependency; do
    if is_bundle_candidate "$dependency"; then
      copy_dependency "$dependency"
      rewrite_dependency "$binary" "$dependency"
    fi
  done < <(otool -L "$binary" | awk 'NR > 1 {print $1}')
}

while :; do
  before_count="$(find "$FRAMEWORKS_DIR" -type f -name '*.dylib' | wc -l | tr -d ' ')"
  bundle_dependencies_for "$EXECUTABLE"
  while IFS= read -r dylib; do
    bundle_dependencies_for "$dylib"
  done < <(find "$FRAMEWORKS_DIR" -type f -name '*.dylib')
  after_count="$(find "$FRAMEWORKS_DIR" -type f -name '*.dylib' | wc -l | tr -d ' ')"
  [[ "$before_count" == "$after_count" ]] && break
done

rewrite_rpath_dependencies_for "$EXECUTABLE"
while IFS= read -r dylib; do
  install_name_tool -id "@loader_path/$(basename "$dylib")" "$dylib" 2>/dev/null || true
  rewrite_rpath_dependencies_for "$dylib"
done < <(find "$FRAMEWORKS_DIR" -type f -name '*.dylib')

mkdir -p "$DIST_APP/Contents/Resources"
cp "$ROOT_DIR/THIRD_PARTY_NOTICES.txt" "$DIST_APP/Contents/Resources/THIRD_PARTY_NOTICES.txt"
cp "$ROOT_DIR/LICENSE" "$DIST_APP/Contents/Resources/LICENSE.txt"

FFMPEG_BASE_URL="https://ffmpeg.martin-riedl.de/download/macos/arm64/1783011502_8.1.2"
FFMPEG_ZIP_SHA256="ef1aa60006c7b77ce170c1608c08d8e4ba1c30c5746f2ac986ded932d0ac2c3c"
FFPROBE_ZIP_SHA256="c39787f4af7a3932502d2d48db6f6feaaa836b48a73ef78c32cc3285df61dfaf"
FFMPEG_DIR="$DIST_APP/Contents/Resources/ffmpeg"
FFMPEG_CACHE="$BUILD_DIR/ffmpeg-download"
mkdir -p "$FFMPEG_DIR" "$FFMPEG_CACHE"

bundle_ffmpeg_tool() {
    local name="$1" expected="$2"
    local archive="$FFMPEG_CACHE/$name.zip"
    if [[ ! -f "$archive" ]]; then
        echo "⬇️  $FFMPEG_BASE_URL/$name.zip"
        curl -fL "$FFMPEG_BASE_URL/$name.zip" -o "$archive"
    fi
    local actual
    actual="$(shasum -a 256 "$archive" | awk '{print $1}')"
    if [[ "$actual" != "$expected" ]]; then
        echo "❌ SHA256 mismatch for $name.zip: got $actual, expected $expected"
        exit 1
    fi
    unzip -o -q "$archive" "$name" -d "$FFMPEG_DIR"
    chmod 755 "$FFMPEG_DIR/$name"
}

bundle_ffmpeg_tool ffmpeg "$FFMPEG_ZIP_SHA256"
bundle_ffmpeg_tool ffprobe "$FFPROBE_ZIP_SHA256"

echo "🧪 Running release tests…"
PATH="$FFMPEG_DIR:$PATH" ctest --test-dir "$BUILD_DIR" --output-on-failure

version_is_not_newer_than() {
    local actual="$1" allowed="$2"
    awk -v actual="$actual" -v allowed="$allowed" '
        BEGIN {
            split(actual, a, "."); split(allowed, b, ".")
            for (i = 1; i <= 3; ++i) {
                av = a[i] + 0; bv = b[i] + 0
                if (av < bv) exit 0
                if (av > bv) exit 1
            }
            exit 0
        }'
}

echo "🔎 Verifying macOS $CLOAKFRAME_MACOS_DEPLOYMENT_TARGET compatibility…"
PLIST_MINIMUM="$(plutil -extract LSMinimumSystemVersion raw \
    "$DIST_APP/Contents/Info.plist" 2>/dev/null || true)"
if [[ "$PLIST_MINIMUM" != "$CLOAKFRAME_MACOS_DEPLOYMENT_TARGET" ]]; then
    echo "❌ LSMinimumSystemVersion is '$PLIST_MINIMUM'; expected '$CLOAKFRAME_MACOS_DEPLOYMENT_TARGET'."
    exit 1
fi

COMPATIBILITY_FAILED=0
while IFS= read -r -d '' candidate; do
    if ! file "$candidate" | grep -q 'Mach-O'; then
        continue
    fi

    MINIMUM_VERSIONS="$(vtool -show-build "$candidate" 2>/dev/null \
        | awk '$1 == "minos" { print $2 }')"
    if [[ -z "$MINIMUM_VERSIONS" ]]; then
        echo "❌ Could not determine the minimum macOS version: $candidate"
        COMPATIBILITY_FAILED=1
        continue
    fi

    while IFS= read -r minimum; do
        if ! version_is_not_newer_than "$minimum" "$CLOAKFRAME_MACOS_DEPLOYMENT_TARGET"; then
            echo "❌ $candidate requires macOS $minimum (maximum allowed: $CLOAKFRAME_MACOS_DEPLOYMENT_TARGET)."
            COMPATIBILITY_FAILED=1
        fi
    done <<< "$MINIMUM_VERSIONS"
done < <(find "$DIST_APP" -type f -print0)

if [[ "$COMPATIBILITY_FAILED" != "0" ]]; then
    exit 1
fi

if find "$DIST_APP" -name '*.onnx' -print -quit | grep -q .; then
    echo "❌ ONNX model files found in the app bundle; models must not be bundled."
    exit 1
fi

SIGN_FLAGS=(--force --timestamp --options runtime)
if [[ "$DISTRIBUTABLE" == "1" ]]; then
    SIGN_FLAGS+=(--entitlements "$ENTITLEMENTS")
fi

echo "🔏 Signing with: $SIGN_IDENTITY"

while IFS= read -r item; do
  codesign "${SIGN_FLAGS[@]}" --sign "$SIGN_IDENTITY" "$item"
done < <(find "$FRAMEWORKS_DIR" -type f \( -name '*.dylib' -o -name '*.so' \) -print)

if [[ -d "$DIST_APP/Contents/PlugIns" ]]; then
    while IFS= read -r item; do
        codesign "${SIGN_FLAGS[@]}" --sign "$SIGN_IDENTITY" "$item"
    done < <(find "$DIST_APP/Contents/PlugIns" -type f \( -name '*.dylib' -o -name '*.so' \) -print)
fi

while IFS= read -r fw; do
  codesign "${SIGN_FLAGS[@]}" --sign "$SIGN_IDENTITY" "$fw"
done < <(find "$FRAMEWORKS_DIR" -maxdepth 1 -type d -name '*.framework' -print)

for tool in "$FFMPEG_DIR/ffmpeg" "$FFMPEG_DIR/ffprobe"; do
  codesign --force --timestamp --options runtime --sign "$SIGN_IDENTITY" "$tool"
  shasum -a 256 "$tool" | awk '{print $1}' > "$tool.sha256"
done

codesign "${SIGN_FLAGS[@]}" --sign "$SIGN_IDENTITY" "$DIST_APP"

echo "🔎 Verifying signature…"
codesign --verify --verbose=2 "$DIST_APP"
if [[ "$DISTRIBUTABLE" == "1" ]]; then
    spctl -a -vv -t exec "$DIST_APP" || echo "ℹ️  spctl rejection is expected until the app is notarized & stapled."
fi

echo "✅ Packaged app: $DIST_APP"

if [[ "$DISTRIBUTABLE" != "1" ]]; then
    echo "ℹ️  Skipping DMG (ad-hoc signed build)."
    exit 0
fi
if [[ "${SKIP_DMG:-0}" == "1" ]]; then
    echo "ℹ️  SKIP_DMG=1 set, skipping DMG."
    exit 0
fi

DMG_NAME="CloakFrame-${VERSION}-arm64.dmg"
DMG_PATH="$DIST_DIR/$DMG_NAME"
STAGING_DIR="$(mktemp -d)"
trap 'rm -rf "$STAGING_DIR" || true' EXIT

ditto "$DIST_APP" "$STAGING_DIR/$APP_NAME"
ln -s /Applications "$STAGING_DIR/Applications"

rm -f "$DMG_PATH"
hdiutil create \
    -volname "CloakFrame" \
    -srcfolder "$STAGING_DIR" \
    -ov \
    -format UDZO \
    "$DMG_PATH"

codesign --force --timestamp --sign "$SIGN_IDENTITY" "$DMG_PATH"
codesign --verify --verbose=2 "$DMG_PATH"

echo "✅ DMG created: $DMG_PATH"
echo ""
echo "Next step — notarize:"
echo "  scripts/notarize_macos.sh \"$DMG_PATH\""

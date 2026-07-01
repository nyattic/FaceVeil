#!/usr/bin/env bash
set -euo pipefail

# ── Config ─────────────────────────────────────────────────────────
# Required environment variables:
#   DEVELOPER_ID   — full name of the Developer ID Application certificate,
#                    e.g. "Developer ID Application: Jane Doe (ABCDE12345)".
#                    If unset, the script falls back to ad-hoc signing and
#                    skips DMG packaging (local-only build).
# Optional:
#   BUNDLE_ID      — override bundle identifier (default: com.faceveil.app)
#   SKIP_DMG=1     — build + sign the .app but skip the DMG step.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build-release"
DIST_DIR="$ROOT_DIR/dist/macos"
APP_NAME="FaceVeil.app"
APP_PATH="$BUILD_DIR/$APP_NAME"
DIST_APP="$DIST_DIR/$APP_NAME"
FRAMEWORKS_DIR="$DIST_APP/Contents/Frameworks"
MACOS_DIR="$DIST_APP/Contents/MacOS"
EXECUTABLE="$MACOS_DIR/FaceVeil"
ENTITLEMENTS="$ROOT_DIR/scripts/entitlements.plist"
BUNDLE_ID="${BUNDLE_ID:-com.faceveil.app}"

# Required tools.
for tool in cmake codesign macdeployqt install_name_tool otool hdiutil ditto brew; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "❌ Required tool not found: $tool"
        exit 1
    fi
done

# Homebrew root is /opt/homebrew on Apple Silicon and /usr/local on Intel Macs;
# query it dynamically so the script works on both.
HOMEBREW_PREFIX="$(brew --prefix)"

DEVELOPER_ID="${DEVELOPER_ID:-}"
if [[ -z "$DEVELOPER_ID" ]]; then
    echo "⚠️  DEVELOPER_ID is not set — falling back to ad-hoc signing (local only)."
    SIGN_IDENTITY="-"
    DISTRIBUTABLE=0
else
    # Validate up front so we fail before spending minutes on packaging.
    if ! security find-identity -p codesigning -v 2>/dev/null | grep -q -F "$DEVELOPER_ID"; then
        echo "❌ Developer signing identity not found in keychain: $DEVELOPER_ID"
        echo "   Available codesigning identities:"
        security find-identity -p codesigning -v || true
        exit 1
    fi
    SIGN_IDENTITY="$DEVELOPER_ID"
    DISTRIBUTABLE=1
fi

# ── Build ──────────────────────────────────────────────────────────
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build "$BUILD_DIR" --config Release

rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"
ditto "$APP_PATH" "$DIST_APP"

VERSION="$(plutil -extract CFBundleShortVersionString raw "$DIST_APP/Contents/Info.plist" 2>/dev/null || echo "0.0.0")"

# ── Deploy Qt + bundle third-party dylibs ──────────────────────────
macdeployqt "$DIST_APP" \
  -verbose=1 \
  -libpath="$HOMEBREW_PREFIX/lib" \
  -libpath="$HOMEBREW_PREFIX/Frameworks"

mkdir -p "$FRAMEWORKS_DIR"

is_bundle_candidate() {
  local dependency="$1"
  # Cover both Apple Silicon (/opt/homebrew) and Intel (/usr/local) Homebrew
  # layouts, plus any custom prefix reported by `brew --prefix`.
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

# ── Sign ───────────────────────────────────────────────────────────
SIGN_FLAGS=(--force --timestamp --options runtime)
if [[ "$DISTRIBUTABLE" == "1" ]]; then
    SIGN_FLAGS+=(--entitlements "$ENTITLEMENTS")
fi

echo "🔏 Signing with: $SIGN_IDENTITY"

# Sign every bundled dylib / framework binary first (deep-first order).
while IFS= read -r item; do
  codesign "${SIGN_FLAGS[@]}" --sign "$SIGN_IDENTITY" "$item"
done < <(find "$FRAMEWORKS_DIR" -type f \( -name '*.dylib' -o -name '*.so' \) -print)

# Qt plugins (macdeployqt places them in PlugIns/).
if [[ -d "$DIST_APP/Contents/PlugIns" ]]; then
    while IFS= read -r item; do
        codesign "${SIGN_FLAGS[@]}" --sign "$SIGN_IDENTITY" "$item"
    done < <(find "$DIST_APP/Contents/PlugIns" -type f \( -name '*.dylib' -o -name '*.so' \) -print)
fi

# Frameworks (bundles) need signing at the framework level.
while IFS= read -r fw; do
  codesign "${SIGN_FLAGS[@]}" --sign "$SIGN_IDENTITY" "$fw"
done < <(find "$FRAMEWORKS_DIR" -maxdepth 1 -type d -name '*.framework' -print)

# Finally, sign the app bundle itself (outer signature wraps everything).
codesign "${SIGN_FLAGS[@]}" --sign "$SIGN_IDENTITY" "$DIST_APP"

# Verify.
echo "🔎 Verifying signature…"
codesign --verify --verbose=2 "$DIST_APP"
if [[ "$DISTRIBUTABLE" == "1" ]]; then
    spctl -a -vv -t exec "$DIST_APP" || echo "ℹ️  spctl rejection is expected until the app is notarized & stapled."
fi

echo "✅ Packaged app: $DIST_APP"

# ── DMG ────────────────────────────────────────────────────────────
if [[ "$DISTRIBUTABLE" != "1" ]]; then
    echo "ℹ️  Skipping DMG (ad-hoc signed build)."
    exit 0
fi
if [[ "${SKIP_DMG:-0}" == "1" ]]; then
    echo "ℹ️  SKIP_DMG=1 set, skipping DMG."
    exit 0
fi

DMG_NAME="FaceVeil-${VERSION}-arm64.dmg"
DMG_PATH="$DIST_DIR/$DMG_NAME"
STAGING_DIR="$(mktemp -d)"
trap 'rm -rf "$STAGING_DIR" || true' EXIT

ditto "$DIST_APP" "$STAGING_DIR/$APP_NAME"
ln -s /Applications "$STAGING_DIR/Applications"

rm -f "$DMG_PATH"
hdiutil create \
    -volname "FaceVeil" \
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

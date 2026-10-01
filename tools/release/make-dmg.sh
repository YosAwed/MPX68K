#!/bin/sh -e
# Builds a universal Release MPX68K.app and packs it into a drag-to-install
# disk image: the app and an Applications alias over a background that says
# where to drag it. Also writes X68000.app.zip and SHA256SUMS.txt.
#
#   tools/release/make-dmg.sh [output-dir]
#
# Needs Xcode and python3; dmgbuild is installed into a local venv on first
# use. Signs the .dmg with the same identity as the app.
cd "$(dirname "$0")"
HERE=$(pwd)
ROOT=$(cd ../.. && pwd)
OUT=${1:-"$ROOT/build/release"}
WORK="$OUT/work"
mkdir -p "$OUT" "$WORK"

VENV="$HERE/.venv"
if [ ! -x "$VENV/bin/dmgbuild" ]; then
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install -q dmgbuild
fi

echo "==> Building Release (arm64 + x86_64)"
xcodebuild -project "$ROOT/X68000.xcodeproj" -scheme "X68000 macOS" \
    -configuration Release -derivedDataPath "$WORK/DerivedData" \
    ARCHS="arm64 x86_64" ONLY_ACTIVE_ARCH=NO build -quiet
APP="$WORK/DerivedData/Build/Products/Release/MPX68K.app"
[ -d "$APP" ] || { echo "error: $APP not found" >&2; exit 1; }

VERSION=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$APP/Contents/Info.plist")
BUILD=$(/usr/libexec/PlistBuddy -c "Print :CFBundleVersion" "$APP/Contents/Info.plist")
echo "==> MPX68K $VERSION (build $BUILD): $(lipo -archs "$APP/Contents/MacOS/MPX68K")"
codesign --verify --deep --strict "$APP"

echo "==> Rendering background"
swift "$HERE/dmg-background.swift" "$WORK/background.png" 1
swift "$HERE/dmg-background.swift" "$WORK/background@2x.png" 2
tiffutil -cathidpicheck "$WORK/background.png" "$WORK/background@2x.png" -out "$WORK/background.tiff"

DMG="$OUT/MPX68K-$VERSION-$BUILD.dmg"
rm -f "$DMG"
echo "==> Creating $DMG"
"$VENV/bin/dmgbuild" -s "$HERE/dmg_settings.py" \
    -D app="$APP" -D background="$WORK/background.tiff" \
    "MPX68K $VERSION" "$DMG"

IDENTITY=$(codesign -dvv "$APP" 2>&1 | sed -n 's/^Authority=//p' | head -1)
if [ -n "$IDENTITY" ]; then
    codesign --sign "$IDENTITY" --timestamp=none "$DMG"
fi
# The plain ZIP the earlier releases shipped, for those who prefer it.
ZIP="$OUT/X68000.app.zip"
rm -f "$ZIP"
ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP"
( cd "$OUT" && shasum -a 256 "$(basename "$DMG")" "$(basename "$ZIP")" > SHA256SUMS.txt )
echo "==> Done: $DMG"
cat "$OUT/SHA256SUMS.txt"

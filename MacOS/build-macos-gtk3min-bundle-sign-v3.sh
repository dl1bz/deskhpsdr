#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/.."

GTK_PREFIX="/opt/deskhpsdr-gtk3min"
BREW_PREFIX="/opt/homebrew"
APP="deskHPSDR.app"
EXE="deskhpsdr"
GIT_VERSION="$(git describe --abbrev=0 --tags --always)"
ZIP="deskHPSDR-v${GIT_VERSION}-macos-arm64.zip"
ZIP_NOTARY="deskHPSDR-notary.zip"
IDENTITY="Developer ID Application: Heiko Amft (SAJWA8RU2X)"
PROFILE="DL1BZ-DeveloperID"
ENTITLEMENTS="MacOS/deskHPSDR.entitlements"
PUBLISH="${PUBLISH:-0}"

export PKG_CONFIG_PATH="$GTK_PREFIX/lib/pkgconfig:$GTK_PREFIX/share/pkgconfig:$BREW_PREFIX/lib/pkgconfig:$BREW_PREFIX/share/pkgconfig"
export PATH="$GTK_PREFIX/bin:$BREW_PREFIX/bin:$PATH"
unset DYLD_LIBRARY_PATH
unset DYLD_FALLBACK_LIBRARY_PATH

verify_audio_input_entitlement() {
  local target="$1"
  local entitlements_xml
  local audio_input

  entitlements_xml="$(codesign -d --entitlements :- "$target" 2>&1 | sed -n '/<?xml/,$p')"

  if [ -z "$entitlements_xml" ]; then
    echo "ERROR: no embedded entitlements found in $target"
    return 1
  fi

  audio_input="$(printf '%s\n' "$entitlements_xml" | /usr/libexec/PlistBuddy -c "Print :com.apple.security.device.audio-input" /dev/stdin 2>/dev/null || true)"

  if [ "$audio_input" != "true" ]; then
    echo "ERROR: com.apple.security.device.audio-input is missing or false in $target"
    return 1
  fi

  echo "Verified audio-input entitlement on $target"
}

[ -f "$ENTITLEMENTS" ] || {
  echo "ERROR: entitlements file not found: $ENTITLEMENTS"
  exit 1
}

plutil -lint "$ENTITLEMENTS"

ENTITLEMENT_SOURCE_VALUE="$(/usr/libexec/PlistBuddy -c "Print :com.apple.security.device.audio-input" "$ENTITLEMENTS")"
[ "$ENTITLEMENT_SOURCE_VALUE" = "true" ] || {
  echo "ERROR: com.apple.security.device.audio-input is not true in $ENTITLEMENTS"
  exit 1
}

make clean

GTK_INCLUDE_FLAGS="$(PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --cflags gtk+-3.0 glib-2.0 gio-2.0)"
GTK_LIB_FLAGS="$(PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --libs gtk+-3.0 glib-2.0 gio-2.0)"

make EXTRA_CFLAGS="-DBUNDLED_APP" GTK_INCLUDE="$GTK_INCLUDE_FLAGS" GTK_LIBS="$GTK_LIB_FLAGS"

BIN="./deskhpsdr"
[ -x "$BIN" ] || BIN="./deskHPSDR"
[ -x "$BIN" ] || {
  echo "ERROR: binary not found"
  exit 1
}

otool -L "$BIN" | grep -q "$GTK_PREFIX/lib/libgtk-3.0.dylib" || {
  echo "ERROR: not linked against $GTK_PREFIX GTK"
  otool -L "$BIN" | grep -E "gtk|gdk|glib|gio|gobject|pango|cairo|intl" || true
  exit 1
}

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"

cp MacOS/Info.plist "$APP/Contents/Info.plist"
cp MacOS/PkgInfo "$APP/Contents/PkgInfo"
cp MacOS/hpsdr.icns "$APP/Contents/Resources/hpsdr.icns"
cp MacOS/deskhpsdr.icns "$APP/Contents/Resources/deskhpsdr.icns"
cp MacOS/rigctld_deskhpsdr "$APP/Contents/Resources/rigctld_deskhpsdr"
cp "$BIN" "$APP/Contents/MacOS/$EXE"

chmod 755 "$APP/Contents/MacOS/$EXE" "$APP/Contents/Resources/rigctld_deskhpsdr"
chmod 644 "$APP/Contents/Info.plist" "$APP/Contents/PkgInfo" "$APP/Contents/Resources/"*.icns

plutil -lint "$APP/Contents/Info.plist"

MIC_USAGE="$(plutil -extract NSMicrophoneUsageDescription raw -o - "$APP/Contents/Info.plist" 2>/dev/null || true)"
[ -n "$MIC_USAGE" ] || {
  echo "ERROR: NSMicrophoneUsageDescription missing from Info.plist"
  exit 1
}

xattr -cr "$APP"

dylibbundler -od -b -x "$APP/Contents/MacOS/$EXE" -d "$APP/Contents/Frameworks" -p "@executable_path/../Frameworks" -s "$GTK_PREFIX/lib"

while [ "$(otool -l "$APP/Contents/MacOS/$EXE" | awk '/cmd LC_RPATH/{show=1;next} show&&/path/{print $2;show=0}' | grep -c '^@executable_path/../Frameworks/$' || true)" -gt 1 ]; do
  install_name_tool -delete_rpath "@executable_path/../Frameworks/" "$APP/Contents/MacOS/$EXE"
done

[ -f "$APP/Contents/Frameworks/libgtk-3.0.dylib" ] || {
  echo "ERROR: dylibbundler did not copy libgtk-3.0.dylib"
  exit 1
}

LOADER_SRC_DIR="$GTK_PREFIX/lib/gdk-pixbuf-2.0/2.10.0/loaders"
LOADER_DST_DIR="$APP/Contents/Frameworks"

[ -d "$LOADER_SRC_DIR" ] || {
  echo "ERROR: gdk-pixbuf loader source dir missing: $LOADER_SRC_DIR"
  exit 1
}

[ -f "$LOADER_SRC_DIR/libpixbufloader-svg.so" ] || {
  echo "ERROR: SVG pixbuf loader missing in GTK prefix"
  exit 1
}

cp "$LOADER_SRC_DIR"/*.so "$LOADER_DST_DIR/"

for loader in "$LOADER_DST_DIR"/libpixbufloader-*.so; do
  dylibbundler -of -b -x "$loader" -d "$APP/Contents/Frameworks" -p "@loader_path" -s "$GTK_PREFIX/lib" -s "$BREW_PREFIX/lib"
done

SCHEMA_SRC_DIR="$GTK_PREFIX/share/glib-2.0/schemas"
SCHEMA_DST_DIR="$APP/Contents/Resources/share/glib-2.0/schemas"

[ -d "$SCHEMA_SRC_DIR" ] || {
  echo "ERROR: GSettings schema source dir missing: $SCHEMA_SRC_DIR"
  exit 1
}

[ -x "$GTK_PREFIX/bin/glib-compile-schemas" ] || {
  echo "ERROR: glib-compile-schemas missing: $GTK_PREFIX/bin/glib-compile-schemas"
  exit 1
}

mkdir -p "$SCHEMA_DST_DIR"

find "$SCHEMA_SRC_DIR" -maxdepth 1 -type f \
  \( -name '*.gschema.xml' -o -name '*.gschema.override' \) \
  -exec cp {} "$SCHEMA_DST_DIR/" \;

"$GTK_PREFIX/bin/glib-compile-schemas" "$SCHEMA_DST_DIR"

[ -f "$SCHEMA_DST_DIR/gschemas.compiled" ] || {
  echo "ERROR: bundled GSettings schema cache missing"
  exit 1
}

FONT_DST_DIR="$APP/Contents/Resources/fonts"

mkdir -p "$FONT_DST_DIR"

[ -d fonts/ttf/Roboto ] || {
  echo "ERROR: Roboto fonts missing: fonts/ttf/Roboto"
  exit 1
}

[ -d fonts/ttf/JetBrainsMono ] || {
  echo "ERROR: JetBrains Mono fonts missing: fonts/ttf/JetBrainsMono"
  exit 1
}

[ -d fonts/ttf/Digital ] || {
  echo "ERROR: Digital fonts missing: fonts/ttf/JetBrainsMono"
  exit 1
}

[ -d fonts/otf/GNU ] || {
  echo "ERROR: GNU fonts missing: fonts/otf/GNU"
  exit 1
}

cp -R fonts/ttf/Roboto "$FONT_DST_DIR/"
cp -R fonts/ttf/JetBrainsMono "$FONT_DST_DIR/"
cp -R fonts/ttf/Digital "$FONT_DST_DIR/"
cp -R fonts/otf/GNU "$FONT_DST_DIR/"

# Keep the real Mach-O executable directly as CFBundleExecutable.
# No shell wrapper, no secondary deskhpsdr-bin executable and no runtime
# GDK_PIXBUF/FONTCONFIG environment manipulation.
chmod 755 "$APP/Contents/MacOS/$EXE"


find "$APP" -type f -print | while read -r f; do
  if file "$f" | grep -q "Mach-O"; then
    if otool -L "$f" 2>/dev/null | grep -E "/opt/homebrew|/usr/local|$GTK_PREFIX"; then
      echo "ERROR: external dependency in $f"
      exit 1
    fi
  fi
done

if grep -RInE "/opt/homebrew|/usr/local|$GTK_PREFIX" "$APP" 2>/dev/null; then
  echo "ERROR: external text path found"
  exit 1
fi

sudo chown -R "$USER":admin "$APP"
chmod -R u+rwX,go+rX "$APP"
sudo xattr -cr "$APP"

find "$APP" -name "_CodeSignature" -type d -prune -exec rm -rf {} +

find "$APP" -type f -print | while read -r f; do
  if file "$f" | grep -q "Mach-O"; then
    codesign --force --timestamp --options runtime --sign "$IDENTITY" "$f"
  fi
done

codesign --force --timestamp --options runtime --entitlements "$ENTITLEMENTS" --sign "$IDENTITY" "$APP/Contents/MacOS/$EXE"
verify_audio_input_entitlement "$APP/Contents/MacOS/$EXE"

codesign --force --timestamp --options runtime --entitlements "$ENTITLEMENTS" --sign "$IDENTITY" "$APP"
codesign --verify --deep --strict --verbose=4 "$APP"
verify_audio_input_entitlement "$APP/Contents/MacOS/$EXE"

echo "Gatekeeper check before notarization; rejection as Unnotarized Developer ID is expected here:"
spctl --assess --type execute --verbose=4 "$APP" || true

# Static dependency check; does not launch the application.
otool -L "$APP/Contents/MacOS/$EXE" | tee /tmp/deskhpsdr-dyld-clean.log >/dev/null

if grep -E "/opt/homebrew|/usr/local|$GTK_PREFIX" /tmp/deskhpsdr-dyld-clean.log; then
  echo "ERROR: external path in dyld runtime check"
  exit 1
fi

rm -f "$ZIP" "$ZIP_NOTARY"

if [ "$PUBLISH" = "1" ]; then
  echo "Create notarization ZIP..."
  ditto -c -k --keepParent "$APP" "$ZIP_NOTARY"

  echo "Submit to Apple notary service..."
  xcrun notarytool submit "$ZIP_NOTARY" --keychain-profile "$PROFILE" --wait

  echo "Staple notarization ticket..."
  xcrun stapler staple "$APP"
  xcrun stapler validate "$APP"

  codesign --verify --deep --strict --verbose=4 "$APP"
  verify_audio_input_entitlement "$APP/Contents/MacOS/$EXE"
  spctl --assess --type execute --verbose=4 "$APP"
fi

echo "Create final release ZIP..."
ditto -c -k --keepParent "$APP" "$ZIP"
ls -lh "$ZIP"

mv "$ZIP" "$HOME/Desktop"

echo "OK"

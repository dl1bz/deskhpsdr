#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/.."

GTK_PREFIX="/opt/deskhpsdr-gtk3min"
BREW_PREFIX="/opt/homebrew"
APP="deskHPSDR.app"
EXE="deskhpsdr"
ZIP="deskHPSDR-macos-ARM64.zip"

export PKG_CONFIG_PATH="$GTK_PREFIX/lib/pkgconfig:$GTK_PREFIX/share/pkgconfig:$BREW_PREFIX/lib/pkgconfig:$BREW_PREFIX/share/pkgconfig"
export PATH="$GTK_PREFIX/bin:$BREW_PREFIX/bin:$PATH"
unset DYLD_LIBRARY_PATH
unset DYLD_FALLBACK_LIBRARY_PATH

make clean

GTK_INCLUDE_FLAGS="$(PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --cflags gtk+-3.0 glib-2.0 gio-2.0)"
GTK_LIB_FLAGS="$(PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --libs gtk+-3.0 glib-2.0 gio-2.0)"

make GTK_INCLUDE="$GTK_INCLUDE_FLAGS" GTK_LIBS="$GTK_LIB_FLAGS"

BIN="./deskhpsdr"
[ -x "$BIN" ] || BIN="./deskHPSDR"
[ -x "$BIN" ] || { echo "ERROR: binary not found"; exit 1; }

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
cp MacOS/radio.icns "$APP/Contents/Resources/radio.icns"
cp MacOS/rigctld_deskhpsdr "$APP/Contents/Resources/rigctld_deskhpsdr"
cp "$BIN" "$APP/Contents/MacOS/$EXE"

chmod 755 "$APP/Contents/MacOS/$EXE" "$APP/Contents/Resources/rigctld_deskhpsdr"
chmod 644 "$APP/Contents/Info.plist" "$APP/Contents/PkgInfo" "$APP/Contents/Resources/"*.icns

plutil -lint "$APP/Contents/Info.plist"

dylibbundler \
  -od \
  -b \
  -x "$APP/Contents/MacOS/$EXE" \
  -d "$APP/Contents/Frameworks" \
  -p "@executable_path/../Frameworks" \
  -s "$GTK_PREFIX/lib"

# Fix duplicate RPATH from dylibbundler, exactly like manual build.
while [ "$(otool -l "$APP/Contents/MacOS/$EXE" | awk '/cmd LC_RPATH/{show=1;next} show&&/path/{print $2;show=0}' | grep -c '^@executable_path/../Frameworks/$' || true)" -gt 1 ]; do
  install_name_tool -delete_rpath "@executable_path/../Frameworks/" "$APP/Contents/MacOS/$EXE"
done

[ -f "$APP/Contents/Frameworks/libgtk-3.0.dylib" ] || {
  echo "ERROR: dylibbundler did not copy libgtk-3.0.dylib"
  exit 1
}

mv "$APP/Contents/MacOS/$EXE" "$APP/Contents/MacOS/$EXE-bin"

cat > "$APP/Contents/MacOS/$EXE" <<'EOF'
#!/bin/sh
APPDIR="$(cd "$(dirname "$0")/.." && pwd)"
export DYLD_LIBRARY_PATH="$APPDIR/Frameworks${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
if [ -f "$APPDIR/Resources/fontconfig/fonts.conf" ]; then
  export FONTCONFIG_PATH="$APPDIR/Resources/fontconfig"
  export FONTCONFIG_FILE="$APPDIR/Resources/fontconfig/fonts.conf"
fi
exec "$APPDIR/MacOS/deskhpsdr-bin" "$@"
EOF

chmod 755 "$APP/Contents/MacOS/$EXE" "$APP/Contents/MacOS/$EXE-bin"

mkdir -p "$APP/Contents/Resources/fonts" "$APP/Contents/Resources/fontconfig"
[ -d fonts/ttf/Roboto ] && cp -R fonts/ttf/Roboto "$APP/Contents/Resources/fonts/"
[ -d fonts/ttf/JetBrainsMono ] && cp -R fonts/ttf/JetBrainsMono "$APP/Contents/Resources/fonts/"
[ -d fonts/otf/GNU ] && cp -R fonts/otf/GNU "$APP/Contents/Resources/fonts/"

cat > "$APP/Contents/Resources/fontconfig/fonts.conf" <<'EOF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir prefix="relative">../fonts</dir>
  <dir>/System/Library/Fonts</dir>
  <dir>/Library/Fonts</dir>
  <cachedir prefix="xdg">fontconfig</cachedir>
</fontconfig>
EOF

# Hard checks.
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

codesign --force --deep --timestamp=none --sign - "$APP"
codesign --verify --deep --strict --verbose=4 "$APP"

env -u DYLD_LIBRARY_PATH -u DYLD_FALLBACK_LIBRARY_PATH \
  DYLD_PRINT_LIBRARIES=1 \
  "$APP/Contents/MacOS/$EXE" 2>&1 | tee /tmp/deskhpsdr-dyld-clean.log >/dev/null || true

if grep -E "/opt/homebrew|/usr/local|$GTK_PREFIX" /tmp/deskhpsdr-dyld-clean.log; then
  echo "ERROR: external path in dyld runtime check"
  exit 1
fi

rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"
ls -lh "$ZIP"
echo "OK"

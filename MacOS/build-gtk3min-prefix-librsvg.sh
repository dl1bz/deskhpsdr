#!/bin/bash
set -euo pipefail

PREFIX="/opt/deskhpsdr-gtk3min"
SRCROOT="${HOME}/gtksrc"
VENV="${SRCROOT}/glib-2.80.5/.venv"
BREW_PREFIX="/opt/homebrew"

GLIB_VER="2.80.5"
CAIRO_VER="1.18.2"
HARFBUZZ_VER="8.5.0"
PANGO_VER="1.54.0"
GDK_PIXBUF_VER="2.42.12"
LIBRSVG_VER="2.58.5"
AT_SPI2_VER="2.52.0"
GTK_VER="3.24.43"

export PREFIX
export PKG_CONFIG_PATH="${PREFIX}/lib/pkgconfig:${PREFIX}/share/pkgconfig:${BREW_PREFIX}/lib/pkgconfig:${BREW_PREFIX}/share/pkgconfig"
export PATH="${PREFIX}/bin:${BREW_PREFIX}/bin:${PATH}"
export CPPFLAGS="-I${PREFIX}/include -I${BREW_PREFIX}/opt/gettext/include -I${BREW_PREFIX}/include"
export LDFLAGS="-L${PREFIX}/lib -L${BREW_PREFIX}/opt/gettext/lib -L${BREW_PREFIX}/lib"
export LIBRARY_PATH="${BREW_PREFIX}/opt/gettext/lib:${PREFIX}/lib:${BREW_PREFIX}/lib:${LIBRARY_PATH:-}"
export RUSTFLAGS="${RUSTFLAGS:-} -L native=${BREW_PREFIX}/opt/gettext/lib -L native=${PREFIX}/lib -L native=${BREW_PREFIX}/lib"
export GETTEXT_CFLAGS="-I${BREW_PREFIX}/opt/gettext/include"
export GETTEXT_LIBS="-L${BREW_PREFIX}/opt/gettext/lib -lintl"

mkdir -p "${SRCROOT}"
sudo mkdir -p "${PREFIX}"
sudo chown -R "${USER}:admin" "${PREFIX}"

cd "${SRCROOT}"

fetch_unpack() {
  local url="$1"
  local archive="$2"
  local dir="$3"

  if [ ! -d "${dir}" ]; then
    if [ ! -f "${archive}" ]; then
      curl -LO "${url}"
    fi
    tar xf "${archive}"
  fi
}

setup_venv() {
  cd "${SRCROOT}/glib-${GLIB_VER}"

  if [ ! -d ".venv" ]; then
    python3 -m venv .venv
  fi

  source ".venv/bin/activate"
  python -m pip install --upgrade pip
  python -m pip install packaging meson ninja
}

meson_build_install() {
  local dir="$1"
  shift

  cd "${SRCROOT}/${dir}"
  rm -rf build
  python -m mesonbuild.mesonmain setup build "$@"
  python -m mesonbuild.mesonmain compile -C build
  python -m mesonbuild.mesonmain install -C build
}

autotools_build_install() {
  local dir="$1"
  shift

  cd "${SRCROOT}/${dir}"
  make distclean >/dev/null 2>&1 || true
  ./configure "$@"
  make -j"$(sysctl -n hw.ncpu)"
  make install
}

update_gdk_pixbuf_cache() {
  local base="${PREFIX}/lib/gdk-pixbuf-2.0/2.10.0"
  local loader_dir="${base}/loaders"
  local cache="${base}/loaders.cache"

  if [ -x "${PREFIX}/bin/gdk-pixbuf-query-loaders" ] && [ -d "${loader_dir}" ]; then
    GDK_PIXBUF_MODULEDIR="${loader_dir}" "${PREFIX}/bin/gdk-pixbuf-query-loaders" > "${cache}"
  fi
}

check_svg_loader() {
  local loader="${PREFIX}/lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-svg.so"
  local cache="${PREFIX}/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache"

  if [ ! -f "${loader}" ]; then
    echo "ERROR: missing SVG pixbuf loader: ${loader}"
    exit 1
  fi

  if [ ! -f "${cache}" ]; then
    echo "ERROR: missing gdk-pixbuf loaders.cache: ${cache}"
    exit 1
  fi

  if ! grep -q "libpixbufloader-svg.so" "${cache}"; then
    echo "ERROR: SVG loader is not present in loaders.cache"
    exit 1
  fi
}

patch_gdbus_codegen() {
  if [ -x "${PREFIX}/bin/gdbus-codegen" ]; then
    sed -i.bak "1s|.*|#!${VENV}/bin/python|" "${PREFIX}/bin/gdbus-codegen"
  fi
}

check_prefix() {
  local pc="$1"
  local value

  value="$(pkg-config --variable=prefix "${pc}")"

  if [ "${value}" != "${PREFIX}" ]; then
    echo "ERROR: pkg-config prefix for ${pc} is ${value}, expected ${PREFIX}"
    exit 1
  fi
}

check_no_homebrew_core_leaks() {
  local hits

  hits="$(
    find "${PREFIX}/lib" -type f \( -name "*.dylib" -o -name "*.so" \) -print |
    while read -r f; do
      if file "${f}" | grep -q "Mach-O"; then
        bad="$(otool -L "${f}" 2>/dev/null | grep -E "/opt/homebrew/opt/glib|/opt/homebrew/opt/gobject|/opt/homebrew/opt/gio|/opt/homebrew/opt/pango|/opt/homebrew/opt/cairo|/opt/homebrew/opt/harfbuzz|/opt/homebrew/opt/gdk-pixbuf|/opt/homebrew/opt/librsvg|/opt/homebrew/opt/gtk\+3" || true)"
        if [ -n "${bad}" ]; then
          echo "### ${f}"
          echo "${bad}"
        fi
      fi
    done
  )"

  if [ -n "${hits}" ]; then
    echo "ERROR: Homebrew GTK-core dependency leak detected:"
    echo "${hits}"
    exit 1
  fi
}

echo "== Fetch sources =="

fetch_unpack "https://download.gnome.org/sources/glib/2.80/glib-${GLIB_VER}.tar.xz" "glib-${GLIB_VER}.tar.xz" "glib-${GLIB_VER}"
fetch_unpack "https://cairographics.org/releases/cairo-${CAIRO_VER}.tar.xz" "cairo-${CAIRO_VER}.tar.xz" "cairo-${CAIRO_VER}"
fetch_unpack "https://github.com/harfbuzz/harfbuzz/releases/download/${HARFBUZZ_VER}/harfbuzz-${HARFBUZZ_VER}.tar.xz" "harfbuzz-${HARFBUZZ_VER}.tar.xz" "harfbuzz-${HARFBUZZ_VER}"
fetch_unpack "https://download.gnome.org/sources/pango/1.54/pango-${PANGO_VER}.tar.xz" "pango-${PANGO_VER}.tar.xz" "pango-${PANGO_VER}"
fetch_unpack "https://download.gnome.org/sources/gdk-pixbuf/2.42/gdk-pixbuf-${GDK_PIXBUF_VER}.tar.xz" "gdk-pixbuf-${GDK_PIXBUF_VER}.tar.xz" "gdk-pixbuf-${GDK_PIXBUF_VER}"
fetch_unpack "https://download.gnome.org/sources/librsvg/2.58/librsvg-${LIBRSVG_VER}.tar.xz" "librsvg-${LIBRSVG_VER}.tar.xz" "librsvg-${LIBRSVG_VER}"
fetch_unpack "https://download.gnome.org/sources/at-spi2-core/2.52/at-spi2-core-${AT_SPI2_VER}.tar.xz" "at-spi2-core-${AT_SPI2_VER}.tar.xz" "at-spi2-core-${AT_SPI2_VER}"
fetch_unpack "https://download.gnome.org/sources/gtk+/3.24/gtk+-${GTK_VER}.tar.xz" "gtk+-${GTK_VER}.tar.xz" "gtk+-${GTK_VER}"

echo "== Setup venv =="

setup_venv

echo "== Build glib =="

meson_build_install "glib-${GLIB_VER}" --prefix="${PREFIX}" --libdir=lib --buildtype=release -Dtests=false -Dinstalled_tests=false -Dintrospection=disabled -Dman-pages=disabled
patch_gdbus_codegen

echo "== Build cairo =="

meson_build_install "cairo-${CAIRO_VER}" --prefix="${PREFIX}" --libdir=lib --buildtype=release -Dtests=disabled -Dxlib=disabled -Dxcb=disabled -Dgtk_doc=false -Dfreetype=enabled -Dfontconfig=enabled

echo "== Build harfbuzz =="

meson_build_install "harfbuzz-${HARFBUZZ_VER}" --prefix="${PREFIX}" --libdir=lib --buildtype=release -Dtests=disabled -Dintrospection=disabled -Ddocs=disabled -Dglib=enabled -Dgobject=enabled -Dfreetype=enabled -Dgraphite=enabled -Dcoretext=enabled

echo "== Build pango =="

meson_build_install "pango-${PANGO_VER}" --prefix="${PREFIX}" --libdir=lib --buildtype=release -Dintrospection=disabled -Ddocumentation=false -Dfontconfig=enabled -Dfreetype=enabled

echo "== Build gdk-pixbuf =="

meson_build_install "gdk-pixbuf-${GDK_PIXBUF_VER}" --prefix="${PREFIX}" --libdir=lib --buildtype=release -Dtests=false -Dinstalled_tests=false -Dintrospection=disabled -Ddocs=false -Dman=false -Dpng=enabled -Djpeg=enabled -Dtiff=enabled
update_gdk_pixbuf_cache

echo "== Build librsvg =="

if ! command -v cargo >/dev/null 2>&1; then
  echo "ERROR: cargo is required for librsvg. Install with: brew install rust"
  exit 1
fi

if ! command -v rustc >/dev/null 2>&1; then
  echo "ERROR: rustc is required for librsvg. Install with: brew install rust"
  exit 1
fi

rm -rf "${SRCROOT}/librsvg-${LIBRSVG_VER}/target"
autotools_build_install "librsvg-${LIBRSVG_VER}" --prefix="${PREFIX}" --disable-static --disable-introspection --disable-vala --disable-gtk-doc --enable-pixbuf-loader
update_gdk_pixbuf_cache
check_svg_loader

echo "== Build at-spi2-core =="

meson_build_install "at-spi2-core-${AT_SPI2_VER}" --prefix="${PREFIX}" --libdir=lib --buildtype=release -Dintrospection=disabled -Ddocs=false -Dsystemd_user_dir=disabled

echo "== Build gtk+3 =="

patch_gdbus_codegen
meson_build_install "gtk+-${GTK_VER}" --prefix="${PREFIX}" --libdir=lib --buildtype=release -Dintrospection=false -Dman=false -Dtests=false -Ddemos=false -Dexamples=false -Dx11_backend=false -Dwayland_backend=false -Dquartz_backend=true

echo "== Verify pkg-config prefixes =="

check_prefix glib-2.0
check_prefix cairo
check_prefix harfbuzz
check_prefix harfbuzz-gobject
check_prefix pango
check_prefix pangocairo
check_prefix pangoft2
check_prefix gdk-pixbuf-2.0
check_prefix librsvg-2.0
check_prefix atk
check_prefix atspi-2
check_prefix gtk+-3.0
check_prefix gdk-3.0

echo "== Verify no Homebrew GTK-core leaks =="

check_no_homebrew_core_leaks

echo "== Verify SVG pixbuf loader =="

check_svg_loader

echo "== Final GTK version =="

pkg-config --modversion gtk+-3.0
pkg-config --variable=prefix gtk+-3.0

echo "OK: ${PREFIX} is ready."

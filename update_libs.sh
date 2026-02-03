#!/bin/sh

###############################################################################################################
# Copyright (C) 2026
# Heiko Amft, DL1BZ (Project deskHPSDR)
#
# All code published unter the GPLv3
#
###############################################################################################################

OS_TYPE=$(uname)
SCRIPT_NAME=$(basename "$0")
SRC_DIR="${PWD}"
NR4_DIR="${PWD}/wdsp-libs"
# DO NOT CHANGE TARGET_DIR !!!
TARGET_DIR=$NR4_DIR
CHECK_FILE=".WDSP_libs_updated_V2"

REINSTALL=0

for arg in "$@"; do
  [ "$arg" = "reinstall" ] && REINSTALL=1
done

echo "Build all requirements for WDSP 1.29 with NR3 and NR4 support"
echo ""
echo "This Script $SCRIPT_NAME is running under OS $OS_TYPE"

if [ -f "$SRC_DIR"/"$CHECK_FILE" ] && [ "$REINSTALL" -eq 0 ]; then
  echo ""
  echo "+----------------------------------+"
  echo "| Required libs already updated.   |"
  echo "| No need to run this script again.|"
  echo "+----------------------------------+"
  echo ""
  echo "Exit this script."
  echo ""
  exit 1
fi

if [ -f "$SRC_DIR"/"$CHECK_FILE" ]; then
  rm -f "$SRC_DIR"/"$CHECK_FILE"
fi

if [ "$OS_TYPE" = "Darwin" ]; then
  BREW=junk
  if [ -x /usr/local/bin/brew ]; then
    BREW=/usr/local/bin/brew
  fi

  if [ -x /opt/homebrew/bin/brew ]; then
    BREW=/opt/homebrew/bin/brew
  fi
  if [ "$BREW" = "junk" ]; then
    echo "HomeBrew installation obviously failed..."
    echo "Stopping script $SCRIPT_NAME."
    exit 1
  fi
    $BREW update
    $BREW upgrade
    $BREW install libtool
    $BREW install automake
    $BREW install autoconf
    $BREW install fftw
    $BREW install meson
    $BREW install ninja
    $BREW install wget
else
    sudo apt-get --yes update
    sudo apt-get --yes install build-essential pkg-config
    sudo apt-get --yes install libtool
    sudo apt-get --yes install automake
    sudo apt-get --yes install autoconf
    sudo apt-get --yes install git
    sudo apt-get --yes install libfftw3-dev
    sudo apt-get --yes install meson
    sudo apt-get --yes install ninja-build
    sudo apt-get --yes install wget
    sudo apt-get --yes install llvm
    sudo apt-get --yes install clang
fi

[ -n "$NR4_DIR" ] && [ "$NR4_DIR" != "/" ] || exit 1
rm -rf -- "$NR4_DIR"
mkdir -p -- "$NR4_DIR"

if [ ! -d "$NR4_DIR" ]; then
    echo "Error: '$NR4_DIR' cannot create"
    echo "Stopping script $SCRIPT_NAME."
    exit 1
fi

cd "$NR4_DIR" || exit 1
git clone --depth=1 https://github.com/dl1bz/rnnoise.git || exit 1
if [ ! -d "$NR4_DIR/rnnoise" ]; then
    echo "Error: '$NR4_DIR/rnnoise' download error."
    echo "Stopping script $SCRIPT_NAME."
    exit 1
else
    echo "Installing rnnoise..."
    cd "$NR4_DIR/rnnoise" || exit 1
    ./autogen.sh || exit 1
    ./configure --prefix="$TARGET_DIR" --disable-shared --enable-static || exit 1
    make || exit 1
    make install || exit 1
fi

cd "$NR4_DIR" || exit 1
git clone --depth=1 https://github.com/dl1bz/libspecbleach || exit 1
if [ ! -d "$NR4_DIR/libspecbleach" ]; then
    echo "Error: '$NR4_DIR/libspecbleach' download error."
    echo "Stopping script $SCRIPT_NAME.."
    exit 1
else
    echo "Installing libspecbleach..."
    cd "$NR4_DIR/libspecbleach" || exit 1
    meson setup build --buildtype=release --prefix="$TARGET_DIR" --libdir=lib -Ddefault_library=static || exit 1
    meson compile -C build -v || exit 1
    meson install -C build || exit 1
fi

cd "$SRC_DIR" || exit 1

if [ -f "$TARGET_DIR/lib/libspecbleach.a" ] && [ -f "$TARGET_DIR/lib/librnnoise.a" ]; then
    : > "$SRC_DIR/$CHECK_FILE"
    echo "Library build correct, continue..."
else
    echo "Library build FAILED...EXIT script."
    exit 1
fi

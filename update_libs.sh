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
NR4_DIR="${PWD}/wdsp-nr4-newlibs"
# DO NOT CHANGE TARGET_DIR !!!
TARGET_DIR="/usr/local"

echo "Build all requirements for WDSP 1.29 with NR3 and NR4 support"
echo ""
echo "This Script $SCRIPT_NAME is running under OS $OS_TYPE"

if [ -f "$SRC_DIR"/.WDSP_libs_updated ]; then
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
    sudo apt-get --yes install ninja
    sudo apt-get --yes install wget
fi

if [ -d "$NR4_DIR" ]; then
  rm -fr "$NR4_DIR"
  mkdir -p "$NR4_DIR"
else
  mkdir -p "$NR4_DIR"
fi

if [ ! -d "$NR4_DIR" ]; then
    echo "Error: '$NR4_DIR' cannot create"
    echo "Stopping script $SCRIPT_NAME."
    exit 1
fi

cd "$NR4_DIR"
git clone --depth=1 https://github.com/dl1bz/rnnoise.git
if [ ! -d "$NR4_DIR/rnnoise" ]; then
    echo "Error: '$NR4_DIR/rnnoise' download error."
    echo "Stopping script $SCRIPT_NAME."
    exit 1
else
    echo "Installing rnnoise..."
    cd "$NR4_DIR/rnnoise"
    ./autogen.sh
    ./configure --prefix="$TARGET_DIR"
    make
    sudo make install
fi

cd "$NR4_DIR"
git clone --depth=1 https://github.com/dl1bz/libspecbleach
if [ ! -d "$NR4_DIR/libspecbleach" ]; then
    echo "Error: '$NR4_DIR/libspecbleach' download error."
    echo "Stopping script $SCRIPT_NAME.."
    exit 1
else
    echo "Installing libspecbleach..."
    cd "$NR4_DIR/libspecbleach"
    echo "Remove old lib if exists..."
    sudo rm -f "$TARGET_DIR/lib/"libspecbleach*
    if [ "$OS_TYPE" = "Linux" ]; then
      sudo ldconfig
    fi
    meson setup build --buildtype=release --prefix="$TARGET_DIR" --libdir=lib -Ddefault_library=both
    meson compile -C build -v
    sudo meson install -C build
fi

if [ "$OS_TYPE" = "Linux" ]; then
  sudo ldconfig
fi

cd "$SRC_DIR"

printf '' > "$SRC_DIR"/.WDSP_libs_updated

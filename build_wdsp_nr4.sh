#!/bin/sh

###############################################################################################################
# Copyright (C) 2025
# Heiko Amft, DL1BZ (Project deskHPSDR)
#
# All code published unter the GPLv3
#
###############################################################################################################

OS_TYPE=$(uname)
SCRIPT_NAME=$(basename "$0")
SRC_DIR="${PWD}"
NR4_DIR="${PWD}/nr4"

echo "Build WDSP as shared library with NR3 and NR4 support"
echo ""
echo "Script $SCRIPT_NAME was executed under OS $OS_TYPE"

if [ "$OS_TYPE" = "Darwin" ]; then
  BREW=junk
  if [ -x /usr/local/bin/brew ]; then
    BREW=/usr/local/bin/brew
  fi

  if [ -x /opt/homebrew/bin/brew ]; then
    BREW=/opt/homebrew/bin/brew
  fi
  if [ $BREW == "junk" ]; then
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
else
    sudo apt-get --yes update
    sudo apt-get --yes install libtool
    sudo apt-get --yes install automake
    sudo apt-get --yes install autoconf
    sudo apt-get --yes install git
    sudo apt-get --yes install libfftw3-dev
    sudo apt-get --yes install meson
    sudo apt-get --yes install ninja
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
git clone --depth=1 https://github.com/vu3rdd/rnnoise
if [ ! -d "$NR4_DIR/rnnoise" ]; then
    echo "Error: '$NR4_DIR/rnnoise' download error."
    echo "Stopping script $SCRIPT_NAME."
    exit 1
else
    echo "Installing rnnoise..."
    cd "$NR4_DIR/rnnoise"
    ./autogen.sh
    ./configure --prefix=/usr/local
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
    meson build --buildtype=release --prefix=/usr/local --libdir=lib
    meson compile -C build -v
    sudo meson install -C build
fi

cd "$NR4_DIR"
git clone https://github.com/vu3rdd/wdsp
if [ ! -d "$NR4_DIR/wdsp" ]; then
    echo "Error: '$NR4_DIR/wdsp' download error."
    echo "Stopping script $SCRIPT_NAME."
    exit 1
else
    echo "Installing patched WDSP library..."
    cd "$NR4_DIR/wdsp"
    git checkout fe7f2a5b13da20276056b38683ef29a6f6dfba3e
    make NEW_NR_ALGORITHMS=1
    sudo make install
fi

cd "$SRC_DIR"
echo ""
echo "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++"
echo "+                                                                     +"
echo "+ Now you can set EXTENDED_NR=ON in the make.config.deskhpsdr for NR4 +"
echo "+                                                                     +"
echo "+ After this you need re-compile deskHPSDR for activate NR3 and NR4   +"
echo "+ Don't forget a make clean before re-compiling                       +"
echo "+                                                                     +"
echo "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++"
echo ""
echo "End of $SCRIPT_NAME"

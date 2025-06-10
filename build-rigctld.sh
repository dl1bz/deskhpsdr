#!/bin/sh

###############################################################################################################
# Copyright (C) 2025
# Heiko Amft, DL1BZ (Project deskHPSDR)
#
# All code published unter the GPLv3
#
###############################################################################################################

# === wir verwenden bis auf Weiteres die Hamlib Version 4.5.6
# === ab der Version 4.6 und höher gibt es Probleme mit dem CW Keying
USE_HAMLIB_456=ON

#
# a shell script to compile hamlib
#
OS_TYPE=$(uname)
HAMLIB_DIR="${PWD}/hamlib-static"
HAMLIB_GIT="https://github.com/Hamlib/Hamlib.git"
SRC_DIR="${PWD}"

if [ ! -d "$HAMLIB_DIR" ]; then
  git clone "$HAMLIB_GIT" "$HAMLIB_DIR"
fi

# === Hamlib-Verzeichnis prüfen ===
if [ ! -d "$HAMLIB_DIR" ]; then
    echo "Fehler: Hamlib-Verzeichnis '$HAMLIB_DIR' existiert nicht!"
    exit 1
fi

cd "$HAMLIB_DIR"

# === wechsle branch auf die Hamlib Version 4.5.6
if [ "$USE_HAMLIB_456" = "ON" ]; then
    git checkout Hamlib-4.5.6
fi


if [ ! -f config.log  -o ! -f Makefile ]; then
./bootstrap
autoreconf -i
# ./configure --enable-static --disable-shared --disable-winradio --without-readline --without-libusb --without-indi --with-cxx-binding
./configure --enable-static --disable-shared --disable-winradio --without-readline --without-libusb --without-indi
fi

if [ "$OS_TYPE" = "Darwin" ]; then
    CPU_CORES=$(sysctl -n hw.physicalcpu)
elif [ "$OS_TYPE" = "FreeBSD" ] || [ "$OS_TYPE" = "OpenBSD" ] || [ "$OS_TYPE" = "NetBSD" ]; then
    CPU_CORES=$(sysctl -n hw.ncpu)
else
    CPU_CORES=$(nproc)
fi

make -j $CPU_CORES -l 4

#
# Test presence of certain  files
#
if [ ! -f $HAMLIB_DIR/include/hamlib/rig.h ]; then
  echo "$HAMLIB_DIR/include/hamlib/rig.h NOT FOUND!" 2>&1
  exit 1
fi
if [ ! -f $HAMLIB_DIR/include/hamlib/config.h ]; then
  echo "$HAMLIB_DIR/include/hamlib/config.h NOT FOUND!" 2>&1
  exit 1
fi
if [ ! -f $HAMLIB_DIR/src/.libs/libhamlib.a ]; then
  echo "$HAMLIB_DIR/src/.libs/libhamlib.a NOT FOUND!" 2>&1
  exit 1
fi

if [ -f $HAMLIB_DIR/tests/rigctld ]; then
  if [ "$OS_TYPE" = "Darwin" ] || [ "$OS_TYPE" = "FreeBSD" ] || [ "$OS_TYPE" = "OpenBSD" ] || [ "$OS_TYPE" = "NetBSD" ]; then
    echo "Copy and rename $HAMLIB_DIR/tests/rigctld => $SRC_DIR/MacOS/rigctld_deskhpsdr" 2>&1
    cp tests/rigctld ../MacOS/rigctld_deskhpsdr
  elif [ "$OS_TYPE" = "Linux" ]; then
    echo "Copy and rename $HAMLIB_DIR/tests/rigctld => $SRC_DIR/LINUX/rigctld_deskhpsdr" 2>&1
    cp tests/rigctld ../LINUX/rigctld_deskhpsdr
  fi
else
  echo "ERROR: $HAMLIB_DIR/tests/rigctld not found or not build !" 2>&1
fi

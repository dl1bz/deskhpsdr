#!/bin/sh

###############################################################################################################
# Copyright (C) 2025
# Heiko Amft, DL1BZ (Project deskHPSDR)
#
# All code published unter the GPLv3
#
###############################################################################################################

################################################################
#
#  Prepare a Debian based Linux system include PiOS for
#  Raspberry Pi for use with deskHPSDR by DL1BZ
#
#  This script was checked with the Debian-based PiOS(64bit)
#  (Bookworm) running on Raspberry Pi5 (4GB & SSD-HAT) with
#  X11-Desktop-Environment
#
#
#  2024, November 21th
#
################################################################
#
#  If you need more Soapy-specfic things or modules, you need
#  to compile and install the needed modules manually.
#
#  This script ONLY install the Soapy core, NOT the modules !
#  Look at https://github.com/pothosware for additional infos
#  and modules you can install if you need.
#
#  Don't forget to activate Soapy-Support in the file
#  make.config.deskhpsdr with SOAPYSDR=ON
#  DON'T EDIT THE Makefile itself !
#
################################################################

################################################################
#
# a) determine the location of THIS script
#    (this is where the files should be located)
#    and assume this is in the deskhpsdr directory
#
################################################################

SCRIPTFILE=`realpath $0`
THISDIR=`dirname $SCRIPTFILE`
TARGET=`dirname $THISDIR`
DESKHPSDR=$TARGET/release/deskhpsdr

RASPI=`cat /proc/cpuinfo | grep Model | grep -c Raspberry`

echo
echo "=============================================================="
echo "Script file absolute position  is " $SCRIPTFILE
echo "deskHPSDR target     directory is " $TARGET
echo "Icons and Udev rules  copied from " $DESKHPSDR

if [ $RASPI -ne 0 ]; then
echo "This computer is a Raspberry Pi!"
fi
echo "=============================================================="
echo

if [ "$(grep -Ei 'debian|buntu|mint' /etc/*release)" ]; then
################################################################
#
# b) install lots of packages
# (many of them should already be there)
#
################################################################

echo "=============================================================="
echo
echo "... installing LOTS OF compiles/libraries/helpers"
echo
echo "=============================================================="

# ------------------------------------
# Install standard tools and compilers
# ------------------------------------

sudo apt-get --yes install build-essential
sudo apt-get --yes install module-assistant
sudo apt-get --yes install vim
sudo apt-get --yes install make
sudo apt-get --yes install gcc
sudo apt-get --yes install g++
sudo apt-get --yes install gfortran
sudo apt-get --yes install git
sudo apt-get --yes install pkg-config
sudo apt-get --yes install cmake
sudo apt-get --yes install autoconf
sudo apt-get --yes install autopoint
sudo apt-get --yes install gettext
sudo apt-get --yes install automake
sudo apt-get --yes install libtool
sudo apt-get --yes install cppcheck
sudo apt-get --yes install dos2unix
sudo apt-get --yes install libzstd-dev
sudo apt-get --yes install python3-dev
sudo apt-get --yes install wget
sudo apt-get --yes install meson
sudo apt-get --yes install ninja

# ---------------------------------------
# Install libraries necessary for deskHPSDR
# ---------------------------------------

sudo apt-get --yes install libfftw3-dev
sudo apt-get --yes install libgtk-3-dev
sudo apt-get --yes install libwebkit2gtk-4.0-dev
sudo apt-get --yes install libwebkit2gtk-4.1-dev
sudo apt-get --yes install libasound2-dev
sudo apt-get --yes install libssl-dev
sudo apt-get --yes install libcurl4-openssl-dev
sudo apt-get --yes install libusb-1.0-0-dev
sudo apt-get --yes install libi2c-dev
sudo apt-get --yes install libgpiod-dev
sudo apt-get --yes install libpulse-dev
sudo apt-get --yes install pulseaudio
sudo apt-get --yes install libpcap-dev
sudo apt-get --yes install libjson-c-dev
sudo apt-get --yes install gnome-themes-extra

# ----------------------------------------------
# Install standard libraries necessary for SOAPY
# ----------------------------------------------

sudo apt-get install --yes libaio-dev
sudo apt-get install --yes libavahi-client-dev
sudo apt-get install --yes libad9361-dev
sudo apt-get install --yes libiio-dev
sudo apt-get install --yes bison
sudo apt-get install --yes flex
sudo apt-get install --yes libxml2-dev
sudo apt-get install --yes librtlsdr-dev

################################################################
#
# c) download and install SoapySDR core
#
################################################################

echo "=============================================================="
echo
echo "... installing SoapySDR core"
echo
echo "=============================================================="

cd $THISDIR
yes | rm -r SoapySDR
git clone https://github.com/pothosware/SoapySDR.git

cd $THISDIR/SoapySDR
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(nproc)
sudo make install
sudo ldconfig

elif  [ "$(grep -Ei 'fedora' /etc/*release)" ]; then

################################################################
#
# Fedora installation support added by Heath, NQ7T  - not by me
#
# I (DL1BZ) don't give any support as Maintainer of the deskHPSDR project
# for *not* Debian based distributions !
#
################################################################

sudo dnf group install -y c-development
sudo dnf -y install git gcc-gfortran gettext cppcheck dos2unix \
libzstd-devel python3-devel fftw-devel gtk3-devel \
openssl-devel alsa-lib-devel libcurl-devel  libusb1-devel \
libgpiod-devel  pulseaudio-libs-devel  libpcap-devel  \
json-c-devel  gnome-themes-extra  SoapySDR-devel webkit2gtk4.1-devel

else
	echo "This script is only for Debian and Fedora based or similiar LINUX distributions"
	echo "You need to prepare your Linux system by another way or manually."
	echo "Here we can't help, sorry...exiting this script"
	exit 1
fi

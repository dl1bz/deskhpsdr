#!/bin/sh

###############################################################################################################
# Copyright (C) 2025
# Heiko Amft, DL1BZ (Project deskHPSDR)
#
# All code published unter the GPLv3
#
###############################################################################################################

#####################################################
#
# prepeare your Macintosh for compiling deskHPSDR
#
######################################################

#
# This installs the "command line tools", these are necessary to install the
# homebrew universe
#
# xcode-select --install

if ! xcode-select -p >/dev/null 2>&1; then
    read -p "Xcode command line tools not installed, but required. Install now? (Y/n) " answer_clt
    case "$answer_clt" in
        [Nn])
            echo "Xcode CLT will be not installed. Abort script prep_macos.sh ..."
            exit 1
            ;;
        *)
            echo "Install Xcode command line tools..."
            xcode-select --install
            ;;
    esac
fi

################################################################
#
# a) MacOS does not have "realpath" so we need to fiddle around
#
################################################################

THISDIR="$(cd "$(dirname "$0")" && pwd -P)"

################################################################
#
# b) Initialize HomeBrew and required packages
#    (this does no harm if HomeBrew is already installed)
#
################################################################

#
# This installes the core of the homebrew universe
#
# updated install URL, show https://brew.sh/
#
# /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

if ! command -v brew >/dev/null 2>&1; then
    echo "Homebrew is not installed."
    read -p "Install Homebrew now ? (Y/n) " answer_hb
    case "$answer_hb" in
        [Nn])
            echo "Homebrew will not be installed. Abort script prep_macos.sh ..."
            exit 1
            ;;
        *)
            echo "Install Homebrew now..."
            /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
            ;;
    esac
fi

#
# At this point, there is a "brew" command either in /usr/local/bin (Intel Mac) or in
# /opt/homebrew/bin (Silicon Mac). Look what applies, and set the variable OPTHOMEBREW to 1
# if homebrew is installed in /opt/homebrew rather than in /usr/local
#
BREW=junk
OPTHOMEBREW=0

if [ -x /usr/local/bin/brew ]; then
  BREW=/usr/local/bin/brew
fi

if [ -x /opt/homebrew/bin/brew ]; then
  BREW=/opt/homebrew/bin/brew
  OPTHOMEBREW=1
fi

if [ $BREW == "junk" ]; then
  echo HomeBrew installation obviously failed, exiting
  exit
fi

################################################################
#
# This adjusts the PATH. This is not bullet-proof, so if some-
# thing goes wrong here, the user will later not find the
# 'brew' command.
#
################################################################

if [ $SHELL == "/bin/sh" ]; then
$BREW shellenv sh >> $HOME/.profile
fi
if [ $SHELL == "/bin/csh" ]; then
$BREW shellenv csh >> $HOME/.cshrc
fi
if [ $SHELL == "/bin/zsh" ]; then
$BREW shellenv zsh >> $HOME/.zprofile
fi

################################################################
#
# create links in /usr/local if necessary (only if
# HomeBrew is installed in /opt/homebrew)
#
# Should be done HERE if some of the following packages
# have to be compiled from the sources
#
# Note existing DIRECTORIES in /usr/local will not be deleted,
# the "rm" commands only remove symbolic links should they
# already exist.
################################################################

if [ $OPTHOMEBREW == 0 ]; then
  # we assume that bin, lib, include, and share exist in /usr/(local
  if [ ! -d /usr/local/share/deskhpsdr ]; then
    echo "/usr/local/share/deskhpsdr does not exist, creating ..."
    # this will (and should) file if /usr/local/share/deskhpsdr is a symbolic link
    mkdir /usr/local/share/deskhpsdr
  fi
else
  if [ ! -d /usr/local/lib ]; then
    echo "/usr/local/lib does not exist, creating symbolic link ..."
    sudo rm -f /usr/local/lib
    sudo ln -s /opt/homebrew/lib /usr/local/lib
  fi
  if [ ! -d /usr/local/bin ]; then
    echo "/usr/local/bin does not exist, creating symbolic link ..."
    sudo rm -f /usr/local/bin
    sudo ln -s /opt/homebrew/bin /usr/local/bin
  fi
  if [ ! -d /usr/local/include ]; then
    echo "/usr/local/include does not exist, creating symbolic link ..."
    sudo rm -f /usr/local/include
    sudo ln -s /opt/homebrew/include /usr/local/include
  fi
  if [ ! -d /usr/local/share ]; then
    echo "/usr/local/share does not exist, creating symbolic link ..."
    sudo rm -f /usr/local/share
    sudo ln -s /opt/homebrew/share /usr/local/share
  fi
  if [ ! -d /opt/homebrew/share/deskhpsdr ]; then
    echo "/opt/homebrew/share/deskhpsdr does not exist, creating ..."
    # this will (and should) file if /opt/homebrew/share/deskhpsdr is a symbolic link
    sudo mkdir /opt/homebrew/share/deskhpsdr
  fi
fi
################################################################
#
# All homebrew packages needed for deskhpsdr
#
################################################################
$BREW install gtk+3
$BREW install librsvg
$BREW install pkg-config
$BREW install portaudio
$BREW install fftw
$BREW install openssl@3
$BREW install libusb
$BREW install json-c
$BREW install wget
$BREW install perl
$BREW install libtool
$BREW install automake
$BREW install autoconf
$BREW install meson
$BREW install ninja
################################################################
#
# This is for the SoapySDR universe
# There are even more radios supported for which you need
# additional modules, for a list, goto the web page
# https://formulae.brew.sh
# and insert the search string "pothosware". In the long
# list produced, search for the same string using the
# "search" facility of your internet browser
#
$BREW install cmake
$BREW install python-setuptools
#
# If an older version of SoapySDR exist, a forced
# re-install may be necessary (note parts of this
# is always compiled from the sources).
#
$BREW tap pothosware/pothos
$BREW reinstall soapysdr
#
# We don't install specific Soapy device support anymore, that's users own task !
#
# $BREW reinstall pothosware/pothos/soapyplutosdr
# $BREW reinstall pothosware/pothos/limesuite
# $BREW reinstall pothosware/pothos/soapyrtlsdr
# $BREW reinstall pothosware/pothos/soapyairspy
# $BREW reinstall pothosware/pothos/soapyairspyhf
# $BREW reinstall pothosware/pothos/soapyhackrf
# $BREW reinstall pothosware/pothos/soapyredpitaya
# $BREW reinstall pothosware/pothos/soapyrtlsdr

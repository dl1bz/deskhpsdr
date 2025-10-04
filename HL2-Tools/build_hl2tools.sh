#!/bin/sh

###############################################################################################################
# Copyright (C) 2025
# Heiko Amft, DL1BZ (Project deskHPSDR)
#
# All code published unter the GPLv3
#
###############################################################################################################

SRC_DIR="${PWD}"
OS_TYPE=$(uname)

if [ "$OS_TYPE" = "Darwin" ]; then
    CC=cc
else
    CC=gcc
fi

echo "-------------------------------------------------------------------------" 2>&1
echo "Build some useful CLI network tools for the Hermes Lite 2 SDR transceiver" 2>&1
echo "-------------------------------------------------------------------------" 2>&1

echo "OS: $OS_TYPE" 2>&1
echo "used compiler: $CC" 2>&1

echo "Build $SRC_DIR/hl2_ip_tool" 2>&1
$CC -std=c11 -O2 -D_DEFAULT_SOURCE -Wall $SRC_DIR/hl2_ip_tool.c -o $SRC_DIR/hl2_ip_tool

echo "Build $SRC_DIR/hl2_eeprom_discovery" 2>&1
$CC -std=c11 -O2 -D_DEFAULT_SOURCE -Wall $SRC_DIR/hl2_eeprom_discovery.c -o $SRC_DIR/hl2_eeprom_discovery

echo "Done." 2>&1

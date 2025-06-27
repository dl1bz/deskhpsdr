#!/bin/bash

# This script is made for the deskHPSDR project, maintained by Heiko/DL1BZ
# https://github.com/dl1bz/deskhpsdr.git
#
# PipeWire is required for this script, since Debian 12 "Bookworm" PipeWire is installed per default
#
# if not, try "sudo apt install pipewire pipewire-pulse" for install PipeWire on Debian-based Linux systems
#

if [ "$(uname)" != "Linux" ]; then
  echo "This script run with LINUX only ! Exit script now."
  exit 1
fi

# Prüfen, ob pactl mit PipeWire arbeitet
if ! pactl info | grep -q "PulseAudio (on PipeWire"; then
  echo "pactl don't use PipeWire – possible running only PulseAudio"
  echo "This script need PipeWire. Exit script now."
  exit 1
fi

SINK_NAME_1="VAC_1to2"
SINK_NAME_2="VAC_2to1"

if [ "$1" = "load" ]; then
  pactl load-module module-null-sink sink_name="$SINK_NAME_1"
  pactl load-module module-null-sink sink_name="$SINK_NAME_2"

elif [ "$1" = "unload" ]; then
  MODULE_ID_1=$(pactl list short modules | grep "sink_name=$SINK_NAME_1" | awk '{print $1}')
  [ -n "$MODULE_ID_1" ] && pactl unload-module "$MODULE_ID_1"
  MODULE_ID_2=$(pactl list short modules | grep "sink_name=$SINK_NAME_2" | awk '{print $1}')
  [ -n "$MODULE_ID_2" ] && pactl unload-module "$MODULE_ID_2"

else
  echo "Generate virtual audio sinks $SINK_NAME_1 and $SINK_NAME_2 with PipeWire"
  echo ""
  echo "Use ./vcable.sh [load | unload]"
  echo ""
fi

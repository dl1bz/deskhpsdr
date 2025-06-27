#!/bin/bash

# This script is made for the deskHPSDR project, maintained by Heiko/DL1BZ
# https://github.com/dl1bz/deskhpsdr.git
#
# PipeWire is required for this script, since Debian 12 "Bookworm" PipeWire is installed per default
#
# if not, try "sudo apt install pipewire pipewire-pulse" for install PipeWire on Debian-based Linux systems
#
# for use with systemd copy the vcable.service to $HOME/.config/systemd/user
# systemctl --user daemon-reload
# systemctl --user enable vcable.service
# systemctl --user start vcable.service
# check with systemctl --user status vcable.service
#

if [ "$(uname)" != "Linux" ]; then
  echo "This script runs on LINUX only! Exiting."
  exit 1
fi

# Prüfen, ob pactl mit PipeWire arbeitet
if ! pactl info | grep -q "PulseAudio (on PipeWire"; then
  echo "pactl is not using PipeWire – only PulseAudio detected."
  echo "This script requires PipeWire. Exiting."
  exit 1
fi

SINK_NAME_1="VAC_1to2"
SINK_NAME_2="VAC_2to1"

load_sink() {
  local SINK_NAME=$1
  # Prüfen, ob Modul mit diesem Sink-Name bereits existiert
  if pactl list short modules | grep -q "sink_name=$SINK_NAME"; then
    echo "Sink $SINK_NAME already loaded. Skipping."
  else
    echo "Loading sink: $SINK_NAME"
    pactl load-module module-null-sink sink_name="$SINK_NAME"
  fi
}

unload_sink() {
  local SINK_NAME=$1
  MODULE_ID=$(pactl list short modules | grep "sink_name=$SINK_NAME" | awk '{print $1}')
  if [ -n "$MODULE_ID" ]; then
    echo "Unloading sink: $SINK_NAME (Module ID: $MODULE_ID)"
    pactl unload-module "$MODULE_ID"
  else
    echo "Sink $SINK_NAME not loaded."
  fi
}

case "$1" in
  load)
    load_sink "$SINK_NAME_1"
    load_sink "$SINK_NAME_2"
    ;;
  unload)
    unload_sink "$SINK_NAME_1"
    unload_sink "$SINK_NAME_2"
    ;;
  *)
    echo "Generate virtual audio sinks $SINK_NAME_1 and $SINK_NAME_2 with PipeWire"
    echo ""
    echo "Usage: $0 [load | unload]"
    echo ""
    ;;
esac

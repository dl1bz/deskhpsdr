#!/bin/bash

if [ "$(uname)" != "Linux" ]; then
  echo "This script run with LINUX only ! Exit."
  exit 1
fi

SINK_NAME_1="VAC_1to2"

if [ "$1" = "load" ]; then
  pactl load-module module-null-sink sink_name="$SINK_NAME_1"

elif [ "$1" = "unload" ]; then
  MODULE_ID_1=$(pactl list short modules | grep "sink_name=$SINK_NAME_1" | awk '{print $1}')
  [ -n "$MODULE_ID_1" ] && pactl unload-module "$MODULE_ID_1"

else
  echo "Generate a virtual audio sink $SINK_NAME_1 with PipeWire"
  echo ""
  echo "Use ./vcable.sh [load | unload]"
  echo ""
fi

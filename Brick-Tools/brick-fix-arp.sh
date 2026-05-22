#!/bin/bash

# Brick MAC address must be known
MAC="00:1c:c0:a2:22:5c"

# Command line arguments
IP="$1"
INTERFACE="$2"

# No IP specified -> show status
if [ -z "$IP" ]; then
  echo "Usage: $0 <IP> [INTERFACE]"
  echo
  echo "Current ARP table:"
  arp -a
  exit 0
fi

echo "Removing old ARP entry for $IP ..."
sudo arp -d "$IP" 2>/dev/null || true

echo "Setting static ARP entry for Brick:"
echo "  IP        : $IP"
echo "  MAC       : $MAC"

if [ -n "$INTERFACE" ]; then
  echo "  Interface : $INTERFACE"
  if [ "$(uname)" = "Darwin" ]; then
    echo "Set Brick ARP entry for macOS on interface $INTERFACE ..."
    sudo arp -s "$IP" "$MAC" ifscope "$INTERFACE"
  fi
  if [ "$(uname)" = "Linux" ]; then
    echo "Set Brick ARP entry for Linux on interface $INTERFACE ..."
    sudo arp -s "$IP" "$MAC" -i "$INTERFACE"
  fi
else
  sudo arp -s "$IP" "$MAC"
fi

echo
echo "Current ARP status:"
arp -n "$IP"

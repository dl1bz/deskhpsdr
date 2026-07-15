#!/bin/sh
################################################################
#
# NOTE THIS IS OBSOLETE NOW:
# pre-compiled binaries make more problems than they solve,
# therefore this has now been removed.
#
################################################################
#
# A script to install pihpsdr from pre-compiled binaries.
# This script does not assume that THISDIR is writeable.
# A target directory $HOME/pihpsdr.release is created (if
# it not yet exists), and pihpsdr runs from there.
#
# Note that this script will normally invoked by double-clicking
# from the file manager.
#
################################################################

################################################################
#
# a) determine the location of THIS script
#    and where the installation should go
#
################################################################
SCRIPTFILE=`realpath $0`
THISDIR=`dirname $SCRIPTFILE`
TARGET=$HOME/pihpsdr.release

################################################################
#
# b) remove files to be replaced.
#
################################################################

rm -f $HOME/Desktop/pihpsdr.desktop
rm -f $HOME/.local/share/applications/pihpsdr.desktop

################################################################
#
# c) install possibly missing OS packages
#    (REQUIRES ADMIN PRIVILEGES)
#
################################################################

sudo apt-get -y install libasound2-dev
sudo apt-get -y install libgtk-3-dev
sudo apt-get -y install libfftw3-dev
sudo apt-get -y install libgpiod-dev
sudo apt-get -y install libi2c-dev

################################################################
#
# d) create target (working) directory if it does not exist,
#    if it exists remove files that are to be replaced
#
################################################################

if [ -d $TARGET ]; then 
  echo pihpsdr.release directory exists in home dir
  rm -f $TARGET/hpsdr.png
  rm -f $TARGET/hpsdr_icon.png
  rm -f $TARGET/pihpdsr
  rm -f $TARGET/pihpdsr.sh
else
  echo creating working directory
  rm -f $TARGET
  mkdir $TARGET
fi

################################################################
#
# e) create start script pihpsdr.sh in $TARGET
#
################################################################
echo "creating start script"
cat <<EOT > $TARGET/pihpsdr.sh
cd $TARGET 
$TARGET/pihpsdr >log 2>&1
EOT
chmod +x $TARGET/pihpsdr.sh

################################################################
#
# f) creating desktop shortcuts
#
################################################################
echo "creating desktop shortcut"
cat <<EOT > $TARGET/pihpsdr.desktop
#!/usr/bin/env xdg-open
[Desktop Entry]
Version=1.0
Type=Application
Terminal=false
StartupNotify=false
Name=piHPSDR
Exec=$TARGET/pihpsdr.sh
Icon=$TARGET/hpsdr_icon.png
Name=piHPSDR
EOT

cp $TARGET/pihpsdr.desktop $HOME/Desktop
mkdir -p $HOME/.local/share/applications
cp $TARGET/pihpsdr.desktop $HOME/.local/share/applications

################################################################
#
# g) copy pihpsdr executable and HPSDR icons to $TARGET
#
################################################################
echo Copying executable and icons
cp $THISDIR/pihpsdr $TARGET
cp $THISDIR/hpsdr.png $TARGET
cp $THISDIR/hpsdr_icon.png $TARGET

################################################################
#
# h) copy XDMA rules (only for SATURN/G2)
#    (REQUIRES ADMIN PRIVILEGES)
#
################################################################
echo "copying XDMA udev rules"
sudo cp $THISDIR/60-xdma.rules $THISDIR/xdma-udev-command.sh /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger

################################################################
#
# i) init GPIO lines to "Input Pullup"
#    I guess this is obsolete since newer version of libgpiod
#    should be able to correctly work on the Pi4,
#    on the other hand, this can do little harm
#    (REQUIRES ADMIN PRIVILEGES)
#
################################################################
if test -f "/boot/config.txt"; then
  if grep -q "gpio=4-13,16-27=ip,pu" /boot/config.txt; then
    echo "/boot/config.txt already contains gpio setup."
  else
    echo "/boot/config.txt does not contain gpio setup - adding it."
    echo "Please reboot system for this to take effect."
    cat <<EGPIO | sudo tee -a /boot/config.txt > /dev/null
[all]
# setup GPIO for pihpsdr controllers
gpio=4-13,16-27=ip,pu
EGPIO
  fi
fi

################################################################
#
# ALL DONE.
#
################################################################

# Troubleshooting deskHPSDR for Version 2.7

## Things you can or should do for first-aid

If the application don't work, don't work correctly or crash, here some recommendations.

### 1. Update the code and recompile

The first you need to do is be sure using most up-to-date codebase:<br>
```$ cd deskhpsdr```<br>
```$ git checkout master```<br>
```$ git pull```<br>

If ```git pull``` failed, you can try this:<br><br>
```$ git reset --hard origin/master```<br>
```$ git config pull.rebase true```<br>
```$ git pull```<br>

This reset your local codebase similiar to my repository at github.com. Mostly ```git pull``` failed, if you have done local changes in the codebase.

If nothing helps, delete the whole deskHPSDR source directory and clone again:<br><br>
```$ rm -fr deskhpsdr```<br>
```$ git clone https://git.bzsax.de/dl1bz/deskhpsdr.git```<br>
```$ cd deskhpsdr```<br>

Please don't edit the Makefile direct ! Only do all it in the ```make.config.deskhpsdr```, this file will be used and included in the Makefile.<br>
A template ```make.config.deskhpsdr.template``` is included and can be used, if you make a copy:<br><br>
```$ cp make.config.deskhpsdr.template make.config.deskhpsdr```<br><br>
Edit ```make.config.deskhpsdr``` how you need.

After all, check and follow the instructions written in the ```COMPILE.macOS``` or ```COMPILE.linux``` for compiling deskHPSDR depend on your used OS.

I will permanently update the codebase with bugfixes, so be sure you will be using the last and actual version of codebase. Use ever the master branch, but not the devel branch. The devel branch is a work-in-progress with no guarantee, that the code will be work. Maybe yes, maybe no. The devel branch isn't suitable for production, normal or daily use ! Only the master branch is that, what you must use.

### 1.1 Example of make.config.deskhpsdr

A correct, minimum file ```make.config.deskhpsdr``` look like this as example:
```
MIDI=ON
SATURN=OFF
USBOZY=OFF
STEMLAB=OFF
AUDIO=PULSE
AUTOGAIN=ON
```
Use ```AUTOGAIN=ON``` has only an effect if your SDR is a Hermes Lite 2.<br>
deskHPSDR is made for desktop systems, they all have enough CPU power. But my tests were shown, a Raspberry Pi5 works too without any issues.<br>
**Not defined or non existent options are ever interpreted like ```OFF```**.

### 1.2 Recompile deskHPSDR

Every recompile needs the following step:<br>

```$ make clean && make && make install```<br>

```$ make clean && make install```<br>

Important is ```make clean```, because it removes all old fragments from the last compiling and prevent a mix of old and new.<br>
```make install``` do additional things, which needed deskHPSDR for a clean rumtime (e.g. copy needed fonts and so on).

### 2. Remove the config files if deskHPSDR don't start anymore

deskHPSDR is using for every SDR device a config file, where all settings you have done will be saved and reloaded automaticly. Sometimes this or these file(s) can be wrong for various reasons. If you sure, that deskHPSDR was compiled correct - but don't work correct, try at first to remove these config files.

They are located here:<br>
macOS: ```[home-dir]/Library/Application Support/deskHPSDR/```<br>
Linux: ```[home-dir]/.config/deskhpsdr/```<br>

Close deskHPSDR and remove in the just described directory all *.prop files:<br>
```$ rm *.props```<br>

After removing restart deskHPSDR. **Unfortunately, you need to do again a complete new setup for your used SDR device**. The most problems can be fixed with this action. The config files will be generated new from scratch and old or wrong values won't be imported.
This can be mandatory, if I change code or change variables inside the code.

### 3. Your used OS

It is also important, that your OS is not a very old version and it is up-to-date. I was ever made tests with macOS 15.x and 26.x with my Macs and Linux with PiOS 64bit at my Raspberry Pi5.<br>
Do from time to time this, depend from you OS:
Linux (includes OS and all other updates): ```$ apt-get update && apt-get upgrade```<br>
macOS (OS update do normal at macOS level, but we need to update Homebrew too): ```$ brew update && brew upgrade```<br>

Note: I cannot support old OS - only actual version of the OS. At Linux I can only support Debian-based distributions like Ubuntu, Debian, PiOS. No support for Fedora, ArchLinux and other "special" distributions. Here I can help only very limited.

### 4. Your SDR device

I personally only own a Hermes Lite 2 and a Brick2 as SDR transceiver, connected via Ethernet. The Hermes Lite 2 use the older HPSDR protocol 1 via network and the Brick2 uses protocol 2 via network. **These are my both available SDR devices for testing deskHPSDR here**. Other SDR can work with deskHPSDR, but I cannot check all available SDR devices, that is impossible. The ANAN should be run too, they are HPSDR protocol based SDR.

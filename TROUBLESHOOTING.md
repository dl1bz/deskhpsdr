# Troubleshooting deskHPSDR

## Things you can or should do for first-aid

If the application don't work, don't work correctly or crash, here some recommendations.

### 1. Update the code and recompile

The first you need to do is be sure using most up-to-date codebase:
```$ cd deskhpsdr```
```$ git checkout master```
```$ git pull```

If ```git pull``` failed, you can try this:
```$ git reset --hard origin/master```
```$ git pull```
```$ git update-index --assume-unchanged make.config.deskhpsdr```

This reset your local codebase similiar to my repository at github.com. Mostly ```git pull``` failed, if you have done local changes in the codebase. Please don't edit the Makefile direct ! Only do all it in the ```make.config.deskhpsdr```, this file will be used and included in the Makefile.

After all, check and follow the instructions written in the ```COMPILE.macOS``` or ```COMPILE.linux``` for compiling deskHPSDR depend on your used OS.

I will permanently update the codebase with bugfixes, so be sure you will be using the last and actual version of codebase. Use ever the master branch, but not the devel branch. The devel branch is a work-in-progress with no guarantee, that the code will be work. Maybe yes, maybe no. The devel branch isn't suitable for production, normal or daily use ! Only the master branch is that, what you must use.

### 2. Remove the config files

deskHPSDR is using for every SDR device a config file, where all settings you have done will be saved and reloaded automaticly. Sometimes this or these file(s) can be wrong for various reasons. If you sure, deskHPSDR was compiled correct, but don't work correct, try at first to remove these config files.

They are located here:
macOS: ```[home-dir]/Library/Application Support/deskHPSDR/```
Linux: ```[home-dir]/.config/deskhpsdr/```

Close deskHPSDR and remove in the just described directory all *.prop files:
```$ rm *.props```

After removing restart deskHPSDR. Unfortunately, you need to do again a complete new setup for your used SDR device. The most problems can be fixed with this action. The config files will be generated new from scratch and old or wrong values won't be imported.
This can be mandatory, if I change code or change variables inside the code.

### 3. Your used OS

It is also important, that your OS is not a very old version and it is up-to-date. I was ever made tests with macOS 14.x and 15.x with my Macs and Linux with PiOS 64bit at my Raspberry Pi5.<br>
Do from time to time this, depend from you OS:
Linux (includes OS and all other updates): ```$ apt-get update && apt-get upgrade```<br>
macOS (OS update do normal at macOS level, but we need to update Homebrew too): ```$ brew update && brew upgrade```<br>

Note: I cannot support old OS - only actual version of the OS. At Linux I can only support Debian-based distributions like Ubuntu, Debian, PiOS. No support for Fedora, ArchLinux and other "special" distributions. Here I can help only very limited.

### 4. Your SDR device

I personally only own a Hermes Lite 2 as SDR transceiver, connected via Ethernet and a SDRPlay RSP2Pro as RX-only SDR, connected via USB. The Hermes Lite 2 use the older HPSDR protocol 1 via network, the SDRPlay uses the Soapy-API via USB. **These are my both available SDR devices for testing deskHPSDR here**. Other SDR can work with deskHPSDR, but I cannot check all available SDR devices, that is impossible. I was informed from other users, Pluto SDR and Lime SDR works too. The ANAN should be run too, they are HPSDR protocol based SDR.

### 5. deskHPSDR and piHPSDR

In October 2024 deskHPSDR was born as a code fork from DL1YCF version of piHPSDR. Today the codebase of deskHPSDR is no longer comparable to piHPSDR. They look similar, but they are not. deskHPSDR is now an independent application without any relations to pihpsdr. I have different goals than Christoph/DL1YCF with his pihpsdr. My focus is macOS as OS and the Hermes Lite 2 as SDR device. I am no longer referring to  pihpsdr, it is Thetis. What I want with deskHPSDR is a kind of "Thetis for macOS". That's what drives me in the whole development of deskHPSDR. So please don't ask me things about pihpsdr, that is the wrong address. Ask me about deskHPSDR and I will answer. Use the issues tab at Github.com or contact me via EMail. Normally I will answer fast. If I can help, I will do it.
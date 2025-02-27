# deskHPSDR

This is an improved version based at the code of [piHPSDR](https://github.com/dl1ycf/pihpsdr). But it is not [piHPSDR](https://github.com/dl1ycf/pihpsdr) itself and has no backward dependencies to [piHPSDR](https://github.com/dl1ycf/pihpsdr). My goal was to make an optimzed version running with Desktop-OS like Linux and macOS, what means I don't support small displays less as 1280x600 like such for Raspberry Pi or similiar devices. In this case you need to use [piHPSDR](https://github.com/dl1ycf/pihpsdr), but not my deskHPSDR.

My version here need a screen size 1280x600 at minimum or higher for best GUI experiences.

More information about the develop progress of deskHPSDR can be found here in my Blog:<br>
[https://hamradio.bzsax.de/category/hamradio/deskhpsdr/](https://hamradio.bzsax.de/category/hamradio/deskhpsdr/) (German language only).

## deskHPSDR was splitting October 2024 from the code base of piHPSDR

piHPSDR was first developed by John Melton, G0ORX/N6LYT a few years ago.<br>Later Christoph, DL1YCF, had continued the development of piHPSDR. His version [https://github.com/dl1ycf/pihpsdr](https://github.com/dl1ycf/pihpsdr) is the most up-to-date version of piHPSDR and  is actively being developed by him up to now.<br><br>So his codebase of piHPSDR was my starting point end of October, 2024. But anyway, there is and will be no direct collaboration between piHPSDR and deskHPSDR.<br><br>
Today deskHPSDR go an entire own way. deskHPSDR has got many new functions that are not available in piHPSDR. Things that deskHPSDR doesn't need have also been removed, they exist furthermore in piHPSDR, but are no longer as parts of deskHPSDR. deskHPSDR is an evolution of piHPSDR with  completely different objectives.

From now on (January 2025) I stop merging code from piHPSDR into deskHPSDR. Last changes from piHPSDR like the G2Panel and other things are not available in deskHPSDR and won't be. My future development has the focus at the Hermes Lite 2 SDR, but not for devices come from Apache Labs. My meaning to this manufacturer is not the best, they only sell expensive hardware and let the open source developers do the work for the SDR applications which can be used with their hardware. I did not support this approach. For the Hermes Lite 2 most issues are fixed now in deskHPSDR. There are no more recognizable problems with the HL2.

## Requirements

* a modern Desktop-OS like Linux or macOS with installed developer tools like compiler, linker etc.
* a large screensize starts at 1280x600 or higher
* basic knowledge: how to use your OS, a shell, a text editor and how to compile applications from source code
* *macOS only*: please read the ```COMPILE.macOS``` first
* *Linux only*: please read the ```COMPILE.linux``` first
* a SDR device or transceiver, which supports HPSDR protocol 1 (older) or 2 (newer) like the Hermes Lite 2, the ANAN or similiar devices
* a very good running network without any issues (Ethernet preferred, WiFi not recommended) and an DHCP server inside (without DHCP is possible too, but more complicated or difficult working with the SDR devices)
* for Hermes Lite 2 specific notes look into the ```Notes_if_using_HERMES-Lite-2.md```

**Important:** For best desktop experience please select **VFO bar for 1280px windows** in the *Menu->Screen* (if not selected).

## The further development of deskHPSDR

My work is not completed (are software projects ever finished ???). I have some ideas, what I need to add too. You need to understand this branch as "work in progress". I ever check my code here with my test environment: Intel iMac 21" i5 and Macbook Air M1 running both with macOS 14.7.1 aka Sonoma and my SDR tranceiver Hermes-Lite 2 in combination with my homebrew-LDMOS-PA 600W. My focus is Fonie/SSB and Digimode/FT8+FT4, less CW. And - sorry guys - I have not the time to write any kind of manual for deskHPSDR. Use instead the [published manual of DL1YCF's piHPSDR version](https://github.com/dl1ycf/pihpsdr/releases/) for basic knowledge, how this application works in general.

## Latest Changes
### Version 2.5.x
- completed: if using Hermes-Lite 2 activate CL1 input for inject external 10 MHz reference (e.g. with a GPSDO)
- completed: add (if in duplex mode) the audio level monitoring in the separated TX window too (feature request by CU2ED)
- completed: remove "horizontal stacking" display option for panadapter in screen menu, that disturbs a lot of my changed GUI design
- completed: remove all old piHPSDR Client-Server code, deskHPSDR doesn't support this "One-App-Limited" Client-Server concept
- completed: fix some display errors if horizontal screen resolution >= 1280px and duplex selected (issue by CU2ED, tnx)
- completed: show own callsign in upper left corner of RX panadapter, callsign is configurable in Radio menu (feature request by CU2ED)
- completed: show S meter values if using analogue S meter (feature request by CU2ED)
- completed: if using Hermes Lite 2 TX power slider show now TX output in W (step-size 0.1W) instead of a scale between 0-100
- completed: add more keyboard shortcuts (feature request by DH0DM)
- under development: begin implementation to full-automatic control the RxPGA gain if using a Hermes Lite 2
- completed: merge the new PEAK label feature from piHPSDR into deskHPSDR and add an option for show the peak label as S-Meter values instead of dbm
- completed: add an option for using 3 Mic profiles, which can save and load different audio settings (CFC, TX-EQ, Limiter, Basebandcompressor) if using different types of Mics with special settings per Mic. Load and save is only possible in modes LSB, USB or DSB. Other modes are not supported. If mode is DIGL or DIGU the access to the RX- and TX-EQ is now blocked, because is it important that the frequency spectrum is not manipulated if using digi modes.
- under development: implementation of a TCI Server, which emulates a SunSDR2Pro device (successful tested with JTDX, RumLogNG, MacLoggerDX)<br>
- completed: add an additional serial device interface option, which can switch ON the RTS and/or DTR signal line during TUNE function and PTT output (feature request by DD8JM for TUNE and by CU2ED for PTT output)
- under development: sereral GUI improvements for show additional status infos on the screen<br>
- under development: make deeper access possible to the whole audio tools like CFC, Compressor, phase rotator for the user<br>
- completed: add an additional, adjustable up to +20db, AF preamp for increasing mic input level if required<br>
- under development: automatic switching of different audio inputs depends from the selected mode with automatic save settings<br>
- completed: remove most of the limitations at 60m band (remove channelizing and other non-essential things)<br>
- completed: add new UDP listener for my RX200 ESP32 project, which send via UDP broadcast data in JSON<br>
  format like forward and reflected power, SWR and show the received and parsed data onscreen<br>
  as panadapter overlay (need now json-c as additional lib, so please install it)<br>
- completed: add new serial device, which can be used as an external PTT input using RTS & CTS signaling similar like Thetis

Things marked as "under development" are not fully tested and have maybe issues.<br>
Things marked as "completed" are tested a longer time and will work without known issues up to now.<br>

Most of the new functions need to be activated in the ```make.config.deskhpsdr``` as compiling option. Please look in the beginning of the  ```Makefile``` and set the needed options only in ```make.config.deskhpsdr```, but don't modify the ```Makefile``` itself !

## Issues tab at Github for this project - read carefully !

I'm now activate the Issues tab, but please note the following:<br>
- I ONLY accept error reports or runtime errors for this published codebase here. The Issues tab not suitable for discussion about other things except error messages or error reports.<br>
- Don't ask about feature requests, questions about porting to other systems like WIN/MinGW and support for additional hardware - my answer will be ever NO. Such requests will be ignored and closed without any comment.<br>
- first make a ```git pull``` for using the most up-to-date codebase, compile it, test it and THEN open an issue, if you think there's something wrong with the last codebase<br>
- I don't accept any questions, comments or remarks about transmitting outside of the amateur radio frequencies !!!

## Known problems if using Git for update the code base at your local computer

In the ```Makefile``` I add a comment "don't edit this Makefile". That's I mean so. I'm now add the editable, additional file for this called ```make.config.deskhpsdr```.<br>
But if you have such file yet or edit it and make after this a ```git pull``` , git maybe come back with an error message.<br>
 In this case try this:<br>
```
$ mv make.config.deskhpsdr make.config.deskhpsdr.save
$ git pull
$ rm make.config.deskhpsdr
$ mv make.config.deskhpsdr.save make.config.deskhpsdr
$ git update-index --assume-unchanged make.config.deskhpsdr
```
After this, ```git pull``` should work correct.<br>
Background about this: I made a mistake in the ```.gitignore```, but I correct it in the meantime. ```git pull``` see local changes with this file (if edit) and stop working, because this file is not identical with the file from the upstream master branch.<br>
```git update-index --assume-unchanged make.config.deskhpsdr``` inform git, that this file need to be ignored in the future, so you can edit it how you need.<br>
If this not help, please delete the complete codebase of deskHPSDR and clone it again, then you have a fresh copy.<br>

If ```git pull``` failed, you can also try this:<br>
```
$ git pull
$ git reset --hard origin/main
```
This overwrite local changes, which are different from the remote repo at Github.com and set the status equal between local and remote.

## Known problems with SDR devices
* if using SOAPY-API with SDRPlay RSP2Pro (older model, EOL) via USB, deskHPSDR crash with a segmentation fault if try to start this device (issue is actual under investigation, but not fixed yet)

## Successful and confirmed Tests I had done up to now

So far, deskHPSDR has been successfully tested on the following systems:<br>
* iMac 21" i5 running macOS 14.7.1 aka Sonoma
* Macbook Air M1 running macOS 14.7.1 aka Sonoma
* Raspberry Pi5 with NVMe-HAT running 64bit PiOS and X11 environment
* *Raspberry Pi 3B+ works too, but with limitations (panadapter framerate only 10fps, if want more the CPU hasn't enough power)*
* a hamradio friend of mine has checked it on a Desktop Linux Ubuntu LTS for me, works too

All radio tests are made with my Hermes Lite 2 SDR-Transceiver using HPSDR protocol V1. Actual no issues with the Hermes Lite 2 and deskHPSDR.

## Credits

Big thanks and huge respect to all involved developers for their previous great work on piHPSDR until now and make this application accessible as Open Source under the GPL. Many thanks also to the users who gave me feedback and reported issues which I hadn't noticed by myself.

## Exclusion of any Guarantee and any Warrenty

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

All what you do with this code is at your very own risk. The code is published "as it is" without right of any kind of support or similiar services.

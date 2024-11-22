# deskHPSDR

This is an improved version based at the code of [piHPSDR](https://github.com/dl1ycf/pihpsdr). But it is not [piHPSDR](https://github.com/dl1ycf/pihpsdr) itself. My goal was to make an optimzed version running with Desktop-OS like Linux and macOS, what means I don't support small devices like Rasperry Pi or similiar devices. In this case you need to use [piHPSDR](https://github.com/dl1ycf/pihpsdr), but not deskHPSDR.

My version here need a screen size 1280x720 at minimum or higher for best GUI experiences.

## deskHPSDR start with the code base from piHPSDR

piHPSDR was first developed by John Melton, G0ORX/N6LYT a few years ago. Later Christoph, DL1YCF, had continued the development of piHPSDR. His version [https://github.com/dl1ycf/pihpsdr](https://github.com/dl1ycf/pihpsdr) is the most up-to-date version of piHPSDR and  is actively being developed by him up to now. So his codebase of piHPSDR was my starting point a few weeks ago.

## Requirements

* a modern Desktop-OS like Linux or macOS with installed developer tools like compiler, linker etc.
* a large screensize starts at 1280x720 or higher
* basic knowledge: how to use your OS, a shell, a text editor and how to compile applications from source code
* *macOS only*: please read the ```COMPILE.macOS``` first
* a SDR device or transceiver, which supports HPSDR protocol 1 (older) or 2 (newer) like the Hermes Lite 2, the ANAN or similiar devices
* a very good running network without any issues (Ethernet preferred, WiFi not recommended) and an DHCP server inside (without DHCP is possible too, but more complicated or difficult working with the SDR devices)

**Important:** For best desktop experience please select **VFO bar for 1280px windows** in the *Menu->Screen* (if not selected).

## The development of deskHPSDR continues...

My work is not completed. I have some ideas, what I need to add too. You need to understand this branch as "work in progress". I ever check my code here with my test environment: Intel iMac 21" i5 and Macbook Air M1 running both with macOS 14.7.1 aka Sonoma and my SDR tranceiver Hermes-Lite 2 in combination with my homebrew-LDMOS-PA 600W. My focus is Fonie/SSB and Digimode/FT8+FT4, less CW. I cannot check the code against Linux - I don't use it, only macOS. But normally all should be run with Linux too. And - sorry guys - I have not the time to write any kind of manual for deskHPSDR. Use instead the [published manual of DL1YCF's piHPSDR version](https://github.com/dl1ycf/pihpsdr/releases/download/current/piHPSDR-Manual.pdf) for basic knowledge, how this application works in general.

## Issues

I'm now activate the Issues tab, but please note the following:<br>
- I ONLY accept error reports for this published codebase here. The Issues tab not suitable for discussion about other things except error messages.<br>
- Don't ask about feature requests, questions about porting to other systems like WIN/MinGW and support for additional hardware - my answer will be ever NO. Such requests will be ignored and closed without any comment.<br>
- first make a git pull for using the most up-to-date codebase, compile it, test it and THEN open an issue, if there something wrong with the last codebase<br>

## Credits

Big thanks and huge respect to all involved developers for their previous great work on piHPSDR until now and make this application accessible as Open Source under the GPL.

## Exclusion of any Guarantee and any Warrenty

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

All what you do with this code is at your very own risk. The code is published "as it is" without right of any kind of support or similiar services.

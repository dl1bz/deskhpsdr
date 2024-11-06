# deskHPSDR

This is an improved version based at the code of [piHPSDR](https://github.com/dl1ycf/pihpsdr). But it is not [piHPSDR](https://github.com/dl1ycf/pihpsdr) itself. My goal was to make an optimzed version running with Desktop-OS like Linux and macOS, what means I don't support small devices like Rasperry Pi or similiar devices. In this case you need to use [piHPSDR](https://github.com/dl1ycf/pihpsdr), but not deskHPSDR.

My version here need a screen size 1280x720 at minimum or higher for best GUI experiences.

## piHPSDR as the code base

piHPSDR was first developed by John Melton, G0ORX/N6LYT a few years ago. Later Christoph, DL1YCF, had continued the development of piHPSDR. His version [https://github.com/dl1ycf/pihpsdr](https://github.com/dl1ycf/pihpsdr) is the most up-to-date version of piHPSDR and  is actively being developed by him up to now. So his codebase of piHPSDR was my starting point a few weeks ago.

## Why deskHPSDR ?

In the last time Christoph/DL1YCF has added some important features at piHPSDR. piHPSDR based at the great library WDSP, which was developed by Dr. Warren C. Pratt, NR0V a few years ago. You can say, piHPSDR is a kind of GUI frontend for this library. The most powerful SDR application for WINDOWS, OpenHPSDR-Thetis (earlier also known as PowerDSR) use the same WDSP library and is also a kind of GUI frontend. But Thetis is only availible for WINDOWS. So piHPSDR is the similiar alternative for other OS like Linux or macOS too and and basically offers the same options and results.

But some features of the WDSP library are not fully implemented in piHPSDR. So some features like the Leveler, the Phase Rotator are build-in, but without any access to the parameter of this functions. DL1YCF has a lot of this "hard-coded", which limits the possible use of this functions too much.

I don't like such "hard-coding" things, so I decide to make a more improved version of piHPSDR, which I called deskHPSDR for a clear demarcation of my version against piHPSDR. My focus are Desktops with macOS, because there is the selection of good SDR applications very poor, or Linux. I own a Hermes Lite 2 as my running hamradio station, use Thetis with WINDOWS with it but want a solution with my Mac's too, which is very near to Thetis. piHPSDR has this potential but not yet using it to its full extent. I study the source code of piHPSDR and had decided, I do this by myself. My knowledge in C programming is good enough for do this. So deskHPSDR was born a few weeks ago.

## Requirements

* a modern Desktop-OS like Linux or macOS with installed developer tools like compiler, linker etc.
* a large screensize starts at 1280x720 or higher
* basic knowledge: how to use your OS, a shell, a text editor and how to compile applications from source code
* a SDR device or transceiver, which supports HPSDR protocol 1 (older) or 2 (newer) like the Hermes Lite 2, the ANAN or similiar devices
* a very good running network without any issues (Ethernet preferred, WiFi not recommended) and an DHCP server inside (without DHCP is possible too, but more complicated or difficult working with the SDR devices)

**Important:** For best desktop experience please select **VFO bar for 1280px windows** in the Menu->Screen (if not selected).

## Credits

Big thanks and huge respect to all involved developers for their previous great work on piHPSDR until now and make this application accessible as Open Source under the GPL.

## Exclusion of any Guarantee and any Warrenty

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

All what you do with this code is at your very own risk. The code is published "as it is" without right of any kind of support or similiar services.

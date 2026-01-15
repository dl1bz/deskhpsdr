# deskHPSDR by DL1BZ 

**This fork by DANIEL PECHMANN, PU2ODM implements 3DSS (3D waterfall display)**

<img src="https://github.com/dl1bz/deskhpsdr/blob/master/release/deskhpsdr/screenshot.png" width="1024px" />

## The concept behind - what it's make for and what it isn't for

It's a hamradio application for SDR devices using the HPSDR protocol 1 (aka "old protocol") or 2 (aka "new protocol") and based at previous codeparts of [piHPSDR](https://github.com/dl1ycf/pihpsdr) since it was forked from piHPSDR in October 2024. But anywhere, it's no more piHPSDR yet and has no backward dependencies to piHPSDR. My goal was to make an more optimzed version running with Desktop-OS like Linux and macOS, what means I don't support small displays less as 1280x600 and SoCs like the Raspberry Pi or similar devices are not within the scope of this application. In the case mini-display you need to fallback to piHPSDR, deskHPSDR don't support this anymore.

**deskHPSDR is a dedicated SDR transceiver frontend application using OpenHPSDR protocols 1 or 2 for everyday use in amateur radio. Limited additional SoapySDR support is also available.**

The focus is clear fonie/SSB & digimodes, less CW. deskHPSDR has more added options integraded from the WDSP library like pihpsdr, especially tools for the audio chain, and they are all user-acessible and user-adjustable (pihpsdr has many things only "hardcoded" without user-access). deskHPSDR support **max. two RX**, although some SDR hardware supports more, like the Hermes Lite 2 with up to four RX slices. SoapySDR API is supported, but is not actively developed further.

deskHPSDR not made as a "measurement tool" or for other, very special purposes where SDR devices are used. There are other, more specialized apps for such cases - use these for your special purposes. It's a - not more, not less - SDR transceiver GUI frontend for use in hamradio which will be actively and continuously developed. All things outside the hamradio universe will be not supported generally. The support for commercial SDR products is very limited, because they are mostly not Open Source hardware like the Hermes Lite 2. I do not see myself as obligated to support such SDRs or manufacturers while they make money from their hardware. deskHPSDR is and remains open source. deskHPSDR is a non-commercial hobby software project, which can be used completely free without any kind of payments.

**deskHPSDR need a screen size 1280x600 at minimum or higher** for best GUI experiences, that's one of the difference against piHPSDR. deskHPSDR hasn't a special Client-Server-Mode like pihpsdr (make no sense, we HAVE network-connected SDR devices yet).

My main focus of deskHPSDR development, the improvements and additional functions against piHPSDR is the Hermes Lite 2 SDR Transceiver and his similar devices based on the same hardware platform (like the SquareSDR). Other SDR devices can be used too if supported, but I cannot check it. The second focus is macOS, which is my primary development environment for deskHPSDR. Normally all should be running with Linux too. The third focus is Fonie/SSB/Digimodes and less CW. This SDR software app is made for SDR transceiver used in Hamradio as daily-used app, less for special operations with wide-range RX-only SDR devices. If you agree with me and my ideas, deskHPSDR can be very useful for you. If not, look around for other solutions.

## deskHPSDR was forked once from pihpsdr

piHPSDR was [initiated and first developed](https://github.com/g0orx/pihpsdr) by [John Melton, G0ORX/N6LYT](https://github.com/g0orx) a few years ago.<br>Later Christoph, DL1YCF, had continued the development of piHPSDR. His fork [https://github.com/dl1ycf/pihpsdr](https://github.com/dl1ycf/pihpsdr) is the most up-to-date and current version of piHPSDR today and being actively developed by him up to now.<br><br>So his codebase of piHPSDR was my starting point end of October, 2024. But anyway, there is and will be no direct collaboration between piHPSDR and deskHPSDR.<br><br>
Today deskHPSDR go an entire own way. deskHPSDR has got many new functions they are not available in piHPSDR. Things that deskHPSDR doesn't need have also been removed (e.g. the build-in client-server-mode), they exist furthermore in piHPSDR, but are no longer as parts of deskHPSDR. deskHPSDR is now a kind of evolution from piHPSDR with completely different objectives.

## Requirements

* modern Desktop-OS like Linux or macOS with installed developer tools like compiler, linker etc.
* minimum screensize starts from 1280x600 or higher resolutions
* **basic knowledge**: how to use your OS, a shell, a text editor and how to compile applications from source code
* *macOS only*: please read the ```COMPILE.macOS``` first
* *Linux only*: please read the ```COMPILE.linux``` first
* a SDR device or SDR transceiver, which supports HPSDR protocol 1 (older) like the Hermes Lite 2 or protocol 2 (newer) like the ANAN or similiar devices. Soapy-API based SDR can be used too, but with limitations because of the Soapy-API implementation. Per default Soapy-API is disabled, if needed you must activate Soapy-API support in the ```make.config.deskhpsdr``` as an user-defined option. In fact, support the Soapy-API has no priority and no focus in deskHPSDR and that will not change in future.
* a very good running network without any issues (Ethernet preferred, WiFi not recommended) and a DHCP server inside (without DHCP is possible too, but more complicated or difficult working with the SDR devices)
* for Hermes Lite 2 specific notes look into the ```Notes_if_using_HERMES-Lite-2.md```

## I want use now deskHPSDR. What I need to do ?

deskHPSDR is published exclusively as source code only. You need to clone this Github repository and compile the app before you can use it. Please read all included instructions carefully to avoid installation errors by yourself. Additional notes you can find too under the discussion tab of this project. Please have a look there too from time to time.<br>
I will never publish any ready-compiled binaries, neither for macOS nor for Linux. The task of compiling it yourself remains.

## The further development of deskHPSDR

deskHPSDR is under active development, because software projects never finished. My focus with deskHPSDR is Fonie/SSB and Digimodes, less CW.<br>
My guiding principle is to adapt most of the core functions from [Thetis](https://github.com/mi0bot/OpenHPSDR-Thetis) to deskHPSDR, but without the surrounding playground. What I mean is, it will never be like Thetis, but we will get as close as we can.

## Latest Changes

**CHANGES are located in the [Discussions tab, category CHANGELOG deskhpsdr](https://github.com/dl1bz/deskhpsdr/discussions/categories/changelog-deskhpsdr).**

### Version 2.6.x (current version)

On March 4, 2025 the **first final version 2.6 of deskHPSDR** is published. This version will not get any brand-new base functions, only bugfixes if any become known or improvements if I think there's still something to be done yet.<br>
Further development will start later from version 2.7.x<br>

Most of the new functions need to be activated in the ```make.config.deskhpsdr``` as compiling option. Please look in the beginning of the  ```Makefile``` and set the needed options only in ```make.config.deskhpsdr```, but don't modify the ```Makefile``` itself !

### Version 2.5.x (first developer version [deprecated])

First version after forking from pihpsdr in October 2024, not made for production or public use.

## Issues and Discussion tab at Github for this project - read carefully !

- the **Issues tab is only for reporting issues, bugs or malfunctions of this app** !
- for all other things please use necessarily the [discussions tab](https://github.com/dl1bz/deskhpsdr/discussions/categories/changelog-deskhpsdr)

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
**If this not help, please delete the complete codebase of deskHPSDR and clone it again, then you have a fresh copy.**<br>

If ```git pull``` failed, you can also try this:<br>
```
$ git pull --all
$ git reset --hard origin/master
$ git pull --all
```
This overwrite local changes, which are different from the remote repo at Github.com and set the status equal between local and remote.

## Successful and confirmed Tests I had done up to now

So far, deskHPSDR has been successfully tested on the following systems:<br>
* iMac 21" i5 running macOS 14 aka Sonoma
* Macbook Air M1 running macOS 26 aka Tahoe
* old Macbook Pro i7 & old Macbook Air i5 running Linux Mint "Faye" Debian-Edition
* Raspberry Pi5 with NVMe-HAT running 64bit PiOS (based at Debian "Bookworm") and X11 environment
* *Raspberry Pi 3B+ works too, but with limitations (panadapter framerate only 10fps, if want more the CPU hasn't enough power)*
* a hamradio friend of mine has checked it on a Desktop Linux Ubuntu LTS for me, works too

**All radio tests are made with my Hermes Lite 2 SDR-Transceiver using HPSDR protocol V1 under macOS 14, macOS 15 and macOS 26**
**There are no issues with the Hermes Lite 2 and deskHPSDR yet, but it is not possible to check ALL other exist SDR devices.**

## Credits

Big thanks and huge respect to all involved developers for their previous great work on piHPSDR until now and make this application accessible as Open Source under the GPL. Many thanks also to the users who gave me feedback and reported issues which I hadn't noticed by myself.<br>
Special thanks to:<br>
- my wife for her great patience and understanding
- John Melton G0ORX & Christoph van Wüllen DL1YCF for their great development around pihpsdr
- Dr. Warren C. Pratt NR0V for the great software library WDSP, the "heart" of our deskHPSDR application
- all of the active users for support deskHPSDR

## Exclusion of any Guarantee and any Warrenty and limited Support

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

All what you do with this code is at your very own risk. The code is published "as it is" without right of any kind of support or similiar services.

**There are no rights or obligations to get any kind of support for deskHPSDR from me, I publish the source code "as it is".**

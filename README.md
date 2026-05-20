# deskHPSDR by DL1BZ for OpenHPSDR protocol 1 & 2

<img src="https://github.com/dl1bz/deskhpsdr/blob/master/release/deskhpsdr/screenshot.png" width="1024px" />

## deskHPSDR ≠ pihpsdr: new app, new concept, new name

Correct is, the very first codebase of deskHPSDR was forked ONCE from DL1YCF's pihpsdr codebase in October 2024 without any backward dependencies to piHPSDR.
But there was, is and will be never an active collaboration between pihpsdr and deskHPSDR. They are two different apps without any relationship to each other.

## The concept behind - what it's make for and what it isn't for

My goal was to make an more optimzed version running with focus on Desktop-OS like Linux and macOS, what means I don't support small displays less as 1280x600. SoC like the Raspberry Pi or similar devices are not within the scope in development of this application. In the case mini-display < 1280x600 resolution you need change to piHPSDR, deskHPSDR don't support this.

**deskHPSDR is a dedicated SDR transceiver frontend application using OpenHPSDR protocols 1 or 2 for everyday use in amateur radio. Some limited SoapySDR support is current available, but Soapy support is official discontinued now.**


| Feature                        | Current deskHPSDR version 2.6                 | Notes          |
| -------------------------------- | ----------------------------------------------- | ---------------- |
| TCI CAT                        | supported                                     |                |
| TCI Audio                      | supported                                     |                |
| PTT external                   | supported via serial RTS/CTS or MIDI          |                |
| MIDI Control                   | supported                                     |                |
| Hamlib                         | supported with own included rigctld           |                |
| OpenHPSDR protocol 1           | fully supported                               |                |
| OpenHPSDR protocol 2           | fully supported                               |                |
| GPIO                           | ~~limited support~~ removed                   | deprecated     |
| Soapy API / Soapy protocol     | rudimentary supported                         | deprecated     |
| Hermes Lite 2 N2ADR IO board   | supported                                     |                |
| Use WDSP library ?             | yes, current 1.29                             |                |
| Pure Signal / Pre-Distortion   | supported                                     |                |
| Noise Reduction                | NR1 - NR4 available                           |                |
| CAT over TCP                   | supported (TS2000 & PowerSDR emulation)       |                |
| Client-Server Mode             | None, not supported                           |                |
| Hermes Lite 2+ expansion board | supported                                     |                |
| Bright / Dark Theme support    | yes                                           |                |
| Screen Resolution              | min. 1280x600 or higher                       |                |
| Input devices                  | Touchscreen, Mouse, Keyboard, MIDI            |                |
| OS support                     | macOS 15 or higher, modern Linux              | no WIN support |
| Audio layer support            | PORTAUDIO, PULSEAUDIO, ALSA                   |                |
| Audio devices support          | mono,stereo (only 48kHz supported)            |                |
| DX Cluster support             | yes, inclusive show Spots on RX Panadapter    |                |
| SDR TX support                 | yes                                           |                |
| Used UI framework              | GTK3                                          |                |
| Programming language           | C, partially Objective C/Swift (macOS only)   |                |
| Supported Compiler             | clang, gcc                                    |                |
| App Publishing                 | **Source code only, no binaries distributed** |                |

Other useful app features (availability depend from used SDR device):

* Noisefloor adjustment automatic for Panadapter
* SDR device auto-detection
* Autogain adjustment and ADC overflow protection (Hermes Lite 2 only)
* Keyboard shortcuts (pre-defined)
* Open Collector support (RX, TX, Tune) via OpenHPSDR protocol
* Antenna selection (if SDR support this)
* Transverter Support
* Full RX- and TX-WDSP Audio chain support (RX/TX-EQ, Leveler, CESSB, Limiter, CFC, Speech Processor, DEXP)
* network optimizations if using WiFi for OpenHPSDR protocol P1 or P2
* supported Receiver:2, supported Transmitter:1, VFO:2 per RX/TX
* VFO split, swap, RIT/XIT supported
* Save TX DRIVE and TUNE DRIVE per band, Bandstack available
* display current solar data for propagation, show Greyline DX window
* Panadapter refresh rate adjustment (max. 60fps)
* some special SDR device-specific options supported

The focus is clear fonie/SSB & digimodes, less CW. deskHPSDR has more added options integraded from the WDSP library like pihpsdr, especially tools for the WDSP RX and TX audio chain, and they are all user-acessible and user-adjustable (pihpsdr has many things only "hardcoded" without user-access). deskHPSDR support **max. two RX**, although some SDR hardware supports more, like the Hermes Lite 2 with up to four RX slices. SoapySDR API is only partially supported, but will not actively developed further, the Soapy API and device support considered state "discontinued / deprecated".

deskHPSDR **is not made** as a "measurement tool" or for other, very special purposes where SDR devices are used (e.g. SDR Lab, IF-tap, IF-/Panadapter-Mode). There are other, more specialized apps for such cases - use these for your special purposes. It's - not more, not less - a SDR transceiver GUI frontend for use in hamradio which will be actively and continuously developed. All things outside the hamradio universe are generally not supported by this app. The support for commercial SDR products is limited, because they are mostly not Open Source hardware like the Hermes Lite 2. deskHPSDR is Open Source and a full non-commercial hobby software project by DL1BZ, which can be used completely free without any kind of payments, but respect all copyrights.

**deskHPSDR need a screen size 1280x600 at minimum or higher** for best GUI experiences, that's one of the difference against piHPSDR. deskHPSDR hasn't a special Client-Server-Mode like pihpsdr (make no sense, we HAVE network-connected SDR devices yet).

My main focus of deskHPSDR development is macOS, which is my primary development environment for deskHPSDR. Normally all should be running with Linux too. The second focus is Fonie/SSB/Digimodes and less CW. This SDR software app is made for SDR transceiver used in Hamradio as daily-used app, less for special operations with wide-range RX-only SDR devices. If you agree with me and my ideas, deskHPSDR can be very useful for you. If not, look around for other solutions.

**There are no plans to adapt deskHPSDR for running with WINDOWS ! It's made for UNIX style OS like macOS or Linux.**

## Requirements

* modern Desktop-OS like macOS or Linux with installed developer tools like compiler, linker etc.
* minimum screensize starts from 1280x600
* **basic knowledge**: how to use your OS, a shell, a text editor and how to compile applications from source code
* *macOS only*: please read the``COMPILE.macOS`` first
* *Linux only*: please read the``COMPILE.linux`` first
* a SDR device or SDR transceiver, which supports HPSDR protocol 1 (older) like the Hermes Lite 2 or protocol 2 (newer) like the ANAN or similiar devices. Per default Soapy-API is disabled, if needed you must activate Soapy-API support in the``make.config.deskhpsdr`` as an user-defined option. Please note, development support for Soapy has been discontinued now.
* a very good running network without any issues (Ethernet preferred, WiFi not recommended) and a DHCP server inside (without DHCP is possible too, but more complicated or difficult working with the SDR devices)
* for Hermes Lite 2 specific notes look into the``Notes_if_using_HERMES-Lite-2.md``
* if want using a Raspberry Pi: Revision 5 (aka "Pi5") with >= 8GB RAM is strongly recommended, but deskHPSDR is not optimized for such SoC

## I want use now deskHPSDR. What I need to do ?

deskHPSDR is published exclusively as source code only. You need to clone this Github repository and compile the app before you can use it. Please read all included instructions carefully to avoid installation errors by yourself. Additional notes you can find too under the discussion tab of this project. Please have a look there too from time to time.<br>
I will never publish any ready-compiled binaries, neither for macOS nor for Linux. The task of compiling it yourself remains.

## The further development of deskHPSDR

deskHPSDR is under active development, because software projects never finished. My focus with deskHPSDR is Fonie/SSB and Digimodes, less CW. Primary OS platform is macOS, but not Linux.<br>
My guiding principle is to adapt most of the core functions from [Thetis](https://github.com/mi0bot/OpenHPSDR-Thetis) to deskHPSDR, but without the surrounding playground. What I mean is, it will never be like Thetis, but we will get as close as we can.<br>

deskHPSDR is primarily developed for and under macOS. But made as an cross-platform app, it runs on Linux, but Linux is and will be not a priority.

## Latest Changes

**CHANGES are located in the [Discussions tab, category CHANGELOG deskhpsdr](https://github.com/dl1bz/deskhpsdr/discussions/categories/changelog-deskhpsdr).**

### Version 2.6.x (current version)

On March 4, 2025 the **first final version 2.6 of deskHPSDR** was published.<br>
Further development will start later from version 2.7.x by the end of 2026.<br>

Most of the new functions need to be activated in the ``make.config.deskhpsdr`` as compiling option. Please look in the beginning of the  ``Makefile`` and set the needed options only in ``make.config.deskhpsdr``, but don't modify the ``Makefile`` itself !

## Issues and Discussion tab at Github for this project - read carefully !

- the**Issues tab is only for reporting issues, bugs or malfunctions of this app** !
- for all other things please use necessarily the [discussions tab](https://github.com/dl1bz/deskhpsdr/discussions/categories/changelog-deskhpsdr)

## Known problems if using Git for update the code base at your local computer

In the ``Makefile`` I add a comment "don't edit this Makefile". That's I mean so. I'm now add the editable, additional file for this called ``make.config.deskhpsdr``.<br>

If ``git pull`` failed, you can try this:<br>

```
$ git pull --all
$ git reset --hard origin/master
$ git pull --all
```

This overwrite ALL local changes you are made, which are different from my current repo at Github.com and set the status equal between local and remote.

**If this not help, please delete the complete codebase of deskHPSDR and clone it again, then you have a fresh current copy.**<br>

## Successful and confirmed Tests I had done up to now

So far, deskHPSDR has been successfully tested on the following systems:<br>

* iMac 21" i5 running macOS 15 aka Sequoia
* Macbook Air M1 running macOS 26 aka Tahoe
* Mac mini M4 2024 running macOS 26 aka Tahoe
* old Macbook Pro i7 & old Macbook Air i5 running Linux Mint "Faye" Debian-Edition
* Raspberry Pi5 with NVMe-HAT running 64bit PiOS (based at Debian "Bookworm") and X11 environment
* Raspberry CM5 module based with NVMe SSD running 64bit PiOS (based at Debian "Trixie") and X11 environment

**All radio tests are made with my Hermes Lite 2 SDR-Transceiver using HPSDR protocol V1 under macOS 15 and macOS 26**
**There are no issues with the Hermes Lite 2 and deskHPSDR yet, but it is not possible to check ALL other exist SDR devices.**
**Additional tests with my new Brick2 14bit SDR transceiver are also carried out with P2 OpenHPSDR protocol.**

## Credits

Big thanks and huge respect to all involved developers for their previous and current work on pihpsdr until now and make this application accessible as Open Source under the GPL. Many thanks also to the users who gave me feedback and reported issues which I hadn't noticed by myself.<br>
Special thanks to:<br>

- my wife for her great patience and understanding
- John Melton G0ORX & Christoph van Wüllen DL1YCF for their earlier and current pihpsdr development
- Dr. Warren C. Pratt NR0V for the great software library WDSP, the "heart" of our deskHPSDR application
- all of the active users and contributors for support deskHPSDR

## Exclusion of any Guarantee and any Warrenty and limited Support

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.<br>

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.<br>

All what you do with this code is at your very own risk. The code is published "as it is" without right of any kind of support or similiar services.<br>
This project is not affiliated with or endorsed by Apache Labs, FlexRadio Systems, ramdor/Thetis, mi0bot/Thetis or the OpenHPSDR project.
deskHPSDR is also in no way connected to the current pihpsdr development. Both are independent developments with different concepts and goals.

**There are no rights or obligations to get any kind of user support for deskHPSDR from me, I publish only the app source code "as it is".**

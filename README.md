# deskHPSDR by DL1BZ for OpenHPSDR protocol 1 & 2

<img src="https://github.com/dl1bz/deskhpsdr/blob/master/stuff/deskhpsdr/screenshot.png" width="1024px" />

## deskHPSDR ≠ pihpsdr: new app, revised concept, new name

Correct is, the very first codebase of deskHPSDR was forked ONCE from [DL1YCF's pihpsdr codebase](https://github.com/dl1ycf/pihpsdr) in October 2024 without any backward dependencies to piHPSDR.
But there wasn't, isn't and won't be an interactive and thereby direct collaboration between pihpsdr and deskHPSDR. They are two different apps without any relationship or dependencies to each other. Nevertheless, we are in frequent exchange of ideas to further advance both versions.

## The concept behind - what it's make for and what it isn't for

My goal was to make an more optimzed version running with focus on Desktop-OS like Linux and macOS, what means I don't support small displays less as 1280x600. SoC like the Raspberry Pi or similar devices are not within the scope in development of this application. In the case mini-display < 1280x600 resolution you need change to piHPSDR, deskHPSDR don't support this.

**deskHPSDR is a dedicated SDR transceiver frontend application using OpenHPSDR protocols 1 or 2 for everyday use in amateur radio. Soapy SDR and GPIO code and support is official REMOVED now.**


| Feature                        | Current deskHPSDR version 2.7                 | Notes          |
| -------------------------------- | ----------------------------------------------- | ---------------- |
| TCI CAT                        | supported ✅                                  | TCI 2.0        |
| TCI Audio                      | supported (only 48k and 24k) ✅               | TCI 2.0        |
| TCI I/Q                        | supported (48k/96k/192k/384k) ✅              | TCI 2.0        |
| PTT external                   | supported (via serial RTS/CTS or MIDI) ✅     |                |
| MIDI Control                   | supported ✅                                  |                |
| Hamlib                         | supported (with own included rigctld) ✅      |                |
| OpenHPSDR protocol 1           | supported ✅                                  |                |
| OpenHPSDR protocol 2           | supported ✅                                  |                |
| GPIO                           | not supported (since V2.7) ❌                 | code removed   |
| Soapy API / Soapy protocol     | not supported (since V2.7) ❌                 | code removed   |
| Hermes Lite 2 N2ADR IO board   | supported ✅                                  |                |
| Use WDSP library ?             | yes, current 2.00 ✅                          |                |
| Pure Signal / Pre-Distortion   | supported up to 192k samplingrate ✅          |                |
| Noise Reduction                | NR1 - NR4 available ✅                        |                |
| CAT over TCP                   | supported (TS2000 & PowerSDR emulation) ✅    |                |
| integrated Client-Server Mode  | not supported ❌                              | will never come|
| IF Mode / Panadapter mode      | not supported ❌                              | will never come|
| Hermes Lite 2+ expansion board | supported ✅                                  |                |
| Bright / Dark Theme support    | yes ✅                                        |                |
| Screen Resolution              | min. 1280x600 or higher                       |                |
| Input devices                  | Touchscreen, Mouse, Keyboard, MIDI            |                |
| OS support                     | macOS 15 or higher, modern Linux              | no WIN support |
| Audio layer support            | PORTAUDIO, PULSEAUDIO, ALSA                   |                |
| Audio devices support          | mono,stereo (only 48kHz supported)            |                |
| TX Audio Monitor               | not supported ❌                              | will never come|
| DX Cluster support             | yes, inclusive show Spots on RX Panadapter ✅ |                |
| RBN support                    | yes, inclusive show Spots on RX Panadapter ✅ |                |
| SDR TX support                 | yes, fully supported ✅                       |                |
| Used UI framework              | GTK3                                          |                |
| Programming language           | C, partially Objective C/Swift (macOS only)   |                |
| Supported Compiler             | clang (recommended), gcc                      |                |
| App Publishing                 | **Source code only, no binaries distributed** |                |
|                                | **except macOS (digitally signed & notarized)**|                |

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
* 2 VFO, up to 2 Receiver and 1 Transmitter
* VFO split, swap, RIT/XIT supported
* Save TX DRIVE and TUNE DRIVE per band, Bandstack available
* display current solar data for propagation, show Greyline DX window
* Panadapter refresh rate adjustment (max. 60fps)
* some special SDR device-specific options supported

The focus is clear fonie/SSB & digimodes, less CW. deskHPSDR has more added options integraded from the WDSP library like pihpsdr, especially tools for the WDSP RX and TX audio chain, and they are all user-acessible and user-adjustable (pihpsdr has many things only "hardcoded" without user-access). deskHPSDR support **max. two RX**, although some SDR hardware supports more, like the Hermes Lite 2 with up to four RX slices.

deskHPSDR **is not made** as a "measurement tool" or for other, very special purposes where SDR devices are used (e.g. SDR Lab, IF-tap, IF-/Panadapter-Mode). There are other, more specialized apps for such cases - use these for your special purposes. It's - not more, not less - a SDR transceiver GUI frontend for use in hamradio which will be actively and continuously developed. All things outside the hamradio universe are generally not supported by this app. The support for commercial SDR products is limited, because they are mostly not Open Source hardware like the Hermes Lite 2. deskHPSDR is Open Source and a full non-commercial hobby software project by DL1BZ, which can be used completely free without any kind of payments, but with full respect of all copyrights around the app deskHPSDR.

**deskHPSDR need a screen size 1280x600 at minimum or higher** for best GUI experiences, that's one of the difference against piHPSDR. deskHPSDR hasn't a special Client-Server-Mode like pihpsdr (make no sense, we HAVE network-connected SDR devices yet).

My main focus of deskHPSDR development is macOS, which is my primary development environment for deskHPSDR. Normally all should be running with Linux too. The second focus is Fonie/SSB/Digimodes and less CW. This SDR software app is made for SDR transceiver used in Hamradio as daily-used app, less for special operations with wide-range RX-only SDR devices. If you agree with me and my ideas, deskHPSDR can be very useful for you. If not, look around for other solutions.

**There are no plans to adapt deskHPSDR for running with WINDOWS ! It's made for UNIX style OS like macOS or Linux.**

## Requirements

I recommend using deskHPSDR on macOS, it's also being focused and developed on this OS platform. It can be build and run under Linux too, but my recommendation remains unequivocally macOS for for stable and stress-free operation with deskHPSDR. macOS will also remain the only platform, where I provide ready-made app bundles without build them first.

* modern Desktop-OS like macOS (15 or newer) or Linux with installed developer tools like compiler, linker etc.
* minimum screensize starts from 1280x600
* **basic knowledge**: how to use your OS, a shell, a text editor and how to compile applications from source code
* *macOS only*: please read the``COMPILE.macOS`` first
* *Linux only*: please read the``COMPILE.linux`` first
* a SDR device or SDR transceiver, which supports HPSDR protocol 1 (older) like the Hermes Lite 2 or protocol 2 (newer) like the ANAN or similiar devices.
* a very good running network without any issues (Ethernet preferred, WiFi not recommended) and a DHCP server inside (without DHCP is possible too, but more complicated or difficult working with the SDR devices)
* for Hermes Lite 2 specific notes look into the``Notes_if_using_HERMES-Lite-2.md``
* if want using a Raspberry Pi: Revision 5 (aka "Pi5") with >= 8GB RAM is strongly recommended, but deskHPSDR is not optimized for such SoC

## I want use now deskHPSDR. What I need to do ?

deskHPSDR is published exclusively as source code only (except macOS, meanwhile are ready-made app bundles available). You need to clone this Github repository and compile the app before you can use it. Please read all included instructions carefully to avoid installation errors by yourself. Additional notes you can find too under the discussion tab of this project. Please have a look there too from time to time.<br>
I will never publish any ready-compiled binaries or appimages for Linux. The task of compiling it yourself remains.

## The further development of deskHPSDR

deskHPSDR is under active development, because software projects never finished. My focus with deskHPSDR is Fonie/SSB and Digimodes, less CW.<br>
My guiding principle is to adapt most of the core functions from [Thetis](https://github.com/mi0bot/OpenHPSDR-Thetis) to deskHPSDR, but without the surrounding playground. What I mean is, it will never be like Thetis, but we will get as close as we can.

## Management of user requests

I only accept user requests, if they
a) fit the concept and
b) are useful to everyone.
I decline requests for "only one user" functions.

## macOS is first choice

deskHPSDR is primarily developed for and under macOS. Made as an cross-platform app, it runs on Linux, but Linux is and will be not a priority.

## Latest Changes

**CHANGES are located in the [Discussions tab, category CHANGELOG deskhpsdr](https://github.com/dl1bz/deskhpsdr/discussions/categories/changelog-deskhpsdr).**

### Version 2.6.x (previous version, EOL May 2026)

On March 4, 2025 the **first final version 2.6 of deskHPSDR** was published.<br>
With publishing of new version 2.7 in May 2026 all support for 2.6 is finished inclusive older versions with SoapySDR and GPIO support.

### Version 2.7.x (current version)

Current development start from version 2.7 in May 2026.<br>

Most of the new functions need to be activated in the ``make.config.deskhpsdr`` as compiling option. Please look in the beginning of the  ``Makefile`` and set the needed options only in ``make.config.deskhpsdr``, but don't modify the ``Makefile`` itself !

## Support: Issues and Discussion tab at Github for this project - read carefully !

- the **Issues tab is only for reporting issues, bugs or malfunctions of this app** !
- for all other things please use necessarily the [discussions tab](https://github.com/dl1bz/deskhpsdr/discussions/categories/changelog-deskhpsdr)

Please note: My development focus is only macOS. Linux is of no interest to me. Yes, this app can run with Linux too, that's a result of how this app is programmed based on C program language. However, that does not mean I provide any kind of support for Linux. From time to time, I test whether the app can be built on Linux and whether it runs. If it does that successfully, the topic Linux is closed for me at this point. Read carefully my instructions for build with Linux in document ``COMPILE.linux``.

## Known problems if using Git for update the code base at your local computer

From time to time I need rebase the deskHPSDR repository. This can break using ``git pull`` for update deskHPSDR.<br>
Please make sure you set:<br>

```
$ git config pull.rebase true
```

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

* Macbook Air M1 running macOS 26 aka Tahoe
* Mac mini M4 2024 running macOS 26 aka Tahoe
* old Macbook Pro i7 running Linux Mint "Faye" Debian-Edition
* Raspberry CM5 module based with NVMe SSD running 64bit PiOS (based at Debian "Trixie") and X11 environment

**All radio tests are made with my Hermes Lite 2 SDR-Transceiver using OpenHPSDR protocol P1 under macOS 26.**
**There are no issues with the Hermes Lite 2 and deskHPSDR yet, but it is not possible to check ALL other exist SDR devices.**
**Additional tests with my new Brick2 14bit SDR transceiver are also carried out with P2 OpenHPSDR protocol.**

## Credits

Big thanks and huge respect to all involved developers for their work on pihpsdr until now and make this application accessible as Open Source under the GPL. Many thanks also to the users who gave me feedback and reported issues which I hadn't noticed by myself.<br>
Special thanks to:<br>

- my lovely wife any my family for her great patience and understanding
- John/G0ORX, Steve/KA6S & Jae/K5JAE for initial pihpsdr development
- Christoph/DL1YCF for continue development and current pihpsdr
- Dr. Warren C. Pratt NR0V for the great software library WDSP, the "heart" of our deskHPSDR application
- OpenAI/ChatGPT for many code optimizations and saving a lot of time in development
- all of the active users and contributors for support deskHPSDR, Open Source Soft- and Hardware

## Exclusion of any Guarantee and any Warrenty and limited Support

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.<br>

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.<br>

All what you do with this code is at your very own risk. The code is published "as it is" without right of any kind of support or similiar services.<br>
This project is not affiliated with or endorsed by Apache Labs, FlexRadio Systems, ramdor/Thetis, mi0bot/Thetis or the OpenHPSDR project.
deskHPSDR is also in no way connected to the current pihpsdr development. Both are independent developments with different concepts and goals.

**There are no rights or obligations to get any kind of user support for deskHPSDR from me, I publish only the app source code "as it is".**

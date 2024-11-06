# piHPSDR - Improved version (optimized for Desktops)

piHPSDR is a Open Source hamradio SDR application for Linux and macOS, primarily for use with SDR devices using the HPSDR protocol 1 & 2 and was initially developed by John Melton G0ORX/N6LYT.

Lately Christoph/DL1YCF has continued the development of piHPSDR and add a lot of options, which hasn't include the original version, so DL1YCF's version is the most up-to-date version of piHPSDR. You can find his version here:

[https://github.com/dl1ycf/pihpsdr/](https://github.com/dl1ycf/pihpsdr/)

## The goal of my "improved" version of piHPSDR or why I do this

The original version is made for running with Linux Desktops, Macs with macOS, but for Raspberry Pi 3/4/5 too and can be used with low-resolution Displays or Monitors too, start from 640x480 screen size.

**IMPORTANT: If you want to use piHPSDR with small displays, you <u>need</u> to use Christoph's/DL1YCF version !**

---

My orientation is the most extensive SDR application [OpenHPSDR-Thetis](https://github.com/ramdor/Thetis/), earlier known as PowerSDR. But this application is only available under WINDOWS.
With macOS the selection of good SDR applications is very poor, under Linux it's looks a little bit better. piHPSDR is very near at [OpenHPSDR-Thetis](https://github.com/ramdor/Thetis/), because both applications are based at the [WDSP-Library](https://github.com/TAPR/OpenHPSDR-wdsp) developed by Dr. Warren C. Pratt, NR0V. They are - more or less - GUI frontends for this [WDSP-Library](https://github.com/TAPR/OpenHPSDR-wdsp). That means, we get comparable results with both applications. I personally use piHPSDR only with macOS, but not with Linux.
But my code should also run at both platforms.

My version breaks the limitation of low-resolution screens and **is made for screen sizes equal or higher than 1280 x 800 or more** and has the focus of macOS and Linux Desktops.

I call my version not a "fork" of DL1YCF's version, it's more a improved version for large desktops. As far as possible my version is ever merged to the most actual code base of DL1YCF's version, but with my additional improvements.

## CHANGES in my version against the version of DL1YCF

**Be aware:** In the Screen-Menu of piHPSDR you need to select at minimum **"VFO bar for 1280px windows"** or higher ! Don't use lower settings, otherwise you will get a few problems to display some of my changed GUI elements correctly ! As I said before, this version is optimized for larger screens,<br>but NOT for small screens < 1280px X 800px!

---

1. Change the work-directory from

```
   ˜/Library/Application Support/piHPSDR (macOS)
   ˜/.config/pihpsdr (Linux)
```

to

```
   ˜/Library/Application Support/deskHPSDR (macOS)
   ˜/.config/deskhpsdr (Linux)
```

So we have a different work-directory for my improved version of piHPSDR, which not affected the original work-directory. But you can copy the existing .prop-files from the old to the new work-directory.

2. In the TX menu I removed the option **Digi-drive** and replace it with the option **Leveler gain** and **Leveler switch on/off**.
3. I made some helpful GUI improvements, e.g. I extend the digital S-Meter, show state and level of CFC, Leveler, processor (which is the baseband compressor).
4. For activate my additional improvements I add a new option in the **Makefile** called **DESKTOP=ON**, which add a new compiler directive called `__LDESK__` . That means "large desktop". In my code base all improvements are implemented with

```
#if defined (__LDESK__)
  (improvement code)
#else
  (original code)
#endif
```

So the original code is still present inside and will be **not removed**.

5. I removed the manual part and some other things from my code base, please use the [manual from the version of DL1YCF](https://github.com/dl1ycf/pihpsdr/releases/download/current/piHPSDR-Manual.pdf) instead. I have no plans and no time for a own manual, sri. Christoph is using TeX for the manual, but that is really not my playground for made manuals.

6. The first startup window (before device discovery) show now in addition the used working-directory.

7. Add a new compiler directive `__HAVEATU__`, which controls the new TUNED function. After every band change the current TX drive level is saved and the TX drive goes down to a minimum TX drive level 1. You need to use ONE time the TUNE function and after this the previous saved TX drive level will be restored. That's for outdoor ATU which havn't a feedback signal, so this protect your PA to not send with full power if the ATU is  not tuned or you forgot tuning. To activate this option you need to set in the **Makefile** the option **ATU=ON**. Otherwise you will see only **TUNED** at the VFO display, but only without any control or change of the TX drive level. It's only display if you use the **TUNE** one time after band change, not more.

8. Add an **EXIT** button in the right corner where you have the **Hide** und **Menu** button and disable the system CLOSE window button.

9. De-Channelize 60m band. The previous "Channelizing" of the 60m band was not very helpful and caused some issues e.g. with the bandstack. Most countries have now take over the WRC15 regulations for 60m (except UK & US). Thats why I set WRC15 regulations now as default for the 60m band.

## Requirements: personal skills and technical knowledge

**I only publish pure source code, not ready-compiled binaries, so please never ask me about that**. That means, you need to compile the application by yourself and install it. Follow the instructions *Appendix J: LINUX compile from sources* and *Appendix K: MacOS compile from sources* which you can read in the piHPSDR [manual at DL1YCF pihpsdr Github repository](https://github.com/dl1ycf/pihpsdr/releases/download/current/piHPSDR-Manual.pdf).

I do not teach a basic course in using operating systems too ! **You** need to know, how you use a shell in Linux or macOS, a compiler and so on. Without this knowledge you will surely don't have success, if you don't know, what and how you need to do all. With macOS you need to install the Homebrew-Environment and XQuartz for running piHPSDR.

## My development environment for piHPSDR

I develop the code with macOS 14 aka Sonoma. Here is running an Intel Macbook Air i5, an Intel iMac i5 21" and a Macbook Air M1. My SDR device is a [Hermes Lite 2](http://www.hermeslite.com/) with homebrew LDMOS-Amplifier 600W. Under these conditions I make all of my tests with piHPSDR. **I don't want to make tests with Linux, only with macOS the code will be checked against possible problems !**

---

Have a look too at [my QRZ.com page](https://www.qrz.com/db/DL1BZ) and/or [my Blog](https://hamradio.bzsax.de) (only in German).

## Credits

Thanks to all developers for publish this application piHPSDR as Open Source.
Special thanks goes to Christoph/DL1YCF for the very intensive exchange of ideas and discussion.
We may have different ideas at different points, but at the end that's a good thing for further development of piHPSDR.

I publish my improved version of piHPSDR "as it is". If you want to use it, use it, change it, **however - you do all at your very own risk**.
**There is no guarantee or warranty from me**. And you won't get any support from me, so please don't ask me - the answer is ever "look in the code for understanding". I'm not a professional developer, I do this all only in my little spare time. There is - unfortunately - no extra time for things like support, answer questions and so on. I ask that you respect that carefully.

73 Heiko, DL1BZ

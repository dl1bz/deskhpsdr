# Some notes if using Hermes Lite 2 as SDR-TRX with deskHPSDR #

## Get full 5W output ##

After compilation and installation you need to adjust the output power of the 5W PA from the Hermes Lite 2. Go into the **Menu > Radio Menu** and tick **"PA enable"**. Then go into the **PA Menu** and set all values to 38.8 for full 5W output, otherwise you don't reach the maximum out of the Hermes Lite 2. Set the **MAX POWER** to 5W in the PA Menu if using Hermes Lite 2.

## Audio-Setup (if not using the HL2+ extension board) ##

If not using the HL2+ extension board (which is not a part of the original HL2 and only availible as a third-party extension) you need to setup all audio interfaces to **"Local input"** and **"Local output"**. Thats means, you need to use your internal audio interfaces of your computer. Select the audio input and audio output you want to use.

## Band voltage output ##

For using the bandvoltage output option of the Hermes Lite 2, you need to tick on **Band volts / Dither bit** in the **RX Menu**.

## Using MIDI for control deskHPSDR ##

deskHPSDR has a build-in MIDI support. You can use MIDI controller for the most functions (like a rotary switch as VFO knob), the assignments needs to be done in **MIDI Menu**. You can also use DIY controller (e.g. ESP32 or Arduino based) for developing you own MIDI controller with the help of their MIDI libraries.

## Using GPIO (Raspberry Pi only) ##

If using GPIO control lines (Raspberry Pi only), you need to set **GPIO=ON** in the ```make.config.deskhpsdr``` before you start compiling deskHPSDR. With other OS like macOS we cannot use GPIO, because such computers like Macs havn't any GPIO. Use a DIY MIDI device/controller instead and control deskHPSDR via MIDI. Functions are accessible via GPIO can be used via MIDI too.